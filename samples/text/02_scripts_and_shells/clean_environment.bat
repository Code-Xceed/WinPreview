@echo off
setlocal enabledelayedexpansion

echo [Clean Script] Purging intermediate test artifacts...
if exist "%~dp0..\scratch	emp_*" del /q "%~dp0..\scratch	emp_*"
if exist "%~dp0..\*.tmp" del /q "%~dp0..\*.tmp"

echo [Clean Script] Environment is pristine.
exit /b 0
