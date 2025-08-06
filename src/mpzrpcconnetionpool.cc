#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

#include "mpzrpcconnectionpool.h"

MpzrpcConnectionPool* MpzrpcConnectionPool::getInstance()
{
    static MpzrpcConnectionPool pool;
    return &pool;
}

MpzrpcConnectionPool::MpzrpcConnectionPool() 
{
    loadConfig();
}

MpzrpcConnectionPool::~MpzrpcConnectionPool()
{
    std::lock_guard<std::mutex> lock(m_mapMutex);
    for (auto& pair : m_connectionMap)
    {
        auto& queue = pair.second;
        while (!queue.empty())
        {
            int fd = queue.front();
            queue.pop();
            close(fd);
        }
    }
}

void MpzrpcConnectionPool::loadConfig() {
    m_initSize = MpzrpcApplication::getApp().getConfig().getPoolInitSize();
    m_maxSize = MpzrpcApplication::getApp().getConfig().getPoolMaxSize();
    m_poolTimeout = MpzrpcApplication::getApp().getConfig().getPoolTimeout();
}

int MpzrpcConnectionPool::createConnection(std::string ip, unsigned short port)
{
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) return -1;
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(clientfd);
        return -1;
    }
    return clientfd;
}

void MpzrpcConnectionPool::returnConnection(const std::string& host, int sockfd)
{
    if (sockfd != -1) {
        std::lock_guard<std::mutex> lock(m_mapMutex);
        m_connectionMap[host].push(sockfd);
        m_cv.notify_one();
    }
}

spConnection MpzrpcConnectionPool::getConnection(std::string ip, unsigned short port)
{
    std::string host_key = ip + ":" + std::to_string(port);
    std::unique_lock<std::mutex> lock(m_mapMutex);

    // 首次请求该主机，初始化连接计数器和初始连接
    if (m_connectionCountMap.find(host_key) == m_connectionCountMap.end()) {
        m_connectionCountMap[host_key] = 0;
        for (int i = 0; i < m_initSize; ++i) {
            int sockfd = createConnection(ip, port);
            if (sockfd != -1) {
                m_connectionMap[host_key].push(sockfd);
                m_connectionCountMap[host_key]++;
            }
        }
    }

    // 用一个循环来处理所有情况
    while(true) {
        // 条件1: 池中有可用连接
        if (!m_connectionMap[host_key].empty()) {
            int sockfd = m_connectionMap[host_key].front();
            m_connectionMap[host_key].pop();
            lock.unlock();

            return std::shared_ptr<PooledConnection>(new PooledConnection{sockfd, true, host_key}, 
                [this](PooledConnection* conn){
                    if (conn->is_valid) {
                        this->returnConnection(conn->host_key, conn->sockfd);
                    } else {
                        close(conn->sockfd);
                        std::lock_guard<std::mutex> lock(m_mapMutex);
                        m_connectionCountMap[conn->host_key]--;
                    }
                    delete conn;
                });
        }

        // 条件2: 池中无连接，但未达到最大连接数
        if (m_connectionCountMap[host_key].load() < m_maxSize) {
            lock.unlock(); // 解锁去创建连接
            int sockfd = createConnection(ip, port);
            lock.lock(); // 重新加锁以更新计数器

            if (sockfd != -1) {
                m_connectionCountMap[host_key]++;
                return std::shared_ptr<PooledConnection>(new PooledConnection{sockfd, true, host_key}, 
                    [this](PooledConnection* conn){
                        if (conn->is_valid) {
                            this->returnConnection(conn->host_key, conn->sockfd);
                        } else {
                            close(conn->sockfd);
                            std::lock_guard<std::mutex> lock(m_mapMutex);
                            m_connectionCountMap[conn->host_key]--;
                        }
                        delete conn;
                    });
            } else {
                return nullptr; // 创建失败
            }
        }
        
        // 条件3: 池中无连接，且已达到最大连接数，必须等待
        if (m_cv.wait_for(lock, std::chrono::milliseconds(m_poolTimeout)) == std::cv_status::timeout) {
            // 等待超时后，再次检查队列是否为空
            if (m_connectionMap[host_key].empty()) {
                std::cerr << "Get connection timeout... fail!" << std::endl;
                return nullptr;
            }
        }
        // 如果被notify唤醒，循环会继续，并在下一次迭代时从条件1中获取连接
    }
}