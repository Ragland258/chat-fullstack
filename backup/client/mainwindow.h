#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "const.h"

#include <QMainWindow>
#include <QMap>
#include <QPoint>

#include <functional>

class QEvent;
class QLabel;
class QStackedWidget;
class RegisterPage;
class ForgotPasswordPage;
class LogicDialog;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 初始化主窗口，并搭建登录页/注册页切换容器。
    explicit MainWindow(QWidget *parent = nullptr);

    // 释放 UI 对象。
    ~MainWindow();

    Q_INVOKABLE bool setLoginAvatar(const QString& imagePath);
    Q_INVOKABLE void resetLoginAvatar();

protected:
    // 仅用于无边框窗口标题区域拖动，不改变登录、注册和网络接口。
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // 登录按钮和密码输入框回车共同进入此槽函数。
    void on_login_btn_clicked();

    // 接收 HttpMgr 转发的登录模块 HTTP 回包。
    void slot_login_mod_finish(ReqID id, QString res, ErrorCodes err);

private:
    bool checkUserValid();
    bool checkPwdValid();
    void initHttpHandlers();
    void showLoginTip(const QString& message, bool success);
    void enterLogicDialog(const QJsonObject& response);

    Ui::MainWindow *ui;
    QStackedWidget *authStack_;
    RegisterPage *registerPage_;
    ForgotPasswordPage *forgotPasswordPage_;
    QLabel *loginTipLabel_;
    LogicDialog *logicDialog_;

    QMap<ReqID, std::function<void(const QJsonObject&)>> loginHandlers_;

    bool dragging_;
    QPoint dragOffset_;
};
#endif // MAINWINDOW_H
