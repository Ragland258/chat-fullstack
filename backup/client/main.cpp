#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    // 创建 Qt 应用对象，负责事件循环和全局 UI 资源。
    QApplication a(argc, argv);

    // 显示主登录窗口。
    MainWindow w;
    w.show();



    // 进入 Qt 事件循环，等待用户操作和异步事件。
    return a.exec();
}
