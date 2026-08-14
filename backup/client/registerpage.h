#ifndef REGISTERPAGE_H
#define REGISTERPAGE_H

#include "const.h"

#include <QTimer>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class RegisterPage;
}
QT_END_NAMESPACE

class RegisterPage : public QWidget
{
    Q_OBJECT

public:
    // 初始化注册页面，绑定按钮、输入框回车和网络回调。
    explicit RegisterPage(QWidget *parent = nullptr);

    // 释放注册页面 UI 对象。
    ~RegisterPage();

signals:
    // 请求返回登录页面。
    void backRequested();

    // 注册成功后通知主窗口切回登录页，并填充账号信息。
    void registerRequested(const QString &account, const QString &password);

private slots:
    // 接收 HttpMgr 转发的模块响应，并分发给对应处理函数。
    void slot_reg_mod_finish(ReqID id, QString res, ErrorCodes err);

    // 获取验证码按钮点击槽函数。
    void on_get_code_clicked();

    // 验证码获取倒计时更新。
    void on_countdown_timeout();

private:
    // 初始化请求 ID 到处理函数的映射表。
    void initHandlers();

    // 统一显示注册页面的成功/失败提示。
    void showTip(const QString &message, bool success);

    Ui::RegisterPage *ui;
    QMap<ReqID, std::function<void(const QJsonObject&, ErrorCodes)>> _handlers;
    QTimer *_countdownTimer;
    int _countdownSeconds;
};

#endif // REGISTERPAGE_H
