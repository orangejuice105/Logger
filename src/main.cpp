#include "logger.h"
#include <thread>
#include <iostream>

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
            LOGGER_LOG(LogLevel::INFO,"thread1");
        }
    });


    std::thread t2([]{
        for(int i=0;i<10;i++)
        {
            LOGGER_LOG(LogLevel::ERROR,"thread2");
        }
    });

    std::thread t3([]{
        for(int i=0;i<10;i++)
        {
            LOGGER_WARN("thread3");
        }
    });


    t1.join();
    t2.join();
    t3.join();

    LoggerStats stats = Logger::getInstance().getStats(); //先从getInstance取得唯一对象的引用再调用getStats
    std::cout << "队列满导致的丢弃数量："
          << stats.droppedCount << '\n';
    std::cout << "队列峰值："
          << stats.queuePeak << '\n';

    return 0;
}