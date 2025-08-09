#pragma once
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

class MpzrpcChannel : public google::protobuf::RpcChannel
{
public:
    // 所有通过stub代理对象调用的rpc方法，都走到这里了，统一做rpc方法调用的数据数据序列化和网络发送
    void CallMethod(const google::protobuf::MethodDescriptor *method,
                    google::protobuf::RpcController *controller,
                    const google::protobuf::Message *request,
                    google::protobuf::Message *response,
                    google::protobuf::Closure *done) override;

    // 供Watcher回调使用的，用于清空缓存的静态方法
    static void ClearServiceListCache(const std::string& service_path);
private:
    // 服务地址列表的本地缓存
    static std::unordered_map<std::string, std::vector<std::string>> m_serviceListCache;
    // 保护缓存的互斥锁
    static std::mutex m_cacheMutex;
};
