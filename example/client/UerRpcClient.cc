#include <iostream>

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

    // 演示连续发起三次RPC调用
    for (int i = 0; i < 3; ++i)
    {
        // 3. 准备请求和响应对象
        example::LoginRequest request;
        request.set_name("zhang san");
        request.set_pwd("123456");
        
        example::LoginResponse response;

        // 4. 创建 Controller 对象来接收调用状态
        MpzrpcController controller;

        std::cout << "----------------> " << "RPC Call " << i+1 << " <----------------" << std::endl;

        // 5. 发起RPC调用
        stub.Login(&controller, &request, &response, nullptr); 

        // 6. 检查调用结果
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
    }

    return 0;
}