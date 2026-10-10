#include "logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <mutex>
#include <stdexcept>
#include <exception>

// 由 getInstance() 中的静态对象在首次创建时调用
// 这里只初始化成员状态，打开文件和启动后台线程由 init() 完成
Logger::Logger(): stop(false){

}

bool Logger::init(const LoggerConfig& config){
    if(initialized_){
        std::cerr << "重复调用init(),本次初始化请求被拒绝" << std::endl;
        return false;
    }

    if(config.filePath.empty()){
        std::cerr << "路径为空" << std::endl;
        return false;
    }

    if(config.batchSize == 0){
        std::cerr << "批次为零" << std::endl;
        return false;
    }

    config_ = config;

    logFile.clear(); //清除上一次操作留下的错误标志

    //std::ios::out :以输出方式打开文件; std::ios::app :追加模式，每次写入都追加到文件末尾
    logFile.open(config_.filePath, std::ios::out | std::ios::app); //按配置路径打开文件，使用追加模式

    if(!logFile.is_open()){ //打开失败时报告错误并返回false
        std::cerr << "无法打开日志文件: " << config.filePath << '\n';
        return false;
    }

    //try: 尝试执行其中操作，如果操作抛出异常，当前执行流程被打断，转去寻找匹配的catch
    try { 
        // this指向当前调用init()的Logger对象
        logThread = std::thread(&Logger::processLogs, this); //在线程中执行this->processsLogs
    }
    //catch: 接收异常对象并处理
    catch(const std::exception& e) { //std::exception: 标准异常的共同基类，接收线程创建抛出的标准异常
        std::cerr << "启动日志线程失败" << std::endl;
        std::cerr << e.what() << std::endl; //e.what(): 取得异常的描述文字，可以输出到终端
        logFile.close();
        return false;
    }

    initialized_ = true;
    return true;
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
    if (!initialized_) {
        std::cerr << "Logger 尚未初始化，无法记录日志\n";
        return;
    }

    //如果级别低于最小输出级别直接返回
    if(level < config_.minLevel){ 
        return;
    }
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
    logFile  //logFile会自动记录操作的成功或失败，但不会报告错误
        << "[" << msg.time << "] "
        << "[" << levelToString(msg.level) << "] "
        << "[thread:" << msg.threadId << "] "
        << msg.message
        << '\n'; //"\n"只换行不刷新，提高效率

    if(!logFile){
        std::cerr << "日志文件写入失败： " //std::cerr : 标准错误输出流
                  << msg.message << "\n";
    }
}

void Logger::processLogs(){
    while(true){
        std::queue<LogMessage> batch; //局部队列，存放一批要处理的日志

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

            for(std::size_t i = 0;i < config_.batchSize && !logQueue.empty();i++){
                batch.push(std::move(logQueue.front())); //将队首日志移动到本地批次，减少字符串复制
                logQueue.pop();
            }
            

        } //控制锁的区域避免等待outputLogs

        while(!batch.empty()){
            outputLogs(batch.front());
            batch.pop();
        }

        logFile.flush(); //处理完一批之后刷新一次缓冲区
        if(!logFile){
            std::cerr << "日志文件流发生错误，本批日志可能未完整写入\n";
        }
    }
}

Logger::~Logger(){
    {
        std::lock_guard<std::mutex> lock(logmtx);
        stop = true;
    }
    

    cv.notify_all();

    if(logThread.joinable()) //关联了尚未 join 或 detach 的线程时，等待并回收线程
        logThread.join();

    if(logFile.is_open()){
        logFile.close(); //关闭文件

        if (!logFile) {
            std::cerr << "日志文件关闭后处于失败状态\n";
        }
    }  
}