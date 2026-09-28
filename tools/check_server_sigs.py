# Checks the byte signatures and call sites used by src/server.cpp against KnightShift.ex1
# without running the game:
#   python tools/check_server_sigs.py "C:/.../steamapps/common/KnightShift/KnightShift.ex1"
# Requires: pip install pefile
import os, re, struct, sys
import pefile

if len(sys.argv) < 2:
    sys.exit(__doc__ or "usage: check_server_sigs.py <KnightShift.ex1>")
pe = pefile.PE(sys.argv[1])
ib = pe.OPTIONAL_HEADER.ImageBase
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, "..", "src", "server.cpp"), encoding="utf-8").read()

ok = True
for m in re.finditer(r"\{0x([0-9A-Fa-f]{8}), (\d+), \{([^}]*)\}\}", src):
    va, n = int(m.group(1), 16), int(m.group(2))
    want = bytes(int(x, 16) for x in m.group(3).split(","))
    real = pe.get_data(va - ib, n)
    good = len(want) == n and real == want
    ok &= good
    print("%08X %2d %s" % (va, n, "ok" if good else "MISMATCH have %s want %s" % (real.hex(), want.hex())))

def is_call(site, target):
    rel = struct.unpack("<i", pe.get_data(site + 1 - ib, 4))[0]
    return pe.get_data(site - ib, 1) == b"\xe8" and site + 5 + rel == target

# Call sites checked with MatchCall in server.cpp (main loop hook, observer, input release).
for site, target in ((0x405FF0, 0x7575F0), (0x5591AD, 0x4B8CA0), (0x5591BC, 0x4B8D70), (0x40710E, 0x7D8BD0)):
    good = is_call(site, target)
    ok &= good
    print("call %08X -> %08X %s" % (site, target, "ok" if good else "MISMATCH"))

print("ALL OK" if ok else "PROBLEMS")
sys.exit(0 if ok else 1)
