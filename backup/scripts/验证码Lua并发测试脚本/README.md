# 验证码 Lua 原子消费并发测试

## 测试前必须启动

1. Redis
2. C++ GateServer
3. MySQL

验证码服务不是本测试必需的，因为脚本会直接向 Redis 写入验证码。

## 最简单的运行方式

双击：

```text
run_test.bat
```

它默认测试：

```text
POST http://127.0.0.1:9999/register
并发请求数：30
Redis：127.0.0.1:6379
redis-cli：D:\Redis-x64-5.0.14.1\redis-cli.exe
```

脚本每次自动生成新的邮箱和用户名，避免数据库已有用户影响结果。

## 正确结果

```text
业务成功 error=0  ：1
已过期 error=1003 ：29
判定：[通过]
```

并且测试结束后：

```text
GET code_<本次测试邮箱>
(nil)
```

## 手工运行

在 CMD 中进入脚本目录：

```bat
cd /d 脚本所在目录
```

运行：

```bat
py -3 test_verify_code_concurrency.py --redis-cli "D:\Redis-x64-5.0.14.1\redis-cli.exe"
```

测试 100 个请求、50 并发：

```bat
py -3 test_verify_code_concurrency.py --redis-cli "D:\Redis-x64-5.0.14.1\redis-cli.exe" -n 100 -w 50
```

接口不是默认地址时：

```bat
py -3 test_verify_code_concurrency.py --url "http://127.0.0.1:9999/register"
```

Redis 有密码时：

```bat
py -3 test_verify_code_concurrency.py --redis-password "你的密码"
```

## 返回码

- 退出码 0：严格通过，1 个注册成功，其余全部验证码失效。
- 退出码 2：Lua 单次消费通过，但唯一进入下游的请求被 MySQL 等依赖拒绝。
- 退出码 1：并发结果不符合预期。
- 退出码 3：Redis 或 redis-cli 准备失败。

每次测试会生成一个 CSV 文件，里面保存每个请求的 HTTP 状态、业务错误码、响应正文和耗时。


## 1000 请求 / 1000 并发预设

包内新增：

```text
run_1000x1000_size10.bat
```

运行前确认 GateServer 的 RedisPool 已设为：

```ini
size = 10
```

并重启 GateServer。该批处理文件会执行：

```bat
py -3 test_verify_code_concurrency.py ^
  --redis-cli "D:\Redis-x64-5.0.14.1\redis-cli.exe" ^
  --redis-pool-size 10 ^
  -n 1000 ^
  -w 1000 ^
  --timeout 60
```

新版脚本新增参数：

```text
--redis-pool-size
```

该参数只用于把 RedisPool 大小记录在控制台和 CSV 中，不会修改服务端配置。

## 已归档测试数据

本包已经放入两轮 `RedisPool size=10、1000 请求/1000 并发` 的测试说明和汇总数据：

```text
测试记录/
├─ 1000请求1000并发_size10测试说明.md
└─ 1000请求1000并发_size10测试汇总.csv
```

两轮均为：

```text
error=0    ：1
error=1003 ：999
网络错误    ：0
Redis key  ：不存在
```


## 完整性能分析

测试脚本包现已包含：

```text
分析报告/
├─ RedisPool与Lua并发测试完整分析.md
└─ RedisPool_size10_vs_size32_五轮对比.csv
```

分析内容包括：

```text
Lua 原子消费正确性
RedisPool size=10 与 size=32 五轮对比
关闭调试日志前后的性能变化
1000 请求 / 1000 并发冲击测试
error=1009 的解释
RedisPool 是否需要扩容
后续无锁队列优化与验收标准
```
