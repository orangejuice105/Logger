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
#include <filesystem>
#include <system_error>

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

    if(config.queueCapacity == 0){
        std::cerr << "队列容量为零" << std::endl;
        return false;
    }

    if(config.maxFileSize == 0){
        std::cerr << "日志滚动阈值为零" << std::endl;
        return false;
    }

    if(config.maxBackupFiles == 0){
        std::cerr << "最多保留的归档日志文件数量为零" << std::endl;
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

    //查询日志文件大小
    std::error_code ec; //ec: 接收错误信息
    std::uintmax_t size = 
        std::filesystem::file_size(config_.filePath,ec);
    
    if(ec){
        std::cerr << "查询日志文件大小失败："
          << config_.filePath << ","
          << ec.message() << '\n'; //ec.message(): ec保存的错误描述
        logFile.close();
        return false;
    }

    curFileSize_ = size;

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

                                                          //文件名参数使用 const char* 接收，传参时不复制文件名字符
void Logger::log(LogLevel level,const std::string& message,const char* file,int line){
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
    logMsg.file = file;
    logMsg.line = line;

    {   
        std::lock_guard<std::mutex> lock(logmtx);
        //队列达到容量上限时，丢弃本次新日志
        if(logQueue.size() >= config_.queueCapacity){
            stats_.droppedCount++;
            return;
        }
        logQueue.push(std::move(logMsg));
        if(logQueue.size() > stats_.queuePeak){
            stats_.queuePeak = logQueue.size();
        }
    }
    cv.notify_one();
}

void Logger::outputLogs(const LogMessage& msg){
    const std::string text = formatLog(msg);
    
    //先检查文件输出是否异常
    if (fileOutputFailed_) {
        std::cerr << text;
        return;
    }

    //判断是否需要滚动
    const bool needRotate = 
    curFileSize_ > 0 &&
    (
        curFileSize_ >= config_.maxFileSize ||
        text.size() > config_.maxFileSize - curFileSize_
    );

    if(needRotate && !rotateFiles()){
        fileOutputFailed_ = true;
        std::cerr << text;
        return;
    }

    logFile << text; //将text内容写入logFile

    if(!logFile){
        fileOutputFailed_ = true;
        std::cerr << "日志文件写入失败，后续日志改用标准错误流\n";
        std::cerr << text;
        return;
    }

    curFileSize_ += text.size();
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

        //只在文件输出正常时刷新
        if(!fileOutputFailed_){
          logFile.flush(); //处理完一批之后刷新一次缓冲区
            if(!logFile){
                std::cerr << "日志文件流发生错误，本批日志可能未完整写入\n";
                fileOutputFailed_ = true;
            }  
        }
        
    }
}

LoggerStats Logger::getStats(){
    std::lock_guard<std::mutex> lock(logmtx);
    return stats_;
}

std::string Logger::formatLog(const LogMessage& msg){
    std::ostringstream formatted;

    //拼接完整日志字符串
    formatted
        << "[" << msg.time << "] "
        << "[" << levelToString(msg.level) << "] "
        << "[thread:" << msg.threadId << "] "
        << "[" << msg.file << ":" << msg.line << "] "
        << msg.message
        << '\n';

    return formatted.str();
}

bool Logger::rotateFiles(){ //进行滚动，流程：刷新、关闭、删除、移动、改名、重开文件、计数归零
    logFile.flush();

    if(!logFile){
        std::cerr << "刷新失败" << std::endl;
        return false;
    }

    logFile.close();

    if(!logFile){
        std::cerr << "关闭失败: " << config_.filePath << std::endl;
        return false;
    }

    //滚动时要删除的最旧文件
    const std::string oldest = config_.filePath + '.' + std::to_string(config_.maxBackupFiles);
    
    std::error_code ec;
    std::filesystem::remove(oldest,ec);

    if(ec){
         std::cerr << "删除失败"
          << ec.message() << '\n'; //ec.message(): ec保存的错误描述
        return false;
    }

    for(std::size_t i = config_.maxBackupFiles;i > 1;i--){
        const std::string source = config_.filePath + '.' + std::to_string(i - 1); //待改文件名
        const std::string target = config_.filePath + '.' + std::to_string(i); //目标文件名

        //检查来源是否存在
        const bool sourceExists =
            std::filesystem::exists(source, ec);

        if(ec){
            std::cerr << "查询失败" 
             << ec.message() << '\n';
            return false;
        }

        //存在则执行重命名
        if(sourceExists){
            std::filesystem::rename(source,target,ec);
            if(ec){
                std::cerr << "重命名失败" 
                 << ec.message() << '\n';
                 return false;
            }
        }
        //不存在跳过这一轮
        else {
            continue;
        }
    }

    //移动完后重命名当前文件
    std::filesystem::rename(
        config_.filePath,
        config_.filePath + ".1",
        ec
    );

    if(ec){
        std::cerr << "重命名失败" 
         << ec.message() << '\n';
        return false;
    }

    //再重新打开原来的路径
    logFile.clear();

    logFile.open(config_.filePath,std::ios::out | std::ios::app);

    if(!logFile){
        std::cerr << "打开失败" << std::endl;
        return false;
    }

    curFileSize_ = 0;

    return true;
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