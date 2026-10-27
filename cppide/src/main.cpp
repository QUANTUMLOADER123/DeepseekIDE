#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include "mainwindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setApplicationName(QStringLiteral("DeepSeekCppIDE"));
    app.setOrganizationName(QStringLiteral("DeepSeekCppIDE"));

    QFont ui(QStringLiteral("Segoe UI"));
    ui.setPointSize(9);
    app.setFont(ui);

    MainWindow w;
    w.show();

#ifdef Q_OS_WIN
    // Win11: скруглённые углы у frameless-окна (DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND)
    const DWORD roundPref = 2;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(w.winId()), 33, &roundPref, sizeof(roundPref));
#endif

    return app.exec();
}
