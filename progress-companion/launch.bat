@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM Always prefer absolute Windows launchers. The extensionless Hermes `pnpm`
REM shim is a Unix shell script; when cmd runs it, basedir becomes "." and
REM Corepack looks under this folder (and fails).

set "HERMES_NODE=%LOCALAPPDATA%\hermes\node"
set "PNPM_CMD="
set "NODE_EXE="

if exist "%HERMES_NODE%\pnpm.CMD" set "PNPM_CMD=%HERMES_NODE%\pnpm.CMD"
if exist "%HERMES_NODE%\node.exe" set "NODE_EXE=%HERMES_NODE%\node.exe"

if not defined PNPM_CMD (
  for /f "delims=" %%i in ('where pnpm.cmd 2^>nul') do (
    set "PNPM_CMD=%%i"
    goto :found_pnpm
  )
)
:found_pnpm

if not defined PNPM_CMD if defined NODE_EXE if exist "%HERMES_NODE%\node_modules\corepack\dist\pnpm.js" (
  set "PNPM_JS=%HERMES_NODE%\node_modules\corepack\dist\pnpm.js"
)

if not defined PNPM_CMD if not defined PNPM_JS (
  echo [taiko-stats] Could not find pnpm.CMD.
  echo.
  echo Tried:
  echo   %HERMES_NODE%\pnpm.CMD
  echo   where pnpm.cmd
  echo.
  echo Install Node/pnpm, or open a terminal where `pnpm -v` works and run:
  echo   pnpm electron:dev
  echo.
  pause
  exit /b 1
)

REM Ensure Hermes node wins over other PATHs for child processes.
if exist "%HERMES_NODE%\node.exe" set "PATH=%HERMES_NODE%;%PATH%"

REM Free leftover API / Vite ports from a previous launch (EADDRINUSE).
echo [taiko-stats] clearing ports 3789 / 5173...
call :killport 3789
call :killport 5173

echo [taiko-stats] launching from:
echo   %CD%
echo [taiko-stats] close the app window to stop.
echo.

if defined PNPM_CMD (
  call "%PNPM_CMD%" electron:dev
) else (
  "%NODE_EXE%" "%PNPM_JS%" electron:dev
)
set "EXITCODE=%ERRORLEVEL%"

if not "%EXITCODE%"=="0" (
  echo.
  echo [taiko-stats] exited with code %EXITCODE%
  pause
)

exit /b %EXITCODE%

:killport
set "PORT=%~1"
for /f "tokens=5" %%a in ('netstat -ano ^| findstr /R /C:":%PORT% .*LISTENING"') do (
  if not "%%a"=="0" taskkill /F /PID %%a >nul 2>&1
)
exit /b 0
