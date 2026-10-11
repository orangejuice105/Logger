#pragma once
#include <mutex>
#include<string>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include <cstddef>
#include <cstdint>

//通用日志宏：提交日志，并自动附带调用处的文件名和行号
//使用宏定义方便传参
#define LOGGER_LOG(level, message) \
    Logger::getInstance().log((level), (message), __FILE__, __LINE__)

//专用宏：固定日志级别的快捷宏
#define LOGGER_DEBUG(message) LOGGER_LOG(LogLevel::DEBUG, (message))
#define LOGGER_INFO(message) LOGGER_LOG(LogLevel::INFO, (message))
#define LOGGER_WARN(message) LOGGER_LOG(LogLevel::WARN, (message))
#define LOGGER_ERROR(message) LOGGER_LOG(LogLevel::ERROR, (message))

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
    std::string file; //file: 提交日志的源文件名
    int line = 0; //line: 提交日志的源码行号
};

struct LoggerConfig //Logger的启动配置，各成员提供默认值
{
    std::string filePath = "server.log";
    std::size_t batchSize = 100;
    LogLevel minLevel = LogLevel::DEBUG; //最低输出级别，只记录达到或高于该级别的日志
    std::size_t queueCapacity = 10000; //共享日志队列允许积压的最大条数，默认10000条
    std::uintmax_t maxFileSize = 10*1024*1024; //当前日志文件的滚动大小阈值，单位：字节
    std::size_t maxBackupFiles = 3; //最多保留的归档日志文件数量，不包含当前文件
};

struct LoggerStats{
    std::size_t droppedCount = 0; //记录因共享队列已满而丢弃的新日志数量
    std::size_t queuePeak = 0; //共享队列长度的历史最大值
};

class Logger{
public:
    static Logger& getInstance(); //获取Logger的唯一实例，返回值是引用，调用者无需释放，也不能delete

    bool init(const LoggerConfig& config); //根据配置初始化日志器，成功返回true
    
    //提交日志，接收级别、正文以及调用处的文件名和行号
    void log( //正文通过 const 引用接收，避免按值传参造成的字符串复制
        LogLevel level,
        const std::string& message,        
        const char* file,
        int line
    );

    LoggerStats getStats();

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

    LoggerStats stats_; //保存累计丢弃数量和共享队列峰值

    std::uintmax_t curFileSize_ = 0; //当前文件的字节计数，启动时读取已有大小，之后由后台线程更新

    std::string formatLog(const LogMessage& msg); //将一条LogMessage转换成完整的日志字符串

    bool rotateFiles(); //滚动日志文件，操作成功返回true

    bool fileOutputFailed_ = false; //文件输出发生故障后，由后台线程将后续日志改写到标准错误流
};