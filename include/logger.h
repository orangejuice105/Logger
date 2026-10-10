#pragma once
#include <mutex>
#include<string>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <fstream>

enum class LogLevel //表示日志等级
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

struct LogMessage
{
    LogLevel level;
    std::string time;
    std::string message;
    std::thread::id threadId;
};

class Logger{
public:
    static Logger& getInstance(); //获取Logger的唯一实例，返回值是引用，调用者无需释放，也不能delete

    void log( //声明成员函数log，接收 LogLevel 和 const std::string&(加&表示普通引用，避免拷贝)
        LogLevel level,
        const std::string& message
    );

private:
    Logger(); //私有构造函数，禁止在类外直接创建Logger对象，外部只能通过getInstance获取唯一实例

    ~Logger(); 

    void processLogs();

    void outputLogs(const LogMessage& msg); //加&不拷贝，性能好

    Logger(const Logger&) = delete; //禁止拷贝构造 
    Logger& operator=(const Logger&) = delete; //禁止赋值

    std::mutex logmtx; //保护日志队列，保证多线程访问安全

    std::queue<LogMessage> logQueue; //日志队列，不允许外部调用

    std::condition_variable cv; //有线程时告知后台

    std::thread logThread; //日志处理线程

    std::atomic<bool> stop; //通知日志线程是否可以结束工作,并且保证原子性

    std::ofstream logFile; //声明一个文件输出流对象
};