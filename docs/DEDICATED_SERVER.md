# Serwer dedykowany (RPG, Steam, Windows VPS) — rozpoznanie i plan

Stan (2026-09-28): faza A (RE) zrobiona, prototyp fazy B w `src/server.cpp` — jeszcze nie
przetestowany w grze. Adresy dla `KnightShift.ex1`.

## Ograniczenie architektury

KnightShift to lockstep: **każdy** komputer liczy całą symulację, host tylko zbiera rozkazy i
wyznacza tury (`KSNetFix/docs/NETCODE.md`). Samodzielny, lekki program-serwer musiałby odtworzyć całą
logikę gry — nierealne. Serwer dedykowany = pełna instancja gry jako host **bez gracza**,
bez grafiki/dźwięku, sterowana konfiguracją. Host musi symulować także dlatego, że dołączanie
w trakcie gry polega na zapisaniu stanu gry przez hosta i wysłaniu go nowemu graczowi.

## Co już jest w grze

| mechanizm | gdzie | znaczenie dla serwera |
|---|---|---|
| Dołączanie w trakcie gry („dynamic connection”) | `Net_DynamicConnection` 0x7EA390, zapis `InternetTemp\DynamicConnect%d.sav`, transfer plików (klasy 1–7), komunikaty `translateDynamicConnection*` | stały świat RPG, do którego gracze wchodzą i wychodzą |
| Flaga sesji „pozwól dołączać” | wbudowana funkcja skryptu `IsAllowDynamicConnection` (0x671CE0) → `0x97B410`; opcja `translateMultiplayerDynamicConnect` w ekranach tworzenia sesji RTS i RPG | ustawiana przy tworzeniu sesji |
| Obserwator | rozkaz 0x15B (`0x4B8D70` wysyła, `0x488880` wykonuje), komunikaty 0x4B8DE0 / 0x4B8FC0 / 0x578670 | host przestaje grać po starcie |
| Protokół lobby | wiadomości klasy 0x10: `0x10010…0x100010` rejestrowane w 0x84A950 / 0x84B280 (EarthNet host/klient) i 0x84BBB0 / 0x84C500 (IP host/klient) | serwer musi prowadzić lobby sam |
| Tabela slotów lobby | `0xF632D8`, 8 × 0x24 B (+0 typ: 0 wolny, 3 gracz; +4 DPNID) | liczenie graczy |
| Start sesji | przycisk 0xFF86 w lobby hosta 0x833B50 (warunki niżej), odliczanie u graczy: handler `0xA0010` = 0x83CC10 (`Interface\Countdown.wav`) | automatyczny start gry |
| Bohater RPG | dane postaci `KS_RPG_ChData.1.0` (0x81F9C0, 0x823A30, 0x82F290), wybór 0x83D8B0 / 0x83AAC0 | start nie sprawdza bohatera hosta — do potwierdzenia w grze |
| Tożsamość gracza przy dołączaniu | `0x60010` → host 0x83C220 (duplikat/0 → `0x90010` „zły numer seryjny”) | serwer musi mieć własną tożsamość |
| Konsola | 32 komendy o zaszyfrowanych nazwach (0x401060…, rejestracja 0x794760–0x795040), wykonywanie 0x795A30, `autoexec.con` | **brak** komend sieciowych — automatyzacja przez wywołania wewnętrznych funkcji |

Handlery lobby: `0x10010`→0x83BCC0, `0x20010`→0x83BE40, `0x30010`→0x83BE60, `0x40010`→0x83C040,
`0x60010`→0x83C220, `0x70010`→0x83C500, `0x80010`→0x83C990, `0x90010`→0x83CB50, `0xA0010`→0x83CC10,
`0xB0010`→0x83CD80, `0xC0010`→0x83CFA0, `0xD0010`→0x83D200, `0xE0010`→0x83D2F0, `0xF0010`→0x833640,
`0x100010`→0x833960.

Steam: anonimowy serwer gry Steam (bez konta gracza) dla App ID **254060** loguje się i przyjmuje
połączenia przez SDR (test `sdrtest` z KSNetFix, `KSNetFix/src/test/sdrtest.cpp`, 2026-09-28). Serwery gry nie mogą tworzyć
lobby — wykrywanie przez listę serwerów Steam (`ISteamMatchmakingServers`) z tagiem KSNetFix.

## Wyniki rozpoznania (faza A)

### Menu i interfejs

* **Ekran menu:** młodszy bajt `0x93988C` (0 profil gracza, 1 menu główne, 4 rodzaj połączenia,
  5 sesje, 6 lobby hosta, 7 lobby klienta, 0x1B informacja o grze sieciowej). Okno bieżącego ekranu:
  `0x939824`. Przejście: `0x417E60(podekran << 8 | ekran)`.
