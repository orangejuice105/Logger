#include "logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <mutex>


Logger::Logger(): stop(false){
    //构造函数执行时，this指向正在被构造的Logger对象,即(instance)
    //第一次调用Logger::getInstance()时执行，只执行一次
    logThread = std::thread(&Logger::processLogs, this); //在线程中执行this->processsLogs
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
    std::tm local_tm; //自己准备一个 tm 对象（分配在栈上，安全）
    
    // 调用 localtime_r，传入时间戳地址和tm对象地址
    // 函数会把计算结果直接写进 local_tm 里
    localtime_r(&now, &local_tm);

    // 访问方式要改成 . 因为local_tm是对象不是指针
    int year = local_tm.tm_year + 1900; //年从1900开始算
    int mon = local_tm.tm_mon + 1; //月的范围是0-11
    int day = local_tm.tm_mday; //日期范围1-31
    int hour = local_tm.tm_hour; //小时范围0-23
    int min = local_tm.tm_min; //分钟范围0-59
    int sec = local_tm.tm_sec; //秒的范围0-60 (60用于闰秒)

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
    LogMessage logMsg; //声明一个LogMessage对象

    logMsg.level = level;
    logMsg.message = message;
    logMsg.threadId = std::this_thread::get_id();
    logMsg.time = getCurrentTime();

    {   
        std::lock_guard<std::mutex> lock(logmtx);
        logQueue.push(std::move(logMsg));
    }
    cv.notify_one();
}

void Logger::outputLogs(const LogMessage& msg)
{
    std::cout
        << "[" << msg.time << "] "
        << "[" << levelToString(msg.level) << "] "
        << "[thread:" << msg.threadId << "] "
        << msg.message
        << std::endl;
}

void Logger::processLogs(){
    while(true){
        LogMessage logMsg;

        {
            std::unique_lock<std::mutex> lock(logmtx);
            cv.wait(lock,[this]{
                //通过程序是否退出和队列是否为空判断线程是否继续睡眠
                return stop || !logQueue.empty();
            });

            //程序退出并且日志处理完了结束
            if(stop && logQueue.empty())
            {
                break;
            }

            logMsg = std::move(logQueue.front()); //move直接移动不拷贝
            logQueue.pop();

        } //控制锁的区域避免等待outputLogs

        outputLogs(logMsg);
    }
}

Logger::~Logger(){
    stop = true;

    cv.notify_all();

    if(logThread.joinable()) //判断线程是否真实存在，存在则等待其执行结束
        logThread.join();
}