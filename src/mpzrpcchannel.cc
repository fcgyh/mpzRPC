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

void MpzrpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                               google::protobuf::RpcController *controller,
                               const google::protobuf::Message *request,
                               google::protobuf::Message *response,
                               google::protobuf::Closure *done)
{
    const google::protobuf::ServiceDescriptor *service_des = method->service();
    std::string service_name = service_des->name();
    std::string method_name = method->name();

    // 1. 通过 Zookeeper 做服务发现
    ZkClient zkCli;
    zkCli.Start();
    std::string method_path = "/" + service_name + "/" + method_name;
    std::string host_data = zkCli.GetData(method_path.c_str());
    
    if (host_data == "")
    {
        if (controller) controller->SetFailed(method_path + " is not exist!");
        return;
    }
    int idx = host_data.find(":");
    if (idx == -1)
    {
        if (controller) controller->SetFailed(method_path + " address is invalid!");
        return;
    }
    std::string ip = host_data.substr(0, idx);
    unsigned short port = atoi(host_data.substr(idx + 1).c_str());

    // 2. 组装待发送的 RPC 数据
    std::string args_str;
    if (!request->SerializeToString(&args_str))
    {
        if (controller) controller->SetFailed("serialize request error!");
        return;
    }

    rpcheader::rpcheader header;
    header.set_service_name(service_name);
    header.set_method_name(method_name);
    header.set_args_size(args_str.size());

    std::string header_str;
    if (!header.SerializeToString(&header_str))
    {
        if (controller) controller->SetFailed("serialize header error!");
        return;
    }

    uint32_t header_size = header_str.size();
    uint32_t header_size_net = htonl(header_size);
    std::string send_str;
    send_str.insert(0, std::string((char *)&header_size_net, 4));
    send_str += header_str + args_str;
    
    // 3. 使用连接池获取到指定主机的连接
    spConnection conn_ptr = MpzrpcConnectionPool::getInstance()->getConnection(ip, port);
    if (conn_ptr == nullptr)
    {
        if (controller) controller->SetFailed("get connection error!");
        return;
    }
    
    // 4. 发送和接收
    bool connection_valid = true; // 标志连接是否有效

    if (-1 == send(*conn_ptr, send_str.c_str(), send_str.size(), 0))
    {
        if (controller) controller->SetFailed("send error!");
        connection_valid = false; // 发送失败，连接失效
    }
    else
    {
        char recv_buf[1024] = {0};
        int recv_size = 0;
        if (-1 == (recv_size = recv(*conn_ptr, recv_buf, 1024, 0)) || recv_size == 0)
        {
            if (controller) controller->SetFailed("recv error!");
            connection_valid = false; // 接收失败或对方关闭，连接失效
        }
        else
        {
            // 5. 反序列化
            if (!response->ParseFromArray(recv_buf, recv_size))
            {
                if (controller) controller->SetFailed("parse response error!");
                // 解析失败不一定代表连接失效，但此次调用是失败的
            }
        }
    }
    
    // 6. 执行回调
    if (done) { done->Run(); }

    // 7. 根据连接有效性，决定是归还连接还是销毁连接
    if (!connection_valid)
    {
        // 如果连接无效，我们重置shared_ptr并提供一个新的“关闭”删除器
        // 这样它在析构时会执行close(*p)，而不是归还到池中
        conn_ptr.reset(conn_ptr.get(), [](int* p){ close(*p); delete p; });
    }
    // 如果连接有效(connection_valid为true)，则不执行任何操作，
    // conn_ptr在函数结束时析构，会自动调用池中绑定的删除器，将连接归还。
}