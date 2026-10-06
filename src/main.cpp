#include "logger.h"
#include <iostream>
#include <thread>

int main(){
    for(int i = 0;i < 100;i++){
        std::thread t1([] {
            Logger::getInstance().log(LogLevel::INFO, "我是线程 1 的日志");
        });
        std::thread t2([] {
            Logger::getInstance().log(LogLevel::ERROR, "我是线程 2 的日志");
        });

        t1.join();
        t2.join();
    }
    
    return 0;
}