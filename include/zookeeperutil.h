#pragma once

#include <semaphore.h>
#include <zookeeper/zookeeper.h>
#include <string>
#include <vector>

// 封装的zk客户端类
class ZkClient
{
public:
    // 获取单例实例
    static ZkClient* getInstance();

    // 在程序启动时调用一次
    void Init(const std::string& host);

    void Create(const char *path, const char *data, int datalen, int state = 0);
    std::string GetData(const char *path);
    std::vector<std::string> GetChildren(const char *path);

private:
    ZkClient();
    ~ZkClient();
    ZkClient(const ZkClient&) = delete;
    ZkClient& operator=(const ZkClient&) = delete;

    zhandle_t *m_zhandle;
};