* **Wspólny callback ekranów** `0x41EC80(dialog, msg, id, param)` (cdecl): rozdziela zdarzenia na
  obsługę ekranu (4 → 0x830E40, 5 → 0x831AF0, 6 → 0x833B50, 7 → 0x836C20, 0 → 0x5C95E0,
  8 → 0x408770). `msg` 0 = kliknięcie, 1 = otwarcie, 2 = zamknięcie, 0x11 = zmiana zaznaczenia,
  0x113 = takt interfejsu, 0x203 = dwuklik. Gdy obsługa ekranu zwróci 1, a kontrolka ma flagę
  `0x100000`, okno zamyka się z ID przycisku, a ID koduje następny ekran (np. „Multiplayer”
  0xFF04 → ekran 4).
* **Obiekty UI:** okno +0x68 = callback, vtbl+0xCC = kontrolka po ID. Kontrolka: +0x6C flagi
  (8 = zaznaczone), vtbl+0xC8 / +0xCC = pobierz / ustaw tekst (LPCWSTR). Lista: +0x88 liczba
  pozycji, +0xD0 zaznaczenie, vtbl+0xF8 zaznacz. Combo: vtbl+0xEC zaznacz. `0x7B0B30` ustawia
  pole wyboru.
* **Pętla główna** `0x405F40` (wątek okna). Pod semaforem symulacja/render woła co klatkę:
  `Net_Pump` (poza grą), 0x423A20, **0x7575F0** (miejsce haka serwera, 0x405FF0), render.
  Minimalny czas klatki: `0x937268` (ustawia `0x407360(fps, …)`).

### Od menu do startu gry

1. **Ekran 4** (0x830E40): lista 0x4AC z dostawcami TCP/IP (GUID pod 0x900928) i IPX (0x900948),
   dalej EarthNet. Pole adresu 0x4AF: „Nasłuchuj połączenia” (pozycja 0) → ekran sesji; adres →
   dołączenie (0x839900). „Inicjuj” = 0xFF05.
2. **Ekran 5** (0x831AF0): 0xFF06 = sesja RTS (0x838CB0), **0xFF08 = sesja RPG (0x8390A0)**. Ta
   druga czyta nazwę z pola 0x4D2 (format `nazwa:port`) i hasło z 0x4B5, ustawia flagę RPG `0xF6341C`
   i czyści sloty. Następnie hostuje przez `0x7EE5A0(nazwa, gracz, hasło, port)` i przechodzi do
   ekranu 6.
3. **Ekran 6** (0x833B50, inicjalizacja RPG 0x832240):
   * inicjalizacja rejestruje handlery lobby, **włącza „dołączanie w trakcie gry”** (0x555)
     i 0x553 oraz zaznacza pierwszy poziom na liście 0x511;
   * klik w 0x553 / 0x555 odczytuje stan, rozsyła go (0x20010) i odwraca pole — stan końcowy
     ustala broadcast;
   * zmiana poziomu (msg 0x11 na 0x511): tablica poziomów `0xF62768` (0x88 B: +0x10 nazwa,
     +0x18 wymagany poprzedni poziom, +0x48 GUID), liczba `0xF6276C`, mapowanie listy
     `0xF6279C` / `0xF627A0`, bieżący `0xF62790`. W RPG poziom może wymagać ukończenia
     poprzedniego (0x839E70, `translateRPGLevelFinishPrevLevel`);
   * pozostałe przyciski (np. „Wróć” 0xFF05) zamykają sesję i wracają do ekranu 4.
4. **Start 0xFF86:**
   * wymaga ≥ 2 slotów z graczem (typ 3) i znacznika „gotowy” u wszystkich poza hostem
     (maska `0xF626DC`, bit = slot);
   * w RPG sprawdza też wymagany poprzedni poziom;
   * potem odliczanie 1,5 s (flaga `0x400` w `0xF63414`) i rozesłanie poziomu (bajt 1, RPG: 3).
   * Host wstawia się do pierwszego wolnego slotu przy własnym „create player” (0x837B20).

**Automatyczny host bez UI** istnieje (EarthNet: 0x84A950 / 0x84B280, IP: 0x84BBB0 / 0x84C500, stan
`0x203` / `0x204`), ale jest związany z nieistniejącym już klientem EarthNet (`0xF625B0`) — nie
używamy go.

### W grze

