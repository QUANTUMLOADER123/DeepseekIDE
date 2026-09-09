@echo off
rem ============================================================
rem  DeepSeekIDE - build for Windows (Visual Studio 2022)
rem
rem  Cyrillic REMOVED on purpose: cmd.exe parses .bat in OEM cp
rem  and UTF-8 Russian text breaks parsing (that is the crash
rem  you just saw). All messages here are ASCII-only.
rem
rem  Result: deepseekide.exe in this folder (next to assets\).
rem  Just run it - the IDE opens in your browser.
rem ============================================================
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo [ERROR] cmake not found in PATH.
  echo Install CMake 3.24+ from https://cmake.org/download/ and tick
  echo "Add CMake to system PATH".
  pause
  exit /b 1
)

echo [1/3] CMake configure...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto fail

echo [2/3] Build Release...
cmake --build build --config Release
if errorlevel 1 goto fail

echo [3/3] Core selftests...
"build\Release\deepseekide_tests.exe"
if errorlevel 1 goto fail

echo.
echo ============================================================
echo  OK! Run deepseekide.exe in this folder.
echo  IDE opens in your browser; the agent chat opens in a
echo  separate Chrome/Edge window (button "Connect chat").
echo ============================================================
exit /b 0

:fail
echo.
echo [ERROR] Build failed. Send the text above to the developer.
pause
exit /b 1
