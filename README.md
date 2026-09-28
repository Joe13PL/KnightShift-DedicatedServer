# KnightShift Dedicated Server (RPG)

Serwer dedykowany trybu **RPG** dla **KnightShift (Polanie II)**, wersja Steam 1.3 — dodatek do
[KSNetFix](https://github.com/Joe13PL/KSNetFix). Gra sama zakłada sesję, startuje ją, gdy gracze są
gotowi, a potem tylko hostuje (gracz serwera zostaje obserwatorem). Cel: stały świat RPG na PC albo
VPS (Windows, docelowo też Linux), do którego gracze wchodzą w trakcie gry.

> **Stan: prototyp.** Test w grze na jednym PC (LAN, 2026-09-28): serwer sam założył profil
> i postać, sesję RPG, wystartował grę po „Gotowy”, przeszedł w obserwatora, a po wyjściu gracza
> założył sesję od nowa; 0 rozjazdów stanu gry. Nie sprawdzono jeszcze gry przez Steam
> i dołączania w trakcie gry.

*English summary [below](#english).*

## Dlaczego tak

KnightShift to lockstep: każdy komputer liczy całą symulację, a host wyznacza tury i przy dołączaniu
w trakcie gry zapisuje stan gry dla nowego gracza. Osobny, lekki program-serwer musiałby odtworzyć
całą logikę gry. Dlatego serwer to zwykła instancja gry, którą KSNetFix prowadzi przez jej własne
ekrany menu dokładnie tak, jak klikałby gracz. Szczegóły i adresy: [`docs/DEDICATED_SERVER.md`](docs/DEDICATED_SERVER.md).

## Co robi

1. **Pierwszy start:**
   - wpisuje klucz CD, jeśli gra o niego poprosi (`CdKey`);
   - tworzy profil `ProfileName` i bohatera RPG `HeroName` (domyślnie „Serwer”), bo bez bohatera
     sesji RPG nie da się wystartować.
2. Multiplayer → TCP/IP (przez Steam, KSNetFix) → „Utwórz nową sesję RPG” (`SessionName`, `Password`).
3. W lobby:
   - wybiera poziom (`Level`) i włącza dołączanie w trakcie gry (`DynamicConnect`);
   - zapisuje w logu wejścia, wyjścia i gotowość graczy;
   - klika „Start”, gdy co najmniej `MinPlayers` graczy jest gotowych przez `StartDelay` s.
4. Po starcie gracz serwera zostaje obserwatorem (`Observer`). Gdy gra się skończy albo wyjdzie
   ostatni gracz, serwer po `EndDelay` s sam kończy grę (jak „OK” w oknie „koniec gry”) i zakłada
   sesję od nowa (`AutoRestart`).
5. **Konsola serwera** (`Console`): bieżący log i komendy `status`, `start`, `end`, `pause`,
   `resume`, `show`, `hide`, `quit`; Ctrl+C wyłącza serwer. `Console=auto` daje terminal, gdy serwer
   uruchomiono z terminala (Linux/Wine), a osobne okno konsoli na Windows; `window`/`stdio`/`off`
   wymuszają wariant. Sama gra jest niewidoczna (`Window=hidden`; też `minimized`, `normal`).
   Działa w tle: nie zabiera fokusu, nie pauzuje się bez fokusu, bez dźwięku (`Mute`).
6. Dodatkowo:
   - `Render=startup` rysuje tylko do wejścia do menu (potem 0% GPU); `Fps` ogranicza klatki;
   - `UniqueIdentity` pozwala właścicielowi grać na własnym serwerze z tym samym kluczem CD;
   - Scroll Lock wstrzymuje automat, żeby można było klikać samemu.

Serwer działa z **osobnego folderu** obok zwykłej instalacji: wystarczą w nim `KnightShift.exe`,
`KnightShift.ex1`, `KnightShift.ex2`, `ijl10.dll` z folderu gry oraz `dinput8.dll`, `steam_api.dll`
i `ksnetfix.ini` z `build/` — dane gra czyta z instalacji (rejestr), profil „Serwer” trafia do
`Players` gry. Serwer używa własnego mutexa, więc na tym samym PC można normalnie uruchomić grę
i dołączyć (na razie przez LAN, `[Steam] Enabled=0` u obu — dwie gry na jednym koncie Steam nie
połączą się przez Steam, to rozwiąże anonimowy serwer Steam z planu).

## Wymagania

- KnightShift ze Steama, silnik „D3D8 classic” (`KnightShift.ex1`) — jedyny obsługiwany na razie.
- Wszystko, czego wymaga KSNetFix (Windows 10/11, DirectPlay, Steam). Serwer hostuje na razie jako zwykły
  użytkownik Steam, więc na maszynie serwera musi działać klient Steam z kontem, które ma grę.
- Gracze: [KSNetFix](https://github.com/Joe13PL/KSNetFix) w tej samej wersji.

## Budowanie i instalacja

```bash
git clone --recurse-submodules https://github.com/Joe13PL/KnightShift-DedicatedServer
cd KnightShift-DedicatedServer
./build.sh        # build/dinput8.dll, steam_api.dll, ksnetfix.ini (KSNetFix + [Server])
```

Narzędzia jak dla KSNetFix: Git Bash, Visual Studio 2022 (C++ x86), Steamworks SDK w `./sdk` albo
`STEAMWORKS_SDK`. Przygotuj folder serwera (wyżej) albo skopiuj trzy pliki z `build/` do folderu
gry na maszynie serwera, ustaw sekcję `[Server]` w `ksnetfix.ini` i uruchom `KnightShift.exe` z tego
folderu. Przebieg widać w `ksnetfix.log` (linie `server: ...`).

Sprawdzenie adresów bez uruchamiania gry (np. po aktualizacji gry):

```bash
python tools/check_server_sigs.py "C:/.../steamapps/common/KnightShift/KnightShift.ex1"
```

## Plan

- [x] Test w grze (LAN): profil i bohater, menu, lobby, start, obserwator, restart; praca w tle.
- [x] Konsola serwera, automatyczny koniec gry po wyjściu ostatniego gracza (test 2026-09-28).
- [x] Ograniczenie grafiki (`Render=startup`) i konsola przez terminal (`Console=stdio`); w lobby 0% GPU.
- [ ] Test przez Steam, dołączanie w trakcie gry, klucz CD na czystej maszynie.
- [x] **Linux VPS przez Wine:** skrypty w [`server/linux/`](server/linux/) (Xvfb, prefiks Wine, rejestr
      gry, systemd) i log/komendy w terminalu — **jeszcze nieprzetestowane na żywym Linuksie**.
- [~] **Anonimowy serwer gry Steam** (`[Steam] GameServer=1`): logowanie anonimowe (`SteamGameServer`),
      ruch przez `SteamGameServerNetworkingMessages` (SDR). Bez konta i bez klienta Steam; gracze
      dołączają po SteamID64 serwera. Zbudowane, transport instaluje się — **pełny test na żywo przed nami**;
      lista serwerów w grze (zamiast wpisywania SteamID) później.
- [ ] Wsparcie `KnightShift.ex2`, komunikaty czatu dla graczy („Kliknij Gotowy”), usługa Windows.

## Licencja i zastrzeżenia

Kod na licencji [MIT](LICENSE). Projekt nie jest związany z twórcami ani wydawcami gry; KnightShift /
Polanie II to znaki towarowe ich właścicieli. Repozytorium nie zawiera plików gry — serwer potrzebuje
własnej kopii gry (z własnym kluczem CD).

---

## English

A dedicated **RPG** server for **KnightShift (Polanie II)**, Steam version 1.3, built as an add-on to
[KSNetFix](https://github.com/Joe13PL/KSNetFix). The game is lockstep (every peer simulates everything,
the host also saves the game for players joining mid-game), so the server is a normal game instance that
KSNetFix drives through the game's own menus:
- it creates a profile and an RPG hero;
- it hosts an RPG session with join-in-progress enabled and starts it when the players are ready;
- it turns its own player into an observer and re-hosts after each game.

**Status: prototype, not yet tested in game.**

Build with `./build.sh` (needs the KSNetFix submodule, VS 2022 C++ x86 and the Steamworks SDK). Copy
`build/*` into the game folder of the server machine and configure `[Server]` in `ksnetfix.ini`.

Planned:
- an anonymous Steam game server identity (no Steam client needed);
- Linux VPS support through Wine.

Design notes and addresses: [`docs/DEDICATED_SERVER.md`](docs/DEDICATED_SERVER.md) (Polish). MIT
licensed; not affiliated with the game's developers or publishers.
