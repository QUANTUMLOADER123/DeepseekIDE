#include "deepseekclient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <functional>

static const char* DS_BASE = "https://chat.deepseek.com";
static const char* DS_UA   = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                             "(KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";

DeepSeekClient::DeepSeekClient(QObject* parent) : QObject(parent) {}

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
        quint64 answer = 0; QString perr;
        if (!pow_.solve(chal, answer, perr)) { emit failed(perr); return; }
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
        startCompletion(prompt, QJsonDocument(out).toJson(QJsonDocument::Compact).toBase64());
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
    sseBuf_.clear(); fullText_.clear(); thinkText_.clear(); lastFragType_.clear(); respMsgId_.clear();
    connect(r, &QNetworkReply::readyRead, this, &DeepSeekClient::onReadyRead);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        reply_ = nullptr;
        r->deleteLater();
        const QByteArray tail = r->readAll();
        if (!tail.isEmpty()) { sseBuf_ += tail; }
        // добить остаток буфера как одну строку (без завершающего \n)
        const QByteArray rest = sseBuf_;
        for (const QByteArray& ln : rest.split('\n')) {
            if (ln.startsWith("data:")) {
                QByteArray pay = ln.mid(5).trimmed();
                if (pay != "[DONE]") {
                    auto j = QJsonDocument::fromJson(pay);
                    if (j.isObject()) {
                        const QJsonObject d = j.object();
                        if (d.contains(QStringLiteral("response_message_id")))
                            respMsgId_ = QString::number(d.value(QStringLiteral("response_message_id")).toVariant().toLongLong());
                    }
                }
            }
        }
        if (r->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("completion stream: %1").arg(compactErr(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), sseBuf_)));
            return;
        }
        if (!respMsgId_.isEmpty()) parentMessageId_ = respMsgId_;
        emit log(QStringLiteral("DS: ответ %1 зн, мысли %2 зн, parent=%3")
                 .arg(fullText_.size()).arg(thinkText_.size()).arg(parentMessageId_));
        emit chatFinished(fullText_, thinkText_);
    });
}

void DeepSeekClient::onReadyRead() {
    QNetworkReply* r = reply_.data();
    if (!r) return;
    sseBuf_ += r->readAll();
    int cut;
    while ((cut = sseBuf_.indexOf('\n')) >= 0) {
        QByteArray line = sseBuf_.left(cut);
        if (line.endsWith('\r')) line.chop(1);
        sseBuf_.remove(0, cut + 1);
        if (!line.startsWith("data:")) continue;
        QByteArray pay = line.mid(5).trimmed();
        if (pay.isEmpty() || pay == "[DONE]") continue;
        auto j = QJsonDocument::fromJson(pay);
        if (!j.isObject()) continue;
        const QJsonObject d = j.object();

        if (d.contains(QStringLiteral("response_message_id")))
            respMsgId_ = QString::number(d.value(QStringLiteral("response_message_id")).toVariant().toLongLong());

        const QJsonValue v = d.value(QStringLiteral("v"));
        const QJsonObject vo = v.toObject();
        if (vo.contains(QStringLiteral("response"))) {
            const QJsonObject resp = vo.value(QStringLiteral("response")).toObject();
            if (resp.contains(QStringLiteral("message_id")))
                respMsgId_ = QString::number(resp.value(QStringLiteral("message_id")).toVariant().toLongLong());
        }

        const QString p = d.value(QStringLiteral("p")).toString();

        if (p == QStringLiteral("response/fragments") && v.isArray()) {
            for (const QJsonValue& fv : v.toArray()) {
                const QJsonObject f = fv.toObject();
                const QString t = f.value(QStringLiteral("type")).toString();
                const QString c = f.value(QStringLiteral("content")).toString();
                if (t == QStringLiteral("RESPONSE") || t == QStringLiteral("SEARCH")) {
                    fullText_ += c; lastFragType_ = t;
                    if (!c.isEmpty()) emit chatDelta(c);
                } else if (t == QStringLiteral("THINK") || t == QStringLiteral("REASONING")) {
                    thinkText_ += c; lastFragType_ = t;
                    if (!c.isEmpty()) emit thinkDelta(c);
                }
            }
        } else if (p == QStringLiteral("response/fragments/-1/content") && v.isString()) {
            const QString chunk = v.toString();
            if (lastFragType_ == QStringLiteral("THINK") || lastFragType_ == QStringLiteral("REASONING")) {
                thinkText_ += chunk; emit thinkDelta(chunk);
            } else {
                fullText_ += chunk; emit chatDelta(chunk);
            }
        } else if (p == QStringLiteral("response/content") && v.isString()) {
            const QString chunk = v.toString();
            fullText_ += chunk; emit chatDelta(chunk);
        } else if (p == QStringLiteral("response/finish_reason") && v.isString()) {
            emit log(QStringLiteral("DS: finish_reason=%1").arg(v.toString()));
        }
        // err-события модели
        if (d.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
            emit failed(QStringLiteral("model error: %1 %2")
                        .arg(d.value(QStringLiteral("content")).toString(),
                             d.value(QStringLiteral("message")).toString()));
        }
    }
}
