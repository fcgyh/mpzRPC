#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

// 负载均衡策略的抽象基类
class LoadBalanceStrategy
{
public:
    virtual ~LoadBalanceStrategy() {}
    virtual std::string select(const std::vector<std::string>& hosts) = 0;
};

// 轮询策略
class RoundRobinStrategy : public LoadBalanceStrategy
{
public:
    RoundRobinStrategy() : m_index(0) {}
    std::string select(const std::vector<std::string>& hosts) override;
private:
    std::atomic_size_t m_index;
};

// 随机策略
class RandomStrategy : public LoadBalanceStrategy
{
public:
    std::string select(const std::vector<std::string>& hosts) override;
};


// 负载均衡器
class LoadBalancer
{
public:
    static LoadBalancer* getInstance();

    // 设置负载均衡策略
    void setStrategy(std::unique_ptr<LoadBalanceStrategy> strategy);

    // 从服务列表中选择一个主机地址
    std::string selectHost(const std::vector<std::string>& hosts);

private:
    LoadBalancer();
    ~LoadBalancer();
    LoadBalancer(const LoadBalancer&) = delete;
    LoadBalancer& operator=(const LoadBalancer&) = delete;

    std::unique_ptr<LoadBalanceStrategy> m_strategy;
    std::mutex m_mutex;
};
