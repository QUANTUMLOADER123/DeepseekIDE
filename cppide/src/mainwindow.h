#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QLabel>
#include <QTimer>
#include <QList>
#include <QPoint>
#include "settings.h"
#include "sessionstore.h"

class QListWidget;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
class QPlainTextEdit;
class QScrollBar;
class QFrame;
class SquircleCard;
class Agent;
class DeepSeekClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    void closeEvent(class QCloseEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override; // ресайз без рамки
    bool eventFilter(QObject* obj, QEvent* ev) override;                                    // drag титлбара, фокус композера

private slots:
    void onSend();
    void onNewSession();
    void onPickSession(int row);
    void onPickFolder();
    void onSettings();
    void onOpenSandbox();
    void onAttach();                       // прикрепить файлы (пути уйдут агенту)
    void onCopyCard(const QString& plain); // копия ответа целиком
    void onRegenerate(int histIdx);        // откат к точке этой карточки + resend
    void compactNow();                     // /compact или лимит контекста
    void onCompactDone(const QString& summary);

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
    void fitBubble(SquircleCard* card, const QString& text, bool userSide);
    void fadeIn(QWidget* w, int ms = 240);
    void smoothScrollBottom();
    void clearChatUi();
    void showEmptyState();
    int  pushHist(const QString& role, const QString& text, const QString& name = QString(),
                  const QString& sub = QString(), bool ok = true, const QString& detail = QString());
    void replayHistory();
    void refreshSessionList();
    int  sessionRowOf(int idx) const;
    void persistNow();
    void setSessionTitleFromText(const QString& t);
    void autoTitleFromAnswer(const QString& t);
    void maybeCompact();
    static QString mdToHtml(const QString& md);
    void renderRich(QVBoxLayout* l, const QString& text);   // текст + код-блоки с кнопкой Copy
    void addAssistantActions(QVBoxLayout* l, int histIdx, const QString& plain);
    void addUserActions(QVBoxLayout* l, int histIdx, const QString& plain);
    void buildUserCardBody(QVBoxLayout* l, int histIdx, const QString& text); // и edit-cancel
    void onEditStart(int histIdx, class QPushButton* btn);
    void onEditResend(int histIdx, const QString& newText);
    void refreshAttachUi();
    void activateSession(int i);
    void toggleMax();

    AppCfg cfg_;
    Agent* agent_;
    DeepSeekClient* ds_;

    // сессии
    QList<Session> sessions_;
    int curSess_ = -1;

    // кастомный титлбар (frameless)
    QWidget*     titleBar_  = nullptr;
    QLabel*      logoLbl_   = nullptr;
    QPushButton* winMaxBtn_ = nullptr;
    QPoint       dragOffset_;
    bool         dragOn_ = false;

    // UI
    QListWidget* sessionList_ = nullptr;
    QPushButton* folderPill_  = nullptr;
    QPushButton* chipThink_   = nullptr;
    QPushButton* chipSearch_  = nullptr;
    QPushButton* chipImg_     = nullptr;
    QFrame*      composerBox_ = nullptr;
    QScrollArea* chatScroll_  = nullptr;
    QVBoxLayout* chatLay_     = nullptr;
    QPointer<SquircleCard> liveCard_;
    QPointer<QLabel> liveBubble_;
    QString  liveRaw_;
    QStringList attachPaths_;   // вложения, скопированные в песочницу (уйдут в текст сообщения)
    QWidget*   attachRow_  = nullptr;
    QLabel*    attachLbl_  = nullptr;
    bool       compacting_ = false;
    int        typingTick_ = 0;
    QWidget*   spinner_   = nullptr;   // крутилка «пишет» (сырой ptr, живёт в liveCard_)
    QWidget*   hoverPill_ = nullptr;   // ползущая подсветка наведения в списке сессий
    class QPropertyAnimation* pillAnim_ = nullptr;
    bool       titleQueued_ = false;   // ждём свободного ds для generateTitle
    QString    titleSeed_;
    QString    lastUserText_;          // последнее сообщение (для сида тайтла)
    QTimer   pulseTimer_;
    int      pulsePhase_ = 0;
    QPlainTextEdit* input_    = nullptr;
    QPushButton*    sendBtn_  = nullptr;
    QPlainTextEdit* logView_  = nullptr;
    bool     busyOn_ = false;
    bool     replaying_ = false;
};
