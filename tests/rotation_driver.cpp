#include "logger.h"

#include <iostream>
#include <string>
#include <thread>

// 此程序只负责提交日志。文件检查由 run_tests.py 在进程完全退出后进行，
// 因为 main() 返回之后，单例 Logger 的析构函数才会等待后台线程结束。
int main(int argc, char* argv[])
{
    // argc 是命令行参数数量，argv[0] 是程序名，argv[1] 是测试场景名。
    if (argc != 2) {
        std::cerr << "用法：rotation_driver <场景名>\n";
        return 1;
    }
    const std::string scenario = argv[1];

    LoggerConfig config;
    config.filePath = "test.log";
    config.maxFileSize = 1024;
    config.maxBackupFiles = 3;
    config.batchSize = 5;
    config.queueCapacity = 10000;
    config.minLevel = LogLevel::DEBUG;

    if (scenario == "no_loss") {
        // 100 条日志至多触发 99 次滚动，保留 100 个归档足够检查全部消息。
        config.maxBackupFiles = 100;
    } else if (scenario == "one_backup") {
        config.maxBackupFiles = 1;
    } else if (scenario == "concurrent") {
        config.maxBackupFiles = 160;
    } else if (scenario != "normal" && scenario != "order" &&
               scenario != "long_record" && scenario != "restart" &&
               scenario != "fallback") {
        std::cerr << "未知测试场景：" << scenario << '\n';
        return 1;
    }

    if (!Logger::getInstance().init(config)) {
        return 2;
    }

    if (scenario == "normal" || scenario == "no_loss") {
        const std::string padding(200, 'x');
        for (int i = 0; i < 100; ++i) {
            LOGGER_ERROR("ID=" + std::to_string(i) + " " + padding);
        }
    } else if (scenario == "order") {
        LOGGER_ERROR("FIRST_AFTER_ROTATE");
        LOGGER_ERROR("SECOND_AFTER_ROTATE");
    } else if (scenario == "one_backup") {
        LOGGER_ERROR("ONLY_BACKUP_TEST");
    } else if (scenario == "long_record") {
        LOGGER_ERROR("LONG_BEGIN " + std::string(2048, 'X') + " LONG_END");
        LOGGER_ERROR("AFTER_LONG_RECORD");
    } else if (scenario == "restart") {
        LOGGER_ERROR("RESTART_MARKER");
    } else if (scenario == "fallback") {
        LOGGER_ERROR("FALLBACK_0");
        LOGGER_ERROR("FALLBACK_1");
        LOGGER_ERROR("FALLBACK_2");
    } else if (scenario == "concurrent") {
        std::thread workers[4];
        for (int worker = 0; worker < 4; ++worker) {
            // [worker] 按值捕获，给每个线程保存独立的生产者编号。
            workers[worker] = std::thread([worker] {
                const std::string padding(200, 'x');
                for (int i = 0; i < 40; ++i) {
                    const int id = worker * 40 + i;
                    LOGGER_ERROR("ID=" + std::to_string(id) + " " + padding);
                }
            });
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
    }

    // 所有提交者结束后，丢弃计数不会再因本次测试的提交而增加。
    // 此处不读取文件：后台线程可能还在写，Python 会等整个进程退出。
    const LoggerStats stats = Logger::getInstance().getStats();
    std::cout << "dropped=" << stats.droppedCount
              << " queue_peak=" << stats.queuePeak << '\n';
    if (stats.droppedCount != 0) {
        std::cerr << "测试消息被队列丢弃\n";
        return 3;
    }
    return 0;
}
