#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
验证码 Lua 原子消费并发测试

作用：
1. 自动向 Redis 写入一个全新的验证码；
2. 同时向 /register 发起多次相同验证码的注册请求；
3. 检查是否只有一个请求成功消费验证码；
4. 输出错误码分布、延迟分位数，并保存 CSV 明细。

仅使用 Python 标准库，不需要安装 requests。
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import statistics
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Optional


SUCCESS = 0
VERIFY_EXPIRED = 1003
VERIFY_ERROR = 1004


@dataclass
class RequestResult:
    index: int
    http_status: Optional[int]
    error_code: Optional[int]
    message: str
    elapsed_ms: float
    raw_body: str
    transport_error: str = ""


def configure_console() -> None:
    """尽量保证 Windows 控制台正确显示中文。"""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            try:
                reconfigure(encoding="utf-8")
            except Exception:
                pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="测试 Redis Lua 验证码是否只能被一个并发请求成功消费。"
    )
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:9999/register",
        help="注册接口地址，默认：http://127.0.0.1:9999/register",
    )
    parser.add_argument(
        "-n",
        "--requests",
        type=int,
        default=30,
        help="请求总数，默认 30",
    )
    parser.add_argument(
        "-w",
        "--workers",
        type=int,
        default=30,
        help="并发线程数，默认 30",
    )
    parser.add_argument(
        "--email",
        default="",
        help="测试邮箱；不填写时自动生成，建议保持默认",
    )
    parser.add_argument(
        "--user",
        default="",
        help="测试用户名；不填写时自动生成",
    )
    parser.add_argument(
        "--password",
        default="Aa123456!",
        help="注册密码，默认 Aa123456!",
    )
    parser.add_argument(
        "--code",
        default="a1b2",
        help="验证码，默认 a1b2",
    )
    parser.add_argument(
        "--ttl",
        type=int,
        default=300,
        help="验证码有效期秒数，默认 300",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=20.0,
        help="单个 HTTP 请求超时秒数，默认 20",
    )
    parser.add_argument(
        "--redis-cli",
        default="",
        help="redis-cli.exe 完整路径；不填写时自动查找",
    )
    parser.add_argument(
        "--redis-host",
        default="127.0.0.1",
        help="Redis 地址，默认 127.0.0.1",
    )
    parser.add_argument(
        "--redis-port",
        type=int,
        default=6379,
        help="Redis 端口，默认 6379",
    )
    parser.add_argument(
        "--redis-password",
        default="",
        help="Redis 密码；没有密码时不填写",
    )
    parser.add_argument(
        "--redis-pool-size",
        type=int,
        default=0,
        help=(
            "记录 GateServer 的 RedisPool 大小，仅作为测试元数据，"
            "不会自动修改服务端配置；0 表示未记录"
        ),
    )
    parser.add_argument(
        "--skip-redis-prepare",
        action="store_true",
        help="不自动向 Redis 写验证码；用于你已经手工 SET 的情况",
    )
    parser.add_argument(
        "--csv",
        default="",
        help="CSV 输出路径；不填写时在当前目录自动生成",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if args.requests <= 0:
        raise ValueError("--requests 必须大于 0")
    if args.workers <= 0:
        raise ValueError("--workers 必须大于 0")
    if args.ttl <= 0:
        raise ValueError("--ttl 必须大于 0")
    if args.timeout <= 0:
        raise ValueError("--timeout 必须大于 0")
    if args.redis_pool_size < 0:
        raise ValueError("--redis-pool-size 不能小于 0")
    if not args.code:
        raise ValueError("--code 不能为空")


def find_redis_cli(explicit_path: str) -> Optional[str]:
    """查找 redis-cli，包含截图中使用的常见目录。"""
    candidates: list[str] = []

    if explicit_path:
        candidates.append(explicit_path)

    from_path = shutil.which("redis-cli")
    if from_path:
        candidates.append(from_path)

    if os.name == "nt":
        candidates.extend(
            [
                r"D:\Redis-x64-5.0.14.1\redis-cli.exe",
                r"D:\Redis\redis-cli.exe",
                r"C:\Redis\redis-cli.exe",
                r"C:\Program Files\Redis\redis-cli.exe",
                r"C:\Program Files\Memurai\memurai-cli.exe",
            ]
        )

    for candidate in candidates:
        expanded = os.path.expandvars(os.path.expanduser(candidate))
        if Path(expanded).is_file():
            return str(Path(expanded))

    return None


def redis_command(
    redis_cli: str,
    args: argparse.Namespace,
    *command_parts: str,
) -> subprocess.CompletedProcess[str]:
    command = [
        redis_cli,
        "-h",
        args.redis_host,
        "-p",
        str(args.redis_port),
    ]

    if args.redis_password:
        command.extend(["-a", args.redis_password])

    command.extend(command_parts)

    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=10,
        check=False,
    )


