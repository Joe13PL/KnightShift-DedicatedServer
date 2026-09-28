@echo off
rem KnightShift dedicated RPG server - start (Windows). Self-contained: the game data and
rem settings live in this folder. No install, no admin, no Steam client needed.
setlocal
title KnightShift RPG Server

rem This folder (without the trailing backslash).
set "GAME=%~dp0"
if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"

rem Point the game at this folder's data (per-user, no admin). Harmless to a Steam copy:
rem it uses a different registry root; stop.bat removes these keys.
reg add "HKCU\SOFTWARE\Reality Pump\KnightShift" /v version /t REG_SZ /d "1.3" /f >nul
reg add "HKCU\SOFTWARE\Reality Pump\KnightShift\BaseGame\FileSystem" /v datapath  /t REG_SZ /d "%GAME%/>" /f >nul
reg add "HKCU\SOFTWARE\Reality Pump\KnightShift\BaseGame\FileSystem" /v outputdir /t REG_SZ /d "%GAME%"    /f >nul
reg add "HKCU\SOFTWARE\Reality Pump\KnightShift\BaseGame\Graphics\Default" /v EngineType /t REG_DWORD /d 0 /f >nul
reg add "HKCU\SOFTWARE\Reality Pump\KnightShift\BaseGame\Graphics\Default" /v FullScreen /t REG_DWORD /d 0 /f >nul

rem CD key: put YOUR legal key in ksnetfix.ini, line "CdKey=..." in the [Server] section.
rem It is entered automatically the first time the game runs on a fresh machine.

echo Starting KnightShift RPG server...
echo A console window opens with the live log and commands (help, status, quit).
echo.
rem Launch through KnightShift.exe: Windows does not treat ".ex1" as a program, so it must
rem be started by the game's own launcher, which picks the D3D8 engine (EngineType=0 above).
start "" "%GAME%\KnightShift.exe"
