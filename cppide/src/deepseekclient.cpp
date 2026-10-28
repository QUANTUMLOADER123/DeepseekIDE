#include "deepseekclient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <functional>
#include <thread>

static const char* DS_BASE = "https://chat.deepseek.com";
static const char* DS_UA   = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                             "(KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";

DeepSeekClient::DeepSeekClient(QObject* parent) : QObject(parent) {
    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    watchdog_->setInterval(45000); // SSE-стрим завис: 45 с без ни одного чанка
    connect(watchdog_, &QTimer::timeout, this, [this]() {
        if (reply_) {
            emit log(QStringLiteral("DS: watchdog — нет данных 45с, сбрасываю соединение"));
            reply_->abort();
        }
    });
}

void DeepSeekClient::setCfg(const AppCfg& c) {
    if (c.token != cfg_.token) newChat();           // смена аккаунта — новая беседа
    if (c.wasmUrl != cfg_.wasmUrl) pow_.setWasm({}); // перезагрузить wasm
    cfg_ = c;
}

void DeepSeekClient::newChat() {
    sessionId_.clear();
    parentMessageId_.clear();
}

void DeepSeekClient::abort() {
    if (watchdog_) watchdog_->stop();
    if (reply_) { reply_->abort(); }
}

static void fillHeaders(QNetworkRequest& req, const AppCfg& c) {
    req.setRawHeader("User-Agent", DS_UA);
    req.setRawHeader("x-client-platform", "web");
    req.setRawHeader("x-client-version", "20241122.0");
    req.setRawHeader("x-client-locale", "ru");
    req.setRawHeader("x-client-timezone-offset", "14400");
    req.setRawHeader("x-app-version", "20241122.0");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + c.token.toUtf8());
    req.setRawHeader("x-hif-dliq", c.hifDliq.toUtf8());
    req.setRawHeader("x-hif-leim", c.hifLeim.toUtf8());
    req.setRawHeader("Origin", "https://chat.deepseek.com");
    req.setRawHeader("Referer", "https://chat.deepseek.com/");
    if (!c.cookie.isEmpty()) req.setRawHeader("Cookie", c.cookie.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
}

QString DeepSeekClient::compactErr(int httpStatus, const QByteArray& body) const {
    QString s = QString::fromUtf8(body).simplified();
    if (s.size() > 240) s = s.left(240) + QLatin1String("…");
    return QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(s);
}

void DeepSeekClient::ask(const QString& prompt) {
    if (busy()) { emit failed(QStringLiteral("запрос уже идёт")); return; }
    if (cfg_.token.isEmpty()) { emit failed(QStringLiteral("нет токена — Settings → token")); return; }
    ensureSessionThen([this, prompt]() { createPowThen(prompt); });
}

void DeepSeekClient::ensureSessionThen(std::function<void()> next) {
    if (!sessionId_.isEmpty()) { next(); return; }
    emit log(QStringLiteral("DS: создаю chat session…"));
    QNetworkRequest req(QUrl(QLatin1String(DS_BASE) + QStringLiteral("/api/v0/chat_session/create")));
    fillHeaders(req, cfg_);
    auto* r = nam_.post(req, "{}");
    connect(r, &QNetworkReply::finished, this, [this, r, next]() {
        r->deleteLater();
        const QByteArray body = r->readAll();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("session create: %1").arg(compactErr(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), body)));
            return;
        }
        auto j = QJsonDocument::fromJson(body);
        QString id = j.object().value(QStringLiteral("data")).toObject()
                     .value(QStringLiteral("biz_data")).toObject()
                     .value(QStringLiteral("chat_session")).toObject()
                     .value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            id = j.object().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("biz_data")).toObject()
                 .value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            emit failed(QStringLiteral("session create: нет chat_session.id (auth?: ") + QString::fromUtf8(body.left(160)) + QStringLiteral(")"));
            return;
        }
        sessionId_ = id;
        parentMessageId_.clear();
        emit log(QStringLiteral("DS: session=%1").arg(id));
        next();
    });
}

