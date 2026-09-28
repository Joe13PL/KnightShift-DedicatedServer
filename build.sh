#!/bin/bash
# Builds the dedicated server: KSNetFix (submodule) + src/server.cpp in one dinput8.dll.
#   ./build.sh      build/dinput8.dll, build/steam_api.dll, build/ksnetfix.ini (KSNetFix + [Server])
# Tools: see KSNetFix/README.md (VS 2022 C++ x86, Steamworks SDK in ./sdk or STEAMWORKS_SDK).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f "$ROOT/KSNetFix/build.sh" ]; then
  echo "KSNetFix submodule missing - run: git submodule update --init" >&2
  exit 1
fi
export STEAMWORKS_SDK="${STEAMWORKS_SDK:-$ROOT/sdk}"
KSNF_EXTRA_SRC="$ROOT/src/server.cpp" \
KSNF_INCLUDE="$ROOT/src;$ROOT/KSNetFix/src" \
KSNF_DEFINES="-DKSNETFIX_SERVER" \
KSNF_OUT="$ROOT/build" \
  "$ROOT/KSNetFix/build.sh"
{ cat "$ROOT/KSNetFix/ksnetfix.ini"; printf '\r\n'; cat "$ROOT/server.ini"; } > "$ROOT/build/ksnetfix.ini"
echo "server config: build/ksnetfix.ini"
