#include "mpzrpcapplication.h"
#include <unistd.h>
#include <string>

#include "zookeeperutil.h"

MpzrpcApplication &MpzrpcApplication::getApp()
{
    static MpzrpcApplication app;
    return app;
}

void MpzrpcApplication::init(int argc, char **argv)
{
    if (argc < 2)
    {
        showArgsHelp();
        exit(EXIT_FAILURE);
    }
    else
    {
        int o;
        std::string config_file;
        const char *optstring = "c:";
        while ((o = getopt(argc, argv, optstring)) != -1)
        {
            switch (o)
            {
            case 'c':
                config_file = optarg;
                break;

            default:
                break;
            }
        }

        // 1. 加载配置文件
        getConfig().LoadConfigFromFile(config_file);

        // 2. 初始化Zookeeper客户端连接
        std::string host = getConfig().getZooKeeperIp() + ":" + std::to_string(getConfig().getZooKeeperPort());
        ZkClient::getInstance()->Init(host);
    }
};

MpzrpcConfig &MpzrpcApplication::getConfig()
{
    static MpzrpcConfig m_config;
    return m_config;
};