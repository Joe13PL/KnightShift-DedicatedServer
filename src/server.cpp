// KSNetFix dedicated server mode (RPG).
//
// KnightShift has no headless host: in lockstep every peer simulates the whole
// game, and joining a running game ("dynamic connection") needs the host to save
// the game and upload it. A dedicated server is therefore an ordinary game
// instance that drives its own menu screens the way a player clicks them:
//   profile (created on first start) -> Multiplayer -> TCP/IP (Steam) ->
//   Create new RPG session -> hero (created if the profile has none) ->
//   level + dynamic connection -> Start once the players are ready ->
//   the server's own player becomes an observer -> back to the lobby after the game.
//
// A "click" is the menu dialog callback called with (dialog, 0, control id, 0),
// which is what the UI engine does for a mouse click: the screen handler runs,
// and when it returns 1 the engine closes the dialog with that id and switches
// to the screen the id encodes (e.g. 0xFF04 -> screen 4). Everything runs from a
// hook in the main loop (window thread, simulation/render semaphore held) - the
// context in which the engine itself processes clicks.
//
// Addresses: KnightShift.ex1 (docs/DEDICATED_SERVER.md), verified by signature.

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <deque>
#include <string>

#include "patch.h"
#include "server.h"

namespace {

struct Addrs {
    uint32_t frameSite, frameFn;          // main loop: call <per-frame UI update>
    uint32_t menuCb;                      // shared menu dialog callback (cdecl dialog, msg, id, param)
    uint32_t heroCb;                      // RPG character selection dialog callback (same signature)
    uint32_t screen;                      // u32, low byte = current menu screen
    uint32_t menuDialog;                  // dialog object of the current menu screen
    uint32_t setCheckFn;                  // CheckBox::SetCheck (thiscall ctrl, int)
    uint32_t providerCount, providers;    // DirectPlay service providers, 0x20 bytes, GUID at +4
    uint32_t guidTcpip, guidIpx;          // CLSID_DP8SP_TCPIP / CLSID_DP8SP_IPX as the game stores them
    uint32_t lobbyState;                  // bit 0x400 = start countdown running
    uint32_t rpgFlag;                     // 1 = the hosted session is RPG
    uint32_t slots;                       // 8 x 0x24: +0 type (0 open, 3 human), +4 DPNID
    uint32_t readyMask;                   // bit per slot: player pressed "Ready"
    uint32_t levelMap, levelMapCount;     // lobby list index -> level index
    uint32_t levels, levelCount;          // 0x88-byte entries, +0x10 name (engine string)
    uint32_t localDpnid;                  // this PC's DPNID
    uint32_t localPlayer;                 // in-game player object of this PC
    uint32_t canObserveFn, observeFn;     // fastcall(player)
    uint32_t framePeriod;                 // main loop minimum frame time (clock units)
    uint32_t installIdHi;                 // high dword of the identity decoded from the CD key
    uint32_t inputActivateFn;             // cdecl(int): acquire / release DirectInput keyboard + mouse
    uint32_t mainWindow;                  // HWND of the game window
    uint32_t gameOverResult;              // int: button of the "game over" message box, -1 = none yet
};

const Addrs kEx1 = {
    0x00405FF0, 0x007575F0, 0x0041EC80, 0x0055F210, 0x0093988C, 0x00939824, 0x007B0B30,
    0x00F62384, 0x00F62388, 0x00900928, 0x00900948, 0x00F63414, 0x00F6341C, 0x00F632D8, 0x00F626DC,
    0x00F6279C, 0x00F627A0, 0x00F62768, 0x00F6276C, 0x00F6226C, 0x0097B378,
    0x004B8CA0, 0x004B8D70, 0x00937268, 0x00937914, 0x007D8BD0, 0x009372B4, 0x00939884,
};

const char kWindowClass[] = "EARTH2150_GAME"; // main game window

// Menu screens (low byte of the screen variable) and control ids (dialog resources).
enum Screen : uint32_t {
    SCR_PROFILE = 0x00, SCR_MAIN = 0x01, SCR_PROVIDER = 0x04, SCR_SESSIONS = 0x05,
    SCR_HOST_LOBBY = 0x06, SCR_CLIENT_LOBBY = 0x07, SCR_SERIAL = 0x08, SCR_NET_INFO = 0x1B,
};
enum Ctrl : int {
    ID_SERIAL_OK = 0,
    ID_PROFILE_ENTER = 1, ID_PROFILE_NEW = 0x4D9,
    ID_HERO_LIST = 0x41B, ID_HERO_NAME = 0x4D2, ID_HERO_CREATE = 0x10, ID_HERO_ACCEPT = 0x1A,
    ID_MULTIPLAYER = 0xFF04,
    ID_PROVIDER_LIST = 0x4AC, ID_PROVIDER_ADDRESS = 0x4AF, ID_PROVIDER_INIT = 0xFF05,
    ID_SESSION_NAME = 0x4D2, ID_SESSION_PASSWORD = 0x4B5, ID_CREATE_RPG = 0xFF08,
    ID_LEVEL_LIST = 0x511, ID_DYNAMIC_CONNECT = 0x555, ID_START = 0xFF86, ID_LOBBY_BACK = 0xFF05,
};
enum : int { MSG_COMMAND = 0, MSG_SELECT = 0x11, MSG_EDIT_CHANGED = 0x300 };
enum : uint32_t { LOBBY_STARTING = 0x400, SLOT_HUMAN = 3, PLAYER_OBSERVER = 0x730, PLAYER_GAME_OVER = 0x684 };
enum : int { GAME_OVER_END = 6 };

// Engine UI objects (vtable offsets / fields shared by all builds seen so far).
enum : uint32_t {
    DLG_CALLBACK = 0x68,                               // dialog: callback pointer
    VT_DLG_GET_CONTROL = 0xCC,                         // dialog: control by id
    VT_CTRL_GET_TEXT = 0xC8, VT_CTRL_SET_TEXT = 0xCC,  // text controls, LPCWSTR
    VT_LIST_SET_SEL = 0xF8,                            // list box
    VT_COMBO_SELECT = 0xEC,                            // combo box: select item
    CTRL_FLAGS = 0x6C, CTRL_CHECKED = 0x08,
    LIST_COUNT = 0x88, LIST_SEL = 0xD0,
    HERO_DLG_COUNT = 0xB4, HERO_DLG_SEL = 0xC4,        // character dialog: heroes, selected hero
};

typedef int(__cdecl* MenuCbFn)(void* dialog, int msg, int id, int param);
typedef void*(__thiscall* GetControlFn)(void* dialog, int id);
typedef const wchar_t*(__thiscall* GetTextFn)(void* ctrl);
typedef void(__thiscall* SetTextFn)(void* ctrl, const wchar_t* text);
typedef void(__thiscall* SetSelFn)(void* ctrl, int index);
typedef void(__thiscall* SetCheckFn)(void* ctrl, int checked);
typedef int(__fastcall* PlayerFn)(void* player);
typedef int(__cdecl* FrameFn)();

const Addrs* X = nullptr;
ServerSettings cfg;
ServerGameAddrs game;

inline void* VFn(void* obj, uint32_t off) { return (*reinterpret_cast<void***>(obj))[off / 4]; }
inline uint32_t Field(void* obj, uint32_t off) { return *reinterpret_cast<uint32_t*>((uint8_t*)obj + off); }

void* Dialog() { return G<void*>(X->menuDialog); }

void* Control(void* dlg, int id) { return ((GetControlFn)VFn(dlg, VT_DLG_GET_CONTROL))(dlg, id); }

int Command(void* dlg, int id, int param = 0) {
    auto cb = (MenuCbFn)(uintptr_t)Field(dlg, DLG_CALLBACK);
    return cb(dlg, MSG_COMMAND, id, param);
}

bool SetText(void* dlg, int id, const wchar_t* text) {
    void* c = Control(dlg, id);
    if (!c) return false;
    ((SetTextFn)VFn(c, VT_CTRL_SET_TEXT))(c, text);
    const wchar_t* now = ((GetTextFn)VFn(c, VT_CTRL_GET_TEXT))(c);
    return now && wcscmp(now, text) == 0;
}

bool SelectListItem(void* dlg, int id, int index, bool notify) {
    void* c = Control(dlg, id);
    if (!c || index < 0 || index >= (int)Field(c, LIST_COUNT)) return false;
    ((SetSelFn)VFn(c, VT_LIST_SET_SEL))(c, index);
    if (notify) ((MenuCbFn)(uintptr_t)Field(dlg, DLG_CALLBACK))(dlg, MSG_SELECT, id, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Running in the background
//
// On deactivation the game's window procedure (0x406FC0) pauses the menus and calls
// SetForegroundWindow on itself to grab the focus back; only when that fails does it
// release DirectInput. The renderer (0x88AE80) draws only while GetForegroundWindow() is
// the game window - and the start-up up to the first menu runs through rendering. A server
// must keep running without the focus, so it gets its own window procedure (installed when
// the game registers its window class), the game's SetForegroundWindow / ShowWindow calls
// are redirected, GetForegroundWindow reports the game window, and with Mute the game finds
// no sound device (the same path as a PC without a sound card). Direct3D cannot start in a
// minimized window, so "minimized" keeps the window hidden until the first menu is up.
// ---------------------------------------------------------------------------
enum : int { WIN_NORMAL = 0, WIN_MINIMIZED = 1, WIN_HIDDEN = 2 };
enum : int { RENDER_STARTUP = 0, RENDER_ALWAYS = 1 };

typedef ATOM(WINAPI* RegisterClassAFn)(const WNDCLASSA*);
typedef BOOL(WINAPI* ShowWindowFn)(HWND, int);
typedef int(__cdecl* InputActivateFn)(int active);

RegisterClassAFn g_registerClassA;
ShowWindowFn g_showWindow;
WNDPROC g_gameWndProc;
bool g_menuUp;              // first menu reached: a minimized window is safe from now on
bool g_render = true;       // let the renderer draw (it draws only for its "foreground" window)

LRESULT CALLBACK ServerWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) {
        ((InputActivateFn)(uintptr_t)X->inputActivateFn)(0); // let go of keyboard and mouse only
        return DefWindowProcA(h, m, w, l);
    }
    return CallWindowProcA(g_gameWndProc, h, m, w, l);
}

ATOM WINAPI HookRegisterClassA(const WNDCLASSA* wc) {
    if (wc && wc->lpszClassName && !IS_INTRESOURCE(wc->lpszClassName) && !strcmp(wc->lpszClassName, kWindowClass)) {
        WNDCLASSA copy = *wc;
        g_gameWndProc = copy.lpfnWndProc;
        copy.lpfnWndProc = ServerWndProc;
        return g_registerClassA(&copy);
    }
    return g_registerClassA(wc);
}

bool IsGameWindow(HWND h) { return h && (WNDPROC)GetClassLongPtrA(h, GCLP_WNDPROC) == ServerWndProc; }

BOOL WINAPI HookShowWindow(HWND h, int cmd) {
    if (cmd != SW_HIDE && IsGameWindow(h)) {
        if (cfg.window == WIN_HIDDEN || (cfg.window == WIN_MINIMIZED && !g_menuUp))
            cmd = SW_HIDE;
        else if (cfg.window == WIN_MINIMIZED)
            cmd = SW_SHOWMINNOACTIVE;
        else
            cmd = SW_SHOWNOACTIVATE;
    }
    return g_showWindow(h, cmd);
}

BOOL WINAPI HookSetForegroundWindow(HWND h) {
    return IsGameWindow(h) ? TRUE : SetForegroundWindow(h);
}

// The only caller is the renderer (0x88AE80): report the game window while drawing is wanted,
// otherwise "no foreground window", which skips the whole frame.
HWND WINAPI HookGetForegroundWindow() {
    HWND game = G<HWND>(X->mainWindow);
    if (!game) return GetForegroundWindow();
    return g_render ? game : nullptr;
}

HRESULT WINAPI HookDirectSoundEnumerateA(void*, void*) { return 0; } // DS_OK, no devices

// The game refuses to start a second copy through the named mutex "Earth 2150". The server
// uses its own name, so a player can run the normal game next to it on the same PC.
HANDLE WINAPI HookCreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name) {
    if (name && !strcmp(name, "Earth 2150")) name = "Earth 2150 (KSNetFix server)";
    return CreateMutexA(sa, owner, name);
}

