#include "sessionstore.h"
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QDateTime>
#include <QJsonDocument>
#include <algorithm>

static QString nowIso() { return QDateTime::currentDateTime().toString(Qt::ISODate); }

Session Session::create(const QString& folder) {
    Session s;
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    s.folder = folder;
    s.title = QStringLiteral("New Chat");
    s.created = s.updated = nowIso();
    s.history = QJsonArray{};
    return s;
}

QJsonObject Session::toJson() const {
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("folder"), folder},
        {QStringLiteral("ds_session_id"), dsSessionId},
        {QStringLiteral("parent_message_id"), parentMessageId},
        {QStringLiteral("created"), created},
        {QStringLiteral("updated"), updated},
        {QStringLiteral("ctx"), ctxChars},
        {QStringLiteral("history"), history},
    };
}

Session Session::fromJson(const QJsonObject& o) {
    Session s;
    s.id = o[QStringLiteral("id")].toString();
    if (s.id.isEmpty()) s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    s.title = o[QStringLiteral("title")].toString(QStringLiteral("New Chat"));
    s.folder = o[QStringLiteral("folder")].toString();
    s.dsSessionId = o[QStringLiteral("ds_session_id")].toString();
    s.parentMessageId = o[QStringLiteral("parent_message_id")].toString();
    s.created = o[QStringLiteral("created")].toString();
    s.updated = o[QStringLiteral("updated")].toString();
    s.ctxChars = o[QStringLiteral("ctx")].toInt(0);
    s.history = o[QStringLiteral("history")].toArray();
    return s;
}

QString SessionStore::storagePath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/sessions.json");
}

QList<Session> SessionStore::load() const {
    QList<Session> out;
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    for (const QJsonValue& v : doc.array()) out.append(Session::fromJson(v.toObject()));
    // новые сверху
    std::sort(out.begin(), out.end(), [](const Session& a, const Session& b){ return a.updated > b.updated; });
    return out;
}

void SessionStore::save(const QList<Session>& sessions) const {
    QJsonArray arr;
    QList<Session> copy = sessions;
    std::sort(copy.begin(), copy.end(), [](const Session& a, const Session& b){ return a.updated > b.updated; });
    for (const Session& s : copy) {
        Session t = s; t.updated = nowIso();
        arr.append(t.toJson());
    }
    QFile f(storagePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.close();
    }
}
