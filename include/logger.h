#pragma once

#include <string>
#include <atomic>

#include "lockqueue.h"


// 定义日志级别
enum LogLevel
{
    DEBUG,  // 调试信息
    INFO,   // 普通信息
    WARN,   // 警告信息
    ERROR,  // 错误信息
    FATAL,  // 致命错误，会导致程序退出
};

// 定义一个更通用的日志宏
// 通过可变参数宏，可以方便地传递格式化字符串和参数
#define LOG(level, logmsgformat, ...) \
    do \
    { \
        Logger &logger = Logger::GetInstance(); \
        logger.Log(level, logmsgformat, ##__VA_ARGS__); \
    } while(0)

// 定义方便使用的具体级别的宏
#define LOG_DEBUG(logmsgformat, ...) LOG(DEBUG, logmsgformat, ##__VA_ARGS__)
#define LOG_INFO(logmsgformat, ...)  LOG(INFO, logmsgformat, ##__VA_ARGS__)
#define LOG_WARN(logmsgformat, ...)  LOG(WARN, logmsgformat, ##__VA_ARGS__)
#define LOG_ERROR(logmsgformat, ...) LOG(ERROR, logmsgformat, ##__VA_ARGS__)
#define LOG_FATAL(logmsgformat, ...) LOG(FATAL, logmsgformat, ##__VA_ARGS__)


// Mpzrpc框架提供的日志系统
class Logger
{
public:
    // 获取日志的单例
    static Logger &GetInstance();
    
    // 设置全局日志级别，只有高于等于此级别的日志才会被记录
    void SetLogLevel(LogLevel level);

    // 写日志
    void Log(LogLevel level, const char* format, ...);

private:
    // 日志消息结构体，包含级别和消息
    struct LogMessage {
        LogLevel m_level;
        std::string m_msg;
    };

    std::atomic<LogLevel> m_loglevel;   // 记录日志级别，设为原子类型保证线程安全
    LockQueue<LogMessage> m_lckQue;     // 日志缓冲队列，现在存放结构体
    std::thread m_writeLogTask;         // 写日志线程，不再detach

    Logger();
    ~Logger(); // 析构函数，用于优雅停机
    Logger(const Logger &) = delete;
    Logger(Logger &&) = delete;
};