def prepare_verify_code(
    redis_cli: str,
    args: argparse.Namespace,
    redis_key: str,
) -> None:
    ping = redis_command(redis_cli, args, "PING")
    if ping.returncode != 0 or "PONG" not in ping.stdout.upper():
        details = (ping.stderr or ping.stdout).strip()
        raise RuntimeError(f"Redis PING 失败：{details or '没有返回 PONG'}")

    set_result = redis_command(
        redis_cli,
        args,
        "SET",
        redis_key,
        args.code,
        "EX",
        str(args.ttl),
    )
    if set_result.returncode != 0 or "OK" not in set_result.stdout.upper():
        details = (set_result.stderr or set_result.stdout).strip()
        raise RuntimeError(f"向 Redis 写入验证码失败：{details}")

    get_result = redis_command(redis_cli, args, "GET", redis_key)
    actual = get_result.stdout.strip().strip('"')
    if get_result.returncode != 0 or actual != args.code:
        raise RuntimeError(
            f"Redis 写入后校验失败：期望 {args.code!r}，实际 {actual!r}"
        )

    ttl_result = redis_command(redis_cli, args, "TTL", redis_key)
    print(
        f"[Redis] 已写入：{redis_key} = {args.code}，"
        f"TTL={ttl_result.stdout.strip()} 秒"
    )


def extract_error_code(parsed: Any) -> Optional[int]:
    if not isinstance(parsed, dict):
        return None

    for field in ("error", "error_code", "code", "errcode"):
        value = parsed.get(field)
        if isinstance(value, bool):
            continue
        if isinstance(value, int):
            return value
        if isinstance(value, str):
            try:
                return int(value)
            except ValueError:
                pass

    return None


def send_register_request(
    index: int,
    start_event: threading.Event,
    args: argparse.Namespace,
    payload: dict[str, str],
) -> RequestResult:
    start_event.wait()

    body = json.dumps(
        payload,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")

    request = urllib.request.Request(
        args.url,
        data=body,
        headers={
            "Content-Type": "application/json; charset=utf-8",
            "Accept": "application/json",
            "Connection": "close",
            "User-Agent": "LuaConsumeConcurrencyTest/1.0",
        },
        method="POST",
    )

    started = time.perf_counter()

    try:
        with urllib.request.urlopen(request, timeout=args.timeout) as response:
            http_status = int(response.status)
            raw = response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        http_status = int(exc.code)
        raw = exc.read().decode("utf-8", errors="replace")
    except Exception as exc:
        elapsed_ms = (time.perf_counter() - started) * 1000
        return RequestResult(
            index=index,
            http_status=None,
            error_code=None,
            message="",
            elapsed_ms=elapsed_ms,
            raw_body="",
            transport_error=f"{type(exc).__name__}: {exc}",
        )

    elapsed_ms = (time.perf_counter() - started) * 1000

    parsed: Any = None
    message = ""
    try:
        parsed = json.loads(raw)
        if isinstance(parsed, dict):
            message = str(parsed.get("message", ""))
    except json.JSONDecodeError:
        pass

    return RequestResult(
        index=index,
        http_status=http_status,
        error_code=extract_error_code(parsed),
        message=message,
        elapsed_ms=elapsed_ms,
        raw_body=raw,
    )


def percentile(values: list[float], percent: float) -> float:
    if not values:
        return 0.0

    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]

    position = (len(ordered) - 1) * percent
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = position - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction


