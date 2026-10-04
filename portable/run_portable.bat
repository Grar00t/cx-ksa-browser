@echo off
setlocal

set "CX_PORTABLE_ROOT=%~dp0"
set "APPDATA=%CX_PORTABLE_ROOT%data\Roaming"
set "LOCALAPPDATA=%CX_PORTABLE_ROOT%data\Local"

if not exist "%APPDATA%" mkdir "%APPDATA%"
if not exist "%LOCALAPPDATA%" mkdir "%LOCALAPPDATA%"

if not exist "%CX_PORTABLE_ROOT%cx.exe" (
  echo CX Build portable launcher could not find cx.exe.
  exit /b 2
)

start "" /wait "%CX_PORTABLE_ROOT%cx.exe"
exit /b %errorlevel%
