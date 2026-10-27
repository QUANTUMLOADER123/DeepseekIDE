#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QSurfaceFormat>
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setApplicationName(QStringLiteral("DeepSeekCppIDE"));
    app.setOrganizationName(QStringLiteral("DeepSeekCppIDE"));

    MainWindow w;
    w.show();
    return app.exec();
}