// IAT slot of an import of the game executable, by name or (name == nullptr) by ordinal.
void** ImportSlot(const char* dll, const char* name, WORD ordinal) {
    auto* base = (uint8_t*)GetModuleHandleA(nullptr);
    auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((const char*)(base + d->Name), dll) != 0) continue;
        auto* names = (IMAGE_THUNK_DATA*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* iat = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            bool byOrdinal = IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal);
            if (byOrdinal ? (!name && IMAGE_ORDINAL(names->u1.Ordinal) == ordinal)
                          : (name && !strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, name)))
                return (void**)&iat->u1.Function;
        }
    }
    return nullptr;
}

template <class F> bool HookImport(const char* dll, const char* name, WORD ordinal, void* hook, F* original) {
    void** slot = ImportSlot(dll, name, ordinal);
    if (!slot) return false;
    if (original) *original = (F)*slot;
    return WriteCode((uint32_t)(uintptr_t)slot, &hook, sizeof(hook));
}

bool InstallBackground() {
    bool ok = HookImport("user32.dll", "RegisterClassA", 0, (void*)&HookRegisterClassA, &g_registerClassA) &&
              HookImport("user32.dll", "ShowWindow", 0, (void*)&HookShowWindow, &g_showWindow) &&
              HookImport<void*>("user32.dll", "SetForegroundWindow", 0, (void*)&HookSetForegroundWindow, nullptr) &&
              HookImport<void*>("user32.dll", "GetForegroundWindow", 0, (void*)&HookGetForegroundWindow, nullptr) &&
              HookImport<void*>("kernel32.dll", "CreateMutexA", 0, (void*)&HookCreateMutexA, nullptr);
    if (ok && cfg.mute) ok = HookImport<void*>("dsound.dll", nullptr, 2, (void*)&HookDirectSoundEnumerateA, nullptr);
    return ok;
}

