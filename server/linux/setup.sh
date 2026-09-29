#!/usr/bin/env bash
# One-time Wine prefix setup for the KnightShift dedicated server on Linux.
#
#   ./setup.sh /path/to/game
#
# /path/to/game holds YOUR OWN copy of the game plus the server build:
#     KnightShift.exe  KnightShift.ex1  KnightShift.ex2  ijl10.dll
#     dinput8.dll  steam_api.dll  ksnetfix.ini  d3d8enum.exe    (build/ of this repo)
#     WDFiles/ ...  (the game data; the slim server set is enough - see README)
#
# Needs (see README): Wine with 32-bit support (WineHQ >= 9 recommended), winetricks, Xvfb,
# 32-bit OpenGL/EGL (Mesa). Nothing here downloads game files.
set -euo pipefail

GAMEDIR="${1:-}"
if [[ -z "$GAMEDIR" || ! -f "$GAMEDIR/KnightShift.ex1" ]]; then
    echo "usage: ./setup.sh /path/to/game   (must contain KnightShift.ex1)" >&2
    exit 1
fi
GAMEDIR="$(cd "$GAMEDIR" && pwd)"
export WINEPREFIX="${WINEPREFIX:-$HOME/.knightshift-server}"
export WINEARCH=win32          # 32-bit game -> 32-bit prefix (keys land under SOFTWARE\Reality Pump)
export WINEDEBUG="${WINEDEBUG:--all}"

for tool in wine winetricks Xvfb; do
    command -v "$tool" >/dev/null || { echo "missing: $tool (see README)" >&2; exit 1; }
done
if ! ldconfig -p | grep -q 'libEGL.so.1 (libc6)'; then
    echo "missing 32-bit libEGL - Wine cannot start Direct3D without it (see README: libegl1:i386)" >&2
    exit 1
fi

# a private virtual display for the setup steps
Xvfb :98 -screen 0 1024x768x24 >/dev/null 2>&1 &
XVFB_PID=$!
trap 'kill $XVFB_PID 2>/dev/null; wineserver -k 2>/dev/null || true' EXIT
export DISPLAY=:98
sleep 2

echo ">> creating Wine prefix: $WINEPREFIX"
# no Mono / Gecko installer dialogs (they would wait forever on the invisible display)
WINEDLLOVERRIDES="mscoree=;mshtml=" wineboot --init >/dev/null 2>&1 || true
wineserver -w

echo ">> installing DirectPlay (the game needs it; LAN/TCP-IP multiplayer runs over it)"
if ! winetricks -q directplay >/dev/null 2>&1; then
    echo "   winetricks directplay failed. It downloads directx_feb2010_redist.exe (web.archive.org"
    echo "   sometimes answers 429). Put that file into ~/.cache/winetricks/directx9/ and run"
    echo "   ./setup.sh again - winetricks checks its checksum."
    exit 1
fi

# Registry values the Steam install script normally writes. Set with "wine reg add" - a .reg
# import mangles the backslashes of the game path. Both HKCU and HKLM, like on Windows.
reg() { wine reg add "$@" /f >/dev/null; }
WINPATH="$(winepath -w "$GAMEDIR")"
echo ">> registry for game dir: $WINPATH"
for ROOT in HKCU HKLM; do
    K="$ROOT\\SOFTWARE\\Reality Pump\\KnightShift"
    reg "$K" /v version /t REG_SZ /d "1.3"
    reg "$K" /v language /t REG_SZ /d "ENGLISH"
    reg "$K" /v Charset /t REG_DWORD /d 238
    reg "$K\\BaseGame\\FileSystem" /v datapath /t REG_SZ /d "$WINPATH/>"
    reg "$K\\BaseGame\\FileSystem" /v outputdir /t REG_SZ /d "$WINPATH"
    reg "$K\\BaseGame\\Graphics\\Default" /v EngineType /t REG_DWORD /d 0
    reg "$K\\BaseGame\\Graphics\\Default" /v FullScreen /t REG_DWORD /d 0
    # the server draws almost nothing (Render=startup) - lowest quality keeps start-up quick
    for v in TextureQuality ObjectsQuality ShadowsType ParticlesQuality RainSnowEffects WaterFoam \
             Reflections TreeAnimation CloudsShadows SetAntialiasing; do
        reg "$K\\BaseGame\\Graphics\\Direct3D" /v "$v" /t REG_DWORD /d 0
    done
    # the MMX probe fails on some virtual CPUs (the server DLL also bypasses it)
    reg "$K\\BaseGame\\Processor" /v CheckMMX /t REG_DWORD /d 0
    reg "$K\\BaseGame\\Intro" /v ShowOnStart /t REG_DWORD /d 0
done
# no sound device on a VPS
reg 'HKCU\Software\Wine\Drivers' /v Audio /t REG_SZ /d ""
wineserver -w

echo ">> graphics mode (the game's Config.exe cannot run headless)"
if [[ ! -f "$GAMEDIR/d3d8enum.exe" ]]; then
    echo "   d3d8enum.exe missing in $GAMEDIR (copy it from build/)" >&2
    exit 1
fi
ARG="$(cd "$GAMEDIR" && LIBGL_ALWAYS_SOFTWARE=1 wine d3d8enum.exe --renderer 2>/dev/null | tr -d '\r' | head -1)"
if [[ "$ARG" != -renderer* ]]; then
    echo "   no Direct3D device - check the 32-bit OpenGL/EGL libraries (README)" >&2
    exit 1
fi
echo "$ARG" > "$WINEPREFIX/ks-renderer"
echo "   $ARG"
wineserver -w

echo ">> done. Put your CD key into $GAMEDIR/ksnetfix.ini ([Server] CdKey=) and start:"
echo "     ./run.sh $GAMEDIR"
