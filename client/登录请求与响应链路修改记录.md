# 登录请求与响应链路修改

本次只接入客户端登录请求、HTTP 模块分发和登录回包处理。

## 前后端接口

- URL：`POST /login`
- 请求：`{"email":"...","password":"..."}`
- 成功响应：`{"error":0,"message":"login success","email":"...","token":"...","host":"127.0.0.1","port":9001}`

密码不再执行 `xorString`。当前 Gate Server 会把收到的原始密码交给 libsodium 校验；客户端异或后会导致密码验证失败。生产环境应通过 HTTPS/TLS 保护传输。

## 修改文件

- `const.h`
  - 增加 `ID_LOGIN_USER` 和 `LOGINMOD`，保留旧名称兼容。
- `httpmgr.h/.cpp`
  - 增加 `PostHttpReq`。
  - 增加 `sig_login_mod_finish`。
  - 登录模块响应单独转发。
  - HTTP 4xx/5xx 时保留后端 JSON 响应体。
- `mainwindow.h/.cpp`
  - MainWindow 作为当前项目的登录界面，增加登录按钮槽函数。
  - 增加账号和密码校验。
  - 增加登录响应 Handler 映射。
  - 解析后端业务错误码。
  - 登录成功后打开 `LogicDialog`，并把 email/token/host/port 暂存为其动态属性。
