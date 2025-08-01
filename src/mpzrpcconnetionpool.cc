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

MpzrpcConnectionPool::MpzrpcConnectionPool() {}
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
    }
}

spConnection MpzrpcConnectionPool::getConnection(std::string ip, unsigned short port)
{
    std::string host_key = ip + ":" + std::to_string(port);
    std::unique_lock<std::mutex> lock(m_mapMutex);

    int sockfd = -1;
    if (m_connectionMap.count(host_key) && !m_connectionMap[host_key].empty())
    {
        sockfd = m_connectionMap[host_key].front();
        m_connectionMap[host_key].pop();
    }
    else
    {
        lock.unlock();
        sockfd = createConnection(ip, port);
        lock.lock();
    }

    if (sockfd == -1) {
        return nullptr;
    }

    // 创建智能指针，并绑定一个更智能的删除器
    return std::shared_ptr<PooledConnection>(new PooledConnection{sockfd, true, host_key}, 
        [this](PooledConnection* conn){
            if (conn->is_valid) {
                this->returnConnection(conn->host_key, conn->sockfd);
            } else {
                close(conn->sockfd);
            }
            delete conn;
        });
}
