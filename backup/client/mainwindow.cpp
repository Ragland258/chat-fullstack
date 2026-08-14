#include "mainwindow.h"
#include "forgotpasswordpage.h"
#include "httpmgr.h"
#include "logicdialog.h"
#include "registerpage.h"
#include "./ui_mainwindow.h"
#include "const.h"

#include <QDebug>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStatusBar>


namespace
{
constexpr int LoginAvatarDiameter = 96;

// 检查邮箱格式是否合法。
bool isValidEmail(const QString& text)
{
    static const QRegularExpression emailRegex(
        QStringLiteral(
            R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)"
            )
        );

    return emailRegex.match(text).hasMatch();
}


// 去除控件可能携带的 QFrame 边框。
// 参数使用 QWidget*，因此 QWidget 和 QFrame 类型都能安全传入。
void removeFrameBorder(QWidget* widget)
{
    if (widget == nullptr)
    {
        return;
    }

    if (auto* frame = qobject_cast<QFrame*>(widget))
    {
        frame->setFrameShape(QFrame::NoFrame);
        frame->setFrameShadow(QFrame::Plain);
        frame->setLineWidth(0);
        frame->setMidLineWidth(0);
    }
}

QPixmap circularAvatar(const QPixmap& source, int diameter)
{
    const QPixmap scaled = source.scaled(
        diameter,
        diameter,
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation);

    const QRect sourceRect(
        (scaled.width() - diameter) / 2,
        (scaled.height() - diameter) / 2,
        diameter,
        diameter);

    QPixmap result(diameter, diameter);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath clipPath;
    clipPath.addEllipse(QRectF(0, 0, diameter, diameter));
    painter.setClipPath(clipPath);
    painter.drawPixmap(QRect(0, 0, diameter, diameter), scaled, sourceRect);

    return result;
}
}


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , authStack_(nullptr)
    , registerPage_(nullptr)
    , forgotPasswordPage_(nullptr)
    , loginTipLabel_(nullptr)
    , logicDialog_(nullptr)
    , dragging_(false)
{
    ui->setupUi(this);

    const int avatarNumber = QRandomGenerator::global()->bounded(1, 5);
    const QString randomAvatar =
        QStringLiteral(":/assets/%1.png").arg(avatarNumber);
    if (!setLoginAvatar(randomAvatar))
    {
        resetLoginAvatar();
    }

    setWindowTitle(QStringLiteral("Chat · 登录"));

    // QQ 风格无边框窗口：去掉系统标题栏和系统窗口边框。
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // 窗口外层完全透明，真正的圆角背景由 shellFrame 绘制。
    setStyleSheet(
        styleSheet() +
        QStringLiteral(
            R"(
                QMainWindow,
                QWidget#centralwidget {
                    border: none;
                    background: transparent;
                }

                QFrame#shellFrame {
                    border: none;
                    outline: none;
                    border-radius: 18px;
                }
            )"
            )
        );

    // 去掉最外层空隙，避免透明区域在未开启透明时显示成黑边。
    ui->outerLayout->setContentsMargins(0, 0, 0, 0);
    ui->outerLayout->setSpacing(0);
    ui->shellLayout->setContentsMargins(0, 0, 0, 0);
    ui->shellLayout->setSpacing(0);

    // 自定义标题栏负责拖动窗口；关闭按钮仍沿用原有 UI 连接。
    ui->titleBar->installEventFilter(this);
    ui->titleBar->setMouseTracking(true);

    /*
     * 去掉 QFrame 默认绘制的黑色或深灰色边框。
     *
     * shellFrame:
     *     登录界面最外层的白色主卡片。
     *
     * brandPanel:
     *     左侧品牌介绍区域。
     *
     * loginPanel:
     *     右侧登录区域。
     *
     * featureCard:
     *     左侧功能介绍卡片。
     */
    removeFrameBorder(ui->shellFrame);
    removeFrameBorder(ui->brandPanel);
    removeFrameBorder(ui->loginPanel);
    removeFrameBorder(ui->featureCard);

    /*
     * 使用样式表进一步覆盖 QFrame 默认边框。
     *
     * 这里只修改 border，不会删除原有的：
     *     背景色
     *     渐变
     *     圆角
     *     字体
     */
    setStyleSheet(
        styleSheet() +
        QStringLiteral(
            R"(
                QFrame#shellFrame,
                QFrame#brandPanel,
                QFrame#loginPanel,
                QFrame#featureCard {
                    border: none;
                    outline: none;
                }

                QStatusBar {
                    border: none;
                    background: transparent;
                }

                QStatusBar::item {
                    border: none;
                }
            )"
            )
        );

    // 保留原有 statusBar() 调用接口，但隐藏系统状态栏，避免底部出现边框和“准备登录”。
    statusBar()->setSizeGripEnabled(false);
    statusBar()->hide();
    statusBar()->showMessage(
        QStringLiteral("准备登录")
        );

    /*
     * 创建页面切换容器。
     *
     * 登录页面和注册页面都放进 QStackedWidget，
     * 之后可以在两个页面之间切换。
     */
    authStack_ = new QStackedWidget(this);
    authStack_->setObjectName(
        QStringLiteral("authStack")
        );

    authStack_->setContentsMargins(
        0,
        0,
        0,
        0
        );
    authStack_->setFrameShape(QFrame::NoFrame);
    authStack_->setStyleSheet(
        QStringLiteral("QStackedWidget#authStack { border: none; background: transparent; }")
        );

    /*
     * 将 Designer 创建的登录面板从原布局中取出，
     * 然后放入 QStackedWidget。
     */
    ui->shellLayout->removeWidget(
        ui->loginPanel
        );

    ui->loginPanel->setParent(nullptr);

    authStack_->addWidget(
        ui->loginPanel
        );

    /*
     * 创建注册页面，并加入页面切换容器。
     */
    registerPage_ = new RegisterPage(this);

    authStack_->addWidget(
        registerPage_
        );

    forgotPasswordPage_ = new ForgotPasswordPage(this);
    authStack_->addWidget(forgotPasswordPage_);

    connect(
        forgotPasswordPage_,
        &ForgotPasswordPage::backRequested,
        this,
        [this]()
        {
            authStack_->setCurrentWidget(ui->loginPanel);
            ui->accountEdit->setFocus();
        });

    connect(
        forgotPasswordPage_,
        &ForgotPasswordPage::resetSucceeded,
        this,
        [this](const QString& account)
        {
            authStack_->setCurrentWidget(ui->loginPanel);
            ui->accountEdit->setText(account);
            ui->passwordEdit->clear();
            ui->passwordEdit->setFocus();
        });

    /*
     * 将页面切换容器添加到右侧区域。
     */
    ui->shellLayout->addWidget(
        authStack_
        );

    /*
     * 默认显示登录页面。
     */
    authStack_->setCurrentWidget(
        ui->loginPanel
        );

    ui->accountEdit->setFocus();
    ui->rememberBox->setChecked(true);

    loginTipLabel_ = new QLabel(ui->loginPanel);
    loginTipLabel_->setObjectName(QStringLiteral("loginTipLabel"));
    loginTipLabel_->setAlignment(Qt::AlignCenter);
    loginTipLabel_->setWordWrap(true);
    loginTipLabel_->setVisible(false);

    const int loginButtonIndex =
        ui->loginLayout->indexOf(ui->loginButton);
    ui->loginLayout->insertWidget(
        loginButtonIndex + 1,
        loginTipLabel_);

    initHttpHandlers();

    connect(
        HttpMgr::Getinstance().get(),
        &HttpMgr::sig_login_mod_finish,
        this,
        &MainWindow::slot_login_mod_finish);

    /*
     * 点击登录按钮或在密码框按 Enter 时，
     * 发送真实的 POST /login 请求。
     */
    connect(
        ui->loginButton,
        &QPushButton::clicked,
        this,
        &MainWindow::on_login_btn_clicked);

    connect(
        ui->passwordEdit,
        &QLineEdit::returnPressed,
        this,
        &MainWindow::on_login_btn_clicked);

    /*
     * 在账号输入框中按 Enter 时，
     * 将焦点切换到密码输入框。
     */
    connect(
        ui->accountEdit,
        &QLineEdit::returnPressed,
        ui->passwordEdit,
        qOverload<>(&QWidget::setFocus)
        );

    /*
     * 找回密码按钮。
     */
    connect(
        ui->forgotButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            authStack_->setCurrentWidget(forgotPasswordPage_);
            statusBar()->showMessage(
                QStringLiteral("找回密码功能待接入")
                );
        }
        );

    /*
     * 点击注册按钮后切换到注册页面。
     */
    connect(
        ui->registerButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            authStack_->setCurrentWidget(
                registerPage_
                );

            statusBar()->showMessage(
                QStringLiteral("已切换到注册页面")
                );
        }
        );

    /*
     * 扫码登录按钮。
     */
    connect(
        ui->scanButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            statusBar()->showMessage(
                QStringLiteral("扫码登录功能待接入")
                );
        }
        );

    /*
     * 注册页面点击返回时，
     * 切回登录页面。
     */
    connect(
        registerPage_,
        &RegisterPage::backRequested,
        this,
        [this]()
        {
            authStack_->setCurrentWidget(
                ui->loginPanel
                );

            statusBar()->showMessage(
                QStringLiteral("已返回登录页面")
                );

            ui->accountEdit->setFocus();
        }
        );

    /*
     * 注册页面提交成功后：
     *
     * 1. 切回登录页面。
     * 2. 自动填写注册邮箱。
     * 3. 清空密码输入框。
     */
    connect(
        registerPage_,
        &RegisterPage::registerRequested,
        this,
        [this](
            const QString& account,
            const QString&)
        {
            authStack_->setCurrentWidget(
                ui->loginPanel
                );

            ui->accountEdit->setText(
                account
                );

            ui->passwordEdit->clear();
            ui->passwordEdit->setFocus();

            statusBar()->showMessage(
                QStringLiteral(
                    "注册信息已提交，已返回登录页面"
                    )
                );

            QMessageBox::information(
                this,
                QStringLiteral("注册"),
                QStringLiteral(
                    "邮箱 %1 的注册信息已经通过校验。\n"
                    "账号已经自动填写到登录页面。"
                    ).arg(account)
                );
        }
        );
}


