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

private:
    std::string m_rpcserverip;
    int m_rpcserverport;
    std::string m_zookeeperip;
    int m_zookeeperport;
    int m_muduoThreadNum;
    int m_businessThreadNum;
};