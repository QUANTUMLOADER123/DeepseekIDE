# Архитектура DeepSeekIDE

> Главный принцип с этого релиза: **никаких API-ключей**. Агент разговаривает с моделью
> через встроенный в окно chat.deepseek.com (WebView2 на Windows), а правки файлов
> извлекаются из ответа детерминированным парсером и применяются с подтверждением.

## Общая схема

```
┌─ deepseekide.exe ─────────────────────────────────────────────────────┐
│  Application (владелец всего; 1 GUI-поток)                           │
│                                                                      │
│  ┌───────── левая половина ─────────┐  ┌──────── правая ──────────┐  │
│  │ WebChatPanel                     │  │ DrawAgentBar (задача)    │  │
│  │  • дочерний HWND + WebView2      │  │ dockspace: Проект/Редак- │  │
│  │  • свой std::thread: w.run()     │  │   тор/Журнал/Снимки      │  │
│  │  • installer → window.__dside*   │  │ StatusBar                │  │
│  └──────▲──────────────┬───────────┘  └───────────▲──────────────┘  │
│         │ dsideState   │ SendPrompt/NewChat        │                │
│         └──────────────┴────────── AgentBridge ────┘                │
│                    │ parse → ReplyInfo ▲ ops                        │
│                    ▼                    │                            │
│             OpsParser (pure)     ApplyOps → ToolRegistry.Execute    │
│                                         │      │                    │
│                          SnapshotManager.Begin/Commit               │
└──────────────────────────────────────────────────────────────────────┘
```

Потоки:

- **GUI-поток** — ImGui, панели, ApplyOps.
- **Поток WebChatPanel** — жизнь webview (`run()` блокирующий); все вызовы JS
  идут через `webview::dispatch`, состояние возвращается через `bind("dsideState")`
  → `OnStateJson` → `mState` под мьютексом.
- Лёгкий **поток-инжектор** (внутри WebChatPanel): раз в ~1.2 с идемпотентно
  внедряет мост `__dside*` (переживает SPA-перезагрузки страницы).

## WebChatPanel (`src/ui/WebChatPanel.*`)

- Конструкция `Impl` скрывает платформенные детали; заголовок не тянет ни windows.h,
  ни webview.h (чистое ядро).
- **Windows + `DEEPSEEKIDE_WEBVIEW`**: создаём дочернее `WS_CHILD`-окно поверх GLFW
  (`glfwGetWin32Window`), webview 0.12 получает его как родителя → WebView2 занимает
  ровно этот прямоугольник; `SetRect` каждый кадр двигает его под сплиттер.
- **Linux/macOS**: отдельное окно webview с тем же мостом (SetRect — no-op).
- **Без webview**: полностью выключенная заглушка, `Supported()==false`,
  Application рисует `DrawChatFallback`.

### JS-мост — селекторы chat.deepseek.com

Инъектор ставит:

- `__dsideSendNow(text)` — находит `textarea`, выставляет значение через
  нативный setter `HTMLTextAreaElement.prototype.value.set` (иначе React не заметит),
  шлёт `input/change`, затем ищет среди `div[role=button]` кнопку
  с классами `ds-button--primary` **без** `ds-button--disabled` и жмёт её (до ~4 с).
- `__dsideNewChat()` — жмёт элемент с текстом "New chat"/"Новый чат".
- Observer: `MutationObserver` пишет `__dsideActivity` (ms). Тикер (400 мс) считает
  `busy = активность < 1300 мс` и раз в ~500 мс пушит в C++ JSON
  `{n: число div.ds-markdown, t: innerText последнего ответа, b, p}`.
  Хеш-классы вида `_27c9245` **не используются** (ломаются между билдами сайта).

## AgentBridge (`src/ai/AgentBridge.*`)

Автомат состояний: `Idle → Sending → WaitingSettle → Ready/TimedOut`.

- `SendTask` строит промпт: правила формата deepseekide-ops + таблица операций +
  дерево проекта (`list_files`, ≤350 строк) + сама задача. `SendNote` — сырой текст
  (доп. вопрос/файл).
- Фиксация конца генерации: ответ появился (`n > baseline` или `busy`), затем
  **1.8 с тишины** → забираем `last`. Таймауты: без моста 20 с, без реакции чата
  — 3 повторные отправки, общий потолок 6 минут.
- `ApplyOps` (GUI-поток): `SnapshotManager.Begin("…веб-чата")` →
  `ToolRegistry.Execute` по списку → `Commit`. Отчёт ✓/✗ уходит в Журнал.

## OpsParser (`src/ai/OpsParser.*` — pure, тестируется)

Ищет блоки ```` ```deepseekide-ops ```` (обычные код-блоки сохраняет в тексте ответа),
парсит JSON: массив / `{operations:[…]}` / одиночный объект. Имена сверяются с
`dside::MutationOps()`: `write_file edit_file append_file insert_lines replace_lines
make_dir delete_path copy_file move_file` — чтение/shell в ответах запрещены.

## ToolRegistry — 13 инструментов (`src/ai/ToolRegistry.*`)

Старые: `list_files read_file write_file edit_file make_dir delete_path search_files run_command`.
Новые: `append_file` (создаёт при отсутствии, склеивает `\n`),
`insert_lines {line}` (перед строкой, нумерация с 1, `line = n+1` — в конец),
`replace_lines {start_line,end_line,content}` (пустой content = удаление строк),
`copy_file {from,to}` (не затирает существующее), `move_file {from,to}`
(папки — рекурсивно в снимок). Все пути проходят `Resolve` → `SafeJoin`
(без выхода из корня, без .git/.deepseekide), пишущие операции — автоснимок,
колбэки `fileChanged`/`MarkDirty` обновляют дерево и редактор.

## Application — раскладка (`src/app/Application.*`)

- Кастомный `DrawHeader` (46 px × DPI): логотип, кнопки Проект/Сохранить/Откат/
  Новый чат/Настройки/?, справа — статус моста (подключение/вход/думаю/готов).
- `RenderLayout`: зона чата `splitW = W×mSplitRatio` (долгота в `Settings.chatSplit`,
  тянется сплиттером, управляется курсором `ResizeEW`), за ней окно `##workspace`
  с агент-баром и внутренним `DockSpace` (Проект слева 24 %, Снимки справа 26 %,
  Журнал снизу 28 %, Редактор центр), `DockBuilder` единожды, дальше user-layout
  через `imgui.ini` в ConfigDir.
- Координаты чата: `ImGui WorkPos − glfwGetWindowPos` = клиентские координаты HWND
  (см. `GLFW` семантику позиции клиентской области); на иконификации — SetVisible(false).
- Ответ агента → модал «Ответ DeepSeek — применить?»: текст (700 симв. превью),
  список операций `ToolRegistry::Describe`, кнопка Apply → `ApplyPendingReply`
  (лог + refresh дерева/снимков/вкладок).

## Легаси API-режим

`src/ai/Agent.*`, `src/ai/DeepSeekClient.*`, `src/net/HttpClient.*` остались в
`deepseekide_core` (SSE-парсер и схемы покрыты selftest) — из GUI не вызываются,
поля `apiKey` в `Settings` не редактируются. Удаление — отдельным рефактором.

## Self-tests (`src/tests/selftest.cpp`, таргет без GUI)

74 проверки: SafeJoin, SnapshotManager, SSE-агрегация, схемы инструментов,
живые инструменты, **новые ops** (insert/replace/append/copy/move), **OpsParser**
(массив/объект/битый JSON/запрещённые имена) и сетевой smoke (SKIP без сети).
