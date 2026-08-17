#include "logicdialog.h"
#include "ui_logicdialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColor>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char *secondaryPageStyle = R"(
QWidget#friendsPage, QWidget#spacePage, QWidget#publishPage, QWidget#settingsPage {
    background: #ffffff;
}
QLabel#pageTitle {
    color: #202226;
    font-size: 24px;
    font-weight: 600;
}
QLabel#pageSubtitle, QLabel#sectionTitle, QLabel#settingCaption {
    color: #8a8f97;
    font-size: 12px;
}
QLineEdit#pageSearch, QPlainTextEdit#spaceEditor {
    color: #26282d;
    background: #f3f3f4;
    border: 1px solid #e4e5e7;
    border-radius: 10px;
    padding: 10px 12px;
}
QLineEdit#pageSearch:focus, QPlainTextEdit#spaceEditor:focus {
    background: #ffffff;
    border-color: #afb2b8;
}
QListWidget#friendList {
    color: #26282d;
    background: #ffffff;
    border: none;
    outline: none;
}
QListWidget#friendList::item {
    min-height: 54px;
    padding: 8px 12px;
    border-radius: 10px;
}
QListWidget#friendList::item:hover, QListWidget#friendList::item:selected {
    color: #202226;
    background: #f0f0f1;
}
QFrame#postCard, QFrame#settingsCard {
    background: #fafafa;
    border: 1px solid #e7e8ea;
    border-radius: 12px;
}
QLabel#postAuthor, QLabel#settingValue {
    color: #26282d;
    font-size: 13px;
    font-weight: 600;
}
QLabel#postText {
    color: #373a40;
    font-size: 13px;
}
QPushButton#primaryAction {
    min-height: 38px;
    padding: 0 18px;
    color: #ffffff;
    background: #202226;
    border: none;
    border-radius: 9px;
    font-weight: 600;
}
QPushButton#primaryAction:hover {
    background: #3a3d42;
}
QPushButton#secondaryAction {
    min-height: 34px;
    padding: 0 14px;
    color: #555a62;
    background: #f3f3f4;
    border: 1px solid #e2e3e5;
    border-radius: 8px;
}
QPushButton#secondaryAction:hover {
    color: #202226;
    background: #e9e9ea;
}
)";

QLabel *makeTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QFrame *makePostCard(
    const QString &author,
    const QString &time,
    const QString &text)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("postCard"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 15, 18, 15);
    layout->setSpacing(9);

    auto *header = new QHBoxLayout;
    auto *authorLabel = new QLabel(author);
    authorLabel->setObjectName(QStringLiteral("postAuthor"));
    auto *timeLabel = new QLabel(time);
    timeLabel->setObjectName(QStringLiteral("pageSubtitle"));
    header->addWidget(authorLabel);
    header->addStretch();
    header->addWidget(timeLabel);

    auto *textLabel = new QLabel(text);
    textLabel->setObjectName(QStringLiteral("postText"));
    textLabel->setWordWrap(true);

    layout->addLayout(header);
    layout->addWidget(textLabel);
    return card;
}

} // namespace

