# ChatServer

聊天系统的 C++ WebSocket 服务端，基于 Boost.Asio 与 Boost.Beast 实现。

## 当前实现

- WebSocket 监听与连接升级，默认监听端口 `7891`
- 基于 `IOSPool` 的异步 I/O 线程池
- 连接生命周期管理与消息读取/发送队列
- 消息路由及单聊处理器的初始结构
- `SIGINT` / `SIGTERM` 优雅停止入口

## 目录结构

```text
ChatServer/
├── ioLoop/       # WebSocket 服务、连接及连接管理
├── route/        # 消息路由
├── handler/      # 请求、错误及单聊处理器
├── grpc/         # 后续服务间 RPC 接入位置
├── docs/         # 开发记录
└── main.cpp      # 服务启动入口
```

## 构建

使用 Visual Studio 2022 打开 `ChatServer.sln`，选择 `x64` 配置构建。项目依赖 Boost.Asio、Boost.Beast 和 JsonCpp，依赖环境由本机 vcpkg/Visual Studio 配置提供。

## 状态

当前版本处于开发阶段，WebSocket 通信主链路已建立；身份校验、Redis/MySQL 持久化、与 StatusServer 的联动及完整业务协议仍待接入。

当前迁移版本的 `Debug|x64` 构建尚未通过，已知问题包括 `const.h` 中 `ErrorCode` 重复定义、`LogicRoute` 处理器注册代码不完整，以及 `soloChatHandler` 声明与实现不一致。
