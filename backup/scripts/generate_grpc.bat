@echo off
setlocal

set "PROJECT_DIR=%~dp0"
if defined VCPKG_ROOT (
    set "VCPKG_DIR=%VCPKG_ROOT%"
) else (
    set "VCPKG_DIR=D:\vcpkg"
)

set "PROTOC=%VCPKG_DIR%\installed\x64-windows\tools\protobuf\protoc.exe"
set "GRPC_PLUGIN=%VCPKG_DIR%\installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe"
set "PROTO_DIR=%PROJECT_DIR%..\proto"
set "PROTO_FILE=%PROTO_DIR%\message.proto"
set "GATE_OUT=%PROJECT_DIR%..\server\GateServer\generated"
set "STATUS_OUT=%PROJECT_DIR%..\server\StatusServer"

if not exist "%PROTOC%" (
    echo [ERROR] protoc.exe not found: %PROTOC%
    exit /b 1
)
if not exist "%GRPC_PLUGIN%" (
    echo [ERROR] grpc_cpp_plugin.exe not found: %GRPC_PLUGIN%
    exit /b 1
)
if not exist "%PROTO_FILE%" (
    echo [ERROR] proto file not found: %PROTO_FILE%
    exit /b 1
)
if not exist "%GATE_OUT%" mkdir "%GATE_OUT%"
if not exist "%STATUS_OUT%" mkdir "%STATUS_OUT%"

"%PROTOC%" ^
  --proto_path="%PROTO_DIR%" ^
  --cpp_out="%GATE_OUT%" ^
  --grpc_out="%GATE_OUT%" ^
  --plugin=protoc-gen-grpc="%GRPC_PLUGIN%" ^
  "%PROTO_FILE%"
if errorlevel 1 exit /b 1

"%PROTOC%" ^
  --proto_path="%PROTO_DIR%" ^
  --cpp_out="%STATUS_OUT%" ^
  --grpc_out="%STATUS_OUT%" ^
  --plugin=protoc-gen-grpc="%GRPC_PLUGIN%" ^
  "%PROTO_FILE%"
if errorlevel 1 exit /b 1

copy /Y "%PROTO_FILE%" "%PROJECT_DIR%..\server\VerifyServer\message.proto" >nul
copy /Y "%PROTO_FILE%" "%STATUS_OUT%\message.proto" >nul

echo [SUCCESS] protobuf and gRPC sources regenerated.
endlocal
exit /b 0
