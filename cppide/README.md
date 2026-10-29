# DeepSeekIDE (cppide)

Нативная C++/Qt6-десктопная IDE в стиле Claude Code: слева — лента сессий, в центре — чат с агентом; проводника нет — агент сам ходит по файлам проекта.

## Что умеет

- **Чат с DeepSeek напрямую** — без браузера: chat session, PoW через встроенный wasm3, SSE-стриминг.
- **Сессии** — сохраняются в AppData (`sessions.json`): название, папка проекта, контекст, история чата. Кнопка «+ Новая сессия», переключение кликом.
- **Папка проекта — у каждой сессии своя** (чип справа от режимов).
- **Режимы-чипы** (DeepThink / Поиск / Картинки) прямо у поля ввода.
- **Скверкл-карточки** (superellipse-сглаживание углов), градиентные пузырьки юзера, тени, fade-in анимации, пульсирующая рамка при стриме, плавный автопрокрут.
- **Маркеры агента** (строго **один** инструмент на ответ):
  `NEED FILE / FILE INFO / READ RANGE / NEED SEARCH / WEB FETCH / WEB DOWNLOAD / WEB GREP / SHELL (проект) / SANDBOX SHELL (минипесочница) / SITE VIEW (мини-браузер html) / ASK / IMAGE SHOW / IMAGE FILE / MEMORY`.
- **Правки файлов** — блок ```deepseekide-ops``` или голый JSON-массив: write_file / edit_file / append_file / insert_lines / replace_lines / make_dir / delete_path / copy_file / move_file.
- **Мини-песочница** — отдельная папка `AppData\...\sandbox`, кнопка «Песочница» слева внизу открывает её в Проводнике.
- **SITE VIEW** — встроенный мини-браузер для предпросмотра страниц.
- Лог по F10.

## Сборка (Visual Studio 2026)

1. Зависимости вендорены (wasm3 в `wasm3/`) — нужен только установленный **Qt 6.8 msvc2022_64** (+qt5compat) и VS с C++ workload.
2. Откройте `DeepSeekIDE.slnx` в VS 2026 *или* просто запустите `build.bat`:
   ```bat
   build.bat        :: найдёт vcvars, Qt и MSBuild сам, соберёт Release → out\Release\DeepSeekIDE.exe
   ```
3. Итог: `out\Release\DeepSeekIDE.exe` + windeployqt + assets\logo.png. 

## Получение токена

1. chat.deepseek.com → войти → F12 → Console: `localStorage.getItem('userToken') || localStorage.getItem('token')` → вставить в «Настройки → DeepSeek token».
2. Cookies (по желанию): Application → Cookies → `ds_session_id=…; smidV2=…` → поле «Cookie».

> Прямой доступ к веб-API — неофициальный; при ошибках 401/403 возьмите свежие токен/куки из браузера.

## Структура

- `src/main.cpp` — запуск, тёмный тайтлбар (DWM).
- `src/mainwindow.*` — Claude-подобный UI: сессии, чат, чипы, композер.
- `src/squirclecard.h` — отрисовка superellipse-карточек (no border-radius).
- `src/deepseekclient.*` — прямой Web API-клиент DeepSeek (SSE).
- `src/pow.*` + `wasm3/` — PoW-челлендж (вендоренный wasm3 MIT).
- `src/agent.*` — маркеры/ops, инструменты, авто-continue.
- `src/sessionstore.*` — сессии в JSON.
- `src/sitepreview.*` — мини-браузер SITE VIEW.
- `src/settings.*` — настройки (QSettings).
