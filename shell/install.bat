@echo off
setlocal EnableDelayedExpansion
set "SRC=%~dp0"
set "DEST=%LOCALAPPDATA%\DeepSeekExtended\shell"
set "NAME=ai.deepseekide.shell"
echo == DeepSeek Extended shell host: install ==
if not exist "%SRC%host.ps1" (
  echo ERROR: host.ps1 not found next to install.bat
  pause
  exit /b 1
)
if not exist "%DEST%" mkdir "%DEST%"
copy /y "%SRC%host.ps1" "%DEST%\host.ps1" >nul
copy /y "%SRC%host.bat" "%DEST%\host.bat" >nul
copy /y "%SRC%host.json" "%DEST%\host.json" >nul
set /p PROJ=Project folder absolute path [Enter = %CD%]: 
if "%PROJ%"=="" set "PROJ=%CD%"
(echo %PROJ%)> "%DEST%\project.cwd"
rem absolute host path must land in the manifest
set "MANIFEST=%DEST%\host.json"
powershell -NoProfile -Command "$j = Get-Content '%MANIFEST%' -Raw | ConvertFrom-Json; $j.path = '%DEST%\host.bat' -replace '\\','\\'; $j | ConvertTo-Json | Set-Content '%MANIFEST%' -Encoding UTF8"
reg add "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NAME%" /ve /t REG_SZ /d "%DEST%\host.json" /f >nul
if errorlevel 1 (
  echo ERROR: registry write failed
  pause
  exit /b 1
)
echo OK: host installed to %DEST%, registry key written.
echo Restart Chrome for the change to take effect.
pause
