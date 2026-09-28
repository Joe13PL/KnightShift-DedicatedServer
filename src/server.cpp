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
#include <string.h>
#include <wchar.h>

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
};

const Addrs kEx1 = {
    0x00405FF0, 0x007575F0, 0x0041EC80, 0x0055F210, 0x0093988C, 0x00939824, 0x007B0B30,
    0x00F62384, 0x00F62388, 0x00900928, 0x00900948, 0x00F63414, 0x00F6341C, 0x00F632D8, 0x00F626DC,
    0x00F6279C, 0x00F627A0, 0x00F62768, 0x00F6276C, 0x00F6226C, 0x0097B378,
    0x004B8CA0, 0x004B8D70, 0x00937268, 0x00937914,
};

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
enum : uint32_t { LOBBY_STARTING = 0x400, SLOT_HUMAN = 3, PLAYER_OBSERVER = 0x730 };

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
    uint32_t peers[8] = {};
    int games = 0;
    bool finished = false;       // AutoRestart=0 and a game ended
    bool paused = false;
    bool failed = false;
    uint32_t identity = 0;       // salted identity last written
} st;

void Delay(ULONGLONG now, DWORD ms) { st.nextAction = now + ms; }

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
        Log("server: could not enter the profile name");
        Delay(now, 10000);
        return;
    }
    Log("server: player profile \"%s\" (created if missing)", n8);
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
        Log("server: no RPG hero in the profile - creating one");
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
            Log("server: hero named \"%s\"", n8);
        } else {
            Log("server: could not rename the hero - keeping the default name");
        }
        Delay(now, 1000);
        return;
    }
    Log("server: accepting RPG hero %d of %d", sel + 1, count);
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
            Log(st.serialTried ? "server: CD key rejected - check [Server] CdKey"
                               : "server: the game asks for its CD key - set [Server] CdKey=XXXX-XXXX-XXXX-XXXX");
        st.unknownLogged = true;
        return;
    }
    for (int i = 0; i < 4; i++) {
        wchar_t part[5] = {key[i * 4], key[i * 4 + 1], key[i * 4 + 2], key[i * 4 + 3], 0};
        if (!SetText(dlg, ids[i], part)) {
            Log("server: could not enter the CD key");
            st.serialTried = true;
            return;
        }
    }
    Log("server: entering the CD key from ksnetfix.ini");
    st.serialTried = true;
    Command(dlg, ID_SERIAL_OK);
    Delay(now, 3000);
}

void OnMainMenu(void* dlg, ULONGLONG now) {
    if (st.finished) return;
    Log("server: main menu -> Multiplayer");
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
        Log("server: TCP/IP provider not found (%d providers) - is DirectPlay installed?", n);
        Delay(now, 10000);
        return;
    }
    // Address "Listen for connection" (item 0, the default) hosts; anything else joins that address.
    if (void* addr = Control(dlg, ID_PROVIDER_ADDRESS)) ((SetSelFn)VFn(addr, VT_COMBO_SELECT))(addr, 0);
    Log("server: connection type TCP/IP (Steam) -> Initialize");
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
        Log("server: could not set the session name");
        Delay(now, 10000);
        return;
    }
    if (Control(dlg, ID_SESSION_PASSWORD)) SetText(dlg, ID_SESSION_PASSWORD, cfg.password);
    char n8[192];
    NarrowName(name, n8, sizeof(n8));
    Log("server: creating RPG session \"%s\"%s", n8, cfg.password[0] ? " (password)" : "");
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
            if (st.slotDpnid[i]) Log("server: lobby slot %d: player %08X left", i + 1, st.slotDpnid[i]);
            if (id) Log("server: lobby slot %d: player %08X joined", i + 1, id);
            st.slotDpnid[i] = id;
        }
        bool r = id && (ready >> i & 1);
        if (r != ((st.slotReady >> i & 1) != 0) && id) Log("server: lobby slot %d: %s", i + 1, r ? "ready" : "not ready");
    }
    st.slotReady = ready;
}

void OnHostLobby(void* dlg, ULONGLONG now) {
    if (!G<uint32_t>(X->rpgFlag)) {
        if (!st.rtsLogged) Log("server: RTS lobby - going back to create an RPG session");
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
            Log("server: level \"%s\" not in the lobby list - using the current one", want);
        } else {
            SelectListItem(dlg, ID_LEVEL_LIST, index, true);
            Log("server: level \"%s\"", name);
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
            Log("server: dynamic connection %s", cfg.dynamicConnect ? "on" : "off");
        }
    }

    LogLobbyChanges();
    int players = 0, ready = 0;
    for (int i = 0; i < 8; i++)
        if (st.slotDpnid[i]) {
            players++;
            if (st.slotReady >> i & 1) ready++;
        }
    if (players < cfg.minPlayers || ready < players) {
        st.readySince = 0;
        return;
    }
    if (!st.readySince) {
        st.readySince = now;
        Log("server: %d player(s) ready - starting in %d s", players, cfg.startDelay);
    }
    if (now - st.readySince < (ULONGLONG)cfg.startDelay * 1000) return;
    Log("server: Start");
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
        memset(st.peers, 0, sizeof(st.peers));
        st.games++;
        Log("server: game %d started", st.games);
    }
    auto* table = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(game.playerTable));
    uint32_t self = G<uint32_t>(X->localDpnid);
    for (int i = 0; i < 8; i++) {
        uint32_t id = *reinterpret_cast<uint32_t*>(table + i * 24);
        uint32_t flags = *reinterpret_cast<uint32_t*>(table + i * 24 + 4);
        if (!(flags & 0x80) || id == self) id = 0;
        if (id != st.peers[i]) {
            if (st.peers[i]) Log("server: player %08X disconnected", st.peers[i]);
            if (id) Log("server: player %08X connected", id);
            st.peers[i] = id;
        }
    }
    if (!st.observerDone && now - st.gameSince >= (ULONGLONG)cfg.observerDelay * 1000) {
        void* me = G<void*>(X->localPlayer);
        if (me && Field(me, PLAYER_OBSERVER)) {
            st.observerDone = true;
        } else if (me && ((PlayerFn)(uintptr_t)X->canObserveFn)(me)) {
            ((PlayerFn)(uintptr_t)X->observeFn)(me);
            Log("server: switched to observer");
            st.observerDone = true;
        }
        Delay(now, 2000);
    }
}

void Step() {
    ULONGLONG now = GetTickCount64();
    if (cfg.fps > 0) G<uint32_t>(X->framePeriod) = (uint32_t)(G<uint32_t>(game.unitsPerMs) * 1000ull / (unsigned)cfg.fps);
    if (cfg.uniqueIdentity) {
        uint32_t& id = G<uint32_t>(X->installIdHi);
        if (id && id != st.identity) st.identity = id ^= kIdentitySalt;
    }

    bool paused = (GetKeyState(VK_SCROLL) & 1) != 0;
    if (paused != st.paused) {
        st.paused = paused;
        Log("server: automation %s (Scroll Lock)", paused ? "paused" : "resumed");
    }
    if (paused || now < st.nextAction) return;

    if (G<int>(game.gameMode) == 2) {
        InGame(now);
        return;
    }
    if (st.inGame) {
        st.inGame = false;
        Log("server: game %d ended", st.games);
        if (!cfg.autoRestart) {
            st.finished = true;
            Log("server: AutoRestart=0 - automation stopped");
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
    auto cb = Field(dlg, DLG_CALLBACK);
    if (cb == X->heroCb) {
        OnHeroDialog(dlg, now);
        return;
    }
    if (cb != X->menuCb) { // a message box or another dialog is on top
        if (!st.unknownLogged) Log("server: screen 0x%02X shows a dialog with callback %08X - waiting", screen, cb);
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
        Log("server: joined someone else's lobby - leaving");
        Command(dlg, ID_LOBBY_BACK);
        Delay(now, 3000);
        break;
    default:
        if (!st.unknownLogged) Log("server: waiting on menu screen 0x%02X", screen);
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
            Log("server: exception 0x%08X on screen 0x%02X - automation stopped", GetExceptionCode(), st.screen);
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
    };
    for (const Sig& s : sigs)
        if (!Match(s.va, s.b, s.n)) {
            Log("server: signature mismatch at %08X", s.va);
            return false;
        }
    return MatchCall(a.frameSite, a.frameFn) && MatchCall(0x005591AD, a.canObserveFn) &&
           MatchCall(0x005591BC, a.observeFn);
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
    sv.uniqueIdentity = Int("UniqueIdentity", sv.uniqueIdentity);
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
    if (cfg.minPlayers < 1) cfg.minPlayers = 1;
    if (cfg.minPlayers > 7) cfg.minPlayers = 7;
    if (cfg.startDelay < 0) cfg.startDelay = 0;
    if (cfg.observerDelay < 0) cfg.observerDelay = 0;
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
    return WriteRel32(X->frameSite, 0xE8, (void*)&FrameHook);
}
