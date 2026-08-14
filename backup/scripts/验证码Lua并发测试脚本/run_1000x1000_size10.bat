@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

set "REDIS_CLI=D:\Redis-x64-5.0.14.1\redis-cli.exe"

where py >nul 2>&1
if %errorlevel%==0 (
    set "PYTHON_CMD=py -3"
) else (
    set "PYTHON_CMD=python"
)

echo.
echo RedisPool size=10 - 1000 requests / 1000 workers
echo 请先确认 GateServer 配置中的 RedisPool size 已经设为 10，并已重启服务。
echo.

%PYTHON_CMD% test_verify_code_concurrency.py ^
  --redis-cli "%REDIS_CLI%" ^
  --redis-pool-size 10 ^
  -n 1000 ^
  -w 1000 ^
  --timeout 60

echo.
echo Exit code: %errorlevel%
pause
endlocal