// The host rejects a joining player whose identity (decoded from the CD key) equals its
// own or another player's ("invalid serial number"). The identity is otherwise only used
// by the defunct EarthNet client, so the server flips it in memory; the game recomputes it
// on the profile screen, hence the check every frame.
const uint32_t kIdentitySalt = 0x5352564B; // "KVRS"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
struct State {
    uint32_t screen = 0xFFFFFFFF;
    void* dialog = nullptr;
    ULONGLONG screenSince = 0;
    ULONGLONG nextAction = 0;
    bool unknownLogged = false;
    bool serialTried = false;
    // hero dialog
    bool heroNamed = false;
    // host lobby
    bool levelChosen = false;
    ULONGLONG readySince = 0;
    ULONGLONG lastDynCheck = 0;
    uint32_t slotDpnid[8] = {};
    uint8_t slotReady = 0;
    bool rtsLogged = false;
    // game
    bool inGame = false;
    ULONGLONG gameSince = 0;
    bool observerDone = false;
    ULONGLONG gameOverSince = 0;
    ULONGLONG emptySince = 0;    // no player connected to the running game since
    bool forceStart = false;     // console "start"
    bool cmdPaused = false;      // console "pause"
    uint32_t peers[8] = {};
    int games = 0;
    bool finished = false;       // AutoRestart=0 and a game ended
    bool paused = false;
    bool failed = false;
    uint32_t identity = 0;       // salted identity last written
} st;

void Delay(ULONGLONG now, DWORD ms) { st.nextAction = now + ms; }

// ---------------------------------------------------------------------------
// Console: the face of the server (live log + commands) while the game stays hidden.
// Commands are queued by an input thread and run on the game's main thread in Step().
// ---------------------------------------------------------------------------
enum : int { CON_OFF = 0, CON_AUTO = 1, CON_WINDOW = 2, CON_STDIO = 3 };
HANDLE g_conOut = INVALID_HANDLE_VALUE;
HANDLE g_conIn = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_cmdLock;
std::deque<std::string> g_cmds;

void Say(const char* fmt, ...) {
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    Log("server: %s", buf);
    if (g_conOut != INVALID_HANDLE_VALUE) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[640];
        int n = _snprintf(line, sizeof(line) - 1, "[%02d:%02d:%02d] %s\r\n", t.wHour, t.wMinute, t.wSecond, buf);
        if (n < 0) n = (int)sizeof(line) - 1;
        DWORD w;
        WriteFile(g_conOut, line, (DWORD)n, &w, nullptr);
    }
}

DWORD WINAPI ConsoleInput(void*) {
    std::string acc;
    for (;;) {
        char buf[256];
        DWORD n = 0;
        if (!ReadFile(g_conIn, buf, sizeof(buf), &n, nullptr) || n == 0) {
            Sleep(200);
            continue;
        }
        acc.append(buf, n);
        size_t nl;
        while ((nl = acc.find('\n')) != std::string::npos) {
            std::string cmd = acc.substr(0, nl);
            acc.erase(0, nl + 1);
            while (!cmd.empty() && (unsigned char)cmd.back() <= ' ') cmd.pop_back();
            while (!cmd.empty() && (unsigned char)cmd.front() <= ' ') cmd.erase(0, 1);
            if (cmd.empty()) continue;
            EnterCriticalSection(&g_cmdLock);
            g_cmds.push_back(cmd);
            LeaveCriticalSection(&g_cmdLock);
        }
    }
}

// Ctrl+C, Ctrl+Break or closing the console: let the game leave the session and exit.
BOOL WINAPI ConsoleCtrl(DWORD type) {
    if (X) PostMessageA(G<HWND>(X->mainWindow), WM_CLOSE, 0, 0);
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) Sleep(4000);
    return TRUE;
}

// Usable when launched from a terminal / pipe (a Unix tty under Wine, or a Windows console):
// the standard handle exists and has a known file type.
static bool StdHandleUsable(DWORD which) {
    HANDLE h = GetStdHandle(which);
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    DWORD type = GetFileType(h) & ~FILE_TYPE_REMOTE;
    return type != FILE_TYPE_UNKNOWN;
}

bool OpenConsole() {
    if (cfg.console == CON_OFF) return false;
    bool stdio = cfg.console == CON_STDIO ||
                 (cfg.console == CON_AUTO && StdHandleUsable(STD_OUTPUT_HANDLE) && StdHandleUsable(STD_INPUT_HANDLE));
    if (!stdio) {
        if (!AllocConsole()) return false;
        SetConsoleOutputCP(CP_UTF8);
        wchar_t title[160];
        _snwprintf(title, 159, L"KnightShift RPG Server - %s", cfg.sessionName);
        title[159] = 0;
        SetConsoleTitleW(title);
    }
    g_conOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_conIn = GetStdHandle(STD_INPUT_HANDLE);
    if (g_conOut == INVALID_HANDLE_VALUE || g_conIn == INVALID_HANDLE_VALUE) {
        g_conOut = INVALID_HANDLE_VALUE;
        return false;
    }
    InitializeCriticalSection(&g_cmdLock);
    SetConsoleCtrlHandler(ConsoleCtrl, TRUE);
    HANDLE t = CreateThread(nullptr, 0, ConsoleInput, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return true;
}

void NarrowName(const wchar_t* w, char* out, size_t n) {
    if (!w) w = L"";
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, nullptr, nullptr);
    out[n - 1] = 0;
}

