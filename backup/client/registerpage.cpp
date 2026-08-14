#include "registerpage.h"
#include "httpmgr.h"
#include "./ui_registerpage.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStyle>

namespace
{
bool isValidEmail(const QString& text)
{
    static const QRegularExpression emailRegex(
        QStringLiteral(
            R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)"
            )
        );

    return emailRegex.match(text).hasMatch();
}

/**
     * 获取后端业务错误码。
     *
     * 当前后端使用 error，
     * 同时兼容旧接口使用的 code。
     */
int responseCode(const QJsonObject& json)
{
    if (json.contains(QStringLiteral("error")))
    {
        return json.value(
                       QStringLiteral("error")
                       ).toInt(-1);
    }

    if (json.contains(QStringLiteral("code")))
    {
        return json.value(
                       QStringLiteral("code")
                       ).toInt(-1);
    }

    return -1;
}

/**
     * 获取后端返回的消息。
     *
     * 当前后端使用 message，
     * 同时兼容旧接口使用的 msg。
     */
QString responseMessage(
    const QJsonObject& json,
    const QString& defaultMessage = {})
{
    QString message =
        json.value(
                QStringLiteral("message")
                ).toString();

    if (!message.isEmpty())
    {
        return message;
    }

    message =
        json.value(
                QStringLiteral("msg")
                ).toString();

    if (!message.isEmpty())
    {
        return message;
    }

    return defaultMessage;
}

/**
     * 将后端错误码与消息组合起来显示。
     *
     * 例如：
     * 错误码：1004，验证码错误
     */
QString formatBackendError(
    int code,
    const QString& message)
{
    if (code < 0)
    {
        return message;
    }

    if (message.isEmpty())
    {
        return QStringLiteral("错误码：%1")
            .arg(code);
    }

    return QStringLiteral("错误码：%1，%2")
        .arg(code)
        .arg(message);
}

/**
     * 获取注册接口提示消息。
     *
     * 优先使用后端消息。
     * 后端没有返回消息时，根据错误码显示本地中文提示。
     */
QString registerMessage(
    int code,
    const QJsonObject& json)
{
    switch (code)
    {
    case 0:
        return QStringLiteral("注册成功");

    case 1001:
        return QStringLiteral("请求数据格式错误");
    case 1002:
        return QStringLiteral("验证码获取失败");
    case 1003:
        return QStringLiteral(
            "验证码已过期，请重新获取"
            );

    case 1004:
        return QStringLiteral("验证码错误");

    case 1005:
        return QStringLiteral("用户已存在");

    case 1006:
        return QStringLiteral("密码错误");

    case 1007:
        return QStringLiteral("邮箱不存在");

    case 1008:
        return QStringLiteral("密码更新失败");

    default:
        return responseMessage(
            json,
            QStringLiteral("注册失败")
            );
    }
}

QString verifyMessage(
    int code,
    const QJsonObject& json)
{
    switch (code)
    {
    case 0:
        return QStringLiteral("验证码发送成功");
    case 1001:
        return QStringLiteral("请求数据格式错误");
    case 1002:
        return QStringLiteral("验证码服务调用失败");
    default:
        return responseMessage(
            json,
            QStringLiteral("获取验证码失败")
            );
    }
}

QString networkErrorMessage(const QJsonObject& json)
{
    const QString detail = json.value(
        QStringLiteral("network_message")
        ).toString();

    return detail.isEmpty()
        ? QStringLiteral("网络请求失败")
        : QStringLiteral("网络请求失败：%1").arg(detail);
}
}

