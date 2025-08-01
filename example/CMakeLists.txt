# 自动查找 protos 目录下的源文件
aux_source_directory(${PROJECT_SOURCE_DIR}/example/protos PROTO_SRC)

# 设置可执行文件的输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# -------------------- 服务端目标 --------------------
add_executable(serve 
    service/UserRpcService.cc
    ${PROTO_SRC})
# 只需要链接 mpzrpc 即可
target_link_libraries(serve mpzrpc)

# -------------------- 客户端目标 --------------------
# 1. 功能测试客户端 (client)
add_executable(client 
    client/UerRpcClient.cc
    ${PROTO_SRC})
target_link_libraries(client mpzrpc)

# 2. 性能压测客户端 (benchmark_client)
add_executable(benchmark_client
    client/benchmark_client.cc
    ${PROTO_SRC})
target_link_libraries(benchmark_client mpzrpc)

# -------------------- 安装目标 --------------------
install(TARGETS serve client benchmark_client DESTINATION ../bin)
