# 全栈聊天项目
---暂时未完成
多服务全栈聊天项目：Qt 客户端 + GateServer（HTTP/gRPC 网关）+ StatusServer（在线状态/路由）+ VerifyServer（邮箱验证码），ChatServer 预留。

## 目录结构

```
chat/
├── chat.sln              # Visual Studio 顶层解决方案（GateServer + StatusServer + ChatServer）
├── client/               # Qt6 桌面客户端（登录 / 注册 / 找回密码）
├── server/
│   ├── GateServer/       # C++ 网关服务：HTTP + gRPC，MySQL / Redis
│   ├── StatusServer/     # C++ 状态服务：gRPC，返回 ChatServer 地址
│   ├── VerifyServer/     # Node.js 邮箱验证码服务
│   └── ChatServer/       # 预留：后续聊天服务
├── proto/
│   └── message.proto     # 共享 proto 定义（唯一源）
├── scripts/
│   ├── generate_grpc.bat # 重新生成各服务的 pb / grpc 代码
│   ├── consume_verify_code.lua
│   └── 验证码Lua并发测试脚本/
├── docs/                 # bug 报告、变更日志
└── config / build 说明见各服务 README 与 `*Server.ini.example`
```

## 构建与启动

### client（Qt 客户端）

```bash
cd client
cp config.ini.example config.ini   # 按需修改 GateServer 地址
# Qt Creator 打开 client/CMakeLists.txt 构建，或：
cmake -S . -B build && cmake --build build
```

### GateServer / StatusServer / ChatServer（C++）

1. 用 Visual Studio 2022 打开根目录 `chat.sln`，可在同一解决方案中看到 GateServer、StatusServer 和 ChatServer（也可分别打开各服务自己的 `.sln`）
2. 为每个服务创建独立配置，并按需填写 MySQL、Redis、MinIO 等连接信息：

   ```powershell
   Copy-Item server/GateServer/GateServer.ini.example server/GateServer/GateServer.ini
   Copy-Item server/StatusServer/StatusServer.ini.example server/StatusServer/StatusServer.ini
   Copy-Item server/ChatServer/ChatServer.ini.example server/ChatServer/ChatServer.ini
   Copy-Item server/FileServer/FileServer.ini.example server/FileServer/FileServer.ini
   ```

3. 修改 `proto/message.proto` 后，运行 `scripts/generate_grpc.bat` 重新生成代码

### VerifyServer（Node.js）

```bash
cd server/VerifyServer
cp config.example.json config.json  # 填入 SMTP / Redis 配置
npm install
npm run server
```

## 配置说明

各 C++ 服务的 `*Server.ini` 以及 VerifyServer 的 `config.json` 属于本地运行时配置，已被 `.gitignore` 忽略；提交时只保留 `*.example` 模板。
