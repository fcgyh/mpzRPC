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
        exit(EXIT_FAILURE); // 致命错误，直接退出
    }
    
    nlohmann::json j;
    try
    {
        i >> j;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        exit(EXIT_FAILURE); // 错误，直接退出
    }
    
    // 读取必要的配置项
    if (j.find("rpcserverip") == j.end() ||
        j.find("rpcserverport") == j.end() ||
        j.find("zookeeperip") == j.end() ||
        j.find("zookeeperport") == j.end() ||
        j.find("muduothreadnum") == j.end())
    {
        std::cerr << "Missing required fields in config file." << std::endl;
        exit(EXIT_FAILURE); // 致命错误，直接退出
    }

    m_rpcserverip = j["rpcserverip"];
    m_rpcserverport = j["rpcserverport"];
    m_zookeeperip = j["zookeeperip"];
    m_zookeeperport = j["zookeeperport"];
    m_muduoThreadNum = j["muduothreadnum"];

    // 读取可选的RPC调用超时配置
    if (j.find("rpccalltimeout") != j.end())
    {
        m_rpcCallTimeout = j["rpccalltimeout"];
    }
    else
    {
        m_rpcCallTimeout = 5000; // 默认5秒
    }

    // 读取可选的业务线程池数量配置
    if (j.find("businessthreadnum") != j.end())
    {
        m_businessThreadNum = j["businessthreadnum"];
    }
    else
    {
        // 如果配置文件中没有指定，则使用一个默认值
        m_businessThreadNum = 4; 
    }

    // 读取可选的连接池配置
    if (j.find("poolinitsize") != j.end()) 
    {
         m_poolInitSize = j["poolinitsize"]; 
    }
    else 
    { 
        m_poolInitSize = 2; 
    }

    if (j.find("poolmaxsize") != j.end()) 
    { 
        m_poolMaxSize = j["poolmaxsize"]; 
    }
    else 
    { 
        m_poolMaxSize = 10; 
    }

    if (j.find("pooltimeout") != j.end()) 
    { 
        m_poolTimeout = j["pooltimeout"]; 
    }
    else 
    { 
        m_poolTimeout = 1000; 
    }
}