* Gracz lokalny: `0x97B378`. **Obserwator:**
  * `0x4B8D70(gracz)` (fastcall) ustawia +0x730 i wysyła rozkaz lockstep 0x15B;
  * wykonanie `0x488880` ustawia +0x2C8 = 1 i wspólną widoczność;
  * dozwolone, gdy `0x4B8CA0(gracz)` — jest inny aktywny gracz-człowiek;
  * w grze to samo robią przycisk 0x5B5 w oknie porażki RTS (0x559170) i 0x55A „zostań
    obserwatorem” po śmierci bohatera RPG (0x578840).

### Profil, bohater, klucz CD

* **Profil gracza** = `Players\<nazwa>\UserInfo.dat` (zlib; nagłówek `UI\0\5`, nazwa UTF-16,
  ustawienia, dwie listy bohaterów `RC` z postaciami `RD`: klasa np. `RPG__HUNTER`, imię, GUID,
  ekwipunek). Ekran 0 (0x5C95E0): nazwa w polu „Nowy gracz” (0x4D9) + „Wejdź do gry” (1) wczytuje
  profil albo go tworzy (0x5CA8A0); lista profili to 0x4D8.
* **Bohater sieciowy RPG** (lista `0xA58E28`, pojedyncza gra `0xA58E40`, wybór `0x5CC6A0`):
  lobby hosta RPG przy pierwszym takcie sprawdza listę i gdy jest pusta, samo otwiera okno
  „Wybierz postać” (0x8329B0 → 0x55E6D0, zasób 15552, callback 0x55F210). Bez bohatera sesji
  nie da się wystartować. Okno: lista 0x41B, imię 0x4D2 (zmiana → komunikat 0x300 → 0x5659D0),
  0x10 „Utwórz nową postać” (0x5653B0: klasa domyślna, imię „<klasa> <profil>”), 0x1A
  „Akceptuj” (0x565C90: zapis do profilu, powrót do lobby i wybór postaci komunikatem 0x4BF).
  Pola okna: +0xB4 liczba postaci, +0xC4 wybrana.
* **Klucz CD / tożsamość:** gra trzyma klucz zaszyfrowany czasem utworzenia pliku i przy ekranie 0
  dekoduje z niego 64-bitowe ID (0x408350 → `0x937910/14`). Brak klucza → ekran 8 (0x408770:
  pola 0x484, 0x4B5, 0x485, 0x5AE po 4 znaki, OK = 0). Host odrzuca gracza z ID równym własnemu
  lub innego gracza (0x83C220 → `0x90010` „nieprawidłowy numer seryjny”). Poza tym ID używa tylko
  martwy klient EarthNet (0x803820), więc serwer może bezpiecznie zmienić swoje ID w pamięci.

## Prototyp (faza B) — `src/server.cpp`

`[Server] Enabled=1` w `ksnetfix.ini`. Hak na wywołaniu 0x7575F0 w pętli głównej (0x405FF0),
czyli tam, gdzie silnik sam obsługuje kliknięcia. Automat działa według ekranu menu:

| ekran | działanie |
|---|---|
| 8 klucz CD | wpisuje `CdKey` (tylko przy pierwszym starcie na nowej maszynie) |
| 0 profil | wpisuje `ProfileName` (domyślnie „Serwer”) — profil jest wczytywany albo tworzony |
| 1 / 0x1B | „Multiplayer” (0xFF04) |
| 4 | zaznacza TCP/IP, adres = „Nasłuchuj połączenia”, „Inicjuj” (0xFF05) |
| 5 | wpisuje `SessionName` / `Password`, „Utwórz nową sesję RPG” (0xFF08) |
| okno postaci | brak bohatera → „Utwórz nową postać”, imię `HeroName` (domyślnie „Serwer”), „Akceptuj” |
| 6 | poziom `Level`, pilnuje `DynamicConnect`, loguje wejścia / wyjścia / gotowość; gdy ≥ `MinPlayers` graczy gotowych przez `StartDelay` s → „Start” (0xFF86); lobby RTS → „Wróć” |
| 7 | „Wróć” (serwer nie dołącza do cudzych sesji) |
| gra | log połączeń; po `ObserverDelay` s gracz serwera → obserwator |

Czysta instalacja nie wymaga więc żadnego klikania: profil i bohater „Serwer” powstają przy pierwszym
starcie. Folder `Players\Serwer` można potem kopiować na inne serwery.

Po powrocie z gry automat zakłada sesję od nowa (`AutoRestart`). `Fps` wpisuje czas klatki do
`0x937268`. `UniqueIdentity=1` zmienia w pamięci ID serwera (XOR), więc właściciel z tym samym
kluczem CD może grać na własnym serwerze. Scroll Lock wstrzymuje automat. Serwerem jest tylko
pierwsza instancja gry na PC (mutex `Local\KSNetFixServer`), kolejne działają normalnie — test
serwera i klienta na jednym komputerze. Wszystkie adresy są weryfikowane sygnaturami
(`tools/check_server_sigs.py` sprawdza je offline na `KnightShift.ex1`). Wyjątek w automacie
wyłącza go (wpis `server: exception` w logu) zamiast wywracać grę.

