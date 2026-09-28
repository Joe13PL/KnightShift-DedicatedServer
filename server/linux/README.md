# Serwer KnightShift na Linuksie (Wine)

Uruchamia serwer dedykowany na VPS/serwerze z Linuksem, bez pulpitu. Gra chodzi pod Wine na
wirtualnym ekranie (Xvfb), a bieżący log i komendy serwera są w zwykłym terminalu.

> **Status: nieprzetestowane na żywym Linuksie.** Skrypty powstały na podstawie analizy i działającego
> serwera na Windows (ta sama binarka gry, `KnightShift.ex1` uruchamiany bez launchera, konsola przez
> stdin/stdout). Przetestuj na swoim VPS i zgłoś wynik w Issues.

## Wymagania

- **Własna, legalna kopia gry.** Skrypty nie zawierają ani nie pobierają plików gry. Zdobądź je
  na maszynę zgodnie ze swoją licencją, np.:
  - skopiuj folder gry ze swojego PC (Steam → KnightShift → Zarządzaj → Przeglądaj pliki lokalne), albo
  - zainstaluj przez `steamcmd` na swoim koncie Steam (`app_update 254060`).
- Pakiety: `wine` (z obsługą 32-bit), `winetricks`, `xvfb`. Na Debianie/Ubuntu:
  ```
  sudo dpkg --add-architecture i386 && sudo apt update
  sudo apt install wine wine32:i386 winetricks xvfb
  ```
- Pliki serwera z tego repo: zbuduj `dinput8.dll` i `steam_api.dll` (patrz główny README, `./build.sh`).

## Katalog serwera

Załóż katalog (np. `~/ks-server`) z plikami gry i serwera:

```
KnightShift.exe  KnightShift.ex1  KnightShift.ex2  ijl10.dll   <- z Twojej kopii gry
WDFiles/ ...                                                   <- dane gry (patrz "Lekki zestaw")
dinput8.dll  steam_api.dll                                     <- z build/ tego repo
ksnetfix.ini                                                   <- z build/ (KSNetFix + [Server])
```

W `ksnetfix.ini`, w sekcji `[Server]`, na Linuksie użyj: `Console=auto` (albo `stdio`),
`Window=hidden`, `Render=startup`. W `[Steam]` na razie `Enabled=0` (gra po LAN/relay Steam
przez anonimowy serwer to osobny, planowany krok).

## Uruchomienie

```bash
cd KnightShift-DedicatedServer/server/linux
./setup.sh ~/ks-server     # raz: prefiks Wine, czcionki, klucze rejestru gry
./run.sh   ~/ks-server     # start serwera; log i komendy w tym terminalu
```

W terminalu wpisujesz komendy (`status`, `start`, `end`, `pause`, `resume`, `show`, `hide`, `quit`),
tak jak w konsoli na Windows. `Ctrl+C` wyłącza serwer (gra opuszcza sesję).

Sprawdź `~/ks-server/ksnetfix.log` — pierwsze linie muszą kończyć się `ok`, a przy starcie
pojawi się `server: level ...` (serwer w lobby). Jeśli brakuje czcionek na ekranie klucza,
uzupełnij `winetricks corefonts`.

## Jako usługa (auto-start, restart)

`knightshift-server.service` (systemd): edytuj `User` i ścieżki, skopiuj do
`/etc/systemd/system/`, potem `systemctl enable --now knightshift-server`. Usługa nie ma
terminala, więc sterujesz nią przez `systemctl`; log: `journalctl -u knightshift-server -f`.

## Lekki zestaw danych

Serwer nie potrzebuje filmów, muzyki ani mowy. Możesz pominąć z `WDFiles/` pliki:
`Video*.wd`, `Music.wd`, `Speeches.wd` (razem ~0,5 GB). Które dokładnie da się pominąć, trzeba
potwierdzić na czystym prefiksie — gra przy braku pliku zwykle pisze o tym w logu. Zawsze zostaw
`Scripts.wd`, `Levels*.wd`, `Parameters.wd`, `Config.wd`, `Terrains*.wd`, `Meshes.wd`,
`Textures.wd`, `Interface*.wd`, `Language.wd`, `Shaders.wd`, `SPS.wd1`, `SPG.wd1`, `Players.wd`.

## Grafika bez GPU

`Render=startup` sprawia, że gra rysuje tylko kilka klatek startowych, więc wystarcza programowy
OpenGL (Mesa llvmpipe) na Xvfb — GPU nie jest wymagane. Jeśli start nie dochodzi do menu
(w logu brak `server: level`), sprawdź, czy Wine ma działający OpenGL: `wine glxgears` albo
`LIBGL_ALWAYS_SOFTWARE=1` przed `./run.sh`.

## Jak to działa

`run.sh` uruchamia `KnightShift.ex1` bezpośrednio (pomija launcher `KnightShift.exe`), dzięki czemu
stdin/stdout terminala trafiają do serwera. Klucze rejestru z `setup.sh` zastępują skrypt
instalacyjny Steama, więc gra znajduje swoje dane bez klienta Steam. Reszta (profil i bohater
„Serwer”, lobby, start, obserwator, koniec gry) działa tak samo jak na Windows.