// ---------------------------------------------------------------------------
// Menu screens
// ---------------------------------------------------------------------------
void OnProfile(void* dlg, ULONGLONG now) {
    // A name in "New player" loads that profile, or creates it (Players\<name>\UserInfo.dat).
    char n8[96];
    NarrowName(cfg.profileName, n8, sizeof(n8));
    if (!SetText(dlg, ID_PROFILE_NEW, cfg.profileName)) {
        Say("could not enter the profile name");
        Delay(now, 10000);
        return;
    }
    Say("player profile \"%s\" (created if missing)", n8);
    Command(dlg, ID_PROFILE_ENTER);
    Delay(now, 3000);
}

// RPG character selection. The host lobby opens it by itself when the profile has no
// network hero (a session cannot start without one); the dialog then already holds a new
// default hero "<class> <profile>", which gets the configured name.
void OnHeroDialog(void* dlg, ULONGLONG now) {
    int count = (int)Field(dlg, HERO_DLG_COUNT);
    int sel = (int)Field(dlg, HERO_DLG_SEL);
    if (count <= 0) {
        // "Create new character": default class, name "<class> <profile>", selected.
        Say("no RPG hero in the profile - creating one");
        Command(dlg, ID_HERO_CREATE);
        st.heroNamed = false;
        Delay(now, 1000);
        return;
    }
    if (sel < 0 || sel >= count) {
        SelectListItem(dlg, ID_HERO_LIST, 0, true);
        Delay(now, 1000);
        return;
    }
    void* nameCtrl = Control(dlg, ID_HERO_NAME);
    const wchar_t* name = nameCtrl ? ((GetTextFn)VFn(nameCtrl, VT_CTRL_GET_TEXT))(nameCtrl) : nullptr;
    if (count == 1 && !st.heroNamed && name && wcscmp(name, cfg.heroName) != 0) {
        st.heroNamed = true;
        if (SetText(dlg, ID_HERO_NAME, cfg.heroName)) {
            ((MenuCbFn)(uintptr_t)Field(dlg, DLG_CALLBACK))(dlg, MSG_EDIT_CHANGED, ID_HERO_NAME, 0);
            char n8[96];
            NarrowName(cfg.heroName, n8, sizeof(n8));
            Say("hero named \"%s\"", n8);
        } else {
            Say("could not rename the hero - keeping the default name");
        }
        Delay(now, 1000);
        return;
    }
    Say("accepting RPG hero %d of %d", sel + 1, count);
    Command(dlg, ID_HERO_ACCEPT);
    st.heroNamed = false;
    Delay(now, 3000);
}

// "Enter serial number" (first start on a new machine): four 4-character fields.
void OnSerial(void* dlg, ULONGLONG now) {
    static const int ids[4] = {0x484, 0x4B5, 0x485, 0x5AE};
    wchar_t key[17];
    int n = 0;
    for (const wchar_t* p = cfg.cdKey; *p && n < 17; p++)
        if (*p != L'-' && *p != L' ') key[n++] = *p;
    if (st.serialTried || n != 16) {
        if (!st.unknownLogged)
            Say(st.serialTried ? "CD key rejected - check [Server] CdKey"
                               : "the game asks for its CD key - set [Server] CdKey=XXXX-XXXX-XXXX-XXXX");
        st.unknownLogged = true;
        return;
    }
    for (int i = 0; i < 4; i++) {
        wchar_t part[5] = {key[i * 4], key[i * 4 + 1], key[i * 4 + 2], key[i * 4 + 3], 0};
        if (!SetText(dlg, ids[i], part)) {
            Say("could not enter the CD key");
            st.serialTried = true;
            return;
        }
    }
    Say("entering the CD key from ksnetfix.ini");
    st.serialTried = true;
    Command(dlg, ID_SERIAL_OK);
    Delay(now, 3000);
}

void OnMainMenu(void* dlg, ULONGLONG now) {
    if (st.finished) return;
    Say("main menu -> Multiplayer");
    Command(dlg, ID_MULTIPLAYER);
    Delay(now, 3000);
}

void OnProvider(void* dlg, ULONGLONG now) {
    // The list shows the TCP/IP and IPX providers in provider order (then EarthNet etc.).
    const GUID* tcpip = reinterpret_cast<const GUID*>(static_cast<uintptr_t>(X->guidTcpip));
    const GUID* ipx = reinterpret_cast<const GUID*>(static_cast<uintptr_t>(X->guidIpx));
    int n = G<int>(X->providerCount), shown = 0, index = -1;
    auto* sp = G<uint8_t*>(X->providers);
    for (int i = 0; sp && i < n; i++) {
        const GUID* g = reinterpret_cast<const GUID*>(sp + i * 0x20 + 4);
        if (IsEqualGUID(*g, *tcpip)) {
            index = shown;
            break;
        }
        if (IsEqualGUID(*g, *ipx)) shown++;
    }
    if (index < 0 || !SelectListItem(dlg, ID_PROVIDER_LIST, index, false)) {
        Say("TCP/IP provider not found (%d providers) - is DirectPlay installed?", n);
        Delay(now, 10000);
        return;
    }
    // Address "Listen for connection" (item 0, the default) hosts; anything else joins that address.
    if (void* addr = Control(dlg, ID_PROVIDER_ADDRESS)) ((SetSelFn)VFn(addr, VT_COMBO_SELECT))(addr, 0);
    Say("connection type TCP/IP (Steam) -> Initialize");
    Command(dlg, ID_PROVIDER_INIT);
    Delay(now, 3000);
}

void OnSessions(void* dlg, ULONGLONG now) {
    wchar_t name[64];
    wcsncpy(name, cfg.sessionName, 63);
    name[63] = 0;
    for (wchar_t* p = name; *p; p++)
        if (*p == L':') *p = L' '; // "name:port" is parsed by the game
    if (!SetText(dlg, ID_SESSION_NAME, name)) {
        Say("could not set the session name");
        Delay(now, 10000);
        return;
    }
    if (Control(dlg, ID_SESSION_PASSWORD)) SetText(dlg, ID_SESSION_PASSWORD, cfg.password);
    char n8[192];
    NarrowName(name, n8, sizeof(n8));
    Say("creating RPG session \"%s\"%s", n8, cfg.password[0] ? " (password)" : "");
    Command(dlg, ID_CREATE_RPG);
    Delay(now, 5000);
}

