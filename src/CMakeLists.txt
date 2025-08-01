# 查找当前目录下的所有源文件
aux_source_directory(. DIR_SRCS)

# 生成静态链接库 libmpzrpc.a
add_library(mpzrpc ${DIR_SRCS})

# 为 mpzrpc 库添加头文件路径和所有依赖库
# PUBLIC 关键字会自动将这些依赖传递给链接到 mpzrpc 的任何目标
target_include_directories(mpzrpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(mpzrpc PUBLIC 
    muduo_net 
    muduo_base 
    protobuf 
    zookeeper_mt 
    pthread
)
