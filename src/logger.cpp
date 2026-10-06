#include "logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <mutex>

Logger::Logger(){

}

//实现getInstance
Logger& Logger::getInstance() //Logger&: 返回单例的引用
{
    static Logger instance; //static: 表示变量只在第一次创建一次等到程序结束才销毁

    return instance;
}

std::string levelToString(LogLevel level){ //将强类型枚举(enum class)转化为string类型
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARN";
    default:
        return "UNKNOWN";
    }
}

std::string getCurrentTime() //获取当前时间
{
    time_t now = std::time(nullptr); //获取当前时间，单位为s

    //运用c++标准库中的std::tm结构体,tm_year等为结构体内成员变量
    std::tm* local = std::localtime(&now); //转换位本地时间结构体,将now指向的时间戳转换为本地时区的tm结构，并返回指向静态tm对象的指针
    int year = local->tm_year + 1900; //年从1900开始算
    int mon = local->tm_mon + 1; //月的范围是0-11
    int day = local->tm_mday; //日期范围1-31
    int hour = local->tm_hour; //小时范围0-23
    int min = local->tm_min; //分钟范围0-59
    int sec = local->tm_sec; //秒的范围0-60 (60用于闰秒)

    std::stringstream realtime; //stringstream用来拼接和格式化数据,是流对象
     
    // std::setw(2) 表示占2位，std::setfill('0') 表示不足补0
    realtime << year << "-"
             << std::setw(2) << std::setfill('0') << mon << "-"
             << std::setw(2) << std::setfill('0') << day << " "
             << std::setw(2) << std::setfill('0') << hour << ":"
             << std::setw(2) << std::setfill('0') << min << ":"
             << std::setw(2) << std::setfill('0') << sec;

    //.str()把流内部缓冲区里积累的所有内容拷贝一份包装成string对象返回
    return realtime.str();
}

void Logger::log(LogLevel level,const std::string& message){
    //lock_guard负责加锁和自动解锁，性能比unique_lock更好
    std::lock_guard<std::mutex> lock(logmtx); //log为Logger类成员函数，可以访问类内所有成员
    std::cout
        << "["
        << getCurrentTime()
        << "] "
        << "["
        << levelToString(level)
        << "] "
        << "[thread:"
        << std::this_thread::get_id()
        << "] "
        << message
        << std::endl;
}