int FindLevel(char* nameOut, size_t n) {
    int count = G<int>(X->levelMapCount);
    auto* map = G<int*>(X->levelMap);
    auto* levels = G<uint8_t*>(X->levels);
    int levelCount = G<int>(X->levelCount);
    if (!map || !levels || count <= 0) return -1;
    int pick = cfg.level[0] ? -1 : 0;
    for (int i = 0; i < count; i++) {
        int li = map[i];
        if (li < 0 || li >= levelCount) continue;
        auto* s = *reinterpret_cast<uint8_t**>(levels + li * 0x88 + 0x10);
        const wchar_t* name = s ? reinterpret_cast<const wchar_t*>(s + 0xC) : L"";
        if (pick < 0 && _wcsicmp(name, cfg.level) == 0) pick = i;
        if (i == pick) {
            NarrowName(name, nameOut, n);
            return i;
        }
    }
    return -1;
}

void LogLobbyChanges() {
    auto* slots = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(X->slots));
    uint8_t ready = (uint8_t)G<uint32_t>(X->readyMask);
    uint32_t self = G<uint32_t>(X->localDpnid);
    for (int i = 0; i < 8; i++) {
        uint32_t type = *reinterpret_cast<uint32_t*>(slots + i * 0x24);
        uint32_t id = type == SLOT_HUMAN ? *reinterpret_cast<uint32_t*>(slots + i * 0x24 + 4) : 0;
        if (id == self) id = 0;
        if (id != st.slotDpnid[i]) {
            if (st.slotDpnid[i]) Say("lobby slot %d: player %08X left", i + 1, st.slotDpnid[i]);
            if (id) Say("lobby slot %d: player %08X joined", i + 1, id);
            st.slotDpnid[i] = id;
        }
        bool r = id && (ready >> i & 1);
        if (r != ((st.slotReady >> i & 1) != 0) && id) Say("lobby slot %d: %s", i + 1, r ? "ready" : "not ready");
    }
    st.slotReady = ready;
}

void OnHostLobby(void* dlg, ULONGLONG now) {
    if (!G<uint32_t>(X->rpgFlag)) {
        if (!st.rtsLogged) Say("RTS lobby - going back to create an RPG session");
        st.rtsLogged = true;
        Command(dlg, ID_LOBBY_BACK);
        Delay(now, 3000);
        return;
    }
    if (G<uint32_t>(X->lobbyState) & LOBBY_STARTING) return;

    if (!st.levelChosen) {
        char name[192] = "";
        int index = FindLevel(name, sizeof(name));
        if (index < 0) {
            char want[256];
            NarrowName(cfg.level, want, sizeof(want));
            Say("level \"%s\" not in the lobby list - using the current one", want);
        } else {
            SelectListItem(dlg, ID_LEVEL_LIST, index, true);
            Say("level \"%s\"", name);
        }
        st.levelChosen = true;
        Delay(now, 1000);
        return;
    }

    if (now - st.lastDynCheck > 5000) {
        st.lastDynCheck = now;
        void* c = Control(dlg, ID_DYNAMIC_CONNECT);
        bool on = c && (Field(c, CTRL_FLAGS) & CTRL_CHECKED);
        if (c && on != (cfg.dynamicConnect != 0)) {
            // Same as the lobby init: set the box, then let the handler broadcast it.
            ((SetCheckFn)(uintptr_t)X->setCheckFn)(c, cfg.dynamicConnect != 0);
            Command(dlg, ID_DYNAMIC_CONNECT, 1);
            Say("dynamic connection %s", cfg.dynamicConnect ? "on" : "off");
        }
    }

    LogLobbyChanges();
    int players = 0, ready = 0;
    for (int i = 0; i < 8; i++)
        if (st.slotDpnid[i]) {
            players++;
            if (st.slotReady >> i & 1) ready++;
        }
    if (st.forceStart) {
        st.forceStart = false;
        if (players > 0 && ready == players && !(G<uint32_t>(X->lobbyState) & LOBBY_STARTING)) {
            Say("Start (console)");
            Command(dlg, ID_START);
            st.readySince = 0;
            Delay(now, 5000);
            return;
        }
        Say("cannot start yet: %d player(s), %d ready - players must join and press Ready", players, ready);
    }
    if (players < cfg.minPlayers || ready < players) {
        st.readySince = 0;
        return;
    }
    if (!st.readySince) {
        st.readySince = now;
        Say("%d player(s) ready - starting in %d s", players, cfg.startDelay);
    }
    if (now - st.readySince < (ULONGLONG)cfg.startDelay * 1000) return;
    Say("Start");
    Command(dlg, ID_START);
    st.readySince = 0;
    Delay(now, 5000);
}

// ---------------------------------------------------------------------------
// Running game
// ---------------------------------------------------------------------------
void InGame(ULONGLONG now) {
    if (!st.inGame) {
        st.inGame = true;
        st.gameSince = now;
        st.observerDone = !cfg.observer;
        st.gameOverSince = 0;
        st.emptySince = 0;
        memset(st.peers, 0, sizeof(st.peers));
        st.games++;
        Say("game %d started", st.games);
    }
    auto* table = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(game.playerTable));
    uint32_t self = G<uint32_t>(X->localDpnid);
    for (int i = 0; i < 8; i++) {
        uint32_t id = *reinterpret_cast<uint32_t*>(table + i * 24);
        uint32_t flags = *reinterpret_cast<uint32_t*>(table + i * 24 + 4);
        if (!(flags & 0x80) || id == self) id = 0;
        if (id != st.peers[i]) {
            if (st.peers[i]) Say("player %08X disconnected", st.peers[i]);
            if (id) Say("player %08X connected", id);
            st.peers[i] = id;
        }
    }
    // Nobody left: the game itself only notices when the server is already an observer, so
    // end it the way the "game over" OK does.
    int connected = 0;
    for (uint32_t id : st.peers) connected += id != 0;
    if (connected) {
        st.emptySince = 0;
    } else if (!st.emptySince) {
        st.emptySince = now;
        Say("no players left - ending the game in %d s", cfg.endDelay);
    } else if (now - st.emptySince >= (ULONGLONG)cfg.endDelay * 1000 && G<int>(X->gameOverResult) == -1) {
        G<int>(X->gameOverResult) = GAME_OVER_END;
        Say("game ended - no players");
        Delay(now, 3000);
        return;
    }
    // Game over (all players left the observing server, or the level ended): the game shows a
    // message box (0x4234F0 sets player+0x684) and ends only when its OK stores 6 in the result
    // that the menu callback polls (0x41EC80) - the same as clicking OK.
    void* player = G<void*>(X->localPlayer);
    if (player && Field(player, PLAYER_GAME_OVER) && G<int>(X->gameOverResult) == -1) {
        if (!st.gameOverSince) {
            st.gameOverSince = now;
            Say("game over - ending the game in %d s", cfg.endDelay);
        } else if (now - st.gameOverSince >= (ULONGLONG)cfg.endDelay * 1000) {
            G<int>(X->gameOverResult) = GAME_OVER_END;
            Say("game over confirmed");
            Delay(now, 3000);
            return;
        }
    }
    if (!st.observerDone && now - st.gameSince >= (ULONGLONG)cfg.observerDelay * 1000) {
        void* me = G<void*>(X->localPlayer);
        if (me && Field(me, PLAYER_OBSERVER)) {
            st.observerDone = true;
        } else if (me && ((PlayerFn)(uintptr_t)X->canObserveFn)(me)) {
            ((PlayerFn)(uintptr_t)X->observeFn)(me);
            Say("switched to observer");
            st.observerDone = true;
        }
        Delay(now, 2000);
    }
}

