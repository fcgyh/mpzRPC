#include "zookeeperutil.h"
#include "mpzrpcapplication.h"
#include <semaphore.h>
#include <iostream>

// 全局的watcher观察器   zkserver给zkclient的通知
void global_watcher(zhandle_t *zh, int type,
                    int state, const char *path, void *watcherCtx)
{
    if (type == ZOO_SESSION_EVENT) // 回调的消息类型是和会话相关的消息类型
    {
        if (state == ZOO_CONNECTED_STATE) // zkclient和zkserver连接成功
        {
            sem_t *sem = (sem_t *)zoo_get_context(zh);
            sem_post(sem);
        }
    }
}

ZkClient::ZkClient() : m_zhandle(nullptr)
{
}

ZkClient::~ZkClient()
{
    if (m_zhandle != nullptr)
    {
        zookeeper_close(m_zhandle); // 关闭句柄，释放资源  MySQL_Conn
    }
}

ZkClient* ZkClient::getInstance()
{
    static ZkClient zk;
    return &zk;
}

// 连接zkserver
void ZkClient::Init(const std::string& host)
{
    m_zhandle = zookeeper_init(host.c_str(), global_watcher, 30000, nullptr, nullptr, 0);
    if (nullptr == m_zhandle)
    {
        std::cout << "zookeeper_init error!" << std::endl;
        exit(EXIT_FAILURE);
    }

    sem_t sem;
    sem_init(&sem, 0, 0);
    zoo_set_context(m_zhandle, &sem);

    sem_wait(&sem);
    std::cout << "zookeeper_init success!" << std::endl;
}

// Create方法，支持处理已存在的永久节点
void ZkClient::Create(const char *path, const char *data, int datalen, int state)
{
    char path_buffer[128];
    int bufferlen = sizeof(path_buffer);
    
    // 直接尝试创建节点
    int flag = zoo_create(m_zhandle, path, data, datalen,
                        &ZOO_OPEN_ACL_UNSAFE, state, path_buffer, bufferlen);
    
    if (flag == ZOK)
    {
        std::cout << "znode create success... path:" << path << std::endl;
    }
    else
    {
        // 如果我们创建的是一个永久性节点，并且它已经存在，
        if (flag == ZNODEEXISTS && state == 0)
        {
            // 永久性父节点已存在，无需处理，这符合预期
        }
        else
        {
            // 对于其他所有情况，都视为严重错误
            std::cout << "flag:" << flag << std::endl;
            std::cout << "znode create error... path:" << path << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}

// 根据指定的path，获取znode节点的值
std::string ZkClient::GetData(const char *path)
{
    char buffer[64];
    int bufferlen = sizeof(buffer);
    int flag = zoo_get(m_zhandle, path, 0, buffer, &bufferlen, nullptr);
    if (flag != ZOK)
    {
        std::cout << "get znode error... path:" << path << std::endl;
        return "";
    }
    else
    {
        return buffer;
    }
}

// 获取指定路径下的所有子节点
std::vector<std::string> ZkClient::GetChildren(const char *path)
{
    String_vector children;
    int rc = zoo_get_children(m_zhandle, path, 0, &children);
    if (rc != ZOK)
    {
        std::cout << "get children error... path:" << path << std::endl;
        return {};
    }

    std::vector<std::string> children_vec;
    for (int i = 0; i < children.count; ++i)
    {
        children_vec.push_back(children.data[i]);
    }
    // Zookeeper C API返回的String_vector需要手动释放
    deallocate_String_vector(&children);
    return children_vec;
}