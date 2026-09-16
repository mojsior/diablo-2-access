# Diablo 2 Access

Mod dostępności do klasycznego Diablo II (Game.exe 1.14b) dla osób niewidomych i słabowidzących. Mowa idzie przez NVDA albo syntezator Windows. Mod czyta menu, postać, ekwipunek, drzewko umiejętności i otoczenie. Prowadzi też do celów i gra dźwięki zbliżeniowe tak jak [Diablo Access](https://github.com/mojsior/diablo-access).

An accessibility mod for blind and low-vision players of classic Diablo II (Game.exe 1.14b). Speech goes through NVDA or the Windows speech synthesizer. The mod reads menus, the character, the inventory, the skill tree and the surroundings, guides you to targets and plays proximity sound cues like [Diablo Access](https://github.com/mojsior/diablo-access).

## Dokumentacja / Documentation

- [Instrukcja po polsku](docs/README_PL.md)
- [English user guide](docs/README_EN.md)
- [Notatki z IDA: adresy funkcji i struktur gry](docs/ida_notes.md)

## Wymagania

- Diablo II z dodatkiem Lord of Destruction w wersji 1.14b, domyślnie w `C:\Program Files (x86)\Diablo II`. Nie wybieraj w menu gry opcji BATTLE.NET: uruchamia ona aktualizator Blizzarda, który psuje pliki gry, a nowsza wersja nie działa z modem ([szczegóły](docs/README_PL.md#nie-aktualizuj-gry--mod-działa-z-wersją-114b)).
- Windows 10 lub 11.
- NVDA (opcjonalnie; bez NVDA mod mówi syntezatorem Windows).

## Instalacja

Grę Diablo II i dodatek Lord of Destruction kupisz w Battle.net. Instalator Blizzarda ma graficzne menu i niedostępną umowę licencyjną, dlatego mod ma asystenta `D2AccessSetup.exe`. Szczegóły są w [instrukcji](docs/README_PL.md#instalacja-gry-diablo-ii).

1. Pobierz `D2AccessSetup.exe` z najnowszego wydania na stronie [Releases](https://github.com/mojsior/diablo-2-access/releases) i uruchom go.
2. Asystent przeprowadzi Cię przez instalator gry, czytając jego okna. Klucz CD wpisujesz sam.
3. Po instalacji gry asystent pobierze z GitHuba najnowszą wersję moda i zainstaluje ją w folderze gry. Gdy gra jest już zainstalowana, asystent od razu instaluje albo aktualizuje moda.
4. Grę z modem uruchamiasz plikiem `D2AccessLauncher.exe` z folderu gry.

Paczka `diablo-2-access-…-windows.zip` z tego samego wydania zawiera pliki moda do ręcznego rozpakowania do folderu gry.

## Requirements

- Diablo II with Lord of Destruction, version 1.14b, by default in `C:\Program Files (x86)\Diablo II`. Do not choose BATTLE.NET in the game menu: it starts Blizzard's updater, which breaks the game files, and the newer version does not work with the mod ([details](docs/README_EN.md#keep-the-game-on-version-114b)).
- Windows 10 or 11.
- NVDA (optional; without NVDA the mod speaks with the Windows synthesizer).

## Installation

Diablo II and Lord of Destruction are sold on Battle.net. Blizzard's installer has a graphical menu and an inaccessible licence agreement, so the mod has the assistant `D2AccessSetup.exe`. See the [user guide](docs/README_EN.md#installing-diablo-ii).

1. Download `D2AccessSetup.exe` from the newest release on the [Releases](https://github.com/mojsior/diablo-2-access/releases) page and run it.
2. The assistant guides you through the game installer, reading its windows aloud. You type the CD-key yourself.
3. After the game is installed, the assistant downloads the newest mod release from GitHub and installs it into the game folder. When the game is already installed, it installs or updates the mod right away.
4. Start the game with the mod using `D2AccessLauncher.exe` in the game folder.

The `diablo-2-access-…-windows.zip` package of the same release holds the mod files for extracting into the game folder by hand.

## Build

Visual Studio 2022, Win32:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

Wynik trafia do `build\Release`. Logi `D2AccessLauncher.log` i `D2AccessHook.log` są zapisywane obok plików moda.

The output goes to `build\Release`. The logs `D2AccessLauncher.log` and `D2AccessHook.log` are written next to the mod files.

## Podziękowania / Credits

- [Diablo Access](https://github.com/mojsior/diablo-access): klawisze, tracker, opis drogi i dźwięki zbliżeniowe oraz ich pliki / keys, tracker, path speech, proximity cues and their sound files.
- [Pokemon Access](https://github.com/nuive/pokemon-access): pomysł na opis drogi w krokach / the step-based path description idea.
- [nlohmann/json](https://github.com/nlohmann/json) (MIT) i NVDA Controller Client (LGPL 2.1).
