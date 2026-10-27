@echo off
chcp 65001 >nul
setlocal ENABLEDELAYEDEXPANSION
rem === DeepSeekCppIDE: сборка MSVC + Qt6 (Ninja) ===

rem ---- 1. Qt6 -------------------------------------------------------------
set QTROOT=
if defined QT6_DIR if exist "%QT6_DIR%\lib\cmake\Qt6\Qt6Config.cmake" set QTROOT=%QT6_DIR%
if defined Qt6_DIR if exist "%Qt6_DIR%\lib\cmake\Qt6\Qt6Config.cmake" set QTROOT=%Qt6_DIR%
if not defined QTROOT (
  for /d %%D in (C:\Qt\*\msvc2022_64 C:\Qt\*\msvc2019_64 D:\Qt\*\msvc2022_64 D:\Qt\*\msvc2019_64 E:\Qt\*\msvc2022_64 F:\Qt\*\msvc2022_64 "%USERPROFILE%\Qt\*\msvc2022_64") do (
    if exist "%%D\lib\cmake\Qt6\Qt6Config.cmake" set QTROOT=%%D
  )
)
if not defined QTROOT (
  echo [ERR] Qt6 не установлен!
  echo.
  echo   Вариант 1 ^(официальный^): qt.io/download-qt-installer-oss
  echo     при установке отметьте: Qt 6.8.x -^> MSVC 2022 64-bit + Qt 5 Compatibility
  echo   Вариант 2 ^(консоль^):
  echo     pip install aqtinstall
  echo     aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qt5compat --outputdir C:\Qt
  echo   Если Qt стоит в другом месте: set QT6_DIR=путь\к\msvc2022_64  и запустите снова.
  exit /b 1
)
echo [*] Qt6: %QTROOT%

rem ---- 2. MSVC окружение: vcvars64 из вашей Visual Studio ------------------
set VCVARS=
for /d %%V in ("F:\Program Files\Microsoft Visual Studio\*\*" "C:\Program Files\Microsoft Visual Studio\*\*" "C:\Program Files (x86)\Microsoft Visual Studio\*\*") do (
  if exist "%%V\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%V\VC\Auxiliary\Build\vcvars64.bat
)
if not defined VCVARS (
  echo [ERR] Не нашёл vcvars64.bat — Visual Studio точно стоит с workload C++?
  exit /b 1
)
echo [*] MSVC: %VCVARS%
call "%VCVARS%" || exit /b 1

rem ---- 3. Инструменты -----------------------------------------------------
where cmake >nul 2>nul || (echo [ERR] cmake не найден. PATH-уйдёт после VS-составляющей; иначе: winget install Kitware.CMake && exit /b 1)
where ninja >nul 2>nul && set GEN=Ninja || set GEN=
if not defined GEN (
  echo [!] ninja не найден — пробую генератор Visual Studio 2026
  set GEN=Visual Studio 18 2026
)

rem ---- 4. Конфигурация + сборка -------------------------------------------
set SRC=%~dp0
set SRC=%SRC:~0,-1%
set BLD=%SRC%\build
cmake -S "%SRC%" -B "%BLD%" -G "%GEN%" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTROOT%" || exit /b 1
if /I "%GEN%"=="Ninja" (
  cmake --build "%BLD%" --parallel || exit /b 1
  set OUT=%BLD%
) else (
  cmake --build "%BLD%" --config Release || exit /b 1
  set OUT=%BLD%\Release
)

if not exist "%OUT%\DeepSeekCppIDE.exe" (echo [ERR] exe не собрался && exit /b 1)
echo [*] windeployqt…
"%QTROOT%\bin\windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw "%OUT%\DeepSeekCppIDE.exe" 2>nul
echo.
echo [OK] %OUT%\DeepSeekCppIDE.exe
endlocal