const char* ScreenName(uint32_t screen) {
    switch (screen) {
    case SCR_PROFILE: return "player profile";
    case SCR_MAIN: return "main menu";
    case SCR_PROVIDER: return "connection type";
    case SCR_SESSIONS: return "sessions";
    case SCR_HOST_LOBBY: return "lobby";
    case SCR_CLIENT_LOBBY: return "someone else's lobby";
    case SCR_SERIAL: return "CD key";
    case SCR_NET_INFO: return "network info";
    default: return "menu";
    }
}

void Status(ULONGLONG now) {
    if (G<int>(game.gameMode) == 2) {
        int n = 0;
        for (uint32_t id : st.peers) n += id != 0;
        Say("game %d running for %llu min, %d player(s) connected%s", st.games, (now - st.gameSince) / 60000, n,
            st.observerDone ? ", server is observing" : "");
        for (int i = 0; i < 8; i++)
            if (st.peers[i]) Say("  player %08X", st.peers[i]);
    } else {
        uint32_t screen = G<uint32_t>(X->screen) & 0xFF;
        int players = 0, ready = 0;
        for (int i = 0; i < 8; i++)
            if (st.slotDpnid[i]) {
                players++;
                ready += (st.slotReady >> i & 1);
            }
        Say("%s, %d player(s) in the lobby, %d ready, %d game(s) played%s", ScreenName(screen), players, ready,
            st.games, st.cmdPaused || st.paused ? ", automation PAUSED" : "");
        for (int i = 0; i < 8; i++)
            if (st.slotDpnid[i]) Say("  slot %d: player %08X%s", i + 1, st.slotDpnid[i], st.slotReady >> i & 1 ? " (ready)" : "");
    }
}

void RunCommands(ULONGLONG now) {
    if (g_conOut == INVALID_HANDLE_VALUE) return;
    std::deque<std::string> q;
    EnterCriticalSection(&g_cmdLock);
    q.swap(g_cmds);
    LeaveCriticalSection(&g_cmdLock);
    HWND wnd = G<HWND>(X->mainWindow);
    for (const std::string& line : q) {
        std::string cmd = line.substr(0, line.find(' '));
        for (char& c : cmd) c = (char)tolower((unsigned char)c);
        if (cmd == "help" || cmd == "?") {
            Say("commands: status | start (now, when everyone is ready) | end (end the running game) |");
            Say("          pause / resume (automation) | show / hide (game window) | quit");
        } else if (cmd == "status") {
            Status(now);
        } else if (cmd == "start") {
            st.forceStart = true;
        } else if (cmd == "end") {
            if (G<int>(game.gameMode) == 2) {
                G<int>(X->gameOverResult) = GAME_OVER_END;
                Say("ending the game");
            } else {
                Say("no game is running");
            }
        } else if (cmd == "pause" || cmd == "resume") {
            st.cmdPaused = cmd == "pause";
            Say("automation %s", st.cmdPaused ? "paused" : "resumed");
        } else if (cmd == "show" || cmd == "hide") {
            bool show = cmd == "show";
            g_render = show || cfg.render == RENDER_ALWAYS; // a visible window is drawn
            g_showWindow(wnd, show ? SW_SHOWNORMAL : SW_HIDE);
        } else if (cmd == "quit" || cmd == "exit") {
            Say("shutting down");
            PostMessageA(wnd, WM_CLOSE, 0, 0);
        } else {
            Say("unknown command \"%s\" - type help", cmd.c_str());
        }
    }
}

