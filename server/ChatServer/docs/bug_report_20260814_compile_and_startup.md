# ChatServer 编译与启动问题修复记录

日期：2026-08-14

## 现象

- `Debug|x64` 编译出现 `ErrorCode` 重复定义。
- `LogicRoute::RegisterHandler` 参数数量错误并伴随语法错误。
- `soloChatHandler::Handler` 声明和实现不一致。
- 修复语法后，链接器因 vcpkg 自动加入全部库而在 `grpc.lib` 上触发 `LNK1000`。
- 可执行文件生成后，WebSocket 客户端仍无法连接端口 `7891`。

## 根因

1. `const.h` 缺少 include guard，并且在工程中被错误配置成可编译源文件。
2. 错误处理路由只传入了路由名，没有创建 `ErrorHandler`；构造语句也缺少分号。
3. `soloChatHandler` 的实现遗漏 `Json::Value` 参数，`ErrorHandler::Handler` 和 `Connection::SendResponse` 只有声明或空文件。
4. 部分源码为 GBK，迁移到 UTF-8 后工程未设置 `/utf-8`，MSVC 按本地代码页误解析中文注释后的代码。
5. vcpkg classic 自动链接使用 `*.lib`，把 ChatServer 未使用的 gRPC 库也传给链接器；同时未使用的 `boost/asio/ssl.hpp` 引入了 OpenSSL 链接符号。
6. `IOSPool` 创建了空的 `unique_ptr<io_context>` 数组后立即解引用；`main()` 还用临时 `shared_ptr` 创建服务器，调用 `StartAccept()` 后服务器和 acceptor 立即析构。

## 修复

- 为 `const.h` 增加 `#pragma once`，并在 `.vcxproj` 中改为 `ClInclude`。
- 完成错误处理器注册，修正 JSON 路由验证和所有 Handler 签名。
- 实现基于 WebSocket executor 的串行发送队列，保证异步写入期间消息缓冲区有效。
- 将相关源码统一为 UTF-8，并为所有构建配置增加 `/utf-8`。
- 禁用 vcpkg 的全库自动链接，移除当前非 TLS WebSocket 服务未使用的 SSL 头文件。
- 正确创建 `IOSPool` 的 `io_context` 和 work guard，使 `Stop()` 可重复调用。
- 在 `main()` 中持有 `WebSocketServer`，确保 acceptor 生命周期覆盖 `io_context::run()`。

## 验证

- Visual Studio 2022 MSBuild：`Debug|x64`、`Release|x64` 完整重建成功，均为 0 个警告、0 个错误。
- 后台启动 `ChatServer.exe` 后成功监听端口 `7891`。
- 使用 .NET `ClientWebSocket` 连接 `ws://127.0.0.1:7891/` 成功。
- 发送非法 JSON `not-json`，收到包含 `error: 1001` 和 `message: JsonError` 的响应。
- 发送未知路由 `{ "require": "missing" }`，收到包含 `error: 1001` 和 `message: ErrorRequire` 的响应。

## 遗留事项

- `soloChatHandler` 当前仅保留接口骨架，尚未实现单聊业务。
- 身份校验、StatusServer 联动、Redis/MySQL 持久化和 TLS (`wss://`) 尚未接入。
