# DeepSeek C++ IDE (cppide)

Нативная Qt6-версия DeepSeekIDE: живёт рядом с Python-версией и повторяет её функциональность.

## Что внутри

- **Дерево проекта + редактор кода** — вкладки, подсветка C++/Python, номера строк, Ctrl+S.
- **Чат с DeepSeek напрямую** — клиент ходит в `chat.deepseek.com` Web API сам, без браузера
  (создаёт chat session, решает PoW-челлендж через встроенный **wasm3**, читает SSE-стрим).
- **Агент** — та же система маркеров `[<{…}>]`, что в Python-версии:
  NEED FILE / READ RANGE / WEB FETCH / SHELL / MEMORY / IMAGE …, блоки правок
  `deepseekide-ops` и голые JSON-массивы (`write_file`, `edit_file`, `append_file`,
  `insert_lines`, `replace_lines`, `make_dir`, `delete_path`, `copy_file`, `move_file`),
  авто-continue с потолком.
- **Генерация фото** — subnp.com free (magic/flux), без ключа.
- **Безэмодзи тёмный интерфейс**, русский язык.

## Система маркеров

Контракт в первом сообщении просит модель использовать невидимые пользователю маркеры
`[<{…}\>]`-формата — их агент интерпретирует и убирает из показа.
Блоки правок: fenced-блоки с меткой `deepseekide-ops` или голые JSON-массивы ops в любом месте ответа.

## Сборка (Windows, MSVC + Qt 6.8)

1. Поставьте Qt 6.8 (нужны компоненты: **MSVC 2022 64-bit** и **Qt 5 Compatibility** для CP866):
   - через Qt Online Installer (qt.io/download-qt-installer-oss), **или**
   - консолью: `pip install aqtinstall && aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qt5compat --outputdir C:\Qt`
2. Просто запустите `build.bat` из папки `cppide` — он сам:
   - найдёт Qt (диски C:/D:/E:/F:, `%USERPROFILE%\Qt`; иначе задайте `QT6_DIR`),
   - поднимет окружение Visual Studio (`vcvars64.bat`, включая `F:\Program Files\Microsoft Visual Studio\18\Community`),
   - соберёт через Ninja (или генератор VS) и положит Qt-рантаймы рядом с exe.
3. Итог: `cppide\build\DeepSeekCppIDE.exe` (Ninja) или `cppide\build\Release\` (VS-генератор).
4. Если CMake не найден: `winget install Kitware.CMake` (или поставьте «C++ CMake tools» в VS Installer).

## Где взять токен DeepSeek

1. Откройте chat.deepseek.com и войдите в аккаунт.
2. F12 → Console: `localStorage.getItem('userToken') || localStorage.getItem('token')` — вставьте в «DeepSeek token».
3. Cookies (лучше взять): вкладка Application → Cookies → `https://chat.deepseek.com` →
   скопируйте `ds_session_id=…; smidV2=…` — в поле «Cookie».
4. PoW WASM URL обычно менять не нужно.

> Прямой доступ к веб-API — на свой риск: античиты DeepSeek могут потребовать обновить токен/куки.

## Файлы

- `src/main.cpp`, `src/mainwindow.*` — окно, дерево, табы, чат-лента.
- `src/codeeditor.*` — редактор с номерами строк и подсветкой.
- `src/deepseekclient.*` — прямой Web API-клиент (session + PoW + SSE).
- `src/pow.*` + `wasm3/` — решатель PoW челленджа на wasm3 v0.5.0 (vendored MIT).
- `src/agent.*` — маркеры/ops, оболочка инструментов, авто-continue.
- `src/settings.*` — QSettings (токен, куки, thinking/search, images magic/flux, папка проекта).
