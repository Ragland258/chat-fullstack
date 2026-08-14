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
- 迁移时发现的 `ErrorCode` 重复定义、路由注册不完整及 Handler 签名不一致问题已在后续修复中解决。
- 当前 `Debug|x64` 和 `Release|x64` 完整重建结果均为 0 个警告、0 个错误；WebSocket 客户端连接、非法 JSON 请求、未知路由及错误响应链验证通过。
