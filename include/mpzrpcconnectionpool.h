#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <condition_variable>
#include <chrono>

#include "mpzrpcapplication.h"

// 被智能指针管理的连接对象
struct PooledConnection {
    int sockfd;
    bool is_valid; // 标志此连接是否仍然有效
    std::string host_key; // 用于归还时查找对应的队列
};

using spConnection = std::shared_ptr<PooledConnection>;

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
    void returnConnection(const std::string& host, int sockfd);

    void loadConfig();

    // 核心数据结构：一个map，管理到不同主机的连接队列
    std::unordered_map<std::string, std::queue<int>> m_connectionMap;
    // 每个主机已创建的连接数
    std::unordered_map<std::string, std::atomic_int> m_connectionCountMap;
    std::mutex m_mapMutex;
    std::condition_variable m_cv;

    // 配置参数
    int m_initSize;
    int m_maxSize;
    int m_poolTimeout;
};
