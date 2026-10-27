@echo off
setlocal ENABLEDELAYEDEXPANSION
rem === DeepSeekCppIDE: MSVC 2022 + Qt6 ===

set QTROOT=C:\Qt\6.8.0\msvc2022_64
if defined QT6_DIR set QTROOT=%QT6_DIR%
if defined Qt6_DIR set QTROOT=%Qt6_DIR%
if not exist "%QTROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
  for /d %%D in (C:\Qt\6.*\msvc2022_64 C:\Qt\*msvc2022_64 D:\Qt\6.*\msvc2022_64) do (
    if exist "%%D\lib\cmake\Qt6\Qt6Config.cmake" set QTROOT=%%D
  )
)
if not exist "%QTROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
  echo [ERR] Qt6 не найдено. Задайте переменную окружения QT6_DIR=путь\к\msvc2022_64 или установите Qt в C:\Qt\6.x\msvc2022_64
  exit /b 1
)
echo [*] Qt6: %QTROOT%

where cmake >nul 2>nul || (echo [ERR] cmake не найден в PATH && exit /b 1)

set SRC=%~dp0
set BLD=%SRC%build
cmake -S "%SRC%" -B "%BLD%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QTROOT%" || exit /b 1
cmake --build "%BLD%" --config Release || exit /b 1

if not exist "%BLD%\Release\DeepSeekCppIDE.exe" (echo [ERR] exe не вышел && exit /b 1)
echo [*] deploy Qt…
"%QTROOT%\bin\windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw "%BLD%\Release\DeepSeekCppIDE.exe" 2>nul
echo [OK] %BLD%\Release\DeepSeekCppIDE.exe
endlocal
