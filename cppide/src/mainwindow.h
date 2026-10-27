#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QLabel>
#include <QTimer>
#include <QList>
#include "settings.h"
#include "sessionstore.h"

class QListWidget;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
class QPlainTextEdit;
class QScrollBar;
class SquircleCard;
class Agent;
class DeepSeekClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    void closeEvent(class QCloseEvent* e) override;

private slots:
    void onSend();
    void onNewSession();
    void onPickSession(int row);
    void onPickFolder();
    void onSettings();
    void onOpenSandbox();

    // агент → UI
    void addUser(const QString& text);
    void beginAssistant();
    void appendAssistantDelta(const QString& delta);
    void endAssistant(const QString& cleanText);
    void addToolRow(const QString& name, const QString& sub, bool ok, const QString& detail);
    void addImage(const QByteArray& bytes, const QString& mime, const QString& alt);
    void addError(const QString& err);
    void showSitePreview(const QString& url, const QString& html);
    void logLine(const QString& line);
    void onBusy(bool on);
    void pulseTick();

private:
    SquircleCard* chatAppend(const QString& role, const QString& state = QString(),
                             bool animate = true);
    void fadeIn(QWidget* w, int ms = 240);
    void smoothScrollBottom();
    void clearChatUi();
    void showEmptyState();
    void pushHist(const QString& role, const QString& text, const QString& name = QString(),
                  const QString& sub = QString(), bool ok = true, const QString& detail = QString());
    void replayHistory();
    void refreshSessionList();
    void persistNow();
    void setSessionTitleFromText(const QString& t);
    static QString mdToHtml(const QString& md);
    void activateSession(int i);

    AppCfg cfg_;
    Agent* agent_;
    DeepSeekClient* ds_;

    // сессии
    QList<Session> sessions_;
    int curSess_ = -1;

    // UI
    QListWidget* sessionList_ = nullptr;
    QLabel* logoLbl_ = nullptr;
    QLabel* sessionTitle_ = nullptr;
    QLabel* statusPill_ = nullptr;
    QPushButton* folderPill_ = nullptr;
    QPushButton* chipThink_ = nullptr;
    QPushButton* chipSearch_ = nullptr;
    QPushButton* chipImg_ = nullptr;
    QScrollArea* chatScroll_ = nullptr;
    QVBoxLayout* chatLay_ = nullptr;
    QPointer<SquircleCard> liveCard_;
    QPointer<QLabel> liveBubble_;
    QString  liveRaw_;
    QTimer   pulseTimer_;
    int      pulsePhase_ = 0;
    QPlainTextEdit* input_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    bool     busyOn_ = false;
    bool     replaying_ = false;
};
