# ChatServer（预留）

后续聊天服务器占位目录，计划实现：

- 单聊 / 群聊消息收发
- 用户在线状态维护（与 StatusServer 联动）
- 消息持久化（MySQL）

新增 gRPC 服务时，在根目录 `proto/message.proto` 中扩展消息与服务，然后运行 `scripts/generate_grpc.bat` 重新生成代码。
