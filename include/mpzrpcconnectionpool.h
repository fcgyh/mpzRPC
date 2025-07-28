#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <unordered_map>

// TCP连接的C++封装，这里我们只需要fd
// 在更复杂的实现中，可以封装成一个包含fd和心跳时间的Connection对象
using spConnection = std::shared_ptr<int>;

// 连接池，改为管理到不同主机的连接集合
class MpzrpcConnectionPool
{
public:
    // 获取连接池单例对象
    static MpzrpcConnectionPool* getInstance();

    // 根据 ip 和 port 获取一个连接
    spConnection getConnection(std::string ip, unsigned short port);

private:
    // 单例模式
    MpzrpcConnectionPool();
    ~MpzrpcConnectionPool();
    MpzrpcConnectionPool(const MpzrpcConnectionPool&) = delete;
    MpzrpcConnectionPool& operator=(const MpzrpcConnectionPool&) = delete;

    // 创建一个到指定主机的新连接
    int createConnection(std::string ip, unsigned short port);
    
    // 归还一个连接到它对应的池中
    void returnConnection(std::string host, int sockfd);

    // 核心数据结构：一个map，管理到不同主机的连接队列
    std::unordered_map<std::string, std::queue<int>> m_connectionMap;
    std::mutex m_mapMutex;
};
