@echo off
rem ============================================================
rem  DeepSeekIDE - build for Windows (Visual Studio C++ toolchain)
rem
rem  This script is ASCII-only and CRLF-terminated on purpose:
rem  cmd.exe parses .bat files in the OEM codepage, so UTF-8
rem  Cyrillic or LF endings break parsing.
rem
rem  Generator is AUTO-DETECTED via vswhere (VS 2022 / 2019 /
rem  Build Tools). No VS at all (or you just do not want to build)?
rem  Download the ready-made exe from GitHub Actions:
rem  https://github.com/QUANTUMLOADER123/DeepseekIDE/actions
rem ============================================================
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo [ERROR] cmake not found in PATH.
  echo Install CMake 3.24+ from https://cmake.org/download/ and tick
  echo "Add CMake to system PATH", or grab the prebuilt exe from
  echo https://github.com/QUANTUMLOADER123/DeepseekIDE/actions
  pause
  exit /b 1
)

rem ---------- auto-detect Visual Studio generator ----------
set "GEN="
set "NMAKE=0"
set "VSV="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%v in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2^>nul`) do set "VSV=%%v"
)

if defined VSV (
  echo %VSV% | findstr /b "18." >nul && set "GEN=Visual Studio 18 2026"
  echo %VSV% | findstr /b "17." >nul && set "GEN=Visual Studio 17 2022"
  echo %VSV% | findstr /b "16." >nul && set "GEN=Visual Studio 16 2019"
)

if not defined GEN (
  where cl >nul 2>nul
  if not errorlevel 1 set "GEN=NMake Makefiles" && set "NMAKE=1"
)
if not defined GEN (
  set "GEN=@auto"
)

if not defined GEN (
  echo [ERROR] No Visual Studio C++ toolchain found.
  echo.
  echo Easiest fix - download the prebuilt exe (no build needed):
  echo   https://github.com/QUANTUMLOADER123/DeepseekIDE/actions
  echo.
  echo Or install free Build Tools for Visual Studio 2022:
  echo   https://aka.ms/vs/17/release/vs_BuildTools.exe
  echo During install, tick "Desktop development with C++".
  pause
  exit /b 1
)

echo Using generator: %GEN%

echo [1/3] CMake configure...
if "%NMAKE%"=="1" (
  cmake -S . -B build -G "%GEN%" -DCMAKE_BUILD_TYPE=Release
) else if "%GEN%"=="@auto" (
  rem let cmake choose the newest installed Visual Studio by itself
  cmake -S . -B build -A x64
) else (
  cmake -S . -B build -G "%GEN%" -A x64
)
if errorlevel 1 goto fail

echo [2/3] Build Release...
cmake --build build --config Release
if errorlevel 1 goto fail

echo [3/3] Core selftests...
set "TESTS="
if exist "build\Release\deepseekide_tests.exe" set "TESTS=build\Release\deepseekide_tests.exe"
if exist "build\deepseekide_tests.exe" set "TESTS=build\deepseekide_tests.exe"
if not defined TESTS (
  echo [ERROR] deepseekide_tests.exe not found after build
  goto fail
)
"%TESTS%"
if errorlevel 1 goto fail

echo.
echo ============================================================
echo  OK! Run deepseekide.exe in this folder.
echo  The IDE opens in your browser; the agent chat opens in a
echo  separate Chrome/Edge window (button "Connect chat").
echo ============================================================
exit /b 0

:fail
echo.
echo [ERROR] Build failed. Send the text above to the developer.
echo Tip: prebuilt binaries live at
echo   https://github.com/QUANTUMLOADER123/DeepseekIDE/actions
pause
exit /b 1
