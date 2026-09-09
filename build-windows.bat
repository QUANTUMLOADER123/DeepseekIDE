@echo off
chcp 65001 >nul
rem ============================================================
rem  DeepSeekIDE - сборка в 1 клик для Windows (Visual Studio)
rem
rem  Что делает:
rem   1. Создаёт build\DeepSeekIDE.sln (проект Visual Studio)
rem   2. Собирает build\Release\deepseekide.exe
rem
rem  GIT НЕ НУЖЕН - зависимости качаются zip-архивами.
rem
rem  Нужно установленным:
rem   - Visual Studio 2022+ с компонентом "Разработка классических
rem     приложений на C++" (Desktop development with C++)
rem   - CMake 3.24+  ->  https://cmake.org/download/
rem     (при установке отметить "Add CMake to system PATH")
rem
rem  Если сборка падает на окне chat.deepseek.com (WebView2) -
rem  соберите без него:   build-windows.bat -DDEEPSEEKIDE_ENABLE_WEBVIEW=OFF
rem ============================================================
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo [ОШИБКА] CMake не найден в PATH.
  echo Скачайте: https://cmake.org/download/ и при установке отметьте "Add CMake to system PATH".
  pause
  exit /b 1
)

echo [1/2] Генерация проекта Visual Studio (build\DeepSeekIDE.sln)...
echo      (при первом запуске CMake скачает зависимости - это нормально, 1-3 минуты)
cmake -B build -DCMAKE_BUILD_TYPE=Release %*
if errorlevel 1 goto fail

echo.
echo [2/2] Сборка Release...
cmake --build build --config Release --parallel
if errorlevel 1 goto fail

echo.
echo ============================================================
echo   ГОТОВО!
echo   Запуск:     build\Release\deepseekide.exe
echo   Для Visual Studio: откройте build\DeepSeekIDE.sln
echo   (внутри VS можно кодить и собирать как обычный проект)
echo ============================================================
pause
exit /b 0

:fail
echo.
echo [ОШИБКА] Сборка не удалась - читайте сообщения выше.
echo Совет 1: удалите папку build (rmdir /s /q build) и запустите снова.
echo Совет 2: попробуйте  build-windows.bat -DDEEPSEEKIDE_ENABLE_WEBVIEW=OFF
pause
exit /b 1
