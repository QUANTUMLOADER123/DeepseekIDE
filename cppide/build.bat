@echo off
chcp 65001 >nul
setlocal
rem === DeepSeekIDE: сборка через MSBuild (Visual Studio 2026, .slnx) ===

rem ---- 1. Visual Studio (vcvars64) ---------------------------------------
set VCVARS=
for %%R in ("F:\Program Files\Microsoft Visual Studio" "C:\Program Files\Microsoft Visual Studio" "C:\Program Files (x86)\Microsoft Visual Studio") do (
  if exist "%%~R" (
    for /d %%Y in ("%%~R\*") do (
      if not defined VCVARS (
        if exist "%%~Y\Community\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%~Y\Community\VC\Auxiliary\Build\vcvars64.bat
        if not defined VCVARS if exist "%%~Y\Professional\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%~Y\Professional\VC\Auxiliary\Build\vcvars64.bat
        if not defined VCVARS if exist "%%~Y\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%~Y\Enterprise\VC\Auxiliary\Build\vcvars64.bat
        if not defined VCVARS if exist "%%~Y\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%%~Y\BuildTools\VC\Auxiliary\Build\vcvars64.bat
      )
    )
  )
)
if defined VCVARS goto vc_ok
echo [ERR] Не нашёл vcvars64.bat — в VS должен быть workload C++
exit /b 1
:vc_ok
echo [*] MSVC %VCVARS%
call "%VCVARS%" >nul 2>nul
if errorlevel 1 exit /b 1

rem ---- 2. Qt6 --------------------------------------------------------------
set QTROOT=
if defined QT6_DIR if exist "%QT6_DIR%\bin" set QTROOT=%QT6_DIR%
if defined Qt6_DIR if exist "%Qt6_DIR%\bin" set QTROOT=%Qt6_DIR%
for %%R in (C:\Qt D:\Qt E:\Qt F:\Qt "%USERPROFILE%\Qt") do (
  if exist "%%~R" (
    for /d %%V in ("%%~R\6.*") do (
      for /d %%M in ("%%~V\msvc2022_64" "%%~V\msvc2019_64" "%%~V\msvc20*_64") do (
        if not defined QTROOT if exist "%%~M\bin\moc.exe" set QTROOT=%%~M
      )
    )
  )
)
if defined QTROOT goto qt_ok
echo [ERR] Qt6 не найден — поставьте Qt 6.8 msvc2022_64
exit /b 1
:qt_ok
echo [*] Qt6 %QTROOT%

rem ---- 3. MSBuild -----------------------------------------------
set MSBUILD=
for %%R in ("F:\Program Files\Microsoft Visual Studio" "C:\Program Files\Microsoft Visual Studio" "C:\Program Files (x86)\Microsoft Visual Studio") do (
  if exist "%%~R" (
    for /d %%Y in ("%%~R\*") do (
      if not defined MSBUILD (
        if exist "%%~Y\Community\MSBuild\Current\Bin\MSBuild.exe" set MSBUILD=%%~Y\Community\MSBuild\Current\Bin\MSBuild.exe
        if not defined MSBUILD if exist "%%~Y\Professional\MSBuild\Current\Bin\MSBuild.exe" set MSBUILD=%%~Y\Professional\MSBuild\Current\Bin\MSBuild.exe
        if not defined MSBUILD if exist "%%~Y\Enterprise\MSBuild\Current\Bin\MSBuild.exe" set MSBUILD=%%~Y\Enterprise\MSBuild\Current\Bin\MSBuild.exe
        if not defined MSBUILD if exist "%%~Y\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set MSBUILD=%%~Y\BuildTools\MSBuild\Current\Bin\MSBuild.exe
      )
    )
  )
)
if defined MSBUILD goto ms_ok
echo [ERR] MSBuild.exe не найден
exit /b 1
:ms_ok
echo [*] MSBuild %MSBUILD%

"%MSBUILD%" "%~dp0DeepSeekIDE.vcxproj" /p:Configuration=Release /p:Platform=x64 /m -v:minimal
if errorlevel 1 exit /b 1

if exist "%~dp0out\Release\DeepSeekIDE.exe" goto copied
echo [ERR] exe не появился
exit /b 1
:copied
echo [*] Собрано: %~dp0out\Release\DeepSeekIDE.exe
endlocal
