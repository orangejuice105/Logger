#include "logger.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// 每个进程只初始化一次 Logger。进程退出时由 Logger 析构排空队列；
// run_benchmark.py 在等待进程退出后，记录完整处理耗时并核对文件条数。
int main(int argc, char* argv[])
{
    if (argc != 6) {
        std::cerr << "参数：线程数 总提交条数 正文字节数 批次大小 队列容量\n";
        return 1;
    }

    std::size_t threadCount;
    std::size_t messageCount;
    std::size_t messageBytes;
    std::size_t batchSize;
    std::size_t queueCapacity;
    try {
        std::size_t values[5];
        for (int i = 0; i < 5; ++i) {
            const std::string argument = argv[i + 1];
            if (argument.empty() || argument.find_first_not_of("0123456789") !=
                std::string::npos) {
                throw std::invalid_argument("参数必须是正整数");
            }
            const unsigned long long value = std::stoull(argument);
            if (value == 0 || value > std::numeric_limits<std::size_t>::max()) {
                throw std::out_of_range("参数超出范围");
            }
            values[i] = static_cast<std::size_t>(value);
        }
        threadCount = values[0];
        messageCount = values[1];
        messageBytes = values[2];
        batchSize = values[3];
        queueCapacity = values[4];
        if (threadCount > 64 || messageCount < threadCount) {
            throw std::invalid_argument("线程数应为 1～64，总条数不能少于线程数");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    LoggerConfig config;
    config.filePath = "benchmark.log";
    config.minLevel = LogLevel::INFO;
    config.batchSize = batchSize;
    config.queueCapacity = queueCapacity;
    // 本基线测量异步提交、消费和刷新，避免文件滚动及归档淘汰干扰计数。
    config.maxFileSize = std::numeric_limits<std::uintmax_t>::max();
    config.maxBackupFiles = 1;
    Logger& logger = Logger::getInstance();
    if (!logger.init(config)) {
        return 2;
    }

    const std::string message(messageBytes, 'x');
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    std::vector<std::uint64_t> callNanoseconds(threadCount, 0);
    std::mutex startMutex;
    std::condition_variable readyCondition;
    std::condition_variable startCondition;
    std::size_t readyCount = 0;
    bool started = false;
    bool cancelled = false;

    try {
        for (std::size_t worker = 0; worker < threadCount; ++worker) {
            workers.emplace_back([&, worker] {
                {
                    std::unique_lock<std::mutex> lock(startMutex);
                    ++readyCount;
                    readyCondition.notify_one();
                    startCondition.wait(lock, [&] { return started; });
                    if (cancelled) {
                        return;
                    }
                }
                const std::size_t ownCount = messageCount / threadCount +
                    (worker < messageCount % threadCount ? 1 : 0);
                std::uint64_t elapsed = 0;
                for (std::size_t i = 0; i < ownCount; ++i) {
                    const auto before = std::chrono::steady_clock::now();
                    LOGGER_INFO(message);
                    const auto after = std::chrono::steady_clock::now();
                    elapsed += static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            after - before).count());
                }
                // 各线程先在局部累计，最后写入各自的槽位，不共享计数器。
                callNanoseconds[worker] = elapsed;
            });
        }
    } catch (const std::exception& error) {
        {
            std::lock_guard<std::mutex> lock(startMutex);
            cancelled = true;
            started = true;
        }
        startCondition.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
        std::cerr << "创建生产线程失败：" << error.what() << '\n';
        return 2;
    }

    // 等所有生产线程准备好，再统一开始计时，避免把创建线程时间计入提交阶段。
    std::unique_lock<std::mutex> lock(startMutex);
    readyCondition.wait(lock, [&] { return readyCount == threadCount; });
    const auto begin = std::chrono::steady_clock::now();
    started = true;
    lock.unlock();
    startCondition.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }
    const auto end = std::chrono::steady_clock::now();

    std::uint64_t totalNanoseconds = 0;
    for (std::uint64_t elapsed : callNanoseconds) {
        totalNanoseconds += elapsed;
    }
    const double submitMilliseconds =
        std::chrono::duration<double, std::milli>(end - begin).count();
    const double meanCallMicroseconds =
        static_cast<double>(totalNanoseconds) / messageCount / 1000.0;
    const LoggerStats stats = logger.getStats();
    std::cout << std::fixed << std::setprecision(6)
              << "{\"submitted\":" << messageCount
              << ",\"dropped\":" << stats.droppedCount
              << ",\"queue_peak\":" << stats.queuePeak
              << ",\"submit_ms\":" << submitMilliseconds
              << ",\"mean_call_us\":" << meanCallMicroseconds << "}\n";
    return 0;
}
