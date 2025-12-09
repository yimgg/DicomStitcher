#include "widget.h"
#if defined(_MSC_VER) && (_MSC_VER >= 1600)
# pragma execution_character_set("utf-8")
#endif
#include <QApplication>
#include <QFont>
#include <QLocale>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // 设置控制台代码页为 UTF-8（Windows 10 1803+）
    // 这对于 GUI 程序可能不是必需的，但有助于调试输出
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);
    
    // 设置应用程序区域设置
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    
    // 设置中文字体
    a.setFont(QFont("Microsoft Yahei", 6));
    
    Widget w;
    w.show();
    return a.exec();
}