bool MainWindow::checkUserValid()
{
    const QString user = ui->accountEdit->text().trimmed();

    if (user.isEmpty())
    {
        qDebug() << "User empty";
        showLoginTip(QStringLiteral("请输入邮箱"), false);
        ui->accountEdit->setFocus();
        return false;
    }

    if (!isValidEmail(user))
    {
        showLoginTip(QStringLiteral("邮箱格式不正确"), false);
        ui->accountEdit->setFocus();
        return false;
    }

    return true;
}

bool MainWindow::checkPwdValid()
{
    const QString password = ui->passwordEdit->text();

    if (password.length() < 6 || password.length() > 15)
    {
        qDebug() << "Pass length invalid";
        showLoginTip(QStringLiteral("密码长度必须为 6 到 15 位"), false);
        ui->passwordEdit->setFocus();
        return false;
    }

    return true;
}

void MainWindow::on_login_btn_clicked()
{
    qDebug() << "login btn clicked";

    if (!checkUserValid() || !checkPwdValid())
    {
        return;
    }

    const QString email = ui->accountEdit->text().trimmed();
    const QString password = ui->passwordEdit->text();

    // 当前 Gate Server 的登录接口是 POST /login，字段为 email/password。
    QJsonObject request;
    request[QStringLiteral("email")] = email;
    request[QStringLiteral("password")] = password;

    ui->loginButton->setEnabled(false);
    showLoginTip(QStringLiteral("正在登录，请稍候……"), true);

    HttpMgr::Getinstance()->PostHttpReq(
        QUrl(gateServerUrl() + QStringLiteral("/login")),
        request,
        ReqID::ID_LOGIN_USER,
        Modules::LOGINMOD);
}