RegisterPage::RegisterPage(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::RegisterPage)
    , _countdownTimer(new QTimer(this))
    , _countdownSeconds(0)
{
    ui->setupUi(this);

    ui->tipLabel->clear();
    ui->tipLabel->setVisible(false);

    initHandlers();

    _countdownTimer->setSingleShot(false);

    connect(
        _countdownTimer,
        &QTimer::timeout,
        this,
        &RegisterPage::on_countdown_timeout
        );

    /*
     * 页面切换信号由 MainWindow 接收并处理。
     */
    connect(
        ui->backButton,
        &QPushButton::clicked,
        this,
        &RegisterPage::backRequested
        );

    /*
     * HttpMgr 收到响应后，由当前页面按照 ReqID
     * 分发给对应的业务处理函数。
     */
    connect(
        HttpMgr::Getinstance().get(),
        &HttpMgr::sig_mod_finish,
        this,
        &RegisterPage::slot_reg_mod_finish
        );

    /*
     * 回车时按照表单顺序移动焦点。
     */
    connect(
        ui->accountEdit,
        &QLineEdit::returnPressed,
        ui->verifyCodeEdit,
        qOverload<>(&QWidget::setFocus)
        );

    connect(
        ui->verifyCodeEdit,
        &QLineEdit::returnPressed,
        ui->passwordEdit,
        qOverload<>(&QWidget::setFocus)
        );

    connect(
        ui->passwordEdit,
        &QLineEdit::returnPressed,
        ui->confirmPasswordEdit,
        qOverload<>(&QWidget::setFocus)
        );

    /*
     * 获取验证码。
     */
    connect(
        ui->getCodeButton,
        &QPushButton::clicked,
        this,
        &RegisterPage::on_get_code_clicked
        );

    /*
     * 注册按钮。
     */
    connect(
        ui->submitButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            const QString account =
                ui->accountEdit->text().trimmed();

            const QString verifyCode =
                ui->verifyCodeEdit->text().trimmed();

            const QString password =
                ui->passwordEdit->text();

            const QString confirmPassword =
                ui->confirmPasswordEdit->text();

            if (account.isEmpty())
            {
                showTip(
                    QStringLiteral("请输入邮箱"),
                    false
                    );

                ui->accountEdit->setFocus();
                return;
            }

            if (!isValidEmail(account))
            {
                showTip(
                    QStringLiteral("邮箱格式不正确"),
                    false
                    );

                ui->accountEdit->setFocus();
                return;
            }

            if (verifyCode.isEmpty())
            {
                showTip(
                    QStringLiteral("请输入验证码"),
                    false
                    );

                ui->verifyCodeEdit->setFocus();
                return;
            }

            if (password.isEmpty())
            {
                showTip(
                    QStringLiteral("请输入密码"),
                    false
                    );

                ui->passwordEdit->setFocus();
                return;
            }

            if (password != confirmPassword)
            {
                showTip(
                    QStringLiteral("两次输入的密码不一致"),
                    false
                    );

                ui->confirmPasswordEdit->clear();
                ui->confirmPasswordEdit->setFocus();
                return;
            }

            if (!ui->agreementBox->isChecked())
            {
                showTip(
                    QStringLiteral("请先勾选服务协议"),
                    false
                    );

                return;
            }

            QJsonObject request;

            /*
             * 当前注册接口字段。
             */
            request[QStringLiteral("email")] =
                account;

            request[QStringLiteral("verify_code")] =
                verifyCode;

            request[QStringLiteral("password")] =
                password;

            /*
             * 兼容旧版后端字段。
             */
            request[QStringLiteral("user")] =
                account;

            request[QStringLiteral("passwd")] =
                password;

            request[QStringLiteral("confirm")] =
                confirmPassword;

            /*
             * 请求发送期间禁止重复提交。
             */
            ui->submitButton->setEnabled(false);

            HttpMgr::Getinstance()->PosthttpRec(
                QUrl(
                    gateServerUrl()
                    + QStringLiteral("/register")
                    ),
                request,
                ReqID::ID_REG_USER,
                Modules::REGISTERMOD
                );
        }
        );
}

RegisterPage::~RegisterPage()
{
    delete ui;
}

void RegisterPage::slot_reg_mod_finish(
    ReqID id,
    QString res,
    ErrorCodes err)
{
    /*
     * 即使 HTTP 状态为 400 或 500，
     * Qt 仍可能返回后端的 JSON 响应体。
     *
     * 因此这里始终尝试解析响应体，
     * 后面的 Handler 会优先处理业务错误码。
     */
    const QJsonDocument document =
        QJsonDocument::fromJson(
            res.toUtf8()
            );

    QJsonObject response;

    if (document.isObject())
    {
        response = document.object();
    }

    if (err != ErrorCodes::SUCCESS && !res.isEmpty())
    {
        response.insert(
            QStringLiteral("network_message"),
            res
            );
    }

    auto iterator = _handlers.find(id);

    if (iterator == _handlers.end())
    {
        return;
    }

    iterator.value()(response, err);
}

void RegisterPage::on_get_code_clicked()
{
    /*
     * 每次重新点击获取验证码时，
     * 清除上一次请求留下的提示。
     *
     * 当前请求完成后，会显示新的提示。
     */
    ui->tipLabel->clear();
    ui->tipLabel->setVisible(false);

    const QString account =
        ui->accountEdit->text().trimmed();

    if (account.isEmpty())
    {
        showTip(
            QStringLiteral("请先输入邮箱"),
            false
            );

        ui->accountEdit->setFocus();
        return;
    }

    if (!isValidEmail(account))
    {
        showTip(
            QStringLiteral("邮箱格式不正确"),
            false
            );

        ui->accountEdit->setFocus();
        return;
    }

    QJsonObject request;

    request[QStringLiteral("email")] =
        account;

    /*
     * 请求发送期间禁止重复点击。
     */
    ui->getCodeButton->setEnabled(false);

    _countdownSeconds = 60;

    ui->getCodeButton->setText(
        QStringLiteral("%1秒后重试")
            .arg(_countdownSeconds)
        );

    _countdownTimer->start(1000);

    HttpMgr::Getinstance()->PosthttpRec(
        QUrl(
            gateServerUrl()
            + QStringLiteral("/get_verify")
            ),
        request,
        ReqID::ID_GET_VARIFY_CODE,
        Modules::VERIFYMOD
        );
}

