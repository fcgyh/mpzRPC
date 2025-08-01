#include <iostream>
#include <thread>
#include <chrono>

#include "example.service.pb.h"
#include "mpzrpcapplication.h"
#include "mpzrpcprovider.h"

class UserService : public example::UserRpcService
{
public:
    // 这是一个普通的本地业务方法，为了清晰，我们可以暂时忽略它
    bool LoginBusiness(const std::string &name, const std::string &pwd)
    {
        // 假设这里是真正的业务逻辑
        return pwd == "123";
    }

    // 重写的RPC方法
    void Login(::google::protobuf::RpcController *controller,
               const ::example::LoginRequest *request,
               ::example::LoginResponse *response,
               ::google::protobuf::Closure *done) override // 建议加上override关键字
    {
        // 1. 从request中获取参数
        std::string name = request->name();
        std::string pwd = request->pwd();

        // 2. 业务操作
        std::cout << "Start processing Login request for " << name << "..." << std::endl;
        // std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "Finished processing Login request for " << name << "." << std::endl;
    
        
        // 3. 执行真正的业务
        bool ret = LoginBusiness(name, pwd); 

        // 4. 填充响应
        response->set_success(ret);
        example::ResultCode *result_code = response->mutable_result();
        result_code->set_errcode(0);
        result_code->set_errmsg("");

        // 5. 执行回调，通知框架发送响应
        done->Run();
    };
};

int main(int argc, char **argv)
{
    // 框架初始化
    MpzrpcApplication::init(argc, argv);

    // 创建Provider
    MpzrpcProvider provider;
    
    // 发布服务
    provider.publishService(new UserService());

    // 启动服务
    provider.run();

    return 0;
};