void MainWindow::initHttpHandlers()
{
    loginHandlers_.insert(
        ReqID::ID_LOGIN_USER,
        [this](const QJsonObject& json)
        {
            const int error = json.value(QStringLiteral("error")).toInt(-1);
            const QString backendMessage =
                json.value(QStringLiteral("message")).toString();

            if (error != 0)
            {
                QString message = backendMessage;
                if (message.isEmpty())
                {
                    switch (error)
                    {
                    case 1001:
                        message = QStringLiteral("登录参数错误");
                        break;
                    case 1002:
                        message = QStringLiteral("状态服务调用失败");
                        break;
                    case 1005:
                        message = QStringLiteral("Redis 服务异常");
                        break;
                    case 2006:
                        message = QStringLiteral("用户不存在");
                        break;
                    case 2007:
                        message = QStringLiteral("密码错误");
                        break;
                    case 4001:
                        message = QStringLiteral("登录令牌已过期");
                        break;
                    case 4002:
                        message = QStringLiteral("登录令牌无效");
                        break;
                    case 4003:
                        message = QStringLiteral("暂无可用聊天服务器");
                        break;
                    default:
                        message = QStringLiteral("登录失败");
                        break;
                    }
                }

                showLoginTip(
                    QStringLiteral("%1（错误码：%2）")
                        .arg(message)
                        .arg(error),
                    false);
                return;
            }

            enterLogicDialog(json);
        });
}