void RegisterPage::on_countdown_timeout()
{
    --_countdownSeconds;

    if (_countdownSeconds <= 0)
    {
        _countdownTimer->stop();

        ui->getCodeButton->setEnabled(true);

        ui->getCodeButton->setText(
            QStringLiteral("获取验证码")
            );

        return;
    }

    ui->getCodeButton->setText(
        QStringLiteral("%1秒后重试")
            .arg(_countdownSeconds)
        );
}

void RegisterPage::initHandlers()
{
    /*
     * POST /get_verify
     *
     * 当前后端响应格式：
     *
     * {
     *     "error": 0,
     *     "message": "success",
     *     "email": "example@example.com"
     * }
     */
    _handlers.insert(
        ReqID::ID_GET_VARIFY_CODE,
        [this](
            const QJsonObject& json,
            ErrorCodes networkError)
        {
            const int code =
                responseCode(json);

            const QString message =
                verifyMessage(code, json);

            /*
             * 优先处理后端业务码。
             *
             * 即使 HTTP 返回了 400、500，
             * 只要响应体中包含 error/code，
             * 就显示后端返回的错误码和消息。
             */
            if (code >= 0)
            {
                if (code != 0)
                {
                    _countdownTimer->stop();

                    ui->getCodeButton->setEnabled(true);

                    ui->getCodeButton->setText(
                        QStringLiteral("获取验证码")
                        );

                    showTip(
                        formatBackendError(
                            code,
                            message
                            ),
                        false
                        );

                    return;
                }

                /*
                 * 业务码为 0，验证码发送成功。
                 * 成功后继续执行倒计时。
                 */
                showTip(
                    message,
                    true
                    );

                return;
            }

            /*
             * 后端没有返回业务码时，
             * 再判断是否为网络错误。
             */
            if (networkError != ErrorCodes::SUCCESS)
            {
                _countdownTimer->stop();

                ui->getCodeButton->setEnabled(true);

                ui->getCodeButton->setText(
                    QStringLiteral("获取验证码")
                    );

                showTip(
                    networkErrorMessage(json),
                    false
                    );

                return;
            }

            /*
             * 网络请求完成，但是响应中没有 error/code。
             */
            _countdownTimer->stop();

            ui->getCodeButton->setEnabled(true);

            ui->getCodeButton->setText(
                QStringLiteral("获取验证码")
                );

            showTip(
                QStringLiteral("后端响应格式错误"),
                false
                );
        }
        );

    /*
     * POST /register
     */
    _handlers.insert(
        ReqID::ID_REG_USER,
        [this](
            const QJsonObject& json,
            ErrorCodes networkError)
        {
            /*
             * 注册请求已结束，恢复注册按钮。
             */
            ui->submitButton->setEnabled(true);

            const int code =
                responseCode(json);

            const QString message =
                registerMessage(
                    code,
                    json
                    );

            /*
             * 优先处理后端业务码。
             */
            if (code >= 0)
            {
                if (code != 0)
                {
                    showTip(
                        formatBackendError(
                            code,
                            message
                            ),
                        false
                        );

                    return;
                }

                const QString account =
                    json.value(
                            QStringLiteral("email")
                            ).toString();

                showTip(
                    message,
                    true
                    );

                emit registerRequested(
                    account.isEmpty()
                        ? ui->accountEdit
                              ->text()
                              .trimmed()
                        : account,
                    QString()
                    );

                return;
            }

            /*
             * 没有业务码时，才判断网络错误。
             */
            if (networkError != ErrorCodes::SUCCESS)
            {
                showTip(
                    networkErrorMessage(json),
                    false
                    );

                return;
            }

            showTip(
                QStringLiteral("后端响应格式错误"),
                false
                );
        }
        );
}

void RegisterPage::showTip(
    const QString& message,
    bool success)
{
    ui->tipLabel->setText(message);

    ui->tipLabel->setProperty(
        "success",
        success
        );

    ui->tipLabel->style()->unpolish(
        ui->tipLabel
        );

    ui->tipLabel->style()->polish(
        ui->tipLabel
        );

    ui->tipLabel->setVisible(true);
}
