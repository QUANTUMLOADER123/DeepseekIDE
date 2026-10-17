@echo off
setlocal
set "SRC=%~dp0"
set "DEST=%LOCALAPPDATA%\DeepSeekExtended\shell"
set "NAME=ai.deepseekide.shell"
echo == DeepSeek Extended shell host: install ==

if not exist "%SRC%host.cs" (
  echo ERROR: host.cs not found next to install.bat
  pause
  exit /b 1
)
if not exist "%DEST%" mkdir "%DEST%"

copy /y "%SRC%host.cs" "%DEST%\host.cs" >nul
copy /y "%SRC%host.json" "%DEST%\host.json" >nul

rem --- compile host.exe from host.cs (csc.exe ships with .NET Framework 4.x, present on Win10/11)
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
  echo ERROR: csc.exe not found. Install .NET Framework 4.x or higher.
  pause
  exit /b 1
)
echo Compiling host.exe ...
"%CSC%" /nologo /target:exe /optimize+ /out:"%DEST%\host.exe" "%DEST%\host.cs"
if errorlevel 1 (
  echo ERROR: compilation failed
  pause
  exit /b 1
)
if not exist "%DEST%\host.exe" (
  echo ERROR: host.exe was not produced
  pause
  exit /b 1
)

echo.
set /p PROJ=Project folder absolute path [Enter = %CD%]: 
if "%PROJ%"=="" set "PROJ=%CD%"
(echo %PROJ%)> "%DEST%\project.cwd"

rem --- put the ABSOLUTE path to host.exe into the manifest (UTF-8, NO BOM)
set "MANIFEST=%DEST%\host.json"
powershell -NoProfile -Command "$m='%MANIFEST%'; $j=Get-Content $m -Raw | ConvertFrom-Json; $j.path='%DEST%\host.exe'; $n=$j | ConvertTo-Json -Depth 6; [IO.File]::WriteAllText($m, $n, (New-Object Text.UTF8Encoding($false)))"

rem --- registry: Chrome + Edge
reg add "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NAME%" /ve /t REG_SZ /d "%DEST%\host.json" /f >nul
reg add "HKCU\Software\Microsoft\Edge\NativeMessagingHosts\%NAME%" /ve /t REG_SZ /d "%DEST%\host.json" /f >nul
if errorlevel 1 (
  echo ERROR: registry write failed
  pause
  exit /b 1
)

echo.
echo OK: host installed to %DEST% (host.exe compiled, manifest, Chrome + Edge registry)
echo Running selftest through the same pipeline Chrome uses ...
"%DEST%\host.exe" selftest
if errorlevel 1 (
  echo SELFTEST FAILED - copy the output above and send it to the developer.
  pause
  exit /b 1
)
echo.
echo Restart Chrome COMPLETELY ^(tray icon too^), then reload the chat tab.
pause
