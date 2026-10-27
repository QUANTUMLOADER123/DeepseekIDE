#pragma once
#include <QMainWindow>
#include <QFileSystemModel>
#include <QPointer>
#include <QLabel>
#include <QTimer>
#include "settings.h"

class QTreeView;
class QTabWidget;
class QPlainTextEdit;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
class QScrollBar;
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
    QFrame*  chatAddCard(const QString& role, const QString& state = QString()); // внутр. карточка
    QWidget* chatAppend(const QString& role, const QString& state = QString());  // обёртка с выравниванием
    void     fadeIn(QWidget* w, int ms = 220);
    void     smoothScrollBottom();
    void     pulseTick();   // анимация «печатает…»
    static QString mdToHtml(const QString& md);
    CodeEditor* openFile(const QString& absPath);
    void rebuildTreeRoot();

    AppCfg cfg_;
    Agent* agent_;
    DeepSeekClient* ds_;

    QFileSystemModel* fsModel_ = nullptr;
    QTreeView* tree_ = nullptr;
    QTabWidget* tabs_ = nullptr;

    QScrollArea* chatScroll_ = nullptr;
    QVBoxLayout* chatLay_ = nullptr;
    QPointer<QFrame> liveCard_;
    QPointer<QLabel> liveBubble_;
    QString  liveRaw_;
    QTimer   pulseTimer_;
    int      pulsePhase_ = 0;
    QPlainTextEdit* input_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    bool     busyOn_ = false;
};
