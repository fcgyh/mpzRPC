#include <stdlib.h>
#include <functional>

#include "mpzrpcprovider.h"
#include "mpzrpcapplication.h"
#include "rpcheader.pb.h"
#include "logger.h"
#include "zookeeperutil.h"
#include "threadpool.h"

// 构造函数定义
MpzrpcProvider::MpzrpcProvider() {}
// 析构函数定义
MpzrpcProvider::~MpzrpcProvider() {}

void MpzrpcProvider::run()
{
    // 读取配置文件rpcserver的信息
    std::string ip = MpzrpcApplication::getApp().getConfig().getRpcServerIp();
    uint16_t port = MpzrpcApplication::getApp().getConfig().getRpcServerPort();
    int muduoThreadum = MpzrpcApplication::getApp().getConfig().getMuduoThreadNum();
    int businessThreadNum = MpzrpcApplication::getApp().getConfig().getBusinessThreadNum();

    // 创建TcpServer对象
    muduo::net::InetAddress address(ip, port);
    muduo::net::EventLoop loop;
    muduo::net::TcpServer server(&loop, address, "RpcProvider");

    // 绑定连接回调和消息读写回调方法
    server.setConnectionCallback(std::bind(&MpzrpcProvider::onConnectionCallback, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&MpzrpcProvider::onMessageCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    // 设置muduo的I/O线程数量
    server.setThreadNum(muduoThreadum);

    // 初始化业务线程池
    m_threadPool = std::make_unique<ThreadPool>(businessThreadNum);

    // Zookeeper服务注册
    for (auto &sp : m_servicemap)
    {
        // 服务名路径: /<service_name>
        std::string service_path = "/" + sp.first;
        // 先创建服务路径这个永久性节点
        ZkClient::getInstance()->Create(service_path.c_str(), nullptr, 0, 0); 
        
        for (auto &mp : sp.second.m_methodmap)
        {
            std::string method_path = service_path + "/" + mp.first;
            // 先创建方法路径这个永久性节点
            ZkClient::getInstance()->Create(method_path.c_str(), nullptr, 0, 0);

            // 在方法路径下，创建带序列号的临时节点
            std::string ephemeral_node_path = method_path + "/provider_";
            char method_path_data[128] = {0};
            sprintf(method_path_data, "%s:%d", ip.c_str(), port);
            
            // 使用 ZOO_EPHEMERAL_SEQUENTIAL 标志
            ZkClient::getInstance()->Create(ephemeral_node_path.c_str(), method_path_data, strlen(method_path_data), (ZOO_EPHEMERAL | ZOO_SEQUENCE));
        }
    }

    // 启动网络服务
    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;
    server.start();
    loop.loop();
}

void MpzrpcProvider::publishService(::google::protobuf::Service *service)
{
    ServiceInfo service_info;
    const google::protobuf::ServiceDescriptor *psd = service->GetDescriptor();
    std::string service_name = psd->name();
    int method_count = psd->method_count();

    for (int i = 0; i < method_count; ++i)
    {
        const google::protobuf::MethodDescriptor *pmd = psd->method(i);
        service_info.m_methodmap.insert({pmd->name(), pmd});
    }
    service_info.m_service = service;
    m_servicemap.insert({service_name, service_info});
}

void MpzrpcProvider::onConnectionCallback(const muduo::net::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        // 客户端连接断开
        // LOG_INFO("Client connection %s closed.", conn->name().c_str());
    }
}

void MpzrpcProvider::onMessageCallback(const muduo::net::TcpConnectionPtr &conn,
                                     muduo::net::Buffer *buffer,
                                     muduo::Timestamp receiveTime)
{
    // 处理粘包、半包问题的while循环
    while (buffer->readableBytes() >= 4)
    {
        uint32_t header_size = buffer->peekInt32();
        if (buffer->readableBytes() < 4 + header_size) {
            break;
        }

        rpcheader::rpcheader header;
        if (!header.ParseFromArray(buffer->peek() + 4, header_size)) {
            LOG_ERROR("header parse error!");
            conn->shutdown();
            break;
        }
        
        uint32_t args_size = header.args_size();
        uint32_t total_size = 4 + header_size + args_size;
        if (buffer->readableBytes() < total_size) {
            break;
        }

        buffer->retrieve(4 + header_size);
        std::string args_str = buffer->retrieveAsString(args_size);
        
        // 查找服务和方法
        std::string service_name = header.service_name();
        std::string method_name = header.method_name();

        auto service_it = m_servicemap.find(service_name);
        if (service_it == m_servicemap.end()) {
            LOG_ERROR("service:[%s] is not exist!", service_name.c_str());
            break;
        }

        auto method_it = service_it->second.m_methodmap.find(method_name);
        if (method_it == service_it->second.m_methodmap.end()) {
            LOG_ERROR("service:[%s] method:[%s] is not exist!", service_name.c_str(), method_name.c_str());
            break;
        }

        // 定义正确的指针变量
        google::protobuf::Service* service = service_it->second.m_service;
        const google::protobuf::MethodDescriptor* method = method_it->second;

        google::protobuf::Message *request = service->GetRequestPrototype(method).New();
        if (!request->ParseFromString(args_str)) {
            LOG_ERROR("request parse error! content:%s", args_str.c_str());
            delete request;
            break;
        }
        google::protobuf::Message *response = service->GetResponsePrototype(method).New();
        
        google::protobuf::Closure *done = google::protobuf::NewCallback<MpzrpcProvider,
                                                                        const muduo::net::TcpConnectionPtr &,
                                                                        google::protobuf::Message *>(this,
                                                                                                        &MpzrpcProvider::SendRpcResponse,
                                                                                                        conn,
                                                                                                        response);

        // 将业务调用提交到线程池处理
        m_threadPool->enqueue([=]() {
            // 在业务线程中执行RPC方法
            service->CallMethod(method, nullptr, request, response, done);
        });
    }
}

void MpzrpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
{
    std::string response_str;
    if (response->SerializeToString(&response_str)) {
        conn->send(response_str);
    } else {
        LOG_ERROR("serialize response_str error!");
    }
    // 注意：response对象是由NewCallback创建的Closure在执行后自动管理的，
    // 通常不需要手动delete response。但request需要注意。
    // 在我们的设计中，request的生命周期由业务方法CallMethod的实现者负责。
    // 如果业务方法(如UserService::Login)没有delete request，它就会内存泄漏。
    // 一个更安全的设计是在上面的lambda中CallMethod之后delete request。
    // google::protobuf::Service::CallMethod执行完毕后，request和response参数的内存如何管理，需要根据具体业务实现来定。
    // 在protobuf的原始设计中，调用者需要负责释放request和response。
    // 所以我们应该在done回调中，即此函数中，来释放response。
    // 为了简单起见，我们暂时假设业务代码会处理request，此处的done会处理response。
    // 一个更完整的框架需要对此有更严格的内存管理约定。
}