LogicDialog::LogicDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LogicDialog)
    , navGroup_(new QButtonGroup(this))
    , contentStack_(nullptr)
    , spaceEditor_(nullptr)
    , publishFeedback_(nullptr)
    , settingsEmailLabel_(nullptr)
    , settingsServerLabel_(nullptr)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("Chat"));

    auto *shadow = new QGraphicsDropShadowEffect(ui->rootFrame);
    shadow->setBlurRadius(32.0);
    shadow->setOffset(0.0, 8.0);
    shadow->setColor(QColor(25, 28, 35, 28));
    ui->rootFrame->setGraphicsEffect(shadow);

    // 把原有消息区域包装进主内容栈，侧栏可以切换好友、空间等页面。
    ui->rootLayout->removeWidget(ui->conversationPanel);
    ui->rootLayout->removeWidget(ui->chatPanel);

    contentStack_ = new QStackedWidget(ui->rootFrame);
    contentStack_->setObjectName(QStringLiteral("contentStack"));

    auto *messagePage = new QWidget(contentStack_);
    messagePage->setObjectName(QStringLiteral("messagePage"));
    auto *messagePageLayout = new QHBoxLayout(messagePage);
    messagePageLayout->setContentsMargins(0, 0, 0, 0);
    messagePageLayout->setSpacing(0);
    messagePageLayout->addWidget(ui->conversationPanel);
    messagePageLayout->addWidget(ui->chatPanel, 1);

    contentStack_->addWidget(messagePage);
    contentStack_->addWidget(createFriendsPage());
    contentStack_->addWidget(createSpacePage());
    contentStack_->addWidget(createPublishPage());
    contentStack_->addWidget(createSettingsPage());
    ui->rootLayout->addWidget(contentStack_, 1);

    navGroup_->setExclusive(true);
    navGroup_->addButton(ui->messageNav, 0);
    navGroup_->addButton(ui->contactNav, 1);
    navGroup_->addButton(ui->spaceNav, 2);
    navGroup_->addButton(ui->publishNav, 3);
    navGroup_->addButton(ui->settingsNav, 4);

    connect(ui->messageNav, &QPushButton::clicked, this, [this]() { switchPage(0); });
    connect(ui->contactNav, &QPushButton::clicked, this, [this]() { switchPage(1); });
    connect(ui->spaceNav, &QPushButton::clicked, this, [this]() { switchPage(2); });
    connect(ui->publishNav, &QPushButton::clicked, this, [this]() { switchPage(3); });
    connect(ui->settingsNav, &QPushButton::clicked, this, [this]() { switchPage(4); });

    connect(ui->sendButton, &QPushButton::clicked, this, &LogicDialog::sendCurrentMessage);
    ui->messageEdit->installEventFilter(this);
    ui->messageEdit->setFocus();

    connect(ui->addConversationButton, &QPushButton::clicked, this, [this]() {
        switchPage(1);
    });

    connect(ui->searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString keyword = text.trimmed();
        const auto matches = [&keyword](QLabel *name, QLabel *preview) {
            return keyword.isEmpty()
                || name->text().contains(keyword, Qt::CaseInsensitive)
                || preview->text().contains(keyword, Qt::CaseInsensitive);
        };

        ui->activeConversation->setVisible(matches(ui->conversationNameOne, ui->conversationPreviewOne));
        ui->conversationTwo->setVisible(matches(ui->conversationNameTwo, ui->conversationPreviewTwo));
        ui->conversationThree->setVisible(matches(ui->conversationNameThree, ui->conversationPreviewThree));
        ui->conversationFour->setVisible(matches(ui->conversationNameFour, ui->conversationPreviewFour));
        ui->conversationFive->setVisible(matches(ui->conversationNameFive, ui->conversationPreviewFive));
    });

    switchPage(0);
}

LogicDialog::~LogicDialog()
{
    delete ui;
}

bool LogicDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->messageEdit && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isEnter = keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter;

        if (isEnter && !keyEvent->modifiers().testFlag(Qt::ShiftModifier))
        {
            sendCurrentMessage();
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

void LogicDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refreshSessionInfo();
}

QWidget *LogicDialog::createFriendsPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("friendsPage"));
    page->setStyleSheet(QString::fromUtf8(secondaryPageStyle));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(34, 28, 34, 28);
    layout->setSpacing(14);

    layout->addWidget(makeTitle(QStringLiteral("好友")));
    auto *subtitle = new QLabel(QStringLiteral("全部联系人 · 4 位好友"));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    layout->addWidget(subtitle);

    auto *search = new QLineEdit;
    search->setObjectName(QStringLiteral("pageSearch"));
    search->setPlaceholderText(QStringLiteral("搜索好友"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    auto *section = new QLabel(QStringLiteral("好友列表"));
    section->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(section);

    auto *list = new QListWidget;
    list->setObjectName(QStringLiteral("friendList"));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    struct FriendInfo {
        const char *name;
        const char *detail;
    };

    const FriendInfo friends[] = {
        {"林夏", "在线 · 最近联系"},
        {"张伟", "在线"},
        {"陈墨", "离线 · 昨天"},
        {"王芳", "在线"}
    };

    for (const FriendInfo &friendInfo : friends)
    {
        const QString name = QString::fromUtf8(friendInfo.name);
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2")
                .arg(name, QString::fromUtf8(friendInfo.detail)),
            list);
        item->setData(Qt::UserRole, name);
        item->setData(Qt::UserRole + 1, QString::fromUtf8(friendInfo.detail));
        item->setSizeHint(QSize(0, 58));
    }

    connect(search, &QLineEdit::textChanged, list, [list](const QString &text) {
        for (int row = 0; row < list->count(); ++row)
        {
            QListWidgetItem *item = list->item(row);
            item->setHidden(!text.trimmed().isEmpty()
                            && !item->text().contains(text, Qt::CaseInsensitive));
        }
    });

    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString name = item->data(Qt::UserRole).toString();
        ui->chatName->setText(name);
        ui->chatAvatar->setText(name.left(1));
        ui->chatStatus->setText(QStringLiteral("● %1").arg(item->data(Qt::UserRole + 1).toString()));
        switchPage(0);
    });

    layout->addWidget(list, 1);
    return page;
}

