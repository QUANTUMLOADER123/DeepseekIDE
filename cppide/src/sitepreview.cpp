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
        "QLineEdit{background:#141827;border:1px solid #262b47;border-radius:10px;padding:8px 14px;color:#c6cbdf;}"));
    bar->addWidget(addr, 1);
    lay->addLayout(bar);

    auto* view = new QTextBrowser;
    view->setOpenLinks(false);
    view->setFrameShape(QFrame::NoFrame);
    view->setStyleSheet(QStringLiteral(
        "QTextBrowser{background:#ffffff;border:1px solid #262b47;border-radius:12px;color:#222;padding:10px;}"));
    view->setHtml(html);
    lay->addWidget(view, 1);

    auto* close = new QPushButton(QStringLiteral("Закрыть"));
    close->setStyleSheet(QStringLiteral(
        "QPushButton{background:#232838;border:1px solid #2d3350;border-radius:10px;padding:8px 22px;color:#dfe3f5;}"
        "QPushButton:hover{background:#2a3048;}"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* row = new QHBoxLayout; row->addStretch(1); row->addWidget(close);
    lay->addLayout(row);
}
