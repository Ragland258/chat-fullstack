#include "httpmgr.h"

#include <QDebug>
#include <QNetworkRequest>

#include <utility>

HttpMgr::HttpMgr()
{
    // 将通用 HTTP 完成信号转发到具体业务模块信号。
    connect(this, &HttpMgr::sig_help_finish, this, &HttpMgr::slot_http_finish);
}

HttpMgr::~HttpMgr()
{
}

void HttpMgr::PosthttpRec(QUrl url, QJsonObject json, ReqID req_id, Modules mod)
{
    PostHttpReq(std::move(url), std::move(json), req_id, mod);
}

void HttpMgr::PostHttpReq(QUrl url, QJsonObject json, ReqID req_id, Modules mod)
{
    // 捕获 shared_ptr，保证异步回调执行期间 HttpMgr 不会被提前释放。
    auto self = shared_from_this();

    // 将 JSON 序列化后通过 QNetworkAccessManager 异步 POST。
    QByteArray data = QJsonDocument(json).toJson();
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));

    QNetworkReply *reply = _manager.post(request, data);

    // 请求完成后读取响应，并通过统一信号交给业务模块处理。
    QObject::connect(reply, &QNetworkReply::finished, [self, reply, req_id, mod, url]() {
        const QByteArray responseBody = reply->readAll();
        const QString responseText = QString::fromUtf8(responseBody);

        if (reply->error() != QNetworkReply::NoError) {
            const QString errorMessage = reply->errorString();
            const int httpStatus = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute
                ).toInt();

            qWarning().noquote()
                << QStringLiteral("HTTP 请求失败: url=%1, status=%2, error=%3")
                       .arg(url.toString())
                       .arg(httpStatus)
                       .arg(errorMessage);

            // HTTP 4xx/5xx 也可能携带后端 JSON，优先把响应体交给业务层。
            emit self->sig_help_finish(
                req_id,
                responseText.isEmpty() ? errorMessage : responseText,
                ErrorCodes::ERROR_NETWORK,
                mod
                );
            reply->deleteLater();
            return;
        }

        emit self->sig_help_finish(req_id, responseText, ErrorCodes::SUCCESS, mod);
        reply->deleteLater();
    });
}

void HttpMgr::slot_http_finish(ReqID id, QString res, ErrorCodes err, Modules mod)
{
    if (mod == Modules::LOGINMOD)
    {
        emit sig_login_mod_finish(id, res, err);
        return;
    }

    // 注册、验证码和重置页面继续使用原有统一信号。
    emit sig_mod_finish(id, res, err);
}
