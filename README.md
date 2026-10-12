# Logger

一个使用 C++17 实现的异步文件日志学习项目。生产线程提交消息，后台线程
按批次写入和刷新，支持级别过滤、有界队列、源码位置和按文件大小滚动。
项目用于练习并发、文件操作及测试，为后续 TCP / HTTP Server 项目做准备。

## 编译与运行

当前面向 Linux / WSL，时间转换使用 POSIX `localtime_r`。需要支持 C++17
`std::filesystem` 的编译器和 CMake 3.13 或更新版本；测试和基线脚本还需要
Python 3、g++，不需要第三方 Python 包。

在项目目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/main
```

以上使用 `-S` 指定源码目录、`-B` 指定构建目录，参数说明见
[CMake 官方命令行文档](https://cmake.org/cmake/help/v3.13/manual/cmake.1.html)。
本次已使用 CMake 3.22.1 验证构建。

当前示例最低级别为 WARN，提交 INFO、ERROR 和 WARN 消息，INFO 被过滤。
输出追加到运行目录下的 `test.log`，生产线程完成后打印丢弃数量和队列峰值。
这些统计不代表后台已经写完；程序正常退出时 Logger 析构会等待消费结束。

## 使用方式

在启动生产线程之前完成初始化，检查返回值：

```cpp
#include "logger.h"

