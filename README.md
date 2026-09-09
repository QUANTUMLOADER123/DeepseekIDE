<div align="center">

# 🧠 DeepSeekIDE

**Кастомный AI-IDE на C++ и Dear ImGui со встроенным агентом DeepSeek**

Агент сам читает и правит файлы выбранной вами папки-проекта,
каждое действие можно **откатить одной кнопкой**, а веб-чат
[chat.deepseek.com](https://chat.deepseek.com) доступен прямо из IDE.

[![Build](https://github.com/QUANTUMLOADER123/DeepseekIDE/actions/workflows/build.yml/badge.svg)](../../actions/workflows/build.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Dear ImGui](https://img.shields.io/badge/Dear%20ImGui-1.91.5--docking-orange)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

</div>

---

## ✨ Возможности

| | |
|---|---|
| 🤖 **AI-агент DeepSeek** | Пишете по-русски «создай TODO-CLI на Python с тестами» — агент сам изучает проект, создаёт и правит файлы (function calling `deepseek-chat` / `deepseek-reasoner`) |
| 👁 **Живой стрим** | Ответ модели печатается в чат по мере генерации (SSE), вызовы инструментов видны в ленте |
| ⏪ **Откат действий** | Снапшот каждого изменяемого файла до правки → «Откатить последнее действие» или любую точку истории |
| 🌐 **chat.deepseek.com** | Веб-чат DeepSeek в отдельном окне (WebView2 / WebKitGTK / WKWebView) — логин под своим аккаунтом, без API-ключа |
| 📁 **Проводник** | Дерево проекта, создание/переименование/удаление файлов, drag-and-drop папки в окно |
| 📝 **Редактор** | ImGuiColorTextEdit: подсветка C++/C/C#/Python/Lua/GLSL/HLSL/JSON/SQL, вкладки, undo/redo, поиск (Ctrl+F) |
| 🖥 **Журнал и терминал** | Каждое действие агента и каждая команда `run_command` — в журнале с фильтрами |
| 🎨 **Интерфейс** | Кастомная тема DeepSeek Dark (+Nord, Light), шрифты Inter и JetBrains Mono с кириллицей, HiDPI |
| 💾 **Память** | История чата хранится в `.deepseekide/chat_history.json` внутри проекта |

## 🖼 Интерфейс

```
┌──────────────────────────────────────────────────────────────────────┐
│ Файл  Вид  Проект  Чат  Справка                 ● DeepSeek работает… │
├───────────┬──────────────────────────────────┬───────────────────────┤
│ ПРОЕКТ    │ РЕДАКТОР                         │ DEEPSEEK АССИСТЕНТ    │
│           │ ┌ main.cpp × ┌ utils.cpp ● ┐     │ ┌───────────────────┐ │
│ ▸ src     │ │                              │ │ │ ◆ DeepSeek        │ │
│  main.cpp │  1│ #include <iostream>        │ │ │ Сделал:           │ │
│  utils.cpp│  2│ int main() {…}             │ │ │ • создал todo.py  │ │
│ ▾ assets  │ │                              │ │ │ • создал test_…py │ │
│  logo.png │ │                              │ │ └───────────────────┘ │
│           │ ├ файлы.cpp: ✓ сохранено ┤     │  ⚙ list_files · .     ︾ │
│           ├──────────────────────────────────┤  вы: создай todo-cli ▸ │
│           │ ЖУРНАЛ                           │ [Отправить] [Очистить] │
│           │ 12:00 $ python -m pytest → код 0 ├───────────────────────┤
│           │                                  │ СНИМКИ И ОТКАТ        │
│           │                                  │ ↺ Откатить последнее  │
│           │                                  │ 09.09 17:42 · todo-cli│
├───────────┴──────────────────────────────────┴───────────────────────┤
│ ● проект ~/pet · Готов                        UTF-8 | deepseek-chat  │ 
└──────────────────────────────────────────────────────────────────────┘
```

## 🚀 Быстрый старт

### Зависимости

| Платформа | Пакеты |
|---|---|
| **Windows** | Visual Studio 2022 с «Desktop development with C++», [vcpkg](https://vcpkg.io) `install curl` (или оставьте пустым — CMake соберёт curl сам), WebView2 Runtime (уже есть в Win10/11) |
| **Ubuntu/Debian** | `sudo apt install build-essential cmake ninja-build libglfw3-dev libcurl4-openssl-dev xorg-dev` <br> для веб-чата: `libwebkit2gtk-4.1-dev` |
| **Fedora** | `sudo dnf install cmake ninja-build glfw-devel libcurl-devel webkit2gtk4.1-devel` |
| **macOS** | `brew install cmake ninja glfw curl` |

> GLFW и curl CMake найдёт в системе сам; если не найдёт — скачает и соберёт из исходников (кроме X11-заголовков на Linux — их нужно поставить пакетом).

### Сборка

**Windows — самый простой путь:**
1. Дважды кликните **`build-windows.bat`** — он сам создаст `build\DeepSeekIDE.sln` **и соберёт** `build\Release\deepseekide.exe`.
2. Хотите работать в Visual Studio — откройте `build\DeepSeekIDE.sln` (либо откройте саму папку проекта через *Файл → Открыть → Папка*: VS 2022 понимает CMake напрямую).

**Linux / macOS — аналогично:**
```bash
./build-linux-macos.sh        # запуск: ./build/deepseekide
```

**Руками (везде):**
```bash
git clone https://github.com/QUANTUMLOADER123/DeepseekIDE.git
cd DeepseekIDE
cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release   # без -G Ninja на Windows создастся .sln
cmake --build build -j                              # Windows: добавьте --config Release
./build/deepseekide                                  # Windows: build\Release\deepseekide.exe
```

> Откуда берётся `.sln`: проект собирается **CMake**'ом — файл Visual Studio-решения генерируется автоматически в `build/` при конфигурации на Windows. Коммитить `.sln` в репозиторий не нужно: он конкретен для машины и компилятора.

### Первый запуск

1. **Файл → Открыть папку проекта** (или просто перетащите папку в окно).
2. Выберите путь:
   - 🌐 **Без ключа** — кнопка **«◆ chat.deepseek.com»** открывает веб-чат DeepSeek (логин через Google/почту; бесплатно, лимиты как на сайте);
   - 🔑 **С API-ключом** — **Настройки (Ctrl+,)** → вставьте ключ с [platform.deepseek.com](https://platform.deepseek.com) → и чат справа становится полноценным агентом с доступом к файлам проекта.
3. Напишите, что сделать: *«добавь README с описанием»*, *«напиши скрипт backup.sh»*, *«найди все TODO в коде и выпиши списком»*.
4. Следите за лентой: агент сам вызывает инструменты (`write_file`, `edit_file`, …).
5. Не понравилось? **Панель «Снимки и откат» → ↺ Откатить последнее действие.**

## ⌨ Горячие клавиши

| Клавиши | Действие |
|---|---|
| `Ctrl+O` | Открыть папку проекта |
| `Ctrl+S` / `Ctrl+Shift+S` | Сохранить файл / все файлы |
| `Ctrl+W` | Закрыть вкладку |
| `Ctrl+F` | Поиск в текущем файле |
| `Ctrl+,` | Настройки |
| `Enter` / `Ctrl+Enter` | Отправить сообщение / новая строка в чате |

## 🧰 Инструменты агента

Агент получает 8 инструментов (DeepSeek function calling). Все пути — **только внутри проекта** (выход за пределы и доступ к `.git`/`.deepseekide` заблокированы на уровне кода):

`list_files` · `read_file` · `write_file` · `edit_file` · `make_dir` · `delete_path` · `search_files` · `run_command` *(выключен по умолчанию — включается в Настройках → Безопасность)*

Перед каждым изменением файл копируется в `.deepseekide/snapshots/<id>/` —
поэтому откат восстанавливает состояние **побайтово**, включая удаление созданных файлов.

## 🏗 Архитектура

```
src/
├── app/      Application (окно, цикл, докинг, события), Platform (ОС, UTF-8, процессы)
├── core/     Settings · ProjectManager (песочница путей) · SnapshotManager (откат)
│             · EditorManager (вкладки)
├── net/      HttpClient (libcurl: POST, SSE-стрим, отмена)
├── ai/       DeepSeekClient (API + SSE + агрегация tool_calls)
│             · ToolRegistry (8 инструментов) · Agent (цикл потока) · SystemPrompt
├── ui/       Theme · Fonts (Inter, JetBrains Mono, кириллица) · панели · диалоги
└── tests/    консольные самотесты ядра (47 проверок)
```

Подробнее: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## 🔒 Безопасность

- API-ключ хранится локально в `~/.deepseekide/settings.json` (`%APPDATA%/DeepSeekIDE` в Windows) и отправляется только на `api.deepseek.com`.
- Агент технически не может писать вне выбранной папки (проверка путей `SafeJoin` + защищённые служебные каталоги).
- `run_command` по умолчанию запрещён; таймаут и явное включение пользователем.
- Снапшоты лежат внутри проекта в `.deepseekide/` — добавьте её в `.gitignore` (наш `.gitignore` уже содержит).

## 🛠 Сборка из исходников (подробно)

```bash
# Только консольное ядро + тесты (без GUI):
cmake -B build -DDEEPSEEKIDE_BUILD_GUI=OFF && cmake --build build && ./build/deepseekide_selftest

# Отключить окно chat.deepseek.com:
cmake -B build -DDEEPSEEKIDE_ENABLE_WEBVIEW=OFF

# Статические опции:
#  DEEPSEEKIDE_BUILD_GUI=ON|OFF   DEEPSEEKIDE_BUILD_TESTS=ON|OFF   DEEPSEEKIDE_ENABLE_WEBVIEW=ON|OFF
```

Зависимости подтягиваются через CMake FetchContent с закреплёнными тегами:

| Библиотека | Версия | Назначение |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.5-docking | интерфейс (докинг + вьюпорты) |
| [ImGuiColorTextEdit](https://github.com/santaclose/ImGuiColorTextEdit) | 264bee4, MIT | редактор кода |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | JSON |
| [curl](https://github.com/curl/curl) | системный или curl-8_10_1 | HTTPS/SSE |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | окно/OpenGL |
| [webview](https://github.com/webview/webview) | 0.12.0 | chat.deepseek.com |

## 🗺 Roadmap

- [ ] LSP-клиент (подсказки, диагностика) поверх `run_command`
- [ ] Diff-вьювер «до/после» для снимков
- [ ] Выбор нескольких моделей и профили промптов
- [ ] Плавающие окна webview прямо внутри док-панелей
- [ ] Автообновление дерева файлов (fs watcher)
- [ ] I18n (en/ru) переключатель в настройках

## 📄 Лицензии

- DeepSeekIDE — [MIT](LICENSE).
- Шрифты Inter и JetBrains Mono — SIL OFL 1.1 (`assets/fonts/OFL-*.txt`).
- ImGuiColorTextEdit — MIT (Balazs Jako, форк santaclose).
