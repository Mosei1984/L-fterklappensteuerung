@echo off
setlocal
set "SCRIPT=%~dp0setup-windows.ps1"
if not exist "%SCRIPT%" (
  echo setup-windows.ps1 wurde nicht neben dieser Setup-Datei gefunden.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
  echo.
  echo Setup wurde mit Fehlercode %RESULT% beendet.
  pause
)
exit /b %RESULT%
