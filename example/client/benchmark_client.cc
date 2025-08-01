#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include "mpzrpcapplication.h"
#include "example.service.pb.h"
#include "mpzrpcchannel.h"
#include "mpzrpccontroller.h"

// 全局原子计数器，用于线程安全地统计
std::atomic_int success_count(0);
std::atomic_int error_count(0);

// 工作线程函数
void worker(int thread_id, int requests_per_thread) {
    example::UserRpcService_Stub stub(new MpzrpcChannel());

    for (int i = 0; i < requests_per_thread; ++i) {
        example::LoginRequest request;
        request.set_name("benchmark_user");
        request.set_pwd("123456");
        
        example::LoginResponse response;
        MpzrpcController controller;

        stub.Login(&controller, &request, &response, nullptr);

        if (!controller.Failed() && response.result().errcode() == 0) {
            success_count++;
        } else {
            error_count++;
        }
    }
}

int main(int argc, char **argv) {
    // 初始化框架
    MpzrpcApplication::init(argc, argv);

    // --- 压测参数配置 ---
    const int THREAD_NUM = 8; // 并发线程数
    const int REQUESTS_PER_THREAD = 1000; // 每个线程发送的请求数
    const int TOTAL_REQUESTS = THREAD_NUM * REQUESTS_PER_THREAD;

    std::cout << "===========================================" << std::endl;
    std::cout << "Benchmark starting..." << std::endl;
    std::cout << "Threads: " << THREAD_NUM << std::endl;
    std::cout << "Requests per thread: " << REQUESTS_PER_THREAD << std::endl;
    std::cout << "Total requests: " << TOTAL_REQUESTS << std::endl;
    std::cout << "===========================================" << std::endl;

    // 创建并启动工作线程
    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(worker, i, REQUESTS_PER_THREAD);
    }

    // 等待所有线程完成
    for (auto& th : threads) {
        th.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    // 计算并打印结果
    double qps = success_count / elapsed.count();

    std::cout << "===========================================" << std::endl;
    std::cout << "Benchmark finished." << std::endl;
    std::cout << "Total time: " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Successful requests: " << success_count << std::endl;
    std::cout << "Failed requests: " << error_count << std::endl;
    std::cout << "QPS (Queries Per Second): " << qps << std::endl;
    std::cout << "===========================================" << std::endl;

    return 0;
}
