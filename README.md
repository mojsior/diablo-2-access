# Diablo 2 Access

Mod dostępności do klasycznego Diablo II (Game.exe 1.14b) dla osób niewidomych i słabowidzących. Mowa idzie przez NVDA albo syntezator Windows. Mod czyta menu, postać, ekwipunek, drzewko umiejętności i otoczenie. Prowadzi też do celów i gra dźwięki zbliżeniowe tak jak [Diablo Access](https://github.com/mojsior/diablo-access).

An accessibility mod for blind and low-vision players of classic Diablo II (Game.exe 1.14b). Speech goes through NVDA or the Windows speech synthesizer. The mod reads menus, the character, the inventory, the skill tree and the surroundings, guides you to targets and plays proximity sound cues like [Diablo Access](https://github.com/mojsior/diablo-access).

## Dokumentacja / Documentation

- [Instrukcja po polsku](docs/README_PL.md)
- [English user guide](docs/README_EN.md)
- [Notatki z IDA: adresy funkcji i struktur gry](docs/ida_notes.md)

## Wymagania

- Diablo II z dodatkiem Lord of Destruction w wersji 1.14b, domyślnie w `C:\Program Files (x86)\Diablo II`.
- Windows 10 lub 11.
- NVDA (opcjonalnie; bez NVDA mod mówi syntezatorem Windows).

## Instalacja

1. Pobierz paczkę `diablo-2-access-…-windows.zip` z zakładki [Releases](https://github.com/mojsior/diablo-2-access/releases).
2. Rozpakuj ją do dowolnego folderu.
3. Uruchom `D2AccessLauncher.exe`. Jeśli gra jest w innym miejscu, podaj ścieżkę: `D2AccessLauncher.exe "D:\Gry\Diablo II\Game.exe"`.

## Requirements

- Diablo II with Lord of Destruction, version 1.14b, by default in `C:\Program Files (x86)\Diablo II`.
- Windows 10 or 11.
- NVDA (optional; without NVDA the mod speaks with the Windows synthesizer).

## Installation

1. Download `diablo-2-access-…-windows.zip` from [Releases](https://github.com/mojsior/diablo-2-access/releases).
2. Extract it to any folder.
3. Run `D2AccessLauncher.exe`. If the game is elsewhere, pass its path: `D2AccessLauncher.exe "D:\Games\Diablo II\Game.exe"`.

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
