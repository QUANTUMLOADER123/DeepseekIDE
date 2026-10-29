#pragma once
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>

// Одна беседа агента (левая колонка). Хранится в AppData/sessions.json
struct Session {
    QString id;             // uuid
    QString title;          // короткое название = первое сообщение
    QString folder;         // корень проекта сессии
    QString dsSessionId;    // chat_session_id DeepSeek
    QString parentMessageId;// контекстная цепочка
    QString created, updated;
    int     ctxChars = 0;   // грубая оценка контекста (символы), для 200k-лимита и compact
    bool    pinned = false;   // закреплённая сессия — блоком выше остальных
    QJsonArray history;     // карточки чата [{role,text,name,sub,ok,detail,pprev}]

    static Session create(const QString& folder);
    QJsonObject toJson() const;
    static Session fromJson(const QJsonObject& o);
};

class SessionStore {
public:
    QList<Session> load() const;
    void save(const QList<Session>& sessions) const;
    static QString storagePath();
};
