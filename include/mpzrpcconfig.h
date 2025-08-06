#pragma once

#include <iostream>
#include <string>

class MpzrpcConfig
{
public:
    void LoadConfigFromFile(const std::string &config_file);

    const std::string &getRpcServerIp() const { return m_rpcserverip; };
    const int &getRpcServerPort() const { return m_rpcserverport; };
    const std::string &getZooKeeperIp() const { return m_zookeeperip; };
    const int &getZooKeeperPort() const { return m_zookeeperport; };
    const int &getMuduoThreadNum() const { return m_muduoThreadNum; };
    const int &getBusinessThreadNum() const { return m_businessThreadNum; };
    const int &getRpcCallTimeout() const { return m_rpcCallTimeout; };
    const int &getPoolInitSize() const { return m_poolInitSize; };
    const int &getPoolMaxSize() const { return m_poolMaxSize; };
    const int &getPoolTimeout() const { return m_poolTimeout; };

private:
    std::string m_rpcserverip;
    int m_rpcserverport;
    std::string m_zookeeperip;
    int m_zookeeperport;
    int m_muduoThreadNum;
    int m_businessThreadNum;
    int m_rpcCallTimeout; // RPC调用超时时间
    int m_poolInitSize;
    int m_poolMaxSize;
    int m_poolTimeout;
};