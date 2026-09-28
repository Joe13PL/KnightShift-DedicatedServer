// KSNetFix dedicated server mode (see server.cpp, docs/DEDICATED_SERVER.md).
// Built into dinput8.dll together with KSNetFix when KSNETFIX_SERVER is defined.
#pragma once
#include <stdint.h>

#define KSSERVER_VERSION "0.1"

struct ServerSettings {
    bool enabled = false;
    wchar_t profileName[32] = L"Serwer"; // player profile, created on first start
    wchar_t heroName[32] = L"Serwer";    // RPG hero, created when the profile has none
    wchar_t cdKey[32] = L"";             // entered when the game asks for it (fresh install)
    wchar_t sessionName[64] = L"KnightShift RPG Server";
    wchar_t password[64] = L"";
    wchar_t level[128] = L"";   // level name as listed in the host lobby; empty = first level
    int minPlayers = 1;         // players besides the server needed to start
    int startDelay = 20;        // seconds all players must stay ready before the start
    int dynamicConnect = 1;     // allow joining the running game
    int observer = 1;           // server's own player becomes an observer after the start
    int observerDelay = 10;     // seconds after the start
    int fps = 10;               // frame cap of the server (0 = game default)
    int autoRestart = 1;        // host a new session after a game ends
    int endDelay = 5;           // seconds before the server confirms "game over" (e.g. last player left)
    int uniqueIdentity = 1;     // players with the server's CD key can still join
    int window = 2;             // 0 normal, 1 minimized, 2 hidden - never takes the focus
    int mute = 1;               // no sound (the game sees no sound device)
    int console = 1;            // server console window: live log + commands
    int render = 0;             // 0 = draw only until the first menu (start-up needs it), 1 = always
};

struct ServerGameAddrs {
    uint32_t gameMode;          // 2 = network game running
    uint32_t unitsPerMs;        // engine clock units per millisecond
    uint32_t playerTable;       // 8 x 24 bytes: +0 DPNID, +4 flags (0x80 = connected)
};

// Reads the [Server] section; returns [Server] Enabled.
bool Server_LoadConfig(const char* iniPath);
// Installs the main loop hook. Only KnightShift.ex1 is supported for now.
bool Server_Install(bool ex1, const ServerGameAddrs& game);
