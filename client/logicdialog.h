#ifndef LOGICDIALOG_H
#define LOGICDIALOG_H

#include <QDialog>

class QButtonGroup;
class QLabel;
class QPlainTextEdit;
class QShowEvent;
class QStackedWidget;
class QWidget;

namespace Ui {
class LogicDialog;
}

class LogicDialog : public QDialog
{
    Q_OBJECT

public:
    // 初始化逻辑对话框 UI。
    explicit LogicDialog(QWidget *parent = nullptr);

    // 释放逻辑对话框 UI 对象。
    ~LogicDialog();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QWidget *createFriendsPage();
    QWidget *createSpacePage();
    QWidget *createPublishPage();
    QWidget *createSettingsPage();
    void appendOutgoingMessage(const QString &message);
    void refreshSessionInfo();
    void sendCurrentMessage();
    void switchPage(int pageIndex);

private:
    Ui::LogicDialog *ui;
    QButtonGroup *navGroup_;
    QStackedWidget *contentStack_;
    QPlainTextEdit *spaceEditor_;
    QLabel *publishFeedback_;
    QLabel *settingsEmailLabel_;
    QLabel *settingsServerLabel_;
};

#endif // LOGICDIALOG_H
