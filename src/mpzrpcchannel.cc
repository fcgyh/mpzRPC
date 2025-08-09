#include <string>
#include <muduo/net/TcpConnection.h>
#include <functional>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include "mpzrpcchannel.h"
#include "logger.h"
#include "rpcheader.pb.h"
#include "mpzrpcapplication.h"
#include "zookeeperutil.h"
#include "mpzrpcconnectionpool.h"
#include "mpzrpccontroller.h"
#include "mpzrpcloadbalancer.h"

// 初始化静态成员
std::unordered_map<std::string, std::vector<std::string>> MpzrpcChannel::m_serviceListCache;
std::mutex MpzrpcChannel::m_cacheMutex;

// 清空缓存的静态方法实现
void MpzrpcChannel::ClearServiceListCache(const std::string& service_path) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_serviceListCache[service_path].clear();
    LOG_INFO("Cache cleared for service: %s", service_path.c_str());
}

void MpzrpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                               google::protobuf::RpcController *controller,
                               const google::protobuf::Message *request,
                               google::protobuf::Message *response,
                               google::protobuf::Closure *done)
{
    const google::protobuf::ServiceDescriptor *service_des = method->service();
    std::string service_name = service_des->name();
    std::string method_name = method->name();
    std::string method_path = "/" + service_name + "/" + method_name;

    // 1. 优先从本地缓存获取服务列表
    std::vector<std::string> host_data_list;
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_serviceListCache.count(method_path))
        {
            host_data_list = m_serviceListCache[method_path];
        }
    }

    // 2. 如果缓存未命中，则从Zookeeper查询
    if (host_data_list.empty())
    {
        LOG_INFO("Cache miss for %s, fetching from ZK...", method_path.c_str());
        // 调用GetChildren并设置watch=true，注册一个一次性的Watcher
        std::vector<std::string> children_nodes = ZkClient::getInstance()->GetChildren(method_path.c_str(), true);
        if (children_nodes.empty()) {
            if (controller) controller->SetFailed(method_path + " has no available provider!");
            if (done) done->Run();
            return;
        }
        
        std::sort(children_nodes.begin(), children_nodes.end());

        for (const auto& node_name : children_nodes) {
            std::string node_path = method_path + "/" + node_name;
            std::string host_data = ZkClient::getInstance()->GetData(node_path.c_str());
            if (host_data.empty()) { continue; }
            host_data_list.push_back(host_data);
        }

        // 写入缓存
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_serviceListCache[method_path] = host_data_list;
        }
    } else {
        LOG_INFO("Cache hit for %s.", method_path.c_str());
    }

    if (host_data_list.empty()) {
        if (controller) controller->SetFailed(method_path + " failed to get any valid provider data!");
        if (done) done->Run();
        return;
    }

    // 3. 组装待发送的 RPC 数据
    std::string args_str;
    if (!request->SerializeToString(&args_str)) { /* ... */ return; }
    rpcheader::rpcheader header;
    header.set_service_name(service_name);
    header.set_method_name(method_name);
    header.set_args_size(args_str.size());
    std::string header_str;
    if (!header.SerializeToString(&header_str)) { /* ... */ return; }
    uint32_t header_size = header_str.size();
    uint32_t header_size_net = htonl(header_size);
    std::string send_str;
    send_str.insert(0, std::string((char *)&header_size_net, 4));
    send_str += header_str + args_str;

    // 4. 重试循环
    int max_retries = 3;
    bool rpc_success = false;
    std::vector<std::string> host_data_list_copy = host_data_list;

    for (int i = 0; i < max_retries && !host_data_list_copy.empty(); ++i)
    {
        std::string host_data = LoadBalancer::getInstance()->selectHost(host_data_list_copy);
        if (host_data.empty()) continue;

        int idx = host_data.find(":");
        if (idx == -1) { 
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue; 
        }
        std::string ip = host_data.substr(0, idx);
        unsigned short port = atoi(host_data.substr(idx + 1).c_str());

        spConnection conn_ptr = MpzrpcConnectionPool::getInstance()->getConnection(ip, port);
        if (conn_ptr == nullptr) {
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        if (-1 == send(conn_ptr->sockfd, send_str.c_str(), send_str.size(), 0)) {
            conn_ptr->is_valid = false;
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        struct pollfd pfd;
        pfd.fd = conn_ptr->sockfd;
        pfd.events = POLLIN;
        int timeout_ms = MpzrpcApplication::getApp().getConfig().getRpcCallTimeout();
        int poll_ret = poll(&pfd, 1, timeout_ms);

        if (poll_ret <= 0) {
            conn_ptr->is_valid = false;
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        char recv_buf[1024] = {0};
        int recv_size = recv(conn_ptr->sockfd, recv_buf, 1024, 0);
        if (recv_size <= 0) {
            conn_ptr->is_valid = false;
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        if (response->ParseFromArray(recv_buf, recv_size)) {
            rpc_success = true;
            break;
        } else {
            if (controller) controller->SetFailed("parse response error!");
            break;
        }
    }

    if (!rpc_success && controller) {
        controller->SetFailed("RPC call failed after all retries.");
    }
    
    if (done) {
        done->Run();
    }
}