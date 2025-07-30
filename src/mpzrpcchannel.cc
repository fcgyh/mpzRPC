#include <string>
#include <muduo/net/TcpConnection.h>
#include <functional>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
    if (children_nodes.empty())
    {
        if (controller) controller->SetFailed(method_path + " has no available provider!");
        return;
    }

    // 对获取到的子节点列表进行排序，以保证顺序一致性，保证轮询负载均衡正确工作
    std::sort(children_nodes.begin(), children_nodes.end());

    std::vector<std::string> host_data_list;
    for (const auto& node_name : children_nodes)
    {
        std::string node_path = method_path + "/" + node_name;
        std::string host_data = ZkClient::getInstance()->GetData(node_path.c_str());
        if (!host_data.empty())
        {
            host_data_list.push_back(host_data);
        }
    }

    if (host_data_list.empty())
    {
        if (controller) controller->SetFailed(method_path + " failed to get provider data!");
        return;
    }

    // 2. 使用负载均衡器选择一个主机
    std::string host_data = LoadBalancer::getInstance()->selectHost(host_data_list);
    if (host_data.empty())
    {
        if (controller) controller->SetFailed("Load balancer failed to select a host!");
        return;
    }

    // 解析ip和port
    int idx = host_data.find(":");
    if (idx == -1) { if (controller) controller->SetFailed("Invalid host data format!"); return; }
    std::string ip = host_data.substr(0, idx);
    unsigned short port = atoi(host_data.substr(idx + 1).c_str());

    // 4. 组装待发送的 RPC 数据
    std::string args_str;
    if (!request->SerializeToString(&args_str)) { if (controller) controller->SetFailed("serialize request error!"); return; }
    rpcheader::rpcheader header;
    header.set_service_name(service_name);
    header.set_method_name(method_name);
    header.set_args_size(args_str.size());
    std::string header_str;
    if (!header.SerializeToString(&header_str)) { if (controller) controller->SetFailed("serialize header error!"); return; }
    uint32_t header_size = header_str.size();
    uint32_t header_size_net = htonl(header_size);
    std::string send_str;
    send_str.insert(0, std::string((char *)&header_size_net, 4));
    send_str += header_str + args_str;
    
    // 5. 从连接池获取连接
    spConnection conn_ptr = MpzrpcConnectionPool::getInstance()->getConnection(ip, port);
    if (conn_ptr == nullptr) { if (controller) controller->SetFailed("get connection error!"); return; }
    
    // 6. 发送和接收
    bool connection_valid = true;
    if (-1 == send(*conn_ptr, send_str.c_str(), send_str.size(), 0))
    {
        if (controller) controller->SetFailed("send error!");
        connection_valid = false;
    }
    else
    {
        char recv_buf[1024] = {0};
        int recv_size = 0;
        if (-1 == (recv_size = recv(*conn_ptr, recv_buf, 1024, 0)) || recv_size == 0)
        {
            if (controller) controller->SetFailed("recv error!");
            connection_valid = false;
        }
        else
        {
            if (!response->ParseFromArray(recv_buf, recv_size))
            {
                if (controller) controller->SetFailed("parse response error!");
            }
        }
    }
    
    if (done) { done->Run(); }

    // 7. 根据连接有效性，决定是归还连接还是销毁连接
    if (!connection_valid)
    {
        conn_ptr.reset(conn_ptr.get(), [](int* p){ close(*p); delete p; });
    }
}