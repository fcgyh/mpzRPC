#include <cstdlib>
#include <ctime>

#include "mpzrpcloadbalancer.h"

// 轮询策略的实现
std::string RoundRobinStrategy::select(const std::vector<std::string>& hosts)
{
    if (hosts.empty()) return "";
    return hosts[m_index++ % hosts.size()];
}

// 随机策略的实现
std::string RandomStrategy::select(const std::vector<std::string>& hosts)
{
    if (hosts.empty()) return "";
    return hosts[rand() % hosts.size()];
}


// LoadBalancer 的实现
LoadBalancer* LoadBalancer::getInstance()
{
    static LoadBalancer balancer;
    return &balancer;
}

// 构造函数中设置默认策略为轮询
LoadBalancer::LoadBalancer()
{
    // 设置随机数种子
    srand((unsigned int)time(nullptr));
    // 默认使用轮询策略
    m_strategy = std::make_unique<RoundRobinStrategy>();
}

LoadBalancer::~LoadBalancer() {}

void LoadBalancer::setStrategy(std::unique_ptr<LoadBalanceStrategy> strategy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (strategy)
    {
        m_strategy = std::move(strategy);
    }
}

std::string LoadBalancer::selectHost(const std::vector<std::string>& hosts)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_strategy)
    {
        return m_strategy->select(hosts);
    }
    // 如果没有设置策略，可以返回空或第一个作为降级
    return hosts.empty() ? "" : hosts[0];
}
