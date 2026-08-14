#ifndef HTTPMGR_H
#define HTTPMGR_H

#include "singleton.h"
#include "const.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class HttpMgr : public QObject,
                public Singleton<HttpMgr>,
                public std::enable_shared_from_this<HttpMgr>
{
    Q_OBJECT

public:
    // 释放 HTTP 管理器资源。
    ~HttpMgr();

    // 发送 JSON POST 请求，外部业务层直接指定 URL、请求体、请求 ID 和模块。
    void PostHttpReq(QUrl url, QJsonObject json, ReqID req_id, Modules mod);

    // 兼容项目中已有的旧函数名。
    void PosthttpRec(QUrl url, QJsonObject json, ReqID req_id, Modules mod);

private:
    friend class Singleton<HttpMgr>;

    // 通过 Singleton 创建，外部不能直接构造。
    HttpMgr();

private slots:
    // 根据模块 ID 将通用 HTTP 完成信号转发给对应业务模块。
    void slot_http_finish(ReqID id, QString res, ErrorCodes err, Modules mod);

private:
    QNetworkAccessManager _manager;

signals:
    // 通用 HTTP 完成信号，携带请求 ID、响应内容、错误码和模块 ID。
    void sig_help_finish(ReqID id, QString res, ErrorCodes err, Modules mod);

    // 注册、验证码、重置模块完成信号。
    void sig_mod_finish(ReqID id, QString res, ErrorCodes err);

    // 登录模块专用完成信号，由登录界面接收。
    void sig_login_mod_finish(ReqID id, QString res, ErrorCodes err);
};

#endif // HTTPMGR_H
