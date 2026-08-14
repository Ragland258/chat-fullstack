#include "logicdialog.h"
#include "ui_logicdialog.h"

#include <QColor>
#include <QGraphicsDropShadowEffect>
#include <QPlainTextEdit>
#include <QPushButton>

LogicDialog::LogicDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::LogicDialog)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("Chat · 会话"));

    auto* shadow = new QGraphicsDropShadowEffect(ui->rootFrame);
    shadow->setBlurRadius(36.0);
    shadow->setOffset(0.0, 10.0);
    shadow->setColor(QColor(35, 42, 78, 30));
    ui->rootFrame->setGraphicsEffect(shadow);

    ui->messageEdit->setFocus();

    // Temporary frontend interaction: clear the editor after pressing Send.
    // Replace this connection when the real chat-message API is ready.
    connect(ui->sendButton, &QPushButton::clicked, this, [this]() {
        if (ui->messageEdit->toPlainText().trimmed().isEmpty()) {
            ui->messageEdit->setFocus();
            return;
        }

        ui->messageEdit->clear();
        ui->messageEdit->setFocus();
    });
}

LogicDialog::~LogicDialog()
{
    delete ui;
}
