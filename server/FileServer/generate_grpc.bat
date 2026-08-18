@echo off
setlocal EnableExtensions

set "PROJECT_DIR=%~dp0"
if defined VCPKG_ROOT (
    set "VCPKG_DIR=%VCPKG_ROOT%"
) else (
    set "VCPKG_DIR=D:\vcpkg"
)

set "PROTOC=%VCPKG_DIR%\installed\x64-windows\tools\protobuf\protoc.exe"
set "GRPC_PLUGIN=%VCPKG_DIR%\installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe"
set "PROTO_DIR=%PROJECT_DIR%proto"
set "OUTPUT_DIR=%PROJECT_DIR%generate"

if not exist "%PROTOC%" (
    echo [ERROR] protoc.exe not found: %PROTOC%
    exit /b 1
)
if not exist "%GRPC_PLUGIN%" (
    echo [ERROR] grpc_cpp_plugin.exe not found: %GRPC_PLUGIN%
    exit /b 1
)
if not exist "%PROTO_DIR%" (
    echo [ERROR] proto directory not found: %PROTO_DIR%
    exit /b 1
)

dir /b "%PROTO_DIR%\*.proto" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] no .proto files found in: %PROTO_DIR%
    exit /b 1
)

if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
    if errorlevel 1 (
        echo [ERROR] failed to create output directory: %OUTPUT_DIR%
        exit /b 1
    )
)

for %%F in ("%PROTO_DIR%\*.proto") do (
    echo [INFO] generating: %%~nxF

    "%PROTOC%" ^
      --proto_path="%PROTO_DIR%" ^
      --cpp_out="%OUTPUT_DIR%" ^
      --grpc_out="%OUTPUT_DIR%" ^
      --plugin=protoc-gen-grpc="%GRPC_PLUGIN%" ^
      "%%~fF"

    if errorlevel 1 (
        echo [ERROR] generation failed: %%~nxF
        exit /b 1
    )
)

echo [SUCCESS] all protobuf and gRPC sources generated in: %OUTPUT_DIR%
endlocal
exit /b 0