int main()
{
    LoggerConfig config;
    config.filePath = "server.log";
    config.minLevel = LogLevel::INFO;
    config.batchSize = 100;
    config.queueCapacity = 10000;
    config.maxFileSize = 10 * 1024 * 1024;
    config.maxBackupFiles = 3;

    if (!Logger::getInstance().init(config)) {
        return 1;
    }

    LOGGER_DEBUG("调试消息"); //被最低级别过滤
    LOGGER_INFO("服务器启动");
    LOGGER_WARN("连接处理超时");
    LOGGER_ERROR("处理请求失败");

    const LoggerStats stats = Logger::getInstance().getStats();
    // stats.droppedCount：队列满时被拒绝的新日志数量
    // stats.queuePeak：共享队列历史最大条数
    (void)stats;
    return 0;
}
```

也可以使用通用入口：

```cpp
LOGGER_LOG(LogLevel::INFO, "服务器启动");
```

宏自动附带调用处的 `__FILE__` 和 `__LINE__`。直接调用时需要自己提供来源：

```cpp
Logger::getInstance().log(LogLevel::INFO, "服务器启动", __FILE__, __LINE__);
```

消息正文为普通字符串，当前没有 `{}` 占位符格式化。宏参数中的表达式会先
求值，再进入 `log()` 检查级别；例如生成消息的函数不会因级别过滤自动跳过。

输出示例：

```text
[2026-10-12 10:00:00] [INFO] [thread:12345] [/path/main.cpp:20] 服务器启动
```

时间使用本地时间，精度为秒。线程 ID 属于提交日志的线程，消息保存提交时的
时间和来源；跨线程输出顺序以进入共享队列的顺序为准。

## 启动配置

| 字段 | 默认值 | 含义 |
|---|---|---|
| `filePath` | `server.log` | 当前日志文件路径，相对路径以进程工作目录为基准 |
| `batchSize` | `100` | 每次从共享队列取出的最大条数 |
| `minLevel` | `DEBUG` | 最低输出级别，顺序为 DEBUG、INFO、WARN、ERROR |
| `queueCapacity` | `10000` | 共享队列允许积压的最大条数 |
| `maxFileSize` | `10485760` | 滚动大小阈值，单位字节，默认 10 MiB |
| `maxBackupFiles` | `3` | 最多保留的归档文件数量，不包含当前文件 |

路径不能空，其余四个数量或大小字段必须大于 0。配置由 Logger 保存副本；
成功初始化后不支持重复初始化、运行中改配置或停止后重启。初始化失败可在
没有并发生产者的情况下调整配置后重试。父目录需要提前存在，Logger 不会
自动创建父目录。

## 队列与统计

生产线程先检查初始化和级别，再创建消息，在队列锁内检查容量并入队。队列
满时直接丢弃本次新消息，增加 `droppedCount`，不阻塞等待空位。

后台线程被条件变量唤醒后，最多取出 `batchSize` 条到局部队列，释放锁后
格式化、写文件，并在这批结束时刷新一次。批次大小是上限，不会等待凑满。

`getStats()` 返回加锁读取的统计快照。`queuePeak` 只计算共享队列，不包含
后台局部批次；容量按条数限制，不是严格的字节内存上限。`droppedCount` 不
统计级别过滤、文件错误或因归档保留策略删除的历史日志。当前 `log()` 返回
void，没有逐条提交成功状态，也不会保存被队列拒绝的正文。

## 滚动与重启

打开文件采用追加模式，启动时读取已有大小。写入下一条完整日志前，若当前
文件非空且写入后会超过阈值，则先滚动。保留三个归档时的顺序：

```text
删除 server.log.3
server.log.2 → server.log.3
server.log.1 → server.log.2
server.log   → server.log.1
打开新的 server.log，写入当前消息
```

`.1` 是最近归档，最大编号是最旧归档。单条日志超过阈值时允许完整写进空
文件，下一条再触发滚动，因此阈值不是绝对文件大小上限。已有超限文件也会
在下一条日志到来时归档。有限保留策略会主动淘汰最早的日志。

同一组文件由一个进程管理；当前不支持多个进程同时写入或外部工具并发改名。

## 错误与退出

- 未初始化时提交会报告错误并返回；打开文件、读取大小或创建后台线程失败
  时，`init()` 报告原因并返回 false。
- 写入、刷新或滚动失败后，后台线程停止继续写文件和滚动，后续完整日志
  改写到 `std::cerr`。写入或滚动失败时，当前消息也尝试输出到这个渠道。
- 刷新失败可能影响已经消费的整批日志，替代输出不会恢复这批内容，也不
  回滚已完成的归档移动。当前没有文件输出自动恢复或文件故障统计接口。
- 正常退出时，在所有生产线程结束后，单例析构设置停止标记、唤醒并等待
  后台线程处理完队列，然后关闭文件。当前没有公开的 flush/shutdown 接口。

标准错误流是替代输出渠道，不是持久化保障。文件刷新也不等于 `fsync()`。
异常终止进程时，尚未消费的队列内容可能丢失。

## 功能测试

```bash
python3 tests/run_tests.py
python3 tests/run_tests.py --case order
```

九项自动检查覆盖有限保留、完整写入、归档顺序、一个归档、超长日志、重启
追加、已有文件达到阈值、删除失败后的替代输出，以及四线程提交。
详细检查方法见 [tests/README.md](tests/README.md)。测试结果默认保留在终端
打印的临时目录中，方便查看；本次收尾复核已通过 9/9 项。

## 性能基线

```bash
python3 benchmarks/run_benchmark.py
python3 benchmarks/run_benchmark.py --output /tmp/logger-benchmark.json
```

基线比较单线程与四线程、批次 1 与 100、足够大的队列与默认容量；记录调用
平均耗时、提交阶段耗时、完整处理耗时、写入量、丢弃量和共享队列峰值。
完整处理计时等待进程退出，并在计时结束后检查文件条数，避免把丢弃日志当成
完整写入吞吐量。生成的临时二进制和日志自动清理。

测量定义和局限见 [benchmarks/README.md](benchmarks/README.md)，本次实际
测量结果见 [benchmarks/BASELINE.md](benchmarks/BASELINE.md)。

## 项目结构

```text
include/logger.h             公共接口、配置、统计和日志宏
src/logger.cpp               异步队列、文件输出和滚动实现
src/main.cpp                 使用示例
tests/                       功能测试入口和自动验证脚本
benchmarks/                  性能测量入口、脚本和初始结果
```

Logger 已完成当前学习阶段的功能。下一步进入阻塞 TCP Echo Server，再逐步
学习非阻塞 socket、epoll、连接缓冲区和 HTTP；接入服务器时再整理独立库构建。
