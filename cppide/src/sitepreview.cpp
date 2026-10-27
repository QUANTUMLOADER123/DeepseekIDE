#include "sitepreview.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

SitePreviewDialog::SitePreviewDialog(const QString& url, const QString& html, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Предпросмотр сайта"));
    setMinimumSize(880, 640);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 14);
    lay->setSpacing(10);

    auto* bar = new QHBoxLayout;
    auto* addr = new QLineEdit(url);
    addr->setReadOnly(true);
    addr->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#161719;border:1px solid #2c2d33;border-radius:10px;padding:8px 14px;color:#c9cad0;}"));
    bar->addWidget(addr, 1);
    lay->addLayout(bar);

    auto* view = new QTextBrowser;
    view->setOpenLinks(false);
    view->setFrameShape(QFrame::NoFrame);
    view->setStyleSheet(QStringLiteral(
        "QTextBrowser{background:#ffffff;border:1px solid #2c2d33;border-radius:12px;color:#222;padding:10px;}"));
    view->setHtml(html);
    lay->addWidget(view, 1);

    auto* close = new QPushButton(QStringLiteral("Закрыть"));
    close->setStyleSheet(QStringLiteral(
        "QPushButton{background:#1b1c20;border:1px solid #2a2b30;border-radius:10px;padding:8px 22px;color:#e2e3e8;}"
        "QPushButton:hover{background:#231d20;}"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* row = new QHBoxLayout; row->addStretch(1); row->addWidget(close);
    lay->addLayout(row);
}