void Step() {
    ULONGLONG now = GetTickCount64();
    RunCommands(now);
    if (cfg.fps > 0) G<uint32_t>(X->framePeriod) = (uint32_t)(G<uint32_t>(game.unitsPerMs) * 1000ull / (unsigned)cfg.fps);
    if (cfg.uniqueIdentity) {
        uint32_t& id = G<uint32_t>(X->installIdHi);
        if (id && id != st.identity) st.identity = id ^= kIdentitySalt;
    }

    bool paused = (GetKeyState(VK_SCROLL) & 1) != 0;
    if (paused != st.paused) {
        st.paused = paused;
        Say("automation %s (Scroll Lock)", paused ? "paused" : "resumed");
    }
    if (paused || st.cmdPaused || now < st.nextAction) return;

    if (G<int>(game.gameMode) == 2) {
        InGame(now);
        return;
    }
    if (st.inGame) {
        st.inGame = false;
        Say("game %d ended", st.games);
        if (!cfg.autoRestart) {
            st.finished = true;
            Say("AutoRestart=0 - automation stopped");
        }
    }
    if (st.finished) return;

    uint32_t screen = G<uint32_t>(X->screen) & 0xFF;
    void* dlg = Dialog();
    if (screen != st.screen || dlg != st.dialog) {
        st.screen = screen;
        st.dialog = dlg;
        st.screenSince = now;
        st.unknownLogged = false;
        st.levelChosen = false;
        st.readySince = 0;
        st.rtsLogged = false;
        return;
    }
    if (!dlg || now - st.screenSince < 1500) return; // let the screen settle
    if (!g_menuUp) {
        g_menuUp = true;
        if (cfg.window == WIN_MINIMIZED) {
            g_showWindow(G<HWND>(X->mainWindow), SW_SHOWMINNOACTIVE);
            Say("menu ready - window minimized");
        }
        if (cfg.render == RENDER_STARTUP) {
            g_render = false;
            Say("menu ready - rendering stopped (Render=startup; \"show\" draws again)");
        }
    }
    auto cb = Field(dlg, DLG_CALLBACK);
    if (cb == X->heroCb) {
        OnHeroDialog(dlg, now);
        return;
    }
    if (cb != X->menuCb) { // a message box or another dialog is on top
        if (!st.unknownLogged) Say("screen 0x%02X shows a dialog with callback %08X - waiting", screen, cb);
        st.unknownLogged = true;
        return;
    }

    switch (screen) {
    case SCR_SERIAL: OnSerial(dlg, now); break;
    case SCR_PROFILE: OnProfile(dlg, now); break;
    case SCR_MAIN:
    case SCR_NET_INFO: OnMainMenu(dlg, now); break;
    case SCR_PROVIDER: OnProvider(dlg, now); break;
    case SCR_SESSIONS: OnSessions(dlg, now); break;
    case SCR_HOST_LOBBY: OnHostLobby(dlg, now); break;
    case SCR_CLIENT_LOBBY:
        Say("joined someone else's lobby - leaving");
        Command(dlg, ID_LOBBY_BACK);
        Delay(now, 3000);
        break;
    default:
        if (!st.unknownLogged) Say("waiting on menu screen 0x%02X", screen);
        st.unknownLogged = true;
        break;
    }
}

int __cdecl FrameHook() {
    int r = ((FrameFn)(uintptr_t)X->frameFn)();
    if (!st.failed) {
        __try {
            Step();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            st.failed = true;
            Say("exception 0x%08X on screen 0x%02X - automation stopped", GetExceptionCode(), st.screen);
        }
    }
    return r;
}

bool Verify(const Addrs& a) {
    struct Sig {
        uint32_t va;
        uint8_t n;
        uint8_t b[12];
    };
    // Instructions that reference the globals above, plus the function prologues we call.
    static const Sig sigs[] = {
        {0x0041EC80, 12, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00, 0x53, 0x56, 0x57}},
        {0x0041F53B, 6, {0x8B, 0x3D, 0x8C, 0x98, 0x93, 0x00}},       // mov edi,[screen]
        {0x0041F552, 6, {0x8B, 0x0D, 0x24, 0x98, 0x93, 0x00}},       // mov ecx,[menuDialog]
        {0x007B0B20, 7, {0x8B, 0x41, 0x6C, 0x83, 0xE0, 0x08, 0xC3}}, // GetCheck: [ecx+0x6C] & 8
        {0x007B0B30, 5, {0x55, 0x8B, 0xEC, 0x53, 0x56}},
        {0x00830F2C, 6, {0x8B, 0x0D, 0x84, 0x23, 0xF6, 0x00}},       // provider count
        {0x00830F45, 6, {0x8B, 0x0D, 0x88, 0x23, 0xF6, 0x00}},       // provider array
        {0x00830F4B, 5, {0xBF, 0x48, 0x09, 0x90, 0x00}},             // IPX GUID
        {0x00830F68, 5, {0xBF, 0x28, 0x09, 0x90, 0x00}},             // TCP/IP GUID
        {0x008310F3, 6, {0xFF, 0x92, 0xEC, 0x00, 0x00, 0x00}},       // address combo: select item
        {0x00833B86, 5, {0xA1, 0x1C, 0x34, 0xF6, 0x00}},             // rpg flag
        {0x00833DC9, 5, {0xA1, 0x14, 0x34, 0xF6, 0x00}},             // lobby state
        {0x008352E3, 5, {0xBB, 0xDC, 0x32, 0xF6, 0x00}},             // slots + 4
        {0x008353DB, 6, {0x85, 0x05, 0xDC, 0x26, 0xF6, 0x00}},       // ready mask
        {0x00835323, 6, {0x8B, 0x1D, 0x6C, 0x22, 0xF6, 0x00}},       // local DPNID
        {0x008349EE, 6, {0x3B, 0x05, 0xA0, 0x27, 0xF6, 0x00}},       // level map count
        {0x008349F6, 6, {0x8B, 0x0D, 0x9C, 0x27, 0xF6, 0x00}},       // level map
        {0x00834A20, 6, {0x3B, 0x05, 0x6C, 0x27, 0xF6, 0x00}},       // level count
        {0x00834A2C, 6, {0x8B, 0x35, 0x68, 0x27, 0xF6, 0x00}},       // levels
        {0x005591A3, 6, {0x8B, 0x0D, 0x78, 0xB3, 0x97, 0x00}},       // local player
        {0x004B8CA0, 6, {0x53, 0x56, 0x57, 0x8B, 0xD9, 0xBE}},       // can observe
        {0x004B8D76, 10, {0xC7, 0x87, 0x30, 0x07, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}}, // observe
        {0x004061AD, 5, {0xA1, 0x68, 0x72, 0x93, 0x00}},             // frame period
        {0x005C9BC4, 7, {0x81, 0x7D, 0x10, 0xD9, 0x04, 0x00, 0x00}}, // profile screen: "New player" edit
        {0x0055F210, 12, {0x55, 0x8B, 0xEC, 0x53, 0x56, 0x8B, 0x75, 0x08, 0x57, 0x8B, 0x7D, 0x0C}}, // hero dialog
        {0x0055F3F2, 3, {0x83, 0xF8, 0x1A}},                         // hero dialog: accept
        {0x0055F5FF, 6, {0x81, 0xFF, 0x00, 0x03, 0x00, 0x00}},       // hero dialog: name edited
        {0x00565C9F, 6, {0x8B, 0x83, 0xC4, 0x00, 0x00, 0x00}},       // accept: selected hero
        {0x00565CB0, 6, {0x3B, 0x83, 0xB4, 0x00, 0x00, 0x00}},       // accept: hero count
        {0x0083C24B, 6, {0x3B, 0x35, 0x14, 0x79, 0x93, 0x00}},       // join: compare with own identity
        {0x004087A5, 5, {0x68, 0x84, 0x04, 0x00, 0x00}},             // serial screen: first field
        {0x004087EB, 5, {0x68, 0xAE, 0x05, 0x00, 0x00}},             // serial screen: last field
        {0x00405C57, 7, {0xC7, 0x45, 0xDC, 0xC0, 0x6F, 0x40, 0x00}}, // window class: wndproc 0x406FC0
        {0x00405D17, 5, {0xA3, 0xB4, 0x72, 0x93, 0x00}},             // main window handle
        {0x004235E3, 10, {0xC7, 0x80, 0x84, 0x06, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}}, // game over flag
        {0x004234FA, 10, {0xC7, 0x05, 0x84, 0x98, 0x93, 0x00, 0x06, 0x00, 0x00, 0x00}}, // result = 6 (end)
        {0x0041ECD9, 5, {0xA1, 0x84, 0x98, 0x93, 0x00}},             // menu callback polls the result
        {0x007D8BD0, 9, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x84, 0x00, 0x00, 0x00}}, // input acquire/release
    };
    for (const Sig& s : sigs)
        if (!Match(s.va, s.b, s.n)) {
            Say("signature mismatch at %08X", s.va);
            return false;
        }
    return MatchCall(a.frameSite, a.frameFn) && MatchCall(0x005591AD, a.canObserveFn) &&
           MatchCall(0x005591BC, a.observeFn) && MatchCall(0x0040710E, a.inputActivateFn);
}

} // namespace