QWidget *LogicDialog::createSpacePage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("spacePage"));
    page->setStyleSheet(QString::fromUtf8(secondaryPageStyle));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(34, 28, 34, 28);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout;
    header->addWidget(makeTitle(QStringLiteral("空间")));
    header->addStretch();
    auto *publish = new QPushButton(QStringLiteral("发布动态"));
    publish->setObjectName(QStringLiteral("primaryAction"));
    header->addWidget(publish);
    layout->addLayout(header);

    auto *subtitle = new QLabel(QStringLiteral("好友最近分享"));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    layout->addWidget(subtitle);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 10, 0);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(makePostCard(
        QStringLiteral("林夏"),
        QStringLiteral("10:18"),
        QStringLiteral("登录流程已经联通，接下来开始完善单聊消息。")));
    contentLayout->addWidget(makePostCard(
        QStringLiteral("陈墨"),
        QStringLiteral("昨天"),
        QStringLiteral("今天完成了项目联调，明天继续处理断线重连。")));
    contentLayout->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    connect(publish, &QPushButton::clicked, this, [this]() { switchPage(3); });
    return page;
}

QWidget *LogicDialog::createPublishPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("publishPage"));
    page->setStyleSheet(QString::fromUtf8(secondaryPageStyle));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(34, 28, 34, 28);
    layout->setSpacing(14);

    layout->addWidget(makeTitle(QStringLiteral("发布动态")));
    auto *subtitle = new QLabel(QStringLiteral("分享文字、图片或视频"));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    layout->addWidget(subtitle);

    spaceEditor_ = new QPlainTextEdit;
    spaceEditor_->setObjectName(QStringLiteral("spaceEditor"));
    spaceEditor_->setPlaceholderText(QStringLiteral("分享此刻的想法…"));
    spaceEditor_->setMinimumHeight(190);
    spaceEditor_->setMaximumHeight(240);
    layout->addWidget(spaceEditor_);

    auto *actions = new QHBoxLayout;
    const QString actionTexts[] = {
        QStringLiteral("添加图片"),
        QStringLiteral("添加视频"),
        QStringLiteral("添加文件")
    };
    for (const QString &text : actionTexts)
    {
        auto *button = new QPushButton(text);
        button->setObjectName(QStringLiteral("secondaryAction"));
        actions->addWidget(button);
    }
    actions->addStretch();
    layout->addLayout(actions);

    publishFeedback_ = new QLabel;
    publishFeedback_->setObjectName(QStringLiteral("pageSubtitle"));
    publishFeedback_->hide();
    layout->addWidget(publishFeedback_);

    auto *publish = new QPushButton(QStringLiteral("发布"));
    publish->setObjectName(QStringLiteral("primaryAction"));
    publish->setMaximumWidth(110);
    layout->addWidget(publish, 0, Qt::AlignRight);
    layout->addStretch();

    connect(publish, &QPushButton::clicked, this, [this]() {
        if (spaceEditor_->toPlainText().trimmed().isEmpty())
        {
            publishFeedback_->setText(QStringLiteral("请输入动态内容"));
            publishFeedback_->show();
            spaceEditor_->setFocus();
            return;
        }

        spaceEditor_->clear();
        publishFeedback_->setText(QStringLiteral("动态已发布（当前为本地 UI 预览）"));
        publishFeedback_->show();
    });

    return page;
}

