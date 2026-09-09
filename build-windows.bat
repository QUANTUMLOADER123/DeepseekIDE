@echo off
chcp 65001 >nul
rem ============================================================
rem  DeepSeekIDE — сборка в 1 клик для Windows (Visual Studio)
rem
rem  НИКАКИХ внешних зависимостей: ни vcpkg, ни curl, ни WebView2.
rem  Всё нужное уже внутри репозитория (cpp-httplib + nlohmann_json
rem  докачивается с github один раз при настройке cmake).
rem
rem  Нужно установленным:
rem   - Visual Studio 2022+ с компонентом "Desktop development with C++"
rem   - CMake 3.24+  ->  https://cmake.org/download/
rem     (при установке отметить "Add CMake to system PATH")
rem
rem  Результат: deepseekide.exe в КОРНЕ папки (рядом с assets\).
rem  Просто запустите его — откроется IDE в вашем браузере.
rem ============================================================
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo [ОШИБКА] cmake не найден в PATH. Установите CMake с https://cmake.org/download/
  pause
  exit /b 1
)

echo [1/3] Настройка CMake...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto fail

echo [2/3] Сборка Release...
cmake --build build --config Release
if errorlevel 1 goto fail

echo [3/3] Самотесты ядра...
build\Release\deepseekide_tests.exe
if errorlevel 1 goto fail

echo.
echo ============================================================
echo  Готово! Запускайте deepseekide.exe в корне этой папки.
echo  IDE откроется в вашем браузере, а chat.deepseek.com -
echo  в отдельном окне Chrome/Edge (кнопка "Подключить чат").
echo ============================================================
exit /b 0

:fail
echo.
echo [ОШИБКА] Сборка не прошла. Пришлите текст выше разработчику.
pause
exit /b 1
