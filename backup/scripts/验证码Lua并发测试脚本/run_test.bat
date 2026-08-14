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
echo Starting Lua verification-code concurrency test...
echo.

if exist "%REDIS_CLI%" (
    %PYTHON_CMD% test_verify_code_concurrency.py --redis-cli "%REDIS_CLI%" -n 30 -w 30
) else (
    echo redis-cli was not found at:
    echo %REDIS_CLI%
    echo The Python script will try other common locations.
    %PYTHON_CMD% test_verify_code_concurrency.py -n 30 -w 30
)

echo.
echo Exit code: %errorlevel%
pause
endlocal
