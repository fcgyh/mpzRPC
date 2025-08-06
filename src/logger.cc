#include "logger.h"
#include <time.h>
#include <iostream>
#include <stdarg.h> // 用于处理可变参数

// 获取日志的单例
Logger &Logger::GetInstance()
{
    static Logger logger;
    return logger;
}

// 构造函数
Logger::Logger() : m_loglevel(INFO) // 默认日志级别为INFO
{
    // 启动专门的写日志线程
    m_writeLogTask = std::thread([&]()
    {
        FILE *pf = nullptr;
        std::string current_date = "";

        for (;;)
        {
            // 获取当前的日期
            time_t now = time(nullptr);
            tm *nowtm = localtime(&now);
            char date_buf[32];
            sprintf(date_buf, "%d-%d-%d", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday);

            // 【核心改动】如果日期变化，或者文件句柄为空，则切换/打开日志文件
            if (current_date != date_buf || pf == nullptr) {
                if (pf != nullptr) {
                    fclose(pf);
                }
                
                std::string file_name = std::string(date_buf) + "-log.txt";
                pf = fopen(file_name.c_str(), "a+");
                if (pf == nullptr)
                {
                    std::cout << "logger file : " << file_name << " open error!" << std::endl;
                    // 在实际项目中，这里可以有更复杂的错误处理
                    // 为避免程序退出，我们可以尝试稍后重新打开
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                current_date = date_buf;
            }

            // 从阻塞队列中取出一个日志消息
            LogMessage log_msg = m_lckQue.Pop();
            
            // 优雅停机的检查
            if (log_msg.m_level == FATAL && log_msg.m_msg == "EXIT") {
                if (pf != nullptr) fclose(pf);
                break; // 收到退出信号，跳出循环，线程结束
            }

            // 格式化时间戳
            char time_buf[128] = {0};
            sprintf(time_buf, "%d:%d:%d", nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec);

            // 格式化日志级别
            const char* level_str;
            switch (log_msg.m_level) {
                case DEBUG: level_str = "[DEBUG]"; break;
                case INFO:  level_str = "[INFO]";  break;
                case WARN:  level_str = "[WARN]";  break;
                case ERROR: level_str = "[ERROR]"; break;
                case FATAL: level_str = "[FATAL]"; break;
                default:    level_str = "[UNKNOWN]"; break;
            }

            // 写入日志文件
            fprintf(pf, "%s %s %s\n", time_buf, level_str, log_msg.m_msg.c_str());
            fflush(pf); // 强制刷新缓冲区，确保日志立即写入
        }
    });
}

// 析构函数，用于优雅停机
Logger::~Logger()
{
    // 发送一个特殊的退出消息
    LogMessage exit_msg = {FATAL, "EXIT"};
    m_lckQue.Push(exit_msg);

    // 等待写日志线程结束
    if (m_writeLogTask.joinable()) {
        m_writeLogTask.join();
    }
}

// 设置日志级别
void Logger::SetLogLevel(LogLevel level)
{
    m_loglevel = level;
}

// 写日志
void Logger::Log(LogLevel level, const char* format, ...)
{
    // 如果当前日志级别低于设置的全局级别，则直接忽略
    if (level < m_loglevel) {
        return;
    }

    char buf[1024] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    LogMessage log_msg = {level, std::string(buf)};
    m_lckQue.Push(log_msg);

    // 如果是FATAL级别的日志，通常意味着程序无法继续运行
    if (level == FATAL) {
        // 在实际项目中，这里可以增加dump堆栈等操作
        // 为了简单，这里直接退出
        // 注意：这会导致析构函数无法执行，部分日志可能丢失
        // 一个更完善的设计是等待日志写完再退出
        // exit(EXIT_FAILURE); 
    }
}
