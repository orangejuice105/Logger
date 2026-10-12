#!/usr/bin/env python3
"""测量 Logger 的提交和完整处理耗时；临时二进制和日志在结束后自动清理。"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time


PROJECT = Path(__file__).resolve().parents[1]
BUILD_FLAGS = ["-std=c++17", "-O2", "-DNDEBUG", "-Wall", "-Wextra",
               "-Wpedantic", "-pthread"]


def positive_integer(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("参数必须大于 0")
    return number


def environment(workspace):
    cpu = "unknown"
    info = Path("/proc/cpuinfo")
    if info.exists():
        for line in info.read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    memory_kib = None
    memory = Path("/proc/meminfo")
    if memory.exists():
        for line in memory.read_text().splitlines():
            if line.startswith("MemTotal:"):
                memory_kib = int(line.split()[1])
                break
    filesystem = "unknown"
    try:
        result = subprocess.run(
            ["findmnt", "-T", str(workspace), "-n", "-o", "FSTYPE"],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode == 0:
            filesystem = result.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    compiler = subprocess.run(
        ["g++", "--version"], check=True, capture_output=True, text=True, timeout=5
    ).stdout.splitlines()[0]
    hashes = {}
    for name in ("include/logger.h", "src/logger.cpp",
                 "benchmarks/benchmark_driver.cpp", "benchmarks/run_benchmark.py"):
        hashes[name] = hashlib.sha256((PROJECT / name).read_bytes()).hexdigest()
    return {
        "system": platform.platform(),
        "cpu": cpu,
        "logical_cpus": os.cpu_count(),
        "available_cpus": len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "memory_kib": memory_kib,
        "filesystem": filesystem,
        "compiler": compiler,
        "python": platform.python_version(),
        "build_flags": BUILD_FLAGS,
        "source_sha256": hashes,
    }


def run_once(binary, folder, case, messages, message_bytes):
    folder.mkdir()
    command = [str(binary), str(case["threads"]), str(messages), str(message_bytes),
               str(case["batch_size"]), str(case["queue_capacity"])]
    # 计时包含进程启动、初始化、提交、后台队列排空和文件关闭。
    begin = time.perf_counter()
    result = subprocess.run(command, cwd=folder, capture_output=True, timeout=120)
    end = time.perf_counter()
    if result.returncode != 0 or result.stderr:
        raise RuntimeError("基线进程失败：" + result.stderr.decode("utf-8", errors="replace"))
    metrics = json.loads(result.stdout)
    if metrics["submitted"] != messages or not 0 <= metrics["dropped"] <= messages:
        raise RuntimeError("提交或丢弃计数异常")
    if metrics["queue_peak"] > case["queue_capacity"]:
        raise RuntimeError("共享队列峰值超过容量")

    # 文件验证不计入耗时。流式读取，避免把整份日志再次载入内存。
    path = folder / "benchmark.log"
    suffix = b" " + b"x" * message_bytes + b"\n"
    written = 0
    with path.open("rb") as stream:
        for line in stream:
            if not line.endswith(suffix):
                raise RuntimeError("文件含有截断或非预期日志")
            written += 1
    if written + metrics["dropped"] != messages:
        raise RuntimeError("写入条数 + 丢弃条数不等于提交条数")
    if list(folder.glob("benchmark.log.*")):
        raise RuntimeError("基线不应触发滚动或归档淘汰")
    full_seconds = end - begin
    metrics.update({
        "written": written,
        "file_bytes": path.stat().st_size,
        "full_ms": full_seconds * 1000,
        "attempts_per_second": messages / (metrics["submit_ms"] / 1000),
        "written_per_second": written / full_seconds,
        "drop_percent": metrics["dropped"] * 100.0 / messages,
    })
    # 当前轮计数已验证，删掉日志，避免多轮测试叠加占用临时磁盘。
    path.unlink()
    return metrics


def main():
    parser = argparse.ArgumentParser(description="Logger 性能基线（默认临时文件自动清理）")
    parser.add_argument("--messages", type=positive_integer, default=100000,
                        help="每轮总提交条数，默认 100000")
    parser.add_argument("--message-bytes", type=positive_integer, default=128,
                        help="每条 ASCII 正文字节数，默认 128")
    parser.add_argument("--repeats", type=positive_integer, default=3,
                        help="每种配置正式测量次数，默认 3")
    parser.add_argument("--output", type=Path, help="可选：保存环境和逐轮结果的 JSON 路径")
    args = parser.parse_args()
    if args.messages < 4:
        parser.error("总提交条数至少为 4，以便比较四个生产线程")
    cases = [
        {"name": "single_batch1", "threads": 1, "batch_size": 1, "queue_capacity": args.messages},
        {"name": "single_batch100", "threads": 1, "batch_size": 100, "queue_capacity": args.messages},
        {"name": "four_batch1", "threads": 4, "batch_size": 1, "queue_capacity": args.messages},
        {"name": "four_batch100", "threads": 4, "batch_size": 100, "queue_capacity": args.messages},
        {"name": "four_default_queue", "threads": 4, "batch_size": 100, "queue_capacity": 10000},
    ]
    try:
        with tempfile.TemporaryDirectory(prefix="logger-benchmark-") as temporary:
            workspace = Path(temporary)
            binary = workspace / "benchmark_driver"
            command = ["g++"] + BUILD_FLAGS + [
                "-I" + str(PROJECT / "include"),
                str(PROJECT / "benchmarks" / "benchmark_driver.cpp"),
                str(PROJECT / "src" / "logger.cpp"), "-o", str(binary),
            ]
            subprocess.run(command, check=True, timeout=60)
            report = {
                "measured_at": datetime.now(timezone.utc).isoformat(),
                "environment": environment(workspace),
                "settings": {"messages": args.messages, "message_bytes": args.message_bytes,
                             "repeats": args.repeats, "level": "INFO", "min_level": "INFO",
                             "rotation": "avoided by maximum size threshold",
                             "warmup_messages": min(args.messages, 10000)},
                "cases": [],
            }
            for case in cases:
                # 每种配置单独预热一次，不计入正式结果；所有轮次使用全新文件。
                warmup = run_once(binary, workspace / (case["name"] + "_warmup"), case,
                                  min(args.messages, 10000), args.message_bytes)
                runs = []
                for number in range(args.repeats):
                    folder = workspace / (case["name"] + "_" + str(number + 1))
                    runs.append(run_once(binary, folder, case, args.messages, args.message_bytes))
                keys = ("submit_ms", "mean_call_us", "full_ms", "attempts_per_second",
                        "written_per_second", "written", "dropped", "queue_peak", "drop_percent")
                median = {key: statistics.median(run[key] for run in runs) for key in keys}
                report["cases"].append(dict(case, warmup=warmup, runs=runs, median=median))
                print("{}: 调用均值 {:.3f} us，提交 {:.2f} ms，完整 {:.2f} ms，"
                      "写入 {:.0f} 条，丢弃 {:.0f} 条 ({:.2f}%)".format(
                          case["name"], median["mean_call_us"], median["submit_ms"],
                          median["full_ms"], median["written"], median["dropped"],
                          median["drop_percent"]), flush=True)
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                       encoding="utf-8")
                print("报告已保存：", args.output)
        print("临时二进制和日志已自动清理。")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("基线运行失败：", error)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
