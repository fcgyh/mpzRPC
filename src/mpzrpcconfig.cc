#include "mpzrpcconfig.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

void MpzrpcConfig::LoadConfigFromFile(const std::string &config_file)
{
    std::ifstream i(config_file);
    if (!i.is_open())
    {
        std::cerr << "Failed to open config file: " << config_file << std::endl;
        return;
    }
    nlohmann::json j;
    try
    {
        i >> j;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return;
    }

    if (j.find("rpcserverip") == j.end() ||
        j.find("rpcserverport") == j.end() ||
        j.find("zookeeperip") == j.end() ||
        j.find("zookeeperport") == j.end() ||
        j.find("muduothreadnum") == j.end())
    {
        std::cerr << "Missing required fields in config file." << std::endl;
        return;
    }
    m_rpcserverip = j["rpcserverip"];
    m_rpcserverport = j["rpcserverport"];
    m_zookeeperip = j["zookeeperip"];
    m_zookeeperport = j["zookeeperport"];
    m_muduoThreadNum = j["muduothreadnum"];
}