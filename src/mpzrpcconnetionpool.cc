#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

#include "mpzrpcconnectionpool.h"

// 获取连接池单例对象
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

// 根据 ip 和 port 创建一个新连接
int MpzrpcConnectionPool::createConnection(std::string ip, unsigned short port)
{
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) {
        std::cerr << "create socket error!" << std::endl;
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "connect " << ip << ":" << port << " error!" << std::endl;
        close(clientfd);
        return -1;
    }
    
    return clientfd;
}

// 归还一个连接
void MpzrpcConnectionPool::returnConnection(std::string host, int sockfd)
{
    if (sockfd != -1) {
        std::lock_guard<std::mutex> lock(m_mapMutex);
        m_connectionMap[host].push(sockfd);
    }
}

// 根据 ip 和 port 获取一个连接
spConnection MpzrpcConnectionPool::getConnection(std::string ip, unsigned short port)
{
    std::string host = ip + ":" + std::to_string(port);
    std::unique_lock<std::mutex> lock(m_mapMutex);

    // 检查是否有到这个主机的连接池且池中有连接
    if (m_connectionMap.find(host) != m_connectionMap.end() && !m_connectionMap[host].empty())
    {
        // 连接池中有可用连接
        int sockfd = m_connectionMap[host].front();
        m_connectionMap[host].pop();
        lock.unlock(); 

        // 返回一个智能指针，它在析构时会自动归还连接
        return std::shared_ptr<int>(new int(sockfd), [=](int* p_sockfd){
            returnConnection(host, *p_sockfd);
            delete p_sockfd;
        });
    }
    
    // 无可用连接，需要创建新连接
    lock.unlock(); // 创建连接是耗时操作，先解锁
    int sockfd = createConnection(ip, port);
    if (sockfd == -1) {
        return nullptr;
    }
    
    // 返回智能指针，并绑定自定义删除器用于归还连接
    return std::shared_ptr<int>(new int(sockfd), [=](int* p_sockfd){
        returnConnection(host, *p_sockfd);
        delete p_sockfd;
    });
}