def write_csv(
    path: Path,
    results: list[RequestResult],
    args: argparse.Namespace,
    email: str,
    redis_key: str,
) -> None:
    """写入逐请求明细，并附带本轮测试参数，便于后续横向比较。"""
    worker_count = min(args.workers, args.requests)
    pool_size = args.redis_pool_size if args.redis_pool_size > 0 else ""

    with path.open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "redis_pool_size",
                "request_total",
                "worker_count",
                "url",
                "test_email",
                "redis_key",
                "index",
                "http_status",
                "error_code",
                "message",
                "elapsed_ms",
                "transport_error",
                "raw_body",
            ]
        )
        for result in sorted(results, key=lambda item: item.index):
            writer.writerow(
                [
                    pool_size,
                    args.requests,
                    worker_count,
                    args.url,
                    email,
                    redis_key,
                    result.index,
                    "" if result.http_status is None else result.http_status,
                    "" if result.error_code is None else result.error_code,
                    result.message,
                    f"{result.elapsed_ms:.3f}",
                    result.transport_error,
                    result.raw_body,
                ]
            )


def print_summary(
    results: list[RequestResult],
    args: argparse.Namespace,
    redis_cli: Optional[str],
    redis_key: str,
) -> int:
    total = len(results)
    success = sum(result.error_code == SUCCESS for result in results)
    expired = sum(result.error_code == VERIFY_EXPIRED for result in results)
    wrong = sum(result.error_code == VERIFY_ERROR for result in results)
    transport_errors = sum(bool(result.transport_error) for result in results)
    unknown = sum(
        result.error_code is None and not result.transport_error
        for result in results
    )

    code_counts: dict[str, int] = {}
    for result in results:
        if result.transport_error:
            key = "网络/超时错误"
        elif result.error_code is None:
            key = "无法识别业务码"
        else:
            key = str(result.error_code)
        code_counts[key] = code_counts.get(key, 0) + 1

    latencies = [result.elapsed_ms for result in results]

    print("\n" + "=" * 72)
    print("并发测试结果")
    print("=" * 72)
    print(f"请求总数          ：{total}")
    print(f"业务成功 error=0  ：{success}")
    print(f"已过期 error=1003 ：{expired}")
    print(f"验证码错误 1004   ：{wrong}")
    print(f"网络/超时错误     ：{transport_errors}")
    print(f"无法识别业务码    ：{unknown}")
    print("\n业务码分布：")
    for code, count in sorted(code_counts.items()):
        print(f"  {code:>14} : {count}")

    if latencies:
        print("\n请求延迟：")
        print(f"  min : {min(latencies):9.2f} ms")
        print(f"  avg : {statistics.fmean(latencies):9.2f} ms")
        print(f"  p50 : {percentile(latencies, 0.50):9.2f} ms")
        print(f"  p90 : {percentile(latencies, 0.90):9.2f} ms")
        print(f"  p95 : {percentile(latencies, 0.95):9.2f} ms")
        print(f"  p99 : {percentile(latencies, 0.99):9.2f} ms")
        print(f"  max : {max(latencies):9.2f} ms")

    redis_deleted: Optional[bool] = None
    if redis_cli:
        get_result = redis_command(redis_cli, args, "GET", redis_key)
        redis_value = get_result.stdout.strip()
        redis_deleted = redis_value in ("", "(nil)")
        print("\nRedis 最终状态：")
        print(f"  GET {redis_key}")
        print(f"  返回：{redis_value or '(空)'}")
    else:
        print("\nRedis 最终状态：未检查（没有找到 redis-cli）")

    strict_pass = (
        success == 1
        and expired == total - 1
        and wrong == 0
        and transport_errors == 0
        and unknown == 0
        and redis_deleted is not False
    )

    # 该判断用于识别“Lua 单次消费正常，但唯一进入下游的请求被 MySQL 等依赖拒绝”。
    non_expired = total - expired
    atomic_gate_only_pass = (
        not strict_pass
        and non_expired == 1
        and wrong == 0
        and transport_errors == 0
        and unknown == 0
        and redis_deleted is not False
    )

    print("\n判定：")
    if strict_pass:
        print("  [通过] 只有 1 个请求注册成功，其余请求均无法再次消费验证码。")
        print("  Lua 原子消费符合预期。")
        return 0

    if atomic_gate_only_pass:
        exceptional = [
            result
            for result in results
            if result.error_code != VERIFY_EXPIRED
        ]
        exceptional_code = (
            exceptional[0].error_code if exceptional else None
        )
        print("  [原子消费通过，但注册下游未通过]")
        print(
            "  只有 1 个请求越过验证码门槛，说明 Lua 单次消费有效；"
            f"但该请求业务码为 {exceptional_code}，请检查 MySQL/用户名/邮箱。"
        )
        return 2

    print("  [未通过] 结果不符合“1 个成功 + 其余全部 1003”的预期。")
    print("  请查看下面的异常请求和 CSV 明细。")

    abnormal = [
        result
        for result in results
        if result.error_code not in (SUCCESS, VERIFY_EXPIRED)
        or result.transport_error
    ]
    for result in abnormal[:10]:
        print(
            f"  请求 #{result.index}: HTTP={result.http_status}, "
            f"业务码={result.error_code}, "
            f"耗时={result.elapsed_ms:.2f}ms, "
            f"错误={result.transport_error or result.message or result.raw_body}"
        )

    return 1


