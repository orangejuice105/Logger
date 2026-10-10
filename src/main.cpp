#include "logger.h"
#include <thread>


int main()
{   //准备配置并初始化Logger
    LoggerConfig config; 
    config.batchSize = 5;
    config.filePath = "test.log";
    config.minLevel = LogLevel::WARN;

    //初始化失败返回1
    if(!Logger::getInstance().init(config)){
        return 1;
    }

    std::thread t1([]{
        for(int i=0;i<10;i++)
        {
            Logger::getInstance()
            .log(
                LogLevel::INFO,
                "thread1"
            );
        }
    });


    std::thread t2([]{
        for(int i=0;i<10;i++)
        {
            Logger::getInstance()
            .log(
                LogLevel::ERROR,
                "thread2"
            );
        }
    });


    t1.join();
    t2.join();


    return 0;
}