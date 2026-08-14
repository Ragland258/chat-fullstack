# ChatServer Git 迁移记录

日期：2026-08-14

## 变更

- 将独立开发目录中的 ChatServer 源码迁入全栈仓库 `server/ChatServer`。
- 确认客户端长连接服务采用 WebSocket，默认监听端口为 `7891`。
- 保留 Visual Studio 解决方案及工程配置，补充项目 README。
- 排除 `.git`、`.vs`、`x64`、对象文件、调试产物和 `*.vcxproj.user` 等本机文件。

## 范围

本次提交只包含 `server/ChatServer`，未包含工作区内 GateServer 或 `backup` 目录的其他未提交修改。

## 验证

- 已检查待提交文件清单。
- 已扫描密码、密钥、Token 值及本地配置文件，未发现需要提交的敏感配置。
- 已使用 Visual Studio 2022 MSBuild 执行 `Debug|x64` 构建。
- 当前构建未通过：`const.h` 存在 `ErrorCode` 重复定义，`LogicRoute` 的处理器注册代码不完整，`soloChatHandler` 的声明与实现不一致。上述问题来自迁移前的开发版本，本次仅迁移并如实记录，未扩大范围修改业务逻辑。
