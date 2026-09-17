# Архитектура DeepSeekIDE (сервер-эпоха)

Ревизия 2026-09. Историческая эпоха на ImGui/GLFW/WebView2 **удалена** —
эпоха до неё сохранена в git-истории (тегов нет, искать до коммита пивота).

## Решаемая боль

DeepSeek API платный и требует ключа; веб-чат chat.deepseek.com бесплатен.
IDE позволяет агентить прямо через веб-чат: шлёт туда задачи вместе с деревом
проекта и применяет присланные правки к файлам — со снапшотами и откатом.

## Состав

```
┌─ deepseekide.exe (C++20, без внешних зависимостей) ────────────────┐
│                                                                    │
│  IdeServer (cpp-httplib vendored)                                  │
│   • статика: assets/web (index.html, app.css, app.js, /monaco/*)   │
│   • /api/state /api/file /api/project/open /api/snapshots          │
│     /api/chat/{connect,send,reply,apply,cancel,newchat} /api/log   │
│     /api/events /api/shutdown; всё закрыто токеном, bind 127.0.0.1 │
│                                                                    │
│  ChatDriver (поток)                                                │
│   • находит Chrome/Edge (реестр App Paths, Program Files, PATH)    │
│   • запускает: chrome --remote-debugging-port=P                    │
│         --user-data-dir=%APPDATA%/DeepSeekIDE/browser-profile      │
│         --new-window https://chat.deepseek.com                     │
│   • CDP: CdpClient → WsClient (RFC6455, mask/ping-pong/continu.)   │
│   • опрос состояния через Runtime.evaluate: кол-во ds-markdown,    │
│     пульс активности (MutationObserver), наличие textarea          │
│   • автомат Sending → WaitingSettle(>1.8с тишины) → Ready          │
│   • ответ парсится OpsParser'ом → блоки deepseekide-ops            │
│                                                                    │
│  Ядро (испытано 62 тестами)                                        │
│   ProjectManager (SafeJoin/дерево) · SnapshotManager (откат)       │
│   ToolRegistry (13 мутаций) · EditorManager → удалён               │
│   Platform (ConfigDir/RunCommand/FindChromium/LaunchDetached/…)    │
│   HttpClient (на cpp-httplib; Post/Get; OpenSSL опционально)       │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
                 │                                    ▲
            HTTP + token                    CDP/WebSocket
                 ▼                                    │
┌─ Браузер пользователя (IDE) ─┐        ┌─ Chrome/Edge (агент) ──────┐
│ index.html + app.js + Monaco │        │ вкладка chat.deepseek.com  │
│ адрес http://127.0.0.1:P/?t= │        │ в отдельном профиле        │
└──────────────────────────────┘        └────────────────────────────┘
```

## Поток данных задачи

1. Пользователь: задача → `POST /api/chat/send {kind:"task"}` → ChatDriver очередь.
2. Драйвер в фазе Idle строит промпт: шаблон + `list_files` (дерево проекта) + задача;
   `Runtime.evaluate(__dsideSendNow(text))` — textarea.fill + click кнопки.
   Титул «отправлено, жду ответ…», phase=Sending.
3. Пульс активности: пока есть мутации DOM — ждём; >1.8 c тишины → фаза Ready,
   берём последний `div.ds-markdown` innerText, парсим очереди `deepseekide-ops`.
4. `GET /api/chat/reply` → фронт показывает текст + чекбоксы операций.
5. `POST /api/chat/apply` → SnapshotManager.Begin → ToolRegistry.Execute(по каждой)
   → Commit (снимок создан, если что-то реально изменилось) → отчёт.
6. Откат: `POST /api/snapshots/rollback {id}` (RollbackThrough) — удаляет созданное,
   возвращает старое содержимое изменённых файлов.

## Безопасность

- bind только `127.0.0.1`.
- Токен 28 случайных символов обязателен на всех `/api/*`; автостарт открывает
  браузер уже с `?t=`; фронт кладёт токен в sessionStorage и стирает из URL.
- Все файловые API идут через `ProjectManager::SafeJoin` (без выхода из корня).
- Запись в `assets/web/*` исключена защитой `IsProtectedRel` на уровне инструментов
  (вложенные пути `.deepseekide/`, нулевые байты, `..` отклоняются).

## Отказоустойчивость агента

- Вкладка закрылась — следующий опрос уронит сокет, статус «offline» + ошибка в UI;
  каждые 5 с автоповтор: найти живой CDP-порт (19226+) или запустить браузер заново.
- Ответ не пошёл 15 с после отправки → «войдите в окне браузера» (капча/логин).
- Ответ живёт >6 минут → снять ожидание, вернуть в Idle.
- «Новый чат» и «Стоп» доступны из UI всегда.
