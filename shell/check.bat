@echo off
rem Self-test of the DeepSeek Extended shell host (native messaging)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check.ps1"
pause