void DeepSeekClient::createPowThen(const QString& prompt) {
    if (!pow_.hasWasm()) {
        emit log(QStringLiteral("DS: качаю PoW wasm…"));
        QNetworkRequest req(QUrl(cfg_.wasmUrl)); req.setRawHeader("User-Agent", DS_UA);
        auto* r = nam_.get(req);
        connect(r, &QNetworkReply::finished, this, [this, r, prompt]() {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) {
                emit failed(QStringLiteral("PoW wasm: %1").arg(compactErr(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), r->readAll())));
                return;
            }
            const QByteArray bytes = r->readAll();
            pow_.setWasm(bytes);
            emit log(QStringLiteral("DS: PoW wasm скачан: %1 байт").arg(bytes.size()));
            createPowThen(prompt);
        });
        return;
    }
    QNetworkRequest req(QUrl(QLatin1String(DS_BASE) + QStringLiteral("/api/v0/chat/create_pow_challenge")));
    fillHeaders(req, cfg_);
    QJsonObject tgt; tgt[QStringLiteral("target_path")] = QStringLiteral("/api/v0/chat/completion");
    auto* r = nam_.post(req, QJsonDocument(tgt).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, prompt]() {
        r->deleteLater();
        const QByteArray body = r->readAll();
        if (r->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("pow challenge: %1").arg(compactErr(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), body)));
            return;
        }
        const QJsonObject chal = QJsonDocument::fromJson(body).object()
            .value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("biz_data")).toObject()
            .value(QStringLiteral("challenge")).toObject();
        if (chal.isEmpty()) {
            emit failed(QStringLiteral("pow challenge: пустой challenge (протухла авторизация/капча?): ") + QString::fromUtf8(body.left(160)));
            return;
        }
        // PoW решаем в отдельном потоке — SHA-256 цикл не фризит интерфейс
        std::thread([this, chal, prompt]() {
            quint64 answer = 0; QString perr;
            if (!pow_.solve(chal, answer, perr)) {
                const QString err = perr;
                QMetaObject::invokeMethod(this, [this, err]() { emit failed(err); }, Qt::QueuedConnection);
                return;
            }
            QJsonObject out{
                {QStringLiteral("algorithm"),  chal.value(QStringLiteral("algorithm"))},
                {QStringLiteral("challenge"),  chal.value(QStringLiteral("challenge"))},
                {QStringLiteral("salt"),       chal.value(QStringLiteral("salt"))},
                {QStringLiteral("answer"),     QString::number(answer)},
                {QStringLiteral("signature"),  chal.value(QStringLiteral("signature"))},
                {QStringLiteral("target_path"),QStringLiteral("/api/v0/chat/completion")},
            };
            // в оригинале answer — число
            out.remove(QStringLiteral("answer"));
            out[QStringLiteral("answer")] = QJsonValue::fromVariant(answer);
            const QString b64 = QString::fromLatin1(QJsonDocument(out).toJson(QJsonDocument::Compact).toBase64());
            QMetaObject::invokeMethod(this, [this, prompt, b64]() {
                startCompletion(prompt, b64);
            }, Qt::QueuedConnection);
        }).detach();
    });
}