void MainWindow::slot_login_mod_finish(
    ReqID id,
    QString res,
    ErrorCodes err)
{
    if (id != ReqID::ID_LOGIN_USER)
    {
        return;
    }

    ui->loginButton->setEnabled(true);

    QJsonParseError parseError;
    const QJsonDocument jsonDocument =
        QJsonDocument::fromJson(res.toUtf8(), &parseError);

    if (!jsonDocument.isObject())
    {
        if (err != ErrorCodes::SUCCESS)
        {
            showLoginTip(
                res.isEmpty()
                    ? QStringLiteral("网络请求错误")
                    : QStringLiteral("网络请求错误：%1").arg(res),
                false);
        }
        else
        {
            showLoginTip(
                QStringLiteral("JSON 解析错误：%1")
                    .arg(parseError.errorString()),
                false);
        }
        return;
    }

    const auto iterator = loginHandlers_.find(id);
    if (iterator == loginHandlers_.end())
    {
        showLoginTip(QStringLiteral("未找到登录响应处理器"), false);
        return;
    }

    // 即使 HTTP 状态为 4xx/5xx，只要响应体是后端 JSON，仍处理业务错误码。
    iterator.value()(jsonDocument.object());
}

void MainWindow::showLoginTip(
    const QString& message,
    bool success)
{
    if (loginTipLabel_ == nullptr)
    {
        return;
    }

    loginTipLabel_->setText(message);
    loginTipLabel_->setStyleSheet(
        success
            ? QStringLiteral("QLabel { color: #2f9e68; padding-top: 6px; }")
            : QStringLiteral("QLabel { color: #e25561; padding-top: 6px; }"));
    loginTipLabel_->setVisible(true);
}

