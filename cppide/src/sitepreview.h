#pragma once
#include <QDialog>
class QTextBrowser;
class QLineEdit;

// Мини-браузер: SITE VIEW — показывает HTML страницы (QTextBrowser, css уровня 1-2)
class SitePreviewDialog : public QDialog {
    Q_OBJECT
public:
    SitePreviewDialog(const QString& url, const QString& html, QWidget* parent = nullptr);
};
