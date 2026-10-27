#pragma once
#include <QMainWindow>
#include <QFileSystemModel>
#include <QPointer>
#include <QLabel>
#include "settings.h"

class QTreeView;
class QTabWidget;
class QPlainTextEdit;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
class CodeEditor;
class Agent;
class DeepSeekClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void onSend();
    void onNewChat();
    void onSettings();
    void onPickFolder();
    void onTreeOpen(const QModelIndex& idx);
    void saveCurrent();
    void onTabClose(int i);
    void onTabTitleChanged();

    // агент → UI
    void addUser(const QString& text);
    void beginAssistant();
    void appendAssistantDelta(const QString& delta);
    void endAssistant(const QString& cleanText);
    void addToolRow(const QString& name, const QString& sub, bool ok, const QString& detail);
    void addImage(const QByteArray& bytes, const QString& mime, const QString& alt);
    void addError(const QString& err);
    void logLine(const QString& line);
    void onBusy(bool on);

private:
    QWidget* chatAddCard(const QString& bg, const QString& fg = QString());
    static QString mdToHtml(const QString& md);
    CodeEditor* openFile(const QString& absPath);
    void rebuildTreeRoot();
    QWidget* bottomBar();

    AppCfg cfg_;
    Agent* agent_;
    DeepSeekClient* ds_;

    QFileSystemModel* fsModel_ = nullptr;
    QTreeView* tree_ = nullptr;
    QTabWidget* tabs_ = nullptr;

    QScrollArea* chatScroll_ = nullptr;
    QVBoxLayout* chatLay_ = nullptr;
    QLabel* liveBubble_ = nullptr;
    QString liveRaw_;
    QPlainTextEdit* input_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
};
