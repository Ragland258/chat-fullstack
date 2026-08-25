# 多服务联合启动配置冲突

## 现象

Visual Studio 联合启动多个后端服务时，FileServer 可能读取到 GateServer 的配置，并报告 `FileServer Host or Port is empty`。

## 原因

多个 C++ 服务共用同一个输出目录，并且都把本地配置复制为 `config.ini`。最后完成构建的项目会覆盖其他服务的配置。

## 修复

- GateServer、StatusServer、ChatServer、FileServer 分别读取各自的 `*Server.ini`。
- 各项目的构建后事件只复制本服务的独立配置文件。
- 本地运行配置由 `.gitignore` 忽略，仓库只保存不含真实密钥的 `*.example` 模板。
- VerifyServer 使用自己目录中的 `config.json`，不参与 C++ 输出目录的配置竞争。

## 验证标准

联合启动后，每个 C++ 服务的日志都应显示自己对应的配置路径；任何项目重新构建都不应改变其他服务读取到的配置。
