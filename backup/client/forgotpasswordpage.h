#ifndef FORGOTPASSWORDPAGE_H
#define FORGOTPASSWORDPAGE_H

#include "const.h"

#include <QTimer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class ForgotPasswordPage : public QWidget
{
    Q_OBJECT

public:
    explicit ForgotPasswordPage(QWidget* parent = nullptr);

signals:
    void backRequested();
    void resetSucceeded(const QString& account);

private slots:
    void requestVerifyCode();
    void submitReset();
    void updateCountdown();
    void handleHttpResult(ReqID id, QString response, ErrorCodes error);

private:
    void showTip(const QString& message, bool success);
    void resetCodeButton();

    QLineEdit* accountEdit_;
    QLineEdit* verifyCodeEdit_;
    QLineEdit* passwordEdit_;
    QLineEdit* confirmPasswordEdit_;
    QPushButton* getCodeButton_;
    QPushButton* submitButton_;
    QCheckBox* agreementBox_;
    QLabel* tipLabel_;
    QTimer countdownTimer_;
    int countdownSeconds_;
};

#endif // FORGOTPASSWORDPAGE_H