def main() -> int:
    configure_console()
    args = parse_args()

    try:
        validate_args(args)
    except ValueError as exc:
        print(f"[参数错误] {exc}", file=sys.stderr)
        return 64

    timestamp = datetime.now().strftime("%Y%m%d%H%M%S%f")
    email = args.email or f"lua_concurrent_{timestamp}@example.com"
    user = args.user or f"lua_user_{timestamp}"
    redis_key = "code_" + email

    redis_cli = find_redis_cli(args.redis_cli)

    print("=" * 72)
    print("验证码 Lua 原子消费并发测试")
    print("=" * 72)
    print(f"接口地址 ：{args.url}")
    print(f"请求数量 ：{args.requests}")
    print(f"并发线程 ：{min(args.workers, args.requests)}")
    if args.redis_pool_size > 0:
        print(f"RedisPool：size={args.redis_pool_size}（仅记录，不修改服务端）")
    else:
        print("RedisPool：未记录，可使用 --redis-pool-size 指定")
    print(f"测试邮箱 ：{email}")
    print(f"测试用户 ：{user}")
    print(f"验证码   ：{args.code}")
    print(f"Redis key：{redis_key}")

    if not args.skip_redis_prepare:
        if not redis_cli:
            print("\n[错误] 找不到 redis-cli.exe。", file=sys.stderr)
            print(
                r'请使用参数指定，例如：--redis-cli "D:\Redis-x64-5.0.14.1\redis-cli.exe"',
                file=sys.stderr,
            )
            return 3

        print(f"redis-cli：{redis_cli}")
        try:
            prepare_verify_code(redis_cli, args, redis_key)
        except Exception as exc:
            print(f"\n[错误] Redis 准备失败：{exc}", file=sys.stderr)
            return 3
    else:
        print("[提示] 已跳过 Redis 自动写入，请确认你已手工执行：")
        print(f"       SET {redis_key} {args.code} EX {args.ttl}")

    payload = {
        "email": email,
        "user": user,
        "password": args.password,
        "passwd": args.password,
        "confirm": args.password,
        "verify_code": args.code,
        "varifycode": args.code,
    }

    start_event = threading.Event()
    results: list[RequestResult] = []

    print("\n正在创建并发请求……")

    worker_count = min(args.workers, args.requests)
    with ThreadPoolExecutor(max_workers=worker_count) as executor:
        futures = [
            executor.submit(
                send_register_request,
                index,
                start_event,
                args,
                payload,
            )
            for index in range(1, args.requests + 1)
        ]

        # 给工作线程一点时间进入等待状态，随后同时放行。
        time.sleep(0.5)
        started = time.perf_counter()
        start_event.set()

        for completed, future in enumerate(as_completed(futures), start=1):
            results.append(future.result())
            print(
                f"\r已完成 {completed}/{args.requests}",
                end="",
                flush=True,
            )

    wall_time = time.perf_counter() - started
    print(f"\n总耗时：{wall_time:.3f} 秒")

    csv_path = (
        Path(args.csv)
        if args.csv
        else Path.cwd()
        / f"lua_concurrency_results_{datetime.now():%Y%m%d_%H%M%S}.csv"
    )
    csv_path = csv_path.resolve()
    write_csv(
        csv_path,
        results,
        args=args,
        email=email,
        redis_key=redis_key,
    )
    print(f"CSV 明细：{csv_path}")

    return print_summary(
        results=results,
        args=args,
        redis_cli=redis_cli,
        redis_key=redis_key,
    )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n测试被用户中止。", file=sys.stderr)
        raise SystemExit(130)
