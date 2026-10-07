#include "logger.h"
#include <thread>


int main()
{
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