#include "forgotpasswordpage.h"

#include "httpmgr.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
bool isValidEmail(const QString& text)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)"));
    return pattern.match(text).hasMatch();
}
}

ForgotPasswordPage::ForgotPasswordPage(QWidget* parent)
    : QWidget(parent)
    , accountEdit_(new QLineEdit(this))
    , verifyCodeEdit_(new QLineEdit(this))
    , passwordEdit_(new QLineEdit(this))
    , confirmPasswordEdit_(new QLineEdit(this))
    , getCodeButton_(new QPushButton(QStringLiteral("获取验证码"), this))
    , submitButton_(new QPushButton(QStringLiteral("重置密码"), this))
    , agreementBox_(new QCheckBox(QStringLiteral("已阅读并同意服务协议和隐私保护指引"), this))
    , tipLabel_(new QLabel(this))
    , countdownSeconds_(0)
{
    setObjectName(QStringLiteral("ForgotPasswordPage"));
    accountEdit_->setPlaceholderText(QStringLiteral("请输入邮箱"));
    verifyCodeEdit_->setPlaceholderText(QStringLiteral("请输入验证码"));
    passwordEdit_->setPlaceholderText(QStringLiteral("请输入新密码"));
    confirmPasswordEdit_->setPlaceholderText(QStringLiteral("请再次输入新密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    confirmPasswordEdit_->setEchoMode(QLineEdit::Password);

    auto* backButton = new QPushButton(QStringLiteral("返回登录"), this);
    backButton->setObjectName(QStringLiteral("resetBackButton"));
    auto* titleLabel = new QLabel(QStringLiteral("找回密码"), this);
    titleLabel->setObjectName(QStringLiteral("resetTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);
    auto* hintLabel = new QLabel(QStringLiteral("验证邮箱后设置新的登录密码"), this);
    hintLabel->setObjectName(QStringLiteral("resetHint"));
    hintLabel->setAlignment(Qt::AlignCenter);

    auto* codeLayout = new QHBoxLayout;
    codeLayout->setSpacing(10);
    codeLayout->addWidget(verifyCodeEdit_, 1);
    codeLayout->addWidget(getCodeButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(46, 18, 46, 28);
    layout->setSpacing(12);
    layout->addWidget(backButton, 0, Qt::AlignLeft);
    layout->addSpacing(4);
    layout->addWidget(titleLabel);
    layout->addWidget(hintLabel);
    layout->addSpacing(14);
    layout->addWidget(accountEdit_);
    layout->addLayout(codeLayout);
    layout->addWidget(passwordEdit_);
    layout->addWidget(confirmPasswordEdit_);
    layout->addWidget(agreementBox_);
    layout->addWidget(submitButton_);
    layout->addWidget(tipLabel_);
    layout->addStretch();

    setStyleSheet(QStringLiteral(R"(
        #ForgotPasswordPage { background: transparent; }
        #resetBackButton { border: none; background: transparent; color: #168df0; font-size: 14px; text-align: left; }
        #resetBackButton:hover { color: #006fd0; }
        #resetTitle { color: #202735; font-size: 27px; font-weight: 700; }
        #resetHint { color: #8c96a6; font-size: 13px; }
        QLineEdit { min-height: 48px; border: 1px solid #d6e0eb; border-radius: 12px; padding: 0 15px; background: rgba(255,255,255,220); font-size: 14px; }
        QLineEdit:focus { border-color: #258df4; background: white; }
        QPushButton { min-height: 48px; border: none; border-radius: 12px; }
        #resetGetCodeButton { background: #e1f1ff; color: #168df0; padding: 0 14px; }
        #resetSubmitButton { min-height: 52px; background: #168df0; color: white; font-size: 18px; }
        #resetSubmitButton:hover { background: #087fe1; }
        QPushButton:disabled { background: #c7dff1; color: #f8fbff; }
        QCheckBox { color: #707b8d; font-size: 12px; }
        #resetTipLabel { font-size: 12px; }
    )"));
    getCodeButton_->setObjectName(QStringLiteral("resetGetCodeButton"));
    submitButton_->setObjectName(QStringLiteral("resetSubmitButton"));
    tipLabel_->setObjectName(QStringLiteral("resetTipLabel"));
    tipLabel_->hide();

    countdownTimer_.setInterval(1000);
    connect(&countdownTimer_, &QTimer::timeout, this, &ForgotPasswordPage::updateCountdown);
    connect(getCodeButton_, &QPushButton::clicked, this, &ForgotPasswordPage::requestVerifyCode);
    connect(submitButton_, &QPushButton::clicked, this, &ForgotPasswordPage::submitReset);
    connect(backButton, &QPushButton::clicked, this, &ForgotPasswordPage::backRequested);
    connect(HttpMgr::Getinstance().get(), &HttpMgr::sig_mod_finish, this, &ForgotPasswordPage::handleHttpResult);
}

void ForgotPasswordPage::requestVerifyCode()
{
    const QString account = accountEdit_->text().trimmed();
    if (!isValidEmail(account))
    {
        showTip(QStringLiteral("请输入正确的邮箱"), false);
        accountEdit_->setFocus();
        return;
    }

    QJsonObject request;
    request[QStringLiteral("email")] = account;
    request[QStringLiteral("purpose")] = QStringLiteral("reset_password");
    getCodeButton_->setEnabled(false);
    countdownSeconds_ = 60;
    getCodeButton_->setText(QStringLiteral("60秒后重试"));
    countdownTimer_.start();
    HttpMgr::Getinstance()->PosthttpRec(
        QUrl(gateServerUrl() + QStringLiteral("/get_verify")),
        request, ReqID::ID_GET_RESET_CODE, Modules::VERIFYMOD);
}

void ForgotPasswordPage::submitReset()
{
    const QString account = accountEdit_->text().trimmed();
    const QString code = verifyCodeEdit_->text().trimmed();
    const QString password = passwordEdit_->text();
    const QString confirmPassword = confirmPasswordEdit_->text();

    if (!isValidEmail(account) || code.isEmpty())
    {
        showTip(QStringLiteral("请填写正确邮箱和验证码"), false);
        return;
    }
    if (password.length() < 6)
    {
        showTip(QStringLiteral("新密码至少需要6位"), false);
        return;
    }
    if (password != confirmPassword)
    {
        showTip(QStringLiteral("两次输入的密码不一致"), false);
        return;
    }
    if (!agreementBox_->isChecked())
    {
        showTip(QStringLiteral("请先同意服务协议和隐私保护指引"), false);
        return;
    }

    QJsonObject request;
    request[QStringLiteral("email")] = account;
    request[QStringLiteral("verify_code")] = code;
    request[QStringLiteral("code")] = code;
    request[QStringLiteral("password")] = password;
    request[QStringLiteral("new_password")] = password;
    request[QStringLiteral("confirm")] = confirmPassword;
    submitButton_->setEnabled(false);
    HttpMgr::Getinstance()->PosthttpRec(
        QUrl(gateServerUrl() + QStringLiteral("/reset_password")),
        request, ReqID::ID_RESET_PASSWORD, Modules::REGISTERMOD);
}

void ForgotPasswordPage::updateCountdown()
{
    --countdownSeconds_;
    if (countdownSeconds_ <= 0)
    {
        resetCodeButton();
        return;
    }
    getCodeButton_->setText(QStringLiteral("%1秒后重试").arg(countdownSeconds_));
}

void ForgotPasswordPage::resetCodeButton()
{
    countdownTimer_.stop();
    getCodeButton_->setEnabled(true);
    getCodeButton_->setText(QStringLiteral("获取验证码"));
}

void ForgotPasswordPage::handleHttpResult(ReqID id, QString response, ErrorCodes error)
{
    if (id != ReqID::ID_GET_RESET_CODE && id != ReqID::ID_RESET_PASSWORD)
    {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(response.toUtf8());
    const QJsonObject json = document.isObject() ? document.object() : QJsonObject();
    const int code = json.value(QStringLiteral("error")).toInt(
        json.value(QStringLiteral("code")).toInt(-1));

    if (id == ReqID::ID_GET_RESET_CODE)
    {
        if (error == ErrorCodes::SUCCESS && code == 0)
        {
            showTip(QStringLiteral("验证码发送成功，请检查邮箱"), true);
        }
        else
        {
            resetCodeButton();
            showTip(QStringLiteral("验证码发送失败：%1").arg(response), false);
        }
        return;
    }

    submitButton_->setEnabled(true);
    if (error == ErrorCodes::SUCCESS && code == 0)
    {
        showTip(QStringLiteral("密码重置成功"), true);
        emit resetSucceeded(accountEdit_->text().trimmed());
        return;
    }

    const QString message = response.isEmpty()
        ? QStringLiteral("服务端尚未提供密码重置接口")
        : QStringLiteral("密码重置失败：%1").arg(response);
    showTip(message, false);
}

void ForgotPasswordPage::showTip(const QString& message, bool success)
{
    tipLabel_->setText(message);
    tipLabel_->setStyleSheet(
        success ? QStringLiteral("color: #2f9e68;") : QStringLiteral("color: #e25561;"));
    tipLabel_->show();
}
