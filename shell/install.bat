@echo off
setlocal
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
copy /y "%SRC%check.ps1" "%DEST%\check.ps1" >nul
copy /y "%SRC%check.bat" "%DEST%\check.bat" >nul
set /p PROJ=Project folder absolute path [Enter = %CD%]: 
if "%PROJ%"=="" set "PROJ=%CD%"
(echo %PROJ%)> "%DEST%\project.cwd"
rem rebuild the installed manifest: absolute host.bat path, UTF-8 WITHOUT BOM
set "MANIFEST=%DEST%\host.json"
powershell -NoProfile -Command "$m='%MANIFEST%'; $j=Get-Content $m -Raw | ConvertFrom-Json; $j.path='%DEST%\host.bat'; $n=$j | ConvertTo-Json -Depth 6; [IO.File]::WriteAllText($m, $n, (New-Object Text.UTF8Encoding($false)))" >nul
reg add "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NAME%" /ve /t REG_SZ /d "%DEST%\host.json" /f >nul
if errorlevel 1 (
  echo ERROR: registry write failed
  pause
  exit /b 1
)
echo OK: host installed to %DEST%, manifest written without BOM, registry key set.
powershell -NoProfile -ExecutionPolicy Bypass -File "%DEST%\check.ps1" -ExecFile "%DEST%\host.bat"
echo Restart Chrome COMPLETELY ^(close tray icon too^), then reload the chat tab.
pause
