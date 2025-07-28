#pragma once
#include <google/protobuf/service.h>

#include <muduo/net/TcpConnection.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>

#include <unordered_map>
#include <string>
#include <google/protobuf/descriptor.h>

// 前向声明线程池类，避免在头文件中引入完整的threadpool.h
// 这样可以减少头文件依赖，加快编译速度
class ThreadPool;

class MpzrpcProvider
{
public:
    // 构造函数
    MpzrpcProvider();
    
    // 析构函数
    ~MpzrpcProvider();

    // 发布服务
    void publishService(::google::protobuf::Service *service);

    // 启动RPC服务
    void run();

    // 连接回调
    void onConnectionCallback(const muduo::net::TcpConnectionPtr &conn);

    // 消息回调
    void onMessageCallback(const muduo::net::TcpConnectionPtr &conn,
                           muduo::net::Buffer *buffer,
                           muduo::Timestamp receiveTime);

    // 发送RPC响应
    void SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response);

private:
    // 服务信息结构体
    struct ServiceInfo
    {
        google::protobuf::Service *m_service;
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodmap;
    };

    // 存储所有已注册的服务
    std::unordered_map<std::string, ServiceInfo> m_servicemap;

    // 持有业务线程池的智能指针
    std::unique_ptr<ThreadPool> m_threadPool;
};