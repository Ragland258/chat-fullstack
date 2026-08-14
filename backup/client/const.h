#ifndef CONST_H
#define CONST_H
#include <QMap>
#include <memory>
#include <functional>
#include <QString>
#include <mutex>
#include <iostream>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkReply>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSettings>
#include <QCoreApplication>

static inline QString gateServerUrl()
{
    QSettings settings(QCoreApplication::applicationDirPath() + QStringLiteral("/config.ini"), QSettings::IniFormat);
    const QString host = settings.value(QStringLiteral("GateServer/host"), QStringLiteral("localhost")).toString();
    const int port = settings.value(QStringLiteral("GateServer/port"), 9999).toInt();
    return QStringLiteral("http://%1:%2").arg(host).arg(port);
}

enum ReqID
{
    ID_GET_VARIFY_CODE =1001,//获取验证码
    ID_REG_USER = 1002,//注册用户
    ID_LOGIN_USER = 1003,//登录
    ID_LOGIN = ID_LOGIN_USER,//兼容旧名称
    ID_GET_RESET_CODE = 1004,//找回密码验证码
    ID_RESET_PASSWORD = 1005//提交密码重置
};

enum Modules
{
    REGISTERMOD = 0,
    VERIFYMOD = 1,
    LOGINMOD = 2,
    LOGIN = LOGINMOD//兼容旧名称
};

enum ErrorCodes
{
    SUCCESS = 0,
    ERROR_JSON = 1,
    ERROR_NETWORK = 2,
};

#endif // CONST_H
