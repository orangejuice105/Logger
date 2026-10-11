#!/usr/bin/env python3
"""编译 C++ 测试入口，等它完全退出，再自动检查日志文件。仅使用 Python 标准库。"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


PROJECT = Path(__file__).resolve().parents[1]
OLD_CURRENT = b"CURRENT_OLD\n" + b"A" * 1024 + b"\n"


def require(condition, message):
    """不满足预期就抛出异常，调用处会将这项测试记为失败。"""
    if not condition:
        raise AssertionError(message)


def run_driver(binary, folder, scenario):
    # subprocess.run 等待进程退出，包括 Logger 析构时的队列排空和文件关闭。
    result = subprocess.run(
        [str(binary), scenario], cwd=folder, capture_output=True, timeout=15
    )
    # 保存两种输出，出现故障时可以直接打开文件查看。
    (folder / "stdout.txt").write_bytes(result.stdout)
    (folder / "stderr.txt").write_bytes(result.stderr)
    require(
        result.returncode == 0,
        "测试入口退出码为 {}：{}".format(
            result.returncode, result.stderr.decode("utf-8", errors="replace")
        ),
    )
    require(b"dropped=0 " in result.stdout, "提交日志时发生了队列丢弃")
    return result


def read_log(folder, name="test.log"):
    path = folder / name
    require(path.is_file(), "缺少日志文件：" + name)
    return path.read_bytes()


def log_files(folder):
    # 编号大的归档更旧；先按数值倒序读归档，最后读当前文件。
    # 不能按文件名字母排序，否则 .10 会排在 .2 前面。
    backups = []
    for path in folder.iterdir():
        match = re.fullmatch(r"test\.log\.(\d+)", path.name)
        if match and path.is_file():
            backups.append((int(match.group(1)), path))
    backups.sort(key=lambda item: item[0], reverse=True)
    current = folder / "test.log"
    require(current.is_file(), "缺少当前日志文件")
    return [path for _, path in backups] + [current]


def check_short_files(folder):
    for path in log_files(folder):
        require(0 < path.stat().st_size <= 1024,
                "短日志文件大小异常：" + path.name)


def record_ids(folder):
    ids = []
    for path in log_files(folder):
        data = path.read_bytes()
        require(data.endswith(b"\n"), "日志末尾缺少换行：" + path.name)
        for line in data.splitlines():
            match = re.search(rb" ID=(\d+) x{200}$", line)
            require(match is not None, "日志不完整或出现了非预期内容：" + path.name)
            ids.append(int(match.group(1)))
    return ids


def require_quiet(result):
    require(not result.stderr, "正常场景不应输出文件错误：" +
            result.stderr.decode("utf-8", errors="replace"))


def seed_archives(folder):
    # 在 Logger 启动之前，准备已有文件和可识别的归档内容。
    (folder / "test.log").write_bytes(OLD_CURRENT)
    for number in range(1, 4):
        (folder / ("test.log." + str(number))).write_bytes(
            ("OLD_" + str(number) + "\n").encode("utf-8")
        )


def test_normal(binary, folder):
    require_quiet(run_driver(binary, folder, "normal"))
    names = {path.name for path in folder.glob("test.log*")}
    require(names == {"test.log", "test.log.1", "test.log.2", "test.log.3"},
            "应该只保留当前文件和三个归档")
    check_short_files(folder)
    ids = record_ids(folder)
    require(0 < len(ids) < 100, "有限保留数量应当淘汰最早的一部分日志")
    require(ids == list(range(ids[0], 100)), "保留的日志应连续递增并以 ID=99 结束")


def test_no_loss(binary, folder):
    require_quiet(run_driver(binary, folder, "no_loss"))
    check_short_files(folder)
    require(record_ids(folder) == list(range(100)),
            "足够保留空间下，100 条日志应完整、有序、不重复")


def test_order(binary, folder):
    seed_archives(folder)
    require_quiet(run_driver(binary, folder, "order"))
    require(read_log(folder, "test.log.3") == b"OLD_2\n", ".2 没有正确移动到 .3")
    require(read_log(folder, "test.log.2") == b"OLD_1\n", ".1 没有正确移动到 .2")
    require(read_log(folder, "test.log.1") == OLD_CURRENT, "当前旧文件没有完整归档")
    rows = read_log(folder).splitlines()
    require(len(rows) == 2 and rows[0].endswith(b" FIRST_AFTER_ROTATE") and
            rows[1].endswith(b" SECOND_AFTER_ROTATE"), "新日志未按顺序进入新文件")


def test_one_backup(binary, folder):
    (folder / "test.log").write_bytes(OLD_CURRENT)
    (folder / "test.log.1").write_bytes(b"EXPIRED_BACKUP\n")
    require_quiet(run_driver(binary, folder, "one_backup"))
    require({path.name for path in folder.glob("test.log*")} ==
            {"test.log", "test.log.1"}, "保留一个归档时出现了额外文件")
    require(read_log(folder, "test.log.1") == OLD_CURRENT, "旧 .1 未被当前文件替换")
    require(read_log(folder).endswith(b" ONLY_BACKUP_TEST\n"), "新日志没有写入当前文件")


def test_long_record(binary, folder):
    require_quiet(run_driver(binary, folder, "long_record"))
    require({path.name for path in folder.glob("test.log*")} ==
            {"test.log", "test.log.1"}, "超长日志不应产生空归档或反复滚动")
    archived = read_log(folder, "test.log.1")
    require(len(archived) > 1024, "超长日志没有完整保留")
    require(len(archived.splitlines()) == 1 and archived.endswith(
        b" LONG_BEGIN " + b"X" * 2048 + b" LONG_END\n"), "超长日志被截断或拆开")
    current = read_log(folder)
    require(len(current.splitlines()) == 1 and current.endswith(b" AFTER_LONG_RECORD\n"),
            "超长日志之后的短日志没有进入新文件")


def test_restart_append(binary, folder):
    require_quiet(run_driver(binary, folder, "restart"))
    first = read_log(folder)
    require_quiet(run_driver(binary, folder, "restart"))
    data = read_log(folder)
    require(data.startswith(first) and len(data.splitlines()) == 2 and
            data.count(b" RESTART_MARKER\n") == 2, "重启追加时旧内容丢失或新日志缺失")
    require(not (folder / "test.log.1").exists(), "未达到阈值时不应滚动")


def test_restart_full(binary, folder):
    require_quiet(run_driver(binary, folder, "restart"))
    first = read_log(folder)
    require(len(first) < 1024, "测试日志本身过长，无法准备边界文件")
    # 把上一次留下的文件补到恰好 1024 字节，再启动一个新进程。
    full = first + b"S" * (1024 - len(first))
    (folder / "test.log").write_bytes(full)
    require_quiet(run_driver(binary, folder, "restart"))
    require(read_log(folder, "test.log.1") == full, "重启时没有根据已有大小滚动")
    data = read_log(folder)
    require(len(data.splitlines()) == 1 and data.endswith(b" RESTART_MARKER\n"),
            "重启后的新日志没有进入新文件")


def test_fallback(binary, folder):
    (folder / "test.log").write_bytes(OLD_CURRENT)
    # 非空目录不能被 remove() 删除，稳定制造滚动故障，不依赖用户权限。
    blocked = folder / "test.log.3"
    blocked.mkdir()
    (blocked / "keep.txt").write_bytes(b"KEEP\n")
    result = run_driver(binary, folder, "fallback")
    stderr = result.stderr.decode("utf-8")
    require(stderr.count("删除失败") == 1, "滚动故障应只报告一次删除失败")
    lines = stderr.splitlines()
    messages = [line for line in lines if "FALLBACK_" in line]
    require(len(lines) == 4 and len(messages) == 3, "故障后重复报错或日志缺失")
    for number, line in enumerate(messages):
        require(line.endswith(" FALLBACK_" + str(number)) and
                "[ERROR]" in line and "[thread:" in line and
                "rotation_driver.cpp:" in line, "替代输出缺少日志或完整格式")
    require(read_log(folder) == OLD_CURRENT, "故障后不应继续修改当前文件")
    require((blocked / "keep.txt").read_bytes() == b"KEEP\n", "故障目录被意外修改")
    require(not (folder / "test.log.1").exists() and
            not (folder / "test.log.2").exists(), "删除失败后仍继续移动归档")


def test_concurrent(binary, folder):
    require_quiet(run_driver(binary, folder, "concurrent"))
    check_short_files(folder)
    ids = record_ids(folder)
    # 不同生产者的顺序取决于调度，不能要求全局编号递增。
    require(sorted(ids) == list(range(160)), "四个线程提交的日志出现丢失或重复")
    for worker in range(4):
        first = worker * 40
        own_ids = [number for number in ids if first <= number < first + 40]
        require(own_ids == list(range(first, first + 40)), "同一生产者的日志顺序错误")


TESTS = {
    "normal": test_normal,
    "no_loss": test_no_loss,
    "order": test_order,
    "one_backup": test_one_backup,
    "long_record": test_long_record,
    "restart_append": test_restart_append,
    "restart_full": test_restart_full,
    "fallback": test_fallback,
    "concurrent": test_concurrent,
}


def main():
    parser = argparse.ArgumentParser(description="运行 Logger V5 文件滚动测试")
    parser.add_argument("--case", choices=list(TESTS), help="只运行指定测试")
    args = parser.parse_args()
    # 每次新建独立目录，结果保留下来供查看，不操作项目原来的 test.log。
    workspace = Path(tempfile.mkdtemp(prefix="logger-v5-tests-"))
    binary = workspace / "rotation_driver"
    print("测试结果目录：", workspace, flush=True)
    command = [
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-pthread",
        "-I" + str(PROJECT / "include"),
        str(PROJECT / "tests" / "rotation_driver.cpp"),
        str(PROJECT / "src" / "logger.cpp"), "-o", str(binary),
    ]
    try:
        subprocess.run(command, check=True, timeout=60)
    except (OSError, subprocess.SubprocessError) as error:
        print("[FAIL] 编译失败：", error)
        return 1

    selected = [args.case] if args.case else list(TESTS)
    failures = 0
    for name in selected:
        folder = workspace / name
        folder.mkdir()
        try:
            TESTS[name](binary, folder)
            print("[PASS]", name, flush=True)
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            failures += 1
            print("[FAIL]", name, "：", error, flush=True)
    print("通过 {}/{} 项；结果保留在 {}".format(
        len(selected) - failures, len(selected), workspace
    ))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
