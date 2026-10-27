#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include "settings.h"

class DeepSeekClient;

// Агентный цикл: наша система маркеров [<{…}>] + блоки правок (deepseekide-ops
// и голые JSON-массивы), исполнение fs/shell/web/image, авто-continue.
class Agent : public QObject {
    Q_OBJECT
public:
    explicit Agent(DeepSeekClient* ds, QObject* parent = nullptr);
    void setCfg(const AppCfg& c);
    void setProjectRoot(const QString& dir);
    QString projectRoot() const { return root_; }
    void newChat();
    void restoreSession(bool contractSent); // переключение на существующую сессию
    bool busy() const { return busy_; }

public slots:
    void sendUserMessage(const QString& text);
    void compactSession();   // просит модель сжать диалог (Сделали | Следующее действие)

signals:
    void log(const QString& line);
    void busyChanged(bool on);
    void appendUserBubble(const QString& text);
    void assistantBubbleStart();
    void assistantDelta(const QString& text);      // уже очищенный видимый текст
    void assistantBubbleDone(const QString& text); // итог (полностью очищен)
    void toolRow(const QString& name, const QString& sub, bool ok, const QString& detail);
    void imageShown(const QByteArray& bytes, const QString& mime, const QString& alt);
    void sitePreview(const QString& url, const QString& html); // мини-браузер
    void compactDone(const QString& summary);                    // сводка готова (пусто = фейл)
    void errorBanner(const QString& err);

private slots:
    void onChatDelta(const QString& chunk);
    void onChatFinished(const QString& full, const QString& thinking);
    void onFailed(const QString& err);

private:
    // --- парсинг
    struct Request { QString kind, arg, extra; QStringList opts; };
    QList<Request> parseMarkers(const QString& answer);
    struct Op { QString name; QJsonObject args; };
    QList<Op> parseOps(const QString& answer, QStringList& errors) const;
    static QString repairJson(const QString& in);
    static bool    jsonArrayToOps(const QString& body, QList<Op>& out, QStringList& errors);
    static int     balancedEnd(const QString& s, int i0);
    static QString sanitizeRel(QString p);

    // --- исполнение
    QString serviceRequest(const Request& rq);
    QString applyOp(const Op& op, bool& ok);
    QString runShell(const QString& cmd, const QString& cwd = QString());
    QString sandboxDir() const;
    QString webFetch(const QString& url);
    QPair<QByteArray, QString> genImage(const QString& prompt, const QString& model); // bytes+mime или пусто
    QString askDialog(const QString& q, const QStringList& opts, bool multi);
    void processAnswer(const QString& full);
    QMetaObject::Connection compOk_, compFail_;
    bool                    compActive_ = false;
    QString cleanVisibleText(const QString& full) const; // маркеры/ops из показа

    // --- fs helpers
    QString absPath(const QString& rel, bool* okc = nullptr) const;
    QString readTextFile(const QString& rel, bool* okc = nullptr) const;
    bool    writeTextFile(const QString& rel, const QString& text, QString* err = nullptr) const;

    DeepSeekClient* ds_;
    AppCfg cfg_;
    QString root_;

    bool    busy_ = false;
    bool    firstPrompt_ = true;
    int     autoCount_ = 0;
    QString carry_;                  // видимый хвост-демпинг маркеров "[<{…"
    QString answerRaw_, answerVis_;  // текущий ответ: сырой и видимый

    // дедуп в рамках запуска приложения
    QSet<QString> sentFiles_, sentSearch_, sentWeb_, sentShells_, sentImgs_, appliedOps_;
};