## Linux VPS (Wine)

Wielu operatorów będzie chciało Linuksa. Ustalenia:

* **Gra działa pod Wine/Proton** — ProtonDB „silver” (8 raportów, najlepszy „platinum”). Znane:
  klucz CD przy pierwszym starcie (potrzebne `corefonts`), rozdzielczość 800×600 / 1024×768,
  gra sieciowa w oryginale wymaga `winetricks directplay`.
* **Pliki gry nie mają DRM Steam** (`KnightShift.exe/.ex1/.ex2` bez sekcji `.bind` i bez
  `steam_api`), więc gra działa bez klienta Steam. Importy: `d3d8`, `dinput8` (nasze proxy),
  `dsound`, `ole32` (DirectPlay przez COM).
* **Transport:** obecny prototyp hostuje jako zwykły użytkownik Steam (lobby Steam), więc na VPS
  potrzebowałby zalogowanego klienta Steam w Wine — ciężkie i kruche. Dlatego faza C (anonimowy
  serwer gry Steam) jest warunkiem Linuksa: wystarczy `steamclient.dll` z SteamCMD
  (app 1007 „Steamworks SDK Redist”, wersja Windows), bez konta i bez klienta. Anonimowy serwer
  z SDR dla App ID 254060 już sprawdzony (`sdrtest` z KSNetFix).
* **Grafika bez GPU:** Wine tłumaczy D3D8 na OpenGL (wined3d) albo Vulkan (DXVK) — na VPS
  programowo: Xvfb + Mesa llvmpipe / lavapipe; przy `Fps=10` koszt CPU powinien być mały.
* **Ładowanie KSNetFix:** `WINEDLLOVERRIDES="dinput8=n,b"` (nasze proxy przed wbudowanym dinput8).
* **Rejestr:** gra czyta klucze z `installscript` Steama (`HKLM\SOFTWARE\Reality Pump\KnightShift`:
  `version`, `BaseGame\FileSystem` `datapath`/`outputdir`, język, grafika) — na VPS trzeba je
  wgrać plikiem `.reg`, bo SteamCMD nie uruchamia skryptu instalacyjnego.
* **Pliki gry:** z własnej kopii (SteamCMD z loginem właściciela gry albo skopiowane z PC).

Do zrobienia: pakiet `server/linux` (skrypt przygotowania prefiksu Wine: `corefonts`, `.reg`,
`steamclient.dll`; uruchomienie przez Xvfb; usługa systemd) — po fazie C i teście na Windows.

## Plan

**A. Rozpoznanie (RE)** — zrobione (wyżej). Otwarte: ekrany po zakończeniu gry.

**B. Prototyp „Server mode” (PC z kartą graficzną)** — kod gotowy, test w grze przed nami:
1. Serwer + klient na jednym PC (LAN, `[Steam] Enabled=0`) albo na dwóch kontach Steam: klucz,
   profil i bohater „Serwer”, przejście menu, start, obserwator, dołączenie w trakcie gry, koniec
   gry i restart.
2. Poprawki według logu (`server: ...`), port adresów na `KnightShift.ex2` (`port_sigs.py`).
3. Wyciszenie dźwięku, okno w tle / zminimalizowane, komunikaty czatu dla graczy („Kliknij Gotowy”).

**C. Anonimowy serwer Steam (Windows i Linux VPS)**
- Serwer: `SteamGameServer` + `SteamGameServerNetworkingMessages` zamiast konta gracza i lobby.
- Klienci: sesje serwerów na liście „Dostępne sesje” przez `ISteamMatchmakingServers` (tag KSNetFix).
- Brak GPU: na Windows programowy sterownik (WARP) albo atrapa `d3d8.dll`; na Linuksie Xvfb + llvmpipe.
- Usługa Windows / systemd, restart po awarii.

**D. Linux** — pakiet Wine (wyżej) i test na VPS.

## Ryzyka

- Przejście w obserwatora wymaga innego aktywnego gracza (`0x4B8CA0`) — przy pustym serwerze gracz
  serwera zostaje w grze.
- Ekrany końca gry / statystyk mogą wymagać dodatkowych „kliknięć”.
- Oryginalne dołączanie w trakcie gry może być niestabilne przy długich sesjach.
- Wine: DirectSound bez karty dźwiękowej, D3D8 na llvmpipe, `DirectPlay8Address` z wbudowanego `dpnet`
  (nasz transport z niego korzysta) — do sprawdzenia.