QWidget *LogicDialog::createSettingsPage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("settingsPage"));
    page->setStyleSheet(QString::fromUtf8(secondaryPageStyle));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(34, 28, 34, 28);
    layout->setSpacing(14);
    layout->addWidget(makeTitle(QStringLiteral("设置")));

    auto *subtitle = new QLabel(QStringLiteral("当前登录会话"));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    layout->addWidget(subtitle);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("settingsCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(7);

    auto *emailCaption = new QLabel(QStringLiteral("账号"));
    emailCaption->setObjectName(QStringLiteral("settingCaption"));
    settingsEmailLabel_ = new QLabel(QStringLiteral("尚未读取"));
    settingsEmailLabel_->setObjectName(QStringLiteral("settingValue"));
    auto *serverCaption = new QLabel(QStringLiteral("ChatServer"));
    serverCaption->setObjectName(QStringLiteral("settingCaption"));
    settingsServerLabel_ = new QLabel(QStringLiteral("尚未分配"));
    settingsServerLabel_->setObjectName(QStringLiteral("settingValue"));

    cardLayout->addWidget(emailCaption);
    cardLayout->addWidget(settingsEmailLabel_);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(serverCaption);
    cardLayout->addWidget(settingsServerLabel_);
    layout->addWidget(card);
    layout->addStretch();
    return page;
}

void LogicDialog::appendOutgoingMessage(const QString &message)
{
    auto *row = new QWidget(ui->messageScrollWidget);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);
    rowLayout->addStretch();

    auto *column = new QVBoxLayout;
    column->setSpacing(4);
    auto *bubble = new QLabel(message);
    bubble->setWordWrap(true);
    bubble->setMaximumWidth(440);
    bubble->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; background: #24262a; "
        "border-radius: 12px; padding: 11px 15px; font-size: 12px; }"));

    auto *time = new QLabel(
        QStringLiteral("%1  ·  发送中")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm"))));
    time->setAlignment(Qt::AlignRight);
    time->setStyleSheet(QStringLiteral("QLabel { color: #949aa5; font-size: 9px; }"));
    column->addWidget(bubble);
    column->addWidget(time);
    rowLayout->addLayout(column);

    const int insertIndex = qMax(0, ui->messageListLayout->count() - 1);
    ui->messageListLayout->insertWidget(insertIndex, row);

    QTimer::singleShot(0, this, [this]() {
        QScrollBar *scrollBar = ui->messageScroll->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    });
}

void LogicDialog::refreshSessionInfo()
{
    const QString email = property("loginEmail").toString().trimmed();
    const QString host = property("chatHost").toString().trimmed();
    const int port = property("chatPort").toInt();

    ui->profileBadge->setText(email.isEmpty() ? QStringLiteral("我") : email.left(1).toUpper());

    if (settingsEmailLabel_)
        settingsEmailLabel_->setText(email.isEmpty() ? QStringLiteral("尚未读取") : email);

    if (settingsServerLabel_)
    {
        settingsServerLabel_->setText(
            host.isEmpty() || port <= 0
                ? QStringLiteral("尚未分配")
                : QStringLiteral("%1:%2").arg(host).arg(port));
    }
}

void LogicDialog::sendCurrentMessage()
{
    const QString message = ui->messageEdit->toPlainText().trimmed();
    if (message.isEmpty())
    {
        ui->messageEdit->setFocus();
        return;
    }

    appendOutgoingMessage(message);
    ui->messageEdit->clear();
    ui->messageEdit->setFocus();
}

void LogicDialog::switchPage(int pageIndex)
{
    if (!contentStack_ || pageIndex < 0 || pageIndex >= contentStack_->count())
        return;

    contentStack_->setCurrentIndex(pageIndex);
    if (QAbstractButton *button = navGroup_->button(pageIndex))
        button->setChecked(true);

    if (pageIndex == 0)
        ui->messageEdit->setFocus();
    else if (pageIndex == 3 && spaceEditor_)
        spaceEditor_->setFocus();

    if (pageIndex == 4)
        refreshSessionInfo();
}
