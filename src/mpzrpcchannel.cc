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

void MpzrpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                               google::protobuf::RpcController *controller,
                               const google::protobuf::Message *request,
                               google::protobuf::Message *response,
                               google::protobuf::Closure *done)
{
    const google::protobuf::ServiceDescriptor *service_des = method->service();
    std::string service_name = service_des->name();
    std::string method_name = method->name();

    // 1. 服务发现：获取所有可用的服务实例列表
    std::string method_path = "/" + service_name + "/" + method_name;
    std::vector<std::string> children_nodes = ZkClient::getInstance()->GetChildren(method_path.c_str());
    
    // 为了保证轮询的稳定性，对节点排序
    std::sort(children_nodes.begin(), children_nodes.end());

    if (children_nodes.empty()) {
        if (controller) controller->SetFailed(method_path + " has no available provider!");
        if (done) done->Run();
        return;
    }

    std::vector<std::string> host_data_list;
    for (const auto& node_name : children_nodes) {
        std::string node_path = method_path + "/" + node_name;
        std::string host_data = ZkClient::getInstance()->GetData(node_path.c_str());
        if (!host_data.empty()) {
            host_data_list.push_back(host_data);
        }
    }

    if (host_data_list.empty()) {
        if (controller) controller->SetFailed(method_path + " failed to get provider data!");
        if (done) done->Run();
        return;
    }

    // 2. 组装待发送的 RPC 数据 (这部分不随重试改变，可以预先准备)
    std::string args_str;
    if (!request->SerializeToString(&args_str)) { if (controller) controller->SetFailed("serialize request error!"); if (done) done->Run(); return; }
    rpcheader::rpcheader header;
    header.set_service_name(service_name);
    header.set_method_name(method_name);
    header.set_args_size(args_str.size());
    std::string header_str;
    if (!header.SerializeToString(&header_str)) { if (controller) controller->SetFailed("serialize header error!"); if (done) done->Run(); return; }
    uint32_t header_size = header_str.size();
    uint32_t header_size_net = htonl(header_size);
    std::string send_str;
    send_str.insert(0, std::string((char *)&header_size_net, 4));
    send_str += header_str + args_str;

    // 3. 引入重试循环，实现故障转移
    int max_retries = 3; // 可配置的最大重试次数
    bool rpc_success = false;
    
    // host_data_list_copy 是本次调用中可供尝试的候选节点列表
    std::vector<std::string> host_data_list_copy = host_data_list;

    for (int i = 0; i < max_retries && !host_data_list_copy.empty(); ++i)
    {
        // 3.1 使用负载均衡器从当前候选列表中选择一个主机
        std::string host_data = LoadBalancer::getInstance()->selectHost(host_data_list_copy);
        if (host_data.empty()) continue;

        int idx = host_data.find(":");
        if (idx == -1) { 
            // 移除格式错误的节点
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue; 
        }
        std::string ip = host_data.substr(0, idx);
        unsigned short port = atoi(host_data.substr(idx + 1).c_str());

        // 3.2 获取一个智能管理的连接对象
        spConnection conn_ptr = MpzrpcConnectionPool::getInstance()->getConnection(ip, port);
        if (conn_ptr == nullptr) {
            // 连接失败，将此节点从候选列表中移除，然后重试
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        // 3.3 发送和接收 (带超时)
        if (-1 == send(conn_ptr->sockfd, send_str.c_str(), send_str.size(), 0)) {
            conn_ptr->is_valid = false; // 标记连接失效，让智能指针自动关闭它
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        struct pollfd pfd;
        pfd.fd = conn_ptr->sockfd;
        pfd.events = POLLIN;
        int timeout_ms = MpzrpcApplication::getApp().getConfig().getRpcCallTimeout();
        int poll_ret = poll(&pfd, 1, timeout_ms);

        if (poll_ret <= 0) { // 包括超时或poll错误
            conn_ptr->is_valid = false;
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        char recv_buf[1024] = {0};
        int recv_size = recv(conn_ptr->sockfd, recv_buf, 1024, 0);
        if (recv_size <= 0) { // 对方关闭或读取错误
            conn_ptr->is_valid = false;
            host_data_list_copy.erase(std::remove(host_data_list_copy.begin(), host_data_list_copy.end(), host_data), host_data_list_copy.end());
            continue;
        }

        // 3.4 成功接收到数据
        if (response->ParseFromArray(recv_buf, recv_size)) {
            rpc_success = true; // 标记成功
            break; // 成功，跳出重试循环
        } else {
            // 解析失败通常是协议问题，不进行重试，但报告错误
            if (controller) controller->SetFailed("parse response error!");
            break;
        }
    }

    // 4. 最终状态处理
    if (!rpc_success && controller) {
        controller->SetFailed("RPC call failed after all retries.");
    }
    
    if (done) {
        done->Run();
    }
}