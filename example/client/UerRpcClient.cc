#include <iostream>
#include <thread>
#include <chrono>

#include "mpzrpcapplication.h"
#include "example.service.pb.h"
#include "mpzrpcchannel.h"
#include "mpzrpccontroller.h"

int main(int argc, char **argv)
{
    // 1. 初始化框架
    MpzrpcApplication::init(argc, argv);

    // 2. 创建一个 Stub 实例
    example::UserRpcService_Stub stub(new MpzrpcChannel());

    // 3. 进入一个无限循环，模拟持续的RPC调用
    int call_count = 0;
    while (true)
    {
        // 准备请求和响应对象
        example::LoginRequest request;
        request.set_name("zhang san");
        request.set_pwd("123456");
        
        example::LoginResponse response;

        // 创建 Controller 对象来接收调用状态
        MpzrpcController controller;

        std::cout << "================> RPC Call " << ++call_count << " ================" << std::endl;

        // 发起RPC调用
        stub.Login(&controller, &request, &response, nullptr); 

        // 检查调用结果
        if (controller.Failed())
        {
            std::cout << "RPC call failed: " << controller.ErrorText() << std::endl;
        }
        else
        {
            if (0 == response.result().errcode())
            {
                std::cout << "RPC login response success: " << response.success() << std::endl;
            }
            else
            {
                std::cout << "RPC login response error: " << response.result().errmsg() << std::endl;
            }
        }

        // 每隔2秒发起一次调用
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}
