# DeepSeekIDE

IDE-ускоритель для DeepSeek **без API-ключей**: агент работает через обычный веб-чат
[chat.deepseek.com](https://chat.deepseek.com), а среда разработки открывается прямо
в вашем браузере. Бесплатно: используется ваш существующий логин DeepSeek (через Google и т.п.).

## Как это устроено

```
deepseekide.exe  = локальный HTTP-сервер (127.0.0.1, порт выбирается автоматически)
   ├─ /           → фронтенд IDE:      assets/web (HTML/CSS/JS + Monaco Editor)
   └─ /api/...     → проект, файлы, снапшоты, журнал, мост к агенту
Отдельно запускается Chrome/Edge с отладочным портом (CDP):
   вкладка chat.deepseek.com — туда IDE отправляет задачи и читает ответы.
```

Никакого WebView2/ImGui — только ваш собственный браузер. Поэтому работает везде,
где есть Chrome или Edge.

## Сборка (Windows)

1. Visual Studio 2022+ (компонент *Desktop development with C++*), CMake 3.24+ в PATH.
2. Дважды щёлкните **`build-windows.bat`**. Получите `deepseekide.exe` в корне папки.
3. Запустите `deepseekide.exe` → в браузере откроется IDE.

## Сборка вручную (любая ОС)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j          # deepseekide + deepseekide_tests
```

## Использование

1. **Откройте проект** (слева «Открыть…», укажите папку).
2. **«Подключить чат»** — запустится Chrome/Edge с вкладкой `chat.deepseek.com`.
   Войдите в свой аккаунт DeepSeek **один раз** (сессия сохраняется в отдельном
   профиле браузера `%APPDATA%/DeepSeekIDE/browser-profile`).
3. Сформулируйте **задачу** справа → Enter/Ctrl+Enter/«Отправить задачу».
   Индикатор покажет: «отправлено» → «печатает…» → «ответ получен».
4. В блоке «Ответ агента» появятся **операции с файлами** (`write_file`,
   `edit_file`, `insert_lines`, `replace_lines`, `make_dir`, `delete_path`,
   `copy_file`, `move_file`, …). Выберите нужные чекбоксами → **«Применить»**.
5. Каждое применение создаёт **снапшот** — в любой момент можно
   **«откатить к этому»** из панели «Снапшоты».
6. Если агенту нужно содержимое файла — ответьте заметкой с текстом файла
   (кнопка «Заметка»).

## Безопасность

- Сервер слушает только `127.0.0.1`, все `/api/*` закрыты случайным токеном
  (передаётся в URL при автостарте и хранится в sessionStorage вкладки).
- Путь в API-файлах проверяется: нельзя выйти за пределы проекта.
- Браузера агента — обычный Chrome/Edge, в который вы сами вошли; мы только
  нажимаем кнопку «отправить» и читаем последний ответ.

## Положение файлов

| Что             | Где                                                  |
|-----------------|------------------------------------------------------|
| Серверный бэкенд | `src/server/IdeServer.*`, `src/main.cpp`             |
| Агент (CDP)      | `src/chat/ChatDriver.*`, `src/cdp/CdpClient.*`, `src/ws/WsClient.*` |
| Инструменты      | `src/ai/ToolRegistry.*`, `src/ai/OpsParser.*`        |
| Ядро             | `src/core/ProjectManager.*`, `src/core/SnapshotManager.*` |
| HTTP-клиент      | `src/net/HttpClient.*` (на всемогущем cpp-httplib)   |
| Фронтенд         | `assets/web/` (index.html, app.css, app.js)          |
| Юнит-тесты       | `src/tests/selftest.cpp` → `deepseekide_tests`       |
