# 仓库结构重构变更日志

日期：2026-08-04

## 背景

仓库从"gate 工程 + 杂项混在根目录"重构为全栈聊天项目单仓库结构：客户端与各服务分目录，共享 proto / 脚本 / 文档统一收口。

## 变更内容

1. **client/**：新增，Qt 桌面客户端（来源于 `D:\download\1234`，排除 `build/`、空 `backup/`、`.qtcreator`）。
2. **server/GateServer/**：原根目录的 C++ 网关工程整体移入，`chat.sln` / `chat.vcxproj` / `chat.vcxproj.filters` 改名为 `GateServer.*`。
3. **server/StatusServer/**：新增，StatusServer 源码来自 `D:\repos\StatusServer`（仅源码/工程/配置，排除构建产物）。
4. **server/VerifyServer/**：原 `VarifyServer/`（Node.js 验证码服务）移入并修正拼写。
5. **server/ChatServer/**：新增占位目录（README）。
6. **scripts/**：`generate_grpc.bat`、`consume_verify_code.lua`、`验证码Lua并发测试脚本/` 统一收口。
7. **proto/message.proto**：保留在根目录，作为唯一 proto 源。
8. **chat.sln**：新增顶层解决方案，同时包含 GateServer 与 StatusServer 两个工程。

## 修改的文件

- `server/GateServer/GateServer.sln`：工程名 `chat` → `GateServer`，引用 `GateServer.vcxproj`
- `server/GateServer/GateServer.vcxproj(.filters)`：`proto/`、`scripts/` 相关 None 项改为相对路径 `..\..\`
- `scripts/generate_grpc.bat`：输出路径改为 `..\server\GateServer\generated`、`..\server\StatusServer`、`..\server\VerifyServer\message.proto`
- `.gitignore`：新增 `build/`、`**/build/`；`VarifyServer/config.json` → `server/*/config.json`
- 新增 `README.md`、`docs/REFACTOR_CHANGELOG.md`

## 未改动

- 各服务源代码逻辑、proto 内容、`backup/` 与 `backup.zip`（历史快照，确认后另行清理）
- 本地运行时配置 `config.ini` / `config.json` 已随目录移动，仍被 gitignore

## 备份与回滚

- 重构前备份：`D:\repos\chat_pre_refactor_backup_20260804.zip`（`git archive HEAD`）
- 回滚：`git reset --hard 58946ba` 即可恢复到重构前提交

## 验证

- 两个 vcxproj 中引用的文件均能在新路径下找到
- `scripts\generate_grpc.bat` 重新生成 pb / grpc 代码成功
- 各服务编译验证待用户在本机 VS / Qt Creator 中执行
