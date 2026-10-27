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

## Сборка (Windows, MSVC 2022 + Qt 6.8)

1. Установите Qt 6.8 (компоненты: MSVC 2022 64-bit + **Qt 5 Compatibility** — для CP866):
   ```bat
   aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -m qt5compat
   ```
   или Qt Online Installer: Qt → 6.8.x → MSVC 2022 64-bit, CMake, Ninja.

2. Соберите:
   ```bat
   build.bat
   :: или вручную:
   cmake -S cppide -B cppide/build -G "Visual Studio 17 2022" -A x64 ^
         -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
   cmake --build cppide/build --config Release
   ```

3. Запустите `cppide/build/Release/DeepSeekCppIDE.exe`
   (Qt-рантаймы — через `windeployqt`).

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