void DeepSeekClient::startCompletion(const QString& prompt, const QString& powB64) {
    emit log(QStringLiteral("DS: → completion (%1 зн)").arg(prompt.size()));
    QNetworkRequest req(QUrl(QLatin1String(DS_BASE) + QStringLiteral("/api/v0/chat/completion")));
    fillHeaders(req, cfg_);
    req.setRawHeader("X-DS-PoW-Response", powB64.toUtf8());

    QJsonObject payload{
        {QStringLiteral("chat_session_id"), sessionId_},
        {QStringLiteral("parent_message_id"), parentMessageId_.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                         : QJsonValue::fromVariant(parentMessageId_.toLongLong())}, // сервер ждёт u32, не строку
        {QStringLiteral("prompt"), prompt},
        {QStringLiteral("ref_file_ids"), QJsonArray()},
        {QStringLiteral("thinking_enabled"), cfg_.thinking},
        {QStringLiteral("search_enabled"), cfg_.search},
        {QStringLiteral("action"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("preempt"), false},
    };
    auto* r = nam_.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    reply_ = r;
    sseBuf_.clear(); frags_.clear(); legacy_.clear(); lastPath_.clear(); streamErr_.clear(); respMsgId_.clear(); emittedChat_ = emittedThink_ = 0;
    connect(r, &QNetworkReply::readyRead, this, &DeepSeekClient::onReadyRead);
    connect(r, &QNetworkReply::finished, this, [this, r, prompt, powB64]() {
        reply_ = nullptr;
        const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netErr = r->error();
        if (watchdog_) watchdog_->stop();
        const QByteArray errBody = r->readAll();
        r->deleteLater();

        // добить хвост SSE-буфера (строки без завершающего \n)
        const QByteArray all = sseBuf_ + errBody;
        for (QByteArray line : all.split('\n')) {
            if (line.endsWith('\r')) line.chop(1);
            if (!line.startsWith("data:")) continue;
            const QByteArray pay = line.mid(5).trimmed();
            if (!pay.isEmpty() && pay != "[DONE]") processSsePayload(pay);
        }
        sseBuf_.clear();

        if (netErr != QNetworkReply::NoError) {
            if (httpCode == 400 && !parentMessageId_.isEmpty()) {
                // частая причина 400 — протухший parent_message_id: один ретрай без него
                emit log(QStringLiteral("DS: 400 — повторяю без parent_message_id…"));
                parentMessageId_.clear();
                startCompletion(prompt, powB64);
                return;
            }
            emit failed(QStringLiteral("completion stream: %1").arg(compactErr(httpCode, errBody)));
            return;
        }
        if (!streamErr_.isEmpty()) {
            emit failed(QStringLiteral("model: %1").arg(streamErr_));
            return;
        }
        if (!respMsgId_.isEmpty()) parentMessageId_ = respMsgId_;
        const QString ans = answerText(), th = thinkText();
        emit log(QStringLiteral("DS: ответ %1 зн, мысли %2 зн, parent=%3")
                 .arg(ans.size()).arg(th.size()).arg(parentMessageId_));
        emit chatFinished(ans, th);
    });
}

void DeepSeekClient::onReadyRead() {
    QNetworkReply* r = reply_.data();
    if (!r) return;
    if (watchdog_) watchdog_->start(); // данные пришли — перезарядка watchdog
    sseBuf_ += r->readAll();
    int cut;
    while ((cut = sseBuf_.indexOf('\n')) >= 0) {
        QByteArray line = sseBuf_.left(cut);
        if (line.endsWith('\r')) line.chop(1);
        sseBuf_.remove(0, cut + 1);
        if (!line.startsWith("data:")) continue;
        const QByteArray pay = line.mid(5).trimmed();
        if (pay.isEmpty() || pay == "[DONE]") continue;
        processSsePayload(pay);
    }
}

// 1:1 порт эталонного парсера (FreeDeepseekAPI client.js)
void DeepSeekClient::processSsePayload(const QByteArray& pay) {
    const auto j = QJsonDocument::fromJson(pay);
    if (!j.isObject()) return;
    const QJsonObject d = j.object();

    if (d.value(QStringLiteral("type")).toString() == QLatin1String("error"))
        streamErr_ = d.value(QStringLiteral("content")).toString(QStringLiteral("DeepSeek returned an error"));
    if (d.contains(QStringLiteral("response_message_id")))
        respMsgId_ = QString::number(d.value(QStringLiteral("response_message_id")).toVariant().toLongLong());
    if (d.contains(QStringLiteral("p")))
        lastPath_ = d.value(QStringLiteral("p")).toString();

    const QJsonValue v  = d.value(QStringLiteral("v"));
    const QJsonObject vo = v.toObject();

    auto appendOne = [this](const QJsonValue& fv) {
        const QJsonObject f = fv.toObject();
        if (f.isEmpty()) return;
        frags_.append({ f.value(QStringLiteral("type")).toString(),
                        f.value(QStringLiteral("content")).toString() });
    };
    auto appendFrags = [&](const QJsonValue& arr) {
        if (arr.isArray()) { const auto a = arr.toArray(); for (const QJsonValue& fv : a) appendOne(fv); }
        else appendOne(arr);
    };

    if (!vo.isEmpty() && vo.contains(QStringLiteral("response"))) {
        // полный снапшот ответа — ЗАМЕНА состояния
        const QJsonObject resp = vo.value(QStringLiteral("response")).toObject();
        if (resp.contains(QStringLiteral("message_id")))
            respMsgId_ = QString::number(resp.value(QStringLiteral("message_id")).toVariant().toLongLong());
        if (resp.value(QStringLiteral("fragments")).isArray()) {
            frags_.clear();
            appendFrags(resp.value(QStringLiteral("fragments")));
        }
        if (resp.value(QStringLiteral("content")).isString())
            legacy_ = resp.value(QStringLiteral("content")).toString();
    } else if (lastPath_ == QLatin1String("response/fragments") && !v.isUndefined()) {
        appendFrags(v);
    } else if (lastPath_ == QLatin1String("response") && v.isArray()) {
        const auto ops = v.toArray();
        for (const QJsonValue& opv : ops) {
            const QJsonObject op = opv.toObject();
            if (op.value(QStringLiteral("p")).toString() == QLatin1String("fragments")
                && op.value(QStringLiteral("o")).toString() == QLatin1String("APPEND"))
                appendFrags(op.value(QStringLiteral("v")));
        }
    } else if (lastPath_ == QLatin1String("response/fragments/-1/content") && v.isString() && !frags_.isEmpty()) {
        frags_.last().content += v.toString();
    } else if (lastPath_ == QLatin1String("response/content") && v.isString()) {
        legacy_ += v.toString();
    } else if (lastPath_ == QLatin1String("response/finish_reason") && v.isString()) {
        emit log(QStringLiteral("DS: finish_reason=%1").arg(v.toString()));
    }

    // эмиссия — накопительная дельта (monotonic, как onText/emitted в client.js)
    const QString ans = answerText();
    if (ans.size() > emittedChat_) {
        emit chatDelta(ans.mid(emittedChat_));
        emittedChat_ = ans.size();
    }
    const QString th = thinkText();
    if (th.size() > emittedThink_) {
        emit thinkDelta(th.mid(emittedThink_));
        emittedThink_ = th.size();
    }
}

QString DeepSeekClient::answerText() const {
    QString out;
    for (const Frag& f : frags_)
        if ((f.type == QLatin1String("RESPONSE") || f.type == QLatin1String("SEARCH")) && !f.content.isEmpty())
            out += f.content;
    return out.isEmpty() ? legacy_ : out;
}

QString DeepSeekClient::thinkText() const {
    QString out;
    for (const Frag& f : frags_)
        if ((f.type == QLatin1String("THINK") || f.type == QLatin1String("REASONING")) && !f.content.isEmpty())
            out += f.content;
    return out;
}
