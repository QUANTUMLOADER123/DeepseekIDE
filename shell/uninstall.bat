@echo off
setlocal
set "NAME=ai.deepseekide.shell"
set "DEST=%LOCALAPPDATA%\DeepSeekExtended\shell"
echo == DeepSeek Extended shell host: uninstall ==
reg delete "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NAME%" /f >nul 2>nul
if exist "%DEST%" rmdir /s /q "%DEST%"
echo Done: registry key removed, files deleted.
pause