void MainWindow::enterLogicDialog(const QJsonObject& response)
{
    qDebug().noquote()
    << "[login] response json:"
    << QString::fromUtf8(
           QJsonDocument(response)
               .toJson(QJsonDocument::Compact));

    /*
     * 兼容后端返回：
     *
     * "email": "xxx@qq.com"
     *
     * 以及旧接口：
     *
     * "user": "xxx@qq.com"
     */
    QString email =
        response.value(QStringLiteral("email")).toString();

    if (email.isEmpty())
    {
        email =
            response.value(QStringLiteral("user")).toString();
    }

    const QString token =
        response.value(QStringLiteral("token")).toString();

    /*
     * 兼容两种服务器地址字段：
     *
     * "host": "127.0.0.1"
     * "ip":   "127.0.0.1"
     */
    QString host =
        response.value(QStringLiteral("host")).toString();

    if (host.isEmpty())
    {
        host =
            response.value(QStringLiteral("ip")).toString();
    }

    /*
     * 兼容两种端口格式：
     *
     * "port": 9001
     * "port": "9001"
     */
    int port = 0;

    const QJsonValue portValue =
        response.value(QStringLiteral("port"));

    if (portValue.isDouble())
    {
        port = portValue.toInt();
    }
    else if (portValue.isString())
    {
        bool ok = false;

        port =
            portValue.toString().toInt(&ok);

        if (!ok)
        {
            port = 0;
        }
    }

    qDebug()
        << "[login] parsed fields:"
        << "email=" << email
        << "tokenLength=" << token.length()
        << "host=" << host
        << "port=" << port;

    /*
     * 分开检查每个字段，方便准确定位问题。
     */
    if (email.isEmpty())
    {
        showLoginTip(
            QStringLiteral("登录响应缺少 email/user"),
            false);

        return;
    }

    if (token.isEmpty())
    {
        showLoginTip(
            QStringLiteral("登录响应缺少 token"),
            false);

        return;
    }

    if (host.isEmpty())
    {
        showLoginTip(
            QStringLiteral("登录响应缺少 host/ip"),
            false);

        return;
    }

    if (port <= 0 || port > 65535)
    {
        showLoginTip(
            QStringLiteral("登录响应中的 port 无效"),
            false);

        return;
    }

    showLoginTip(
        QStringLiteral("登录成功"),
        true);

    qDebug()
        << "[login] success"
        << "email=" << email
        << "chat server=" << host
        << port;

    if (logicDialog_ == nullptr)
    {
        logicDialog_ =
            new LogicDialog(nullptr);

        logicDialog_->setAttribute(
            Qt::WA_DeleteOnClose,
            true);

        connect(
            logicDialog_,
            &QObject::destroyed,
            this,
            [this]()
            {
                logicDialog_ = nullptr;

                show();
                raise();
                activateWindow();
            });
    }

    /*
     * 保存登录会话。
     *
     * 后续连接 Chat Server 时可以读取：
     *
     * logicDialog_->property("loginEmail")
     * logicDialog_->property("loginToken")
     * logicDialog_->property("chatHost")
     * logicDialog_->property("chatPort")
     */
    logicDialog_->setProperty(
        "loginEmail",
        email);

    logicDialog_->setProperty(
        "loginToken",
        token);

    logicDialog_->setProperty(
        "chatHost",
        host);

    logicDialog_->setProperty(
        "chatPort",
        port);

    logicDialog_->show();
    logicDialog_->raise();
    logicDialog_->activateWindow();

    hide();
}


bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == ui->titleBar)
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);

            if (mouseEvent->button() == Qt::LeftButton)
            {
                dragging_ = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                dragOffset_ =
                    mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
#else
                dragOffset_ =
                    mouseEvent->globalPos() - frameGeometry().topLeft();
#endif
                return true;
            }
            break;
        }

        case QEvent::MouseMove:
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);

            if (dragging_ && (mouseEvent->buttons() & Qt::LeftButton))
            {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                move(mouseEvent->globalPosition().toPoint() - dragOffset_);
#else
                move(mouseEvent->globalPos() - dragOffset_);
#endif
                return true;
            }
            break;
        }

        case QEvent::MouseButtonRelease:
            dragging_ = false;
            return true;

        default:
            break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}


MainWindow::~MainWindow()
{
    delete ui;
}

bool MainWindow::setLoginAvatar(const QString& imagePath)
{
    const QPixmap source(imagePath);
    if (source.isNull())
    {
        return false;
    }

    ui->avatarBadge->setPixmap(
        circularAvatar(source, LoginAvatarDiameter));
    ui->avatarBadge->setScaledContents(false);
    ui->avatarBadge->setAlignment(Qt::AlignCenter);

    return true;
}

void MainWindow::resetLoginAvatar()
{
    const QPixmap source(QStringLiteral(":/assets/avatar.png"));
    ui->avatarBadge->setPixmap(
        circularAvatar(source, LoginAvatarDiameter));
    ui->avatarBadge->setScaledContents(false);
    ui->avatarBadge->setAlignment(Qt::AlignCenter);

}
