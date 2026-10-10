#pragma once
#include <mutex>
#include<string>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include <cstddef>

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

struct LoggerConfig //Logger的启动配置，各成员提供默认值
{
    std::string filePath = "server.log";
    std::size_t batchSize = 100;
    LogLevel minLevel = LogLevel::DEBUG; //最低输出级别，只记录达到或高于该级别的日志
};

class Logger{
public:
    static Logger& getInstance(); //获取Logger的唯一实例，返回值是引用，调用者无需释放，也不能delete

    bool init(const LoggerConfig& config); //根据配置初始化日志器，成功返回true

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

    std::condition_variable cv; //有日志或收到停止请求时，唤醒后台线程

    std::thread logThread; //日志处理线程

    std::atomic<bool> stop; //通知日志线程是否可以结束工作,并且保证原子性

    std::ofstream logFile; //声明一个文件输出流对象

    LoggerConfig config_; //保存Logger内部的配置

    bool initialized_ = false; //标记Logger是否已经成功初始化
};