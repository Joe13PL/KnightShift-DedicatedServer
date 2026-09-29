#!/bin/bash
# Builds the dedicated server: KSNetFix (submodule) + src/server.cpp in one dinput8.dll.
#   ./build.sh      build/dinput8.dll, build/steam_api.dll, build/ksnetfix.ini (KSNetFix + [Server]),
#                   build/d3d8enum.exe (graphics mode for Linux/Wine, see server/linux)
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

# d3d8enum.exe - picks the -renderer argument for headless Linux/Wine runs (server/linux/run.sh)
if [ -z "$VCTOOLS" ]; then
  for base in "C:/Program Files (x86)/Microsoft Visual Studio/2022" "C:/Program Files/Microsoft Visual Studio/2022"; do
    for ed in BuildTools Community Professional Enterprise; do
      d="$base/$ed/VC/Tools/MSVC"
      if [ -d "$d" ]; then VCTOOLS="$d/$(ls "$d" | sort -V | tail -1)"; break 2; fi
    done
  done
fi
WINSDK="${WINSDK:-C:/Program Files (x86)/Windows Kits/10}"
WINSDKV="${WINSDKV:-$(ls "$WINSDK/Include" | grep '^10\.' | sort -V | tail -1)}"
(
  export INCLUDE="$VCTOOLS/include;$WINSDK/Include/$WINSDKV/ucrt;$WINSDK/Include/$WINSDKV/um;$WINSDK/Include/$WINSDKV/shared"
  export LIB="$VCTOOLS/lib/x86;$WINSDK/Lib/$WINSDKV/ucrt/x86;$WINSDK/Lib/$WINSDKV/um/x86"
  export MSYS2_ARG_CONV_EXCL='*'
  O="$(cygpath -m "$ROOT/build")"
  cd "$ROOT/server/linux"
  "$VCTOOLS/bin/Hostx64/x86/cl.exe" -nologo -O2 -MT -W3 d3d8enum.cpp -Fo"$O/" -Fe"$O/d3d8enum.exe" >/dev/null
)
rm -f "$ROOT/build/d3d8enum.obj"
echo "linux helper: build/d3d8enum.exe"
