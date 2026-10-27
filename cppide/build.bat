@echo off
chcp 65001 >nul
setlocal
rem === DeepSeekCppIDE: сборка MSVC + Qt6 ===
rem (без ENABLEDELAYEDEXPANSION — иначе съедаются "!" в echo)

rem ---- 1. Qt6 -------------------------------------------------------------
set QTROOT=
if defined QT6_DIR if exist "%QT6_DIR%\lib\cmake\Qt6\Qt6Config.cmake" set QTROOT=%QT6_DIR%
if defined Qt6_DIR if exist "%Qt6_DIR%\lib\cmake\Qt6\Qt6Config.cmake" set QTROOT=%Qt6_DIR%

rem ВНИМАНИЕ: cmd не раскрывает * в середине пути — перебираем поэтажно
for %%R in (C:\Qt D:\Qt E:\Qt F:\Qt "%USERPROFILE%\Qt") do (
  if exist "%%~R" (
    for /d "%%V" in ("%%~R\6.*") do (
      for /d "%%M" in ("%%V\msvc2022_64" "%%V\msvc2019_64" "%%V\mingw_64" "%%V\msvc20*_64") do (
        if not defined QTROOT if exist "%%M\lib\cmake\Qt6\Qt6Config.cmake" set QTROOT=%%M
      )
    )
  )
)

if not defined QTROOT (
  echo [ERR] Qt6 не установлен
  echo.
  echo   Вариант 1 (официальный): qt.io/download-qt-installer-oss
  echo     при установке отметьте: Qt 6.8.x - MSVC 2022 64-bit + Qt 5 Compatibility
  echo   Вариант 2 (консоль):
  echo     pip install aqtinstall
  echo     aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qt5compat --outputdir C:\Qt
  echo   Если Qt стоит в другом месте: set QT6_DIR=путь\к\msvc2022_64 и запустите снова.
  exit /b 1
)
echo [*] Qt6: %QTROOT%

rem ---- 2. MSVC окружение: vcvars64 из Visual Studio -----------------------
set VCVARS=
for %%R in ("F:\Program Files\Microsoft Visual Studio" "C:\Program Files\Microsoft Visual Studio" "C:\Program Files (x86)\Microsoft Visual Studio") do (
  if exist "%%~R" (
    for /d "%%Y" in ("%%~R\*") do (
      if not defined VCVARS (
        if exist "%%Y\Community\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%Y\Community\VC\Auxiliary\Build\vcvars64.bat
        if not defined VCVARS if exist "%%Y\Professional\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%Y\Professional\VC\Auxiliary\Build\vcvars64.bat
        if not defined VCVARS if exist "%%Y\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%Y\Enterprise\VC\Auxiliary\Build\vcvars64.bat
        if not defined VCVARS if exist "%%Y\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%Y\BuildTools\VC\Auxiliary\Build\vcvars64.bat
      )
    )
  )
)
if not defined VCVARS (
  echo [ERR] Не нашёл vcvars64.bat - в VS должен быть workload C++
  exit /b 1
)
echo [*] MSVC: %VCVARS%
call "%VCVARS%" || exit /b 1

rem ---- 3. Инструменты -----------------------------------------------------
where cmake >nul 2>nul || (echo [ERR] cmake не найден: winget install Kitware.CMake или выберите C++ CMake tools в VS Installer && exit /b 1)
where ninja >nul 2>nul && set GEN=Ninja || set GEN=Visual Studio 17 2022
echo [*] Generator: %GEN%

rem ---- 4. Конфигурация + сборка -------------------------------------------
set SRC=%~dp0
set SRC=%SRC:~0,-1%
set BLD=%SRC%\build
if not "%GEN%"=="Ninja" goto nvn
cmake -S "%SRC%" -B "%BLD%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTROOT%" || exit /b 1
cmake --build "%BLD%" --parallel || exit /b 1
set OUT=%BLD%
goto have_out
:nvn
cmake -S "%SRC%" -B "%BLD%" -G "%GEN%" -A x64 -DCMAKE_PREFIX_PATH="%QTROOT%" || exit /b 1
cmake --build "%BLD%" --config Release || exit /b 1
set OUT=%BLD%\Release
:have_out

if not exist "%OUT%\DeepSeekCppIDE.exe" (echo [ERR] exe не собрался && exit /b 1)
echo [*] windeployqt...
"%QTROOT%\bin\windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw "%OUT%\DeepSeekCppIDE.exe" 2>nul
echo.
echo [OK] %OUT%\DeepSeekCppIDE.exe
endlocal
