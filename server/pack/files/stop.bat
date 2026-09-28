@echo off
rem Stops the server and removes the per-user registry keys that start.bat added.
setlocal
taskkill /im KnightShift.ex1 /t >nul 2>&1
reg delete "HKCU\SOFTWARE\Reality Pump\KnightShift" /f >nul 2>&1
echo Server stopped and its registry keys removed.
