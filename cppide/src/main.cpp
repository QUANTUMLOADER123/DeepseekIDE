#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QCoreApplication>
#include "mainwindow.h"
#include "assets_blob.h"

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

    // шрифт: MuseoSansCyrl 500 (assets или вшитый blob), фолбэк Segoe UI
    QString fam = QStringLiteral("Segoe UI");
    QString fontPath = QStringLiteral("assets/museosanscyrl-500.otf");
    if (!QFile::exists(fontPath))
        fontPath = QCoreApplication::applicationDirPath() + QStringLiteral("/assets/museosanscyrl-500.otf");
    int fid = -1;
    if (QFile::exists(fontPath)) fid = QFontDatabase::addApplicationFont(fontPath);
    if (fid < 0)
        fid = QFontDatabase::addApplicationFontFromData(QByteArray::fromRawData(
            reinterpret_cast<const char*>(blob::museo_500_otf), int(sizeof(blob::museo_500_otf))));
    if (fid >= 0) {
        const auto fams = QFontDatabase::applicationFontFamilies(fid);
        if (!fams.isEmpty()) fam = fams.first();
    }
    QFont ui(fam);
    ui.setPointSize(9);
    ui.setStyleStrategy(QFont::PreferAntialias);   // максимальное сглаживание текста
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