bool Server_LoadConfig(const char* ini) {
    wchar_t iniW[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, ini, -1, iniW, MAX_PATH);
    auto Str = [&](const wchar_t* k, wchar_t* out, DWORD n) {
        wchar_t def[128];
        wcsncpy(def, out, 127);
        def[127] = 0;
        GetPrivateProfileStringW(L"Server", k, def, out, n, iniW);
    };
    auto Int = [&](const char* k, int def) { return (int)GetPrivateProfileIntA("Server", k, def, ini); };
    ServerSettings& sv = cfg;
    sv.enabled        = Int("Enabled", 0) != 0;
    Str(L"ProfileName", sv.profileName, 32);
    Str(L"HeroName", sv.heroName, 32);
    Str(L"CdKey", sv.cdKey, 32);
    Str(L"SessionName", sv.sessionName, 64);
    Str(L"Password", sv.password, 64);
    Str(L"Level", sv.level, 128);
    sv.minPlayers     = Int("MinPlayers", sv.minPlayers);
    sv.startDelay     = Int("StartDelay", sv.startDelay);
    sv.dynamicConnect = Int("DynamicConnect", sv.dynamicConnect);
    sv.observer       = Int("Observer", sv.observer);
    sv.observerDelay  = Int("ObserverDelay", sv.observerDelay);
    sv.fps            = Int("Fps", sv.fps);
    sv.autoRestart    = Int("AutoRestart", sv.autoRestart);
    sv.endDelay       = Int("EndDelay", sv.endDelay);
    sv.uniqueIdentity = Int("UniqueIdentity", sv.uniqueIdentity);
    sv.mute           = Int("Mute", sv.mute);
    wchar_t con[16] = L"auto";
    Str(L"Console", con, 16);
    sv.console = !_wcsicmp(con, L"0") || !_wcsicmp(con, L"off") ? CON_OFF
               : !_wcsicmp(con, L"window") ? CON_WINDOW
               : !_wcsicmp(con, L"stdio") ? CON_STDIO
               : CON_AUTO;
    wchar_t render[16] = L"startup";
    Str(L"Render", render, 16);
    sv.render = !_wcsicmp(render, L"always") ? RENDER_ALWAYS : RENDER_STARTUP;
    wchar_t win[16] = L"hidden";
    Str(L"Window", win, 16);
    sv.window = !_wcsicmp(win, L"normal") ? WIN_NORMAL : !_wcsicmp(win, L"hidden") ? WIN_HIDDEN : WIN_MINIMIZED;
    return sv.enabled;
}

bool Server_Install(bool ex1, const ServerGameAddrs& g) {
    if (!ex1) {
        Log("server mode: only KnightShift.ex1 is supported (set the engine to D3D8 classic)");
        return false;
    }
    if (!Verify(kEx1)) return false;
    // One server per PC; further instances (AllowMultipleInstances) run as normal games.
    CreateMutexA(nullptr, FALSE, "Local\\KSNetFixServer");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Log("server mode: another instance on this PC is the server - running as a normal game");
        return true;
    }
    X = &kEx1;
    game = g;
    if (cfg.console && OpenConsole()) {
        char title[192];
        NarrowName(cfg.sessionName, title, sizeof(title));
        Say("KnightShift RPG Server (KSNetFix dedicated server " KSSERVER_VERSION ") - \"%s\"", title);
        Say("log: ksnetfix.log - type help for commands, Ctrl+C or quit to stop");
    }
    if (cfg.minPlayers < 1) cfg.minPlayers = 1;
    if (cfg.minPlayers > 7) cfg.minPlayers = 7;
    if (cfg.startDelay < 0) cfg.startDelay = 0;
    if (cfg.observerDelay < 0) cfg.observerDelay = 0;
    if (cfg.endDelay < 0) cfg.endDelay = 0;
    if (cfg.fps < 0 || cfg.fps > 200) cfg.fps = 0;
    if (!cfg.profileName[0]) wcscpy(cfg.profileName, L"Serwer");
    if (!cfg.heroName[0]) wcscpy(cfg.heroName, L"Serwer");
    char name[192], level[256], profile[96], hero[96];
    NarrowName(cfg.sessionName, name, sizeof(name));
    NarrowName(cfg.level[0] ? cfg.level : L"(first in list)", level, sizeof(level));
    NarrowName(cfg.profileName, profile, sizeof(profile));
    NarrowName(cfg.heroName, hero, sizeof(hero));
    Log("server mode: RPG session \"%s\", profile \"%s\", hero \"%s\", level %s, min players %d, start delay %d s, "
        "dynamic connection %d, observer %d, fps %d, auto restart %d, unique identity %d "
        "(Scroll Lock pauses the automation)",
        name, profile, hero, level, cfg.minPlayers, cfg.startDelay, cfg.dynamicConnect, cfg.observer, cfg.fps,
        cfg.autoRestart, cfg.uniqueIdentity);
    static const char* kWindowNames[] = {"normal", "minimized", "hidden"};
    Say("background mode (window %s, never takes the focus, sound %s): %s", kWindowNames[cfg.window],
        cfg.mute ? "off" : "on", InstallBackground() ? "ok" : "FAILED");
    return WriteRel32(X->frameSite, 0xE8, (void*)&FrameHook);
}
