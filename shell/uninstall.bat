@echo off
set "NAME=ai.deepseekide.shell"
reg delete "HKCU\Software\Google\Chrome\NativeMessagingHosts\%NAME%" /f >nul 2>nul
reg delete "HKCU\Software\Microsoft\Edge\NativeMessagingHosts\%NAME%" /f >nul 2>nul
rmdir /s /q "%LOCALAPPDATA%\DeepSeekExtended\shell" >nul 2>nul
echo OK: shell host removed (registry Chrome+Edge, files in LOCALAPPDATA).
echo Restart Chrome for the change to take effect.
pause
