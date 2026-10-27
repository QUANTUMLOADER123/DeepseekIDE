#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <functional>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include "pow.h"
#include "settings.h"

// Прямой клиент DeepSeek Web API (без браузера):
//   POST /api/v0/chat_session/create        — новая беседа (лениво)
//   POST /api/v0/chat/create_pow_challenge  — PoW-челлендж перед completion
//   POST /api/v0/chat/completion            — SSE-стрим ответа
//   data: {"p":"response/fragments","v":[{type:RESPONSE|THINK,content}]}
//   data: {"p":"response/fragments/-1/content","v":"…кусок…"}   — дельта текста
//   data: {"p":"response/content","v":"…"} / finish_reason / status=FINISHED
class DeepSeekClient : public QObject {
    Q_OBJECT
public:
    explicit DeepSeekClient(QObject* parent = nullptr);

    void setCfg(const AppCfg& c);   // копия настроек + сброс wasm при смене URL
    void newChat();                 // сбросить chat_session (создастся при след. ask)
    void setSession(const QString& sid, const QString& parentId) { sessionId_ = sid; parentMessageId_ = parentId; }
    QString sessionId() const { return sessionId_; }
    QString parentMessageId() const { return parentMessageId_; }
    bool busy() const { return reply_ != nullptr; }
    void abort();

public slots:
    void ask(const QString& prompt);   // PoW + стрим; ошибки → failed()

signals:
    void chatDelta(const QString& text);    // видимый текст (фрагменты RESPONSE)
    void thinkDelta(const QString& text);   // размышления (THINK) — можно прятать
    void chatFinished(const QString& full, const QString& thinking);
    void failed(const QString& error);
    void log(const QString& line);

private slots:
    void onReadyRead();

private:
    void ensureSessionThen(std::function<void()> next);
    void createPowThen(const QString& prompt);
    void startCompletion(const QString& prompt, const QString& powB64);
    QString compactErr(int httpStatus, const QByteArray& body) const;

    QNetworkAccessManager nam_;
    AppCfg cfg_;
    PowSolver pow_;
    QString sessionId_;
    QString parentMessageId_;     // id последнего ответа ассистента (для контекста)
    QPointer<QNetworkReply> reply_;

    // состояние стрима — семантика 1:1 с эталонным клиентом (fragments + cumulative-diff):
    //   data: {"p":"response/fragments","v":[{type,content}]}           — новые фрагменты
    //   data: {"p":"response/fragments/-1/content","v":"chunk"}        — дельта последнего
    //   data: {"v":"chunk"}                                            — bare-line: тот же lastPath
    //   data: {"p":"response","v":{"response":{fragments:[…],content}} — полный снапшот
    struct Frag { QString type, content; };
    QByteArray  sseBuf_;
    QList<Frag> frags_;
    QString     legacy_, lastPath_, streamErr_, respMsgId_;
    int         emittedChat_ = 0, emittedThink_ = 0;

    QTimer* watchdog_ = nullptr;   // зависание SSE: 45 с без чанков => abort
    void    processSsePayload(const QByteArray& pay);
    QString answerText() const;
    QString thinkText() const;
};
