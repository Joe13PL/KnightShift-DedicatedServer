// d3d8enum - lists the Direct3D 8 adapters and display modes Wine (or Windows) reports.
//
//   d3d8enum.exe              human-readable list
//   d3d8enum.exe --renderer   prints the "-renderer ^<adapter>^,W,H,BPP,0,0" argument for
//                             KnightShift.ex1 (what the game's Config.exe normally passes)
//
// KnightShift.ex1 only picks a graphics mode from the registry or from -renderer. With neither
// it runs Config.exe (a GUI) and exits, which never works headless - so run.sh passes -renderer.
// No SDK headers needed: the few declarations below match d3d8.h.
#include <windows.h>
#include <stdio.h>
#include <string.h>

struct D3DADAPTER_IDENTIFIER8 {
    char Driver[512];
    char Description[512];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId, DeviceId, SubSysId, Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
};
struct D3DDISPLAYMODE {
    UINT Width, Height, RefreshRate;
    DWORD Format;
};
struct IDirect3D8 {
    void** vt;
};
typedef ULONG(__stdcall* FnRelease)(IDirect3D8*);
typedef UINT(__stdcall* FnCount)(IDirect3D8*);
typedef HRESULT(__stdcall* FnIdent)(IDirect3D8*, UINT, DWORD, D3DADAPTER_IDENTIFIER8*);
typedef UINT(__stdcall* FnModeCount)(IDirect3D8*, UINT);
typedef HRESULT(__stdcall* FnEnumMode)(IDirect3D8*, UINT, UINT, D3DDISPLAYMODE*);
typedef HRESULT(__stdcall* FnCurMode)(IDirect3D8*, UINT, D3DDISPLAYMODE*);

const DWORD kX8R8G8B8 = 22, kA8R8G8B8 = 21, kR5G6B5 = 23, kX1R5G5B5 = 24;

int Bpp(DWORD fmt) {
    if (fmt == kX8R8G8B8 || fmt == kA8R8G8B8) return 32;
    if (fmt == kR5G6B5 || fmt == kX1R5G5B5) return 16;
    return 0;
}

int main(int argc, char** argv) {
    bool arg = argc > 1 && strcmp(argv[1], "--renderer") == 0;
    HMODULE m = LoadLibraryA("d3d8.dll");
    typedef IDirect3D8*(WINAPI * FnCreate)(UINT);
    FnCreate create = m ? (FnCreate)GetProcAddress(m, "Direct3DCreate8") : nullptr;
    IDirect3D8* d = create ? create(220) : nullptr; // D3D_SDK_VERSION 220
    if (!d) {
        fprintf(stderr, "Direct3DCreate8 failed - no 3D device (check OpenGL/EGL libraries)\n");
        return 1;
    }
    UINT n = ((FnCount)d->vt[4])(d);
    if (!arg) printf("adapters: %u\n", n);
    for (UINT a = 0; a < n; a++) {
        D3DADAPTER_IDENTIFIER8 id = {};
        ((FnIdent)d->vt[5])(d, a, 0, &id);
        D3DDISPLAYMODE cur = {};
        ((FnCurMode)d->vt[8])(d, a, &cur);
        UINT mc = ((FnModeCount)d->vt[6])(d, a);
        // pick 1024x768x32, else the current desktop mode
        D3DDISPLAYMODE best = cur;
        for (UINT i = 0; i < mc; i++) {
            D3DDISPLAYMODE dm = {};
            ((FnEnumMode)d->vt[7])(d, a, i, &dm);
            if (!arg) printf("  mode %ux%u %u Hz, %d bpp\n", dm.Width, dm.Height, dm.RefreshRate, Bpp(dm.Format));
            if (dm.Width == 1024 && dm.Height == 768 && Bpp(dm.Format) == 32) best = dm;
        }
        if (arg) {
            printf("-renderer ^%s^,%u,%u,%d,0,0\n", id.Description, best.Width, best.Height,
                   Bpp(best.Format) ? Bpp(best.Format) : 32);
            break; // first adapter only
        }
        printf("[%u] \"%s\" (driver %s), desktop %ux%u, %u modes\n", a, id.Description, id.Driver, cur.Width,
               cur.Height, mc);
    }
    ((FnRelease)d->vt[2])(d);
    return 0;
}
