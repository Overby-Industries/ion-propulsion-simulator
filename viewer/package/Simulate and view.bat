@echo off
rem Runs abep_run.exe with the settings in settings.txt, then shows the result in the orbit view.
rem (Set ABEP_NO_VIEWER=1 to run the simulation and stop there: the release script tests this launcher that way.)
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

rem Each line of settings.txt that does not start with # is a setting, name=value; only its first word is used, so a note may follow.
set "SETTINGS="
if exist "settings.txt" (
    for /f "usebackq eol=# tokens=1 delims= " %%L in ("settings.txt") do call :add "%%L"
)

echo Running: abep_run.exe%SETTINGS%
"%~dp0abep_run.exe"%SETTINGS% out="%~dp0my_run"
if errorlevel 1 (
    echo.
    echo abep_run.exe stopped with an error. The message above says which setting it did not like; settings.txt is where to fix it.
    pause
    exit /b 1
)
if defined ABEP_NO_VIEWER exit /b 0
start "" "%~dp0ABEPOrbitView.exe" -- "%~dp0my_run"
exit /b 0

:add
set "SETTINGS=%SETTINGS% %~1"
exit /b 0
