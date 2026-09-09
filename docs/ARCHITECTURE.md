# Архитектура DeepSeekIDE

## Слои

```
┌──────────────────────────────────────────────────────────────┐
│ GUI (вызывается только из главного потока)                   │
│  Application → MenuBar/DockLayout → панели (ui/*)            │
│   ├─ FileExplorerPanel  ── показывает ProjectManager.Tree    │
│   ├─ EditorPanel        ── показывает EditorManager.tabs     │
│   ├─ ChatPanel          ── показывает события Agent          │
│   ├─ SnapshotsPanel     ── показывает SnapshotManager.List   │
│   ├─ LogPanel / StatusBar                                  │
└──────────────▲───────────────────────────────────────────────┘
               │ события (AgentEvent) раз в кадр
┌──────────────┴───────────────────────────────────────────────┐
│ Агентный контур (рабочий поток)                              │
│  Agent::ThreadMain                                           │
│    loop ≤ maxSteps:                                          │
│      messages = system(prompts::kAgentSystemPrompt) + history│
│      res = DeepSeekClient::ChatStream(messages, tools, onTok)│
│      if res.toolCalls.empty() → Done                         │
│      for tc in res.toolCalls:                                │
│          ToolRegistry::Execute → ToolRunResult → history     │
└──────────────▲───────────────────────────────────────────────┘
               │ только через контекст (callbacks + mutex)
┌──────────────┴───────────────────────────────────────────────┐
│ Ядро                                                           │
│  ToolRegistry ── SafeJoin(pesочница) ── SnapshotManager       │
│  ProjectManager (дерево файлов)   Settings (JSON в ConfigDir) │
│  HttpClient (libcurl; POST/POST-stream/GET; cancel flag)      │
└───────────────────────────────────────────────────────────────┘
```

## Потоковая модель

- **Главный поток**: GLFW + ImGui. Всё CPU-лёгкое.
- **Поток агента** (один): сетевые запросы + исполнение инструментов.
  Обмен только через:
  - `Agent::DrainEvents()` — очередь `AgentEvent` под мьютексом (UI забирает каждый кадр);
  - `LogPanel::Add` — потокобезопасная очередь;
  - флаги `std::atomic<bool> cancel/busy + ProjectManager::dirty`.
- Отмена: `cancel` читается и в curl-callback (обрыв HTTP), и между итерациями агента.

## Откат (SnapshotManager)

Каждое обращение пользователя = один снимок (`Begin → Record* → Commit`):

- `RecordFileChange()` вызывается из `write_file`/`edit_file`/`delete_path`
  **до** изменения; оригинал копируется в `.deepseekide/snapshots/<id>/files/<relpath>`.
- `meta.json`: `id`, `title` (из запроса), `epoch_ms`, `files[] {path, existed}`.
- Пустое действие (модель ничего не меняла) — снимка нет.
- **Откат**: `existed=true` → копия возвращается; `existed=false` → созданный файл удаляется.
  После успешного отката запись удаляется — «Откатить последнее» шагает назад по истории.
  `RollbackThrough(id)` идёт от новых к старым и пропускает уже восстановленные файлы —
  получается корректное состояние «до» выбранного действия.

## Песочница путей

- Инструменты получают пути только относительно корня проекта.
- `ProjectManager::SafeJoin`: `weakly_canonical(root/rel)` + проверка префикса root.
  Абсолютные пути и выход через `..` отклоняются.
- `IsProtectedRel`: первый компонент `.git` или `.deepseekide` — запись запрещена.

## DeepSeek API

- `POST {baseUrl}/chat/completions`, `Authorization: Bearer <key>`, OpenAI-формат.
- Стрим: `stream=true`, SSE `data:`-строки; дельты `content` → токены в UI,
  дельты `tool_calls` склеиваются по `index` (id/name/arguments — фрагментированные строки).
- История хранится в API-формате: ассистентские сообщения с полем `tool_calls`,
  результаты — `role=tool` + `tool_call_id`. Persist в `.deepseekide/chat_history.json`
  только user/assistant текст (tool-кадры при загрузке не нужны).

## Окно chat.deepseek.com

- Библиотека webview (`webview::webview`) в отдельном потоке: WebView2 (Windows),
  WebKitGTK (Linux), WKWebView (macOS). Логин и куки — штатные, в профиле ОС.
- Собирается только при наличии бэкенда (`DEEPSEEKIDE_WEBVIEW=ON`), иначе —
  честное сообщение-подсказка в чате.

## Почему эти зависимости

| Выбор | Причина |
|---|---|
| Dear ImGui 1.91.5 **docking** | Докинг панелей + мульти-окна (viewports) из коробки |
| ImGuiColorTextEdit (santaclose) | Совместим с ImGui 1.91; boost/regex заменён shim'ом на `std::regex` |
| libcurl | Зрелый HTTPS + потоковый SSE, кросс-платформа (schannel на Windows) |
| glfw | Минимальный кросс-платформенный GL-контекст |
| nlohmann/json | Де-факто стандарт JSON в C++ |
