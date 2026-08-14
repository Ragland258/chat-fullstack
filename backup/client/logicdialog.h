#ifndef LOGICDIALOG_H
#define LOGICDIALOG_H

#include <QDialog>

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

private:
    Ui::LogicDialog *ui;
};

#endif // LOGICDIALOG_H
