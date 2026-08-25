# ChatServer 加入顶层解决方案

日期：2026-08-14

## 变更

- 将 `server/ChatServer/ChatServer.vcxproj` 加入根目录 `chat.sln`。
- 为 Debug/Release、Win32/x64 四套解决方案配置补齐 ChatServer 的构建映射。
- 修正 ChatServer filters 中 `const.h` 的项目类型，使其显示在“头文件”筛选器下。
- 更新根 README，说明三个 C++ 服务可通过同一个解决方案打开。

## Visual Studio 效果

打开根目录 `chat.sln` 后，解决方案资源管理器会并列显示：

- GateServer
- StatusServer
- ChatServer

`VerifyServer` 是 Node.js 服务，因此仍通过其目录中的 npm 命令运行，不作为 VC++ 项目加入解决方案。

## 验证

- 根解决方案可识别三个 C++ 项目。
- 已通过根解决方案调用 ChatServer 的 `Debug|x64` 构建。
