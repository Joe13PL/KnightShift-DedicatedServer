# Serwer KnightShift na Linuksie (Wine)

Serwer dedykowany na VPS/serwerze z Linuksem, bez pulpitu. Gra chodzi pod Wine na wirtualnym
ekranie (Xvfb), a bieżący log i komendy serwera są w zwykłym terminalu (albo w sesji `tmux`).

> **Stan (2026-09-29): sprawdzone na VPS** — Ubuntu 22.04, 4 vCPU AMD EPYC (KVM), bez GPU,
> WineHQ staging 11.18. Od czystego prefiksu: `setup.sh` ~2,5 min, start gry do menu ~20–35 s,
> potem rysowanie wyłączone (`Render=startup`). Doszło do ekranu klucza CD; pełna gra z graczami
> przez internet jeszcze przed nami.

## Wymagania

- **Własna, legalna kopia gry** z **własnym kluczem CD**. Skrypty nie zawierają ani nie pobierają
  plików gry. Najprościej: lekka paczka z [`server/pack`](../pack/) zbudowana z Twojej instalacji
  (~0,6 GB), przegrana na serwer (WinSCP/`scp`) i rozpakowana.
- Pakiety (Ubuntu 22.04/24.04, Debian). Wine z repozytorium dystrybucji (6.x) jest za stary —
  użyj WineHQ:
  ```bash
  sudo dpkg --add-architecture i386
  sudo mkdir -pm755 /etc/apt/keyrings
  sudo wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key
  sudo wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/$(lsb_release -cs)/winehq-$(lsb_release -cs).sources
  sudo apt update
  sudo apt install --install-recommends winehq-staging
  sudo apt install xvfb tmux cabextract unzip \
      libegl1:i386 libegl-mesa0:i386 libgl1:i386 libglx-mesa0:i386 libgl1-mesa-dri:i386
  # winetricks (aktualny):
  sudo wget -O /usr/local/bin/winetricks https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks
  sudo chmod +x /usr/local/bin/winetricks
  ```
  Bez 32-bitowego `libEGL` Wine nie uruchomi Direct3D — gra zamknie się zaraz po starcie.
- Pliki serwera z `build/` tego repo: `dinput8.dll`, `steam_api.dll`, `ksnetfix.ini`,
  `d3d8enum.exe` (lekka paczka zawiera je już).

## Katalog serwera

```
KnightShift.exe  KnightShift.ex1  KnightShift.ex2  ijl10.dll   <- z Twojej kopii gry
WDFiles/ ...                                                   <- dane gry (lekki zestaw wystarczy)
dinput8.dll  steam_api.dll  ksnetfix.ini  d3d8enum.exe         <- z build/ tego repo
```

W `ksnetfix.ini` wpisz swój klucz: `[Server] CdKey=XXXX-XXXX-XXXX-XXXX` (tylko w pliku, nigdy
w konsoli). Zalecane na Linuksie: `Console=auto`, `Window=hidden`, `Render=startup`. Dla gry
po TCP/IP (adres IP serwera) w `[Steam]` zostaw `Enabled=0`.

## Uruchomienie

```bash
cd KnightShift-DedicatedServer/server/linux
./setup.sh ~/ks-server     # raz: prefiks Wine, DirectPlay, rejestr gry, tryb grafiki
./run.sh   ~/ks-server     # serwer w tym terminalu (log + komendy), Ctrl+C wyłącza
```

Albo w tle, z konsolą dostępną w każdej chwili:

```bash
./start.sh ~/ks-server           # start w sesji tmux "knightshift"
tmux attach -t knightshift       # konsola serwera; wyjście bez zatrzymania: Ctrl+B, potem D
./stop.sh                        # wysyła "quit" i czeka na zamknięcie
```

Komendy: `status`, `start`, `end`, `pause`, `resume`, `show`, `hide`, `quit`. Log też w
`~/ks-server/ksnetfix.log` — przy poprawnym starcie pojawia się `menu ready`, a potem `level ...`
(serwer w lobby).

Gracze łączą się przez **Multiplayer → TCP/IP**, adres = IP serwera. U graczy w `ksnetfix.ini`
musi być `[Steam] Enabled=0` (inaczej KSNetFix zamienia TCP/IP na Steam). Jeśli VPS ma firewall
(np. `ufw`), przepuść ruch gry — DirectPlay zwykle używa UDP 6073 (wyszukiwanie sesji) i portów
z zakresu 2300–2400 (nie sprawdzone dla tej gry; najprościej zacząć bez firewalla).

## Jako usługa (systemd)

`knightshift-server.service`: edytuj `User` i ścieżki, skopiuj do `/etc/systemd/system/`,
potem `systemctl enable --now knightshift-server`. Usługa nie ma terminala — log:
`journalctl -u knightshift-server -f`. Jeśli chcesz mieć komendy, użyj `start.sh` (tmux).

## Co robi `setup.sh` (i dlaczego)

Każdy punkt to przeszkoda znaleziona przy uruchamianiu na VPS:

1. **Prefiks Wine 32-bit** bez instalatorów Mono/Gecko (ich okna czekałyby w nieskończoność na
   niewidocznym ekranie).
2. **DirectPlay** (`winetricks directplay`) — gra go wymaga, tak jak na Windows 11 trzeba włączyć
   funkcję DirectPlay. winetricks pobiera `directx_feb2010_redist.exe`; web.archive.org bywa
   przeciążone (błąd 429) — wtedy wgraj ten plik do `~/.cache/winetricks/directx9/` i powtórz.
3. **Rejestr gry** (`wine reg add`, nie import `.reg` — import gubił backslashe w ścieżce, a gra
   pokazywała „Game isn't properly installed”): ścieżka danych, język, `CheckMMX=0`, bez intro,
   najniższa jakość grafiki, sterownik dźwięku wyłączony.
4. **Tryb grafiki.** Bez zapisanego trybu `KnightShift.ex1` uruchamia konfigurator `Config.exe`
   (okno do klikania) i sam się zamyka. Na Windows robi to przy każdym starcie, dlatego rejestr
   bywa pusty. `d3d8enum.exe` odczytuje urządzenie D3D8, które widzi Wine (na VPS bez GPU:
   „NVIDIA GeForce GTX 470” — atrapa Wine na programowym OpenGL), a `run.sh` podaje grze
   ten sam parametr, co `Config.exe`: `-renderer ^<urządzenie>^,1024,768,32,0,0`.

Dodatkowo serwer (`dinput8.dll`) omija test MMX gry: na niektórych wirtualnych CPU jej
wykrywanie przez CPUID zawodzi i gra kończy się komunikatem „MMX Processor Required”.

## Grafika bez GPU

`Render=startup` sprawia, że gra rysuje tylko do wejścia do menu (~20–35 s na programowym
OpenGL, Mesa llvmpipe), potem nic — GPU nie jest potrzebne. Jeśli start nie dochodzi do menu,
sprawdź: `wine d3d8enum.exe` w katalogu gry (musi pokazać urządzenie) i czy są pakiety
`libegl1:i386` itd.
