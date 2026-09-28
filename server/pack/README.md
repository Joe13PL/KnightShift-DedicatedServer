# Odchudzona paczka serwera (plug and play)

`make-pack.sh` buduje samodzielną paczkę serwera **z Twojej własnej, legalnej kopii gry**.
Wynik działa bez instalacji, bez Steama i bez uprawnień administratora — wystarczy rozpakować,
wpisać swój klucz i uruchomić `start.bat`.

> Paczka zawiera pliki gry, więc jest **tylko do Twojego użytku — nie rozpowszechniaj jej**.
> Repozytorium nie zawiera plików gry ani paczki (`dist/` jest w `.gitignore`); do serwera
> potrzebna jest własna, legalnie posiadana kopia gry i własny klucz.

## Budowanie

```bash
../../build.sh                       # najpierw zbuduj dinput8.dll (patrz główny README)
./make-pack.sh "/sciezka/do/gry"     # np. ".../steamapps/common/KnightShift"
# wynik: dist/KnightShift-Server/  oraz  dist/KnightShift-Server.zip
```

Paczka pomija filmy, muzykę i mowę (`Video*.wd`, `Music.wd`, `Speeches.wd`) — serwer ich nie
używa. Zmniejsza to rozmiar z ~1,2 GB do ~0,6 GB. Reszta danych gry jest w środku, więc paczka
jest samodzielna (potwierdzone: startuje z własnych danych i dochodzi do menu / ekranu klucza).

## Użycie paczki

Zawartość `dist/KnightShift-Server/`: pliki gry, `dinput8.dll` + `steam_api.dll` (nasz serwer),
`ksnetfix.ini`, `start.bat`, `stop.bat`, `INSTRUKCJA.txt`.

1. Rozpakuj gdziekolwiek.
2. W `ksnetfix.ini`, sekcja `[Server]`, w linii `CdKey=` wpisz swój legalny klucz gry.
   **Nie wpisuj klucza w konsoli serwera** — służy do tego wyłącznie ta linia w pliku.
3. Uruchom `start.bat`. `start.bat` kieruje grę do danych w folderze paczki (klucz rejestru dla
   bieżącego użytkownika, w HKCU — bez administratora), a `stop.bat` go usuwa. Gra startuje przez
   `KnightShift.exe` (Windows nie uruchamia `.ex1` bezpośrednio).

Szczegóły dla operatora w `INSTRUKCJA.txt` w paczce. Wersja na Linux/VPS: `server/linux/`.
