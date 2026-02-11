@echo off
rem Windows 7 Compatible Wrapper for debug.ps1
rem Bypasses PowerShell Execution Policy automatically

set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%debug.ps1"

rem Check if PowerShell is available
where powershell >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo Error: PowerShell is not found in PATH.
    exit /b 1
)

rem Invoke PowerShell with Bypass policy
set "PS_ARGS="
if "%~1"=="run" (
    set "PS_ARGS=-Run"
    shift
)

:arg_loop
if "%~1"=="" goto arg_end
set "PS_ARGS=%PS_ARGS% %1"
shift
goto arg_loop

:arg_end
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %PS_ARGS% -Password Shanghaith8 -ForegroundGdbserver

if %ERRORLEVEL% NEQ 0 (
    exit /b %ERRORLEVEL%
)
