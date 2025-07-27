#include "mpzrpcprovider.h"
#include "mpzrpcapplication.h"
#include <stdlib.h>
#include <functional>
#include "rpcheader.pb.h"
#include "logger.h"
#include "zookeeperutil.h"

void MpzrpcProvider::run()
{
    // 读取配置文件rpcserver的信息
    std::string ip = MpzrpcApplication::getApp().getConfig().getRpcServerIp();
    uint16_t port = MpzrpcApplication::getApp().getConfig().getRpcServerPort();
    int muduoThreadum = MpzrpcApplication::getApp().getConfig().getMuduoThreadNum();

    // 创建TcpServer对象
    muduo::net::InetAddress address(ip, port);
    muduo::net::EventLoop loop;
    muduo::net::TcpServer server(&loop, address, "RpcProvider");

    // 绑定连接回调和消息读写回调方法  分离了网络代码和业务代码
    server.setConnectionCallback(std::bind(&MpzrpcProvider::onConnectionCallback, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&MpzrpcProvider::onMessageCallback,
                                        this,
                                        std::placeholders::_1,
                                        std::placeholders::_2,
                                        std::placeholders::_3));
    // 设置muduo库的线程数量
    server.setThreadNum(muduoThreadum);

    ZkClient zkCli;
    zkCli.Start();
    for (auto &sp : m_servicemap)
    {
        // /service_name   /UserServiceRpc
        std::string service_path = "/" + sp.first;
        zkCli.Create(service_path.c_str(), nullptr, 0);
        for (auto &mp : sp.second.m_methodmap)
        {
            // /service_name/method_name   /UserServiceRpc/Login 存储当前这个rpc服务节点主机的ip和port
            std::string method_path = service_path + "/" + mp.first;
            char method_path_data[128] = {0};
            sprintf(method_path_data, "%s:%d", ip.c_str(), port);
            // ZOO_EPHEMERAL表示znode是一个临时性节点
            zkCli.Create(method_path.c_str(), method_path_data, strlen(method_path_data), ZOO_EPHEMERAL);
        }
    }

    // rpc服务端准备启动，打印信息
    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;

    // 启动subEventLoop用户事件处理服务，启动网络服务接受用户连接
    server.start();
    loop.loop();
};

void MpzrpcProvider::publishService(::google::protobuf::Service *service)
{
    ServiceInfo servic_info;

    // 获取了服务对象的描述信息
    const google::protobuf::ServiceDescriptor *service_des = service->GetDescriptor();
    // 获取服务的名字
    std::string service_name = service_des->name();
    // 获取服务对象service的方法的数量
    int methodnum = service_des->method_count();
    for (int i = 0; i < methodnum; ++i)
    {
        // 获取了服务对象指定下标的服务方法的描述（抽象描述）
        const google::protobuf::MethodDescriptor *method_des = service_des->method(i);
        servic_info.m_methodmap.insert({method_des->name(), method_des});
    }

    servic_info.m_service = service;
    m_servicemap.insert({service_name, servic_info});
};

void MpzrpcProvider::onMessageCallback(const muduo::net::TcpConnectionPtr &conn,
                                     muduo::net::Buffer *buffer,
                                     muduo::Timestamp receiveTime)
{
    // 外层循环，用于正确处理TCP粘包、半包问题
    while (buffer->readableBytes() >= 4)
    {
        // 1. 从缓冲区中“偷窥”出头部长度（4字节），但不移动读指针
        uint32_t header_size = buffer->peekInt32();

        // 2. 检查数据是否足够解析出完整的RPC头部
        if (buffer->readableBytes() < 4 + header_size)
        {
            // 数据不足，这是一个“半包”，退出循环等待下一次数据到达
            break;
        }

        // 3. 解析头部，获取参数长度
        rpcheader::rpcheader header;
        if (!header.ParseFromArray(buffer->peek() + 4, header_size))
        {
            // 头部反序列化失败，记录日志并关闭连接
            LOG_ERR("header parse error!");
            conn->shutdown();
            break;
        }
        
        uint32_t args_size = header.args_size();
        uint32_t total_size = 4 + header_size + args_size;

        // 4. 检查数据是否足够构成一个完整的RPC请求包
        if (buffer->readableBytes() < total_size)
        {
            // 参数部分数据不完整，是“半包”，退出循环
            break;
        }

        // =================================================================
        //  到这里，说明已收到了一个完整的RPC请求包，开始处理
        // =================================================================

        // 5. 从缓冲区中正式取走数据
        buffer->retrieve(4 + header_size); // 取走header_size和header_str
        std::string args_str = buffer->retrieveAsString(args_size);

        // 6. 获取服务名和方法名
        std::string service_name = header.service_name();
        std::string method_name = header.method_name();

        // 7. 查找服务与方法
        auto service_it = m_servicemap.find(service_name);
        if (service_it == m_servicemap.end())
        {
            LOG_ERR("service:[%s] is not exist!", service_name.c_str());
            break; 
        }

        auto method_it = service_it->second.m_methodmap.find(method_name);
        if (method_it == service_it->second.m_methodmap.end())
        {
            LOG_ERR("service:[%s] method:[%s] is not exist!", service_name.c_str(), method_name.c_str());
            break;
        }

        const google::protobuf::MethodDescriptor *method_des = method_it->second;

        // 8. 准备请求和响应对象
        google::protobuf::Message *request = service_it->second.m_service->GetRequestPrototype(method_des).New();
        if (!request->ParseFromString(args_str))
        {
            LOG_ERR("request parse error! content:%s", args_str.c_str());
            delete request;
            break;
        }
        google::protobuf::Message *response = service_it->second.m_service->GetResponsePrototype(method_des).New();
        
        // 9. 绑定用于发送响应的回调函数
        google::protobuf::Closure *done = google::protobuf::NewCallback<MpzrpcProvider,
                                                                        const muduo::net::TcpConnectionPtr &,
                                                                        google::protobuf::Message *>(this,
                                                                                                        &MpzrpcProvider::SendRpcResponse,
                                                                                                        conn,
                                                                                                        response);
        // 10. 调用本地业务方法
        service_it->second.m_service->CallMethod(method_des, nullptr, request, response, done);
    }
}

void MpzrpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
{
    std::string send_str;
    if (!response->SerializeToString(&send_str))
    {
        LOG_ERR("%s", "message response serialization failed");
    }
    else
    {
        // 序列化成功后，通过网络把rpc方法执行的结果发送会rpc的调用方
        conn->send(send_str);
    }
    conn->shutdown(); // 模拟http的短链接服务，由rpcprovider主动断开连接
}