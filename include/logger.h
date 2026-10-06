#pragma once
#include <mutex>
#include<string>

enum class LogLevel //表示日志等级
{
    DEBUG,
    INFO,
    WARN,
    ERROR
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

    Logger(const Logger&) = delete; //禁止拷贝构造 
    Logger& operator=(const Logger&) = delete; //禁止赋值

    std::mutex logmtx; //防止外部误用
};