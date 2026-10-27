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
    // тёмный системный тайтлбар (Win10 1607+ / Win11)
    const BOOL dark = TRUE;
    HWND hw = reinterpret_cast<HWND>(w.winId());
    DwmSetWindowAttribute(hw, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DwmSetWindowAttribute(hw, 19 /* старый id */,                       &dark, sizeof(dark));
#endif

    return app.exec();
}
