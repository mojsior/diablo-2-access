# IDA notes for classic Diablo II `Game.exe`

Plik dokumentuje funkcje zdekompilowane i opisane w IDA. Dotyczy `Game.exe` 1.14b
(wersja pliku 1.14.1.68, baza obrazu `0x400000`). Baza IDA: `C:\Program Files (x86)\Diablo II\Game.exe.i64`.

## Serwer MCP dla IDA

- Plugin `ida-pro-mcp` jest w `%APPDATA%\Hex-Rays\IDA Pro\plugins` i startuje automatycznie po otwarciu bazy
  (`http://127.0.0.1:13337/mcp`).
- Serwer jest zarejestrowany w Claude Code (zakres użytkownika) pod nazwą `ida-pro-mcp`.
- Plugin nie startuje, dopóki w IDA nie ma otwartej bazy.

## Frontend (zmienione nazwy w IDA)

- `0x685E80` -> `FE_MainMenu_Show`
- `0x687D10` -> `FE_CharacterCreate_Show`
- `0x6863C0` -> `FE_CharacterCreate_HandleClassSelection`
- `0x682EF0` -> `FE_CharacterCreate_ValidateName`
- `0x689180` -> `FE_CharacterCreate_OnCreateButton`
- `0x688D40` -> `FE_CharacterCreate_Commit`

### Obserwacje

- `FE_MainMenu_Show` inicjalizuje klasyczne menu główne i jest dobrym punktem wejścia do ogłoszenia ekranu.
- `FE_CharacterCreate_Show` buduje ekran tworzenia postaci, resetuje klasę, pole imienia i przycisk create.
- `FE_CharacterCreate_HandleClassSelection` obsługuje hover i wybór klasy, aktualizuje indeks klasy w `0x745BBC`.
- `FE_CharacterCreate_ValidateName` włącza lub wyłącza przycisk create.
- `FE_CharacterCreate_OnCreateButton` sprawdza przycisk i miejsce na dysku, potem woła `FE_CharacterCreate_Commit`.

### Globalne dane frontendu

- `0x745BBC` - zaznaczona klasa
- `0x97F628` - przycisk create
- `0x97F864..0x97F874` - przyciski klas (Barbarian, Necromancer, Amazon, Sorceress, Paladin)
- `0x688450` / `0x6884B0` / `0x688500` - Single Player / Battle.net / Other Multiplayer

## Świat gry (gameplay)

Układ struktur w 1.14b odpowiada 1.13c, a nie 1.10 z D2MOO. Wcześniejsza wersja moda używała offsetów
z 1.10 (np. lista jednostek w roomie pod `0x2C`), dlatego widziała tylko jeden room i `level id` 0.

### Funkcje (nazwy nadane w bazie IDA)

| Adres | Nazwa | Uwagi |
| --- | --- | --- |
| `0x43C280` | `CL_GameFrameCallback` | `stdcall(int)`, wywoływana co klatkę przez `CL_MessageLoop`; mod ją hookuje (prolog 5 bajtów) |
| `0x43ED70` | `CL_MessageLoop` | pętla komunikatów w grze |
| `0x43C630` | `CL_RunGameSession` | sesja gry, woła `CL_MessageLoop(CL_GameFrameCallback)` |
| `0x451180` | `CL_SetCurrentPlayerUnit` | zapisuje `gpCurrentPlayerUnit` |
| `0x6160A0` | `DUNGEON_FindRoomBySubtileCoords` | `stdcall(Act*, x, y)` |
| `0x615A60` | `DUNGEON_GetAdjacentRoomsList` | Room1+0x00 lista, Room1+0x24 liczba |
| `0x616300` | `DUNGEON_GetCollisionGridFromRoom` | Room1+0x20 |
| `0x6164A0` | `DUNGEON_GetLevelIdFromRoom` | Room1+0x10 -> Room2 -> Level -> id |
| `0x6164C0` | `DUNGEON_GetWarpDestinationLevel` | `stdcall(Room1*, tileClassId)` |
| `0x616360` | `DUNGEON_AddRoomData` | `stdcall(Act*, levelId, room2 tileX, room2 tileY, Room1* hint)` |
| `0x6163B0` | `DUNGEON_RemoveRoomData` | ta sama sygnatura |
| `0x617910` / `0x617960` | `DRLGACTIVATE_AddRoomData` / `RemoveRoomData` | `fastcall(DrlgMisc*, levelId, x, y, Room2* hint)`, liczniki aktywacji Room2+0x0C |
| `0x6405F0` | `DRLG_GetLevel` | DrlgMisc+0x47C lista leveli, Level+0x1AC następny |
| `0x640090` | `DRLG_GetRoom2FromLevelAt` | Level+0x10 pierwszy Room2, Room2+0x24 następny |
| `0x66AD40` | `DRLGROOM_GetLevelId` | Room2+0x58 Level, Level+0x1D0 id |
| `0x66AD50` | `DRLGROOM_GetWarpDestinationLevel` | `fastcall(Room2*, tileClassId)`; **woła `exit(-1)` gdy nie znajdzie przejścia** |
| `0x669E30` | (dopasowanie RoomTile) | RoomTile+0x10 -> rekord lvlwarp, pierwsze pole == classId kafla |
| `0x61D390` | `UNITS_GetRoom` | typy 2, 4, 5 mają ścieżkę statyczną |
| `0x64B170` | `COLLISION_CheckMaskWithPattern` | wzór 1/3/5 = krzyż 5 podpól |
| `0x64AC90` | `COLLISION_CheckMask` | |
| `0x465C40` | `CL_SendPacket` | `usercall`: pakiet w `ebx`, długość w `edi`; odrzuca identyczny pakiet ruchu w ciągu 200 ms |
| `0x465EB0` | `CL_SendSmallPacket` | `fastcall(id, x, y)` |
| `0x63E7D0` | `DATATBLS_GetObjectsTxtRecord` | rekord 0x1C0, `exit()` przy złym id |
| `0x61A0B0` | `DATATBLS_GetLevelsTxtRecord` | rekord 0x220, klucz nazwy +0xF5 |
| `0x520BC0` | (tekst po kluczu) | `thiscall(char* key)` -> zlokalizowany `wchar_t*` |
| `0x5207C0` | `STRTABLE_GetStringById` | |
| `0x451E40` | `CL_GetUnitName` | `thiscall(unit)`, zlokalizowana nazwa potwora/obiektu/przedmiotu |
| `0x450C00` | `UNITS_GetMonsterName` | MonsterData+0x2C |
| `0x4421B0` | `CL_DrawUnitHoverText` | podpowiedź po najechaniu, rozgałęzienie po typie jednostki |
| `0x66B1A0` | `DRLGROOM_AllocPresetUnit` | budowa listy presetów Room2+0x5C |
| `0x66B080` | `DRLGROOM_AllocRoomTile` | budowa listy RoomTile Room2+0x4C |

### Globalne dane

- `0x79D0B0` `gpCurrentPlayerUnit`
- `0x796C74` `gpClientAct`
- `0x97F418` `gpObjectsTxt`, `0x97F41C` `gnObjectsTxtCount`
- `0x7B1BFC` lista zwierzaków gracza (RosterPets.cpp, `sub_466430`): rekord 0x34, +0x00 właściciel, +0x04 typ,
  +0x08 id jednostki, +0x30 następny. Najemnicy i przywołańcy nie są liczeni jako potwory.

### Struktury

```
Unit:        +00 type, +04 classId, +0C unitId, +10 mode, +14 data, +2C path, +C4 flags, +E8 next in room
DynamicPath: +02 x (word), +06 y (word), +1C Room1
StaticPath:  +00 Room1, +0C x, +10 y
Act:         +10 first Room1, +48 DrlgMisc
Room1:       +00 adjacent list, +10 Room2, +20 collision, +24 adjacent count,
             +4C/50/54/58 x/y/w/h (subtiles), +74 first unit, +7C next Room1
Collision:   +00/04/08/0C x/y/w/h (subtiles), +20 uint16 mask
Room2:       +08 adjacent list, +24 next, +2C adjacent count, +30 Room1,
             +34/38/3C/40 x/y/w/h (tiles), +4C room tiles, +58 Level, +5C presets
Preset:      +04 classId, +08 x, +0C next, +14 unit type, +18 y (subtiles relative to Room2)
RoomTile:    +00 destination Room2, +04 next, +08 enabled, +10 lvlwarp record
Level:       +10 first Room2, +1C/20/24/28 x/y/w/h (tiles), +1AC next, +1B4 DrlgMisc, +1D0 id
ObjectsTxt:  +00 name, +40 wide name, +C4 selectable[mode], +13A isDoor, +167 subclass, +1B3 operateFn
MonsterData: +2C wchar_t* name
PlayerData:  +00 char name[16]
```

1 kafel = 5 podpól. Współrzędne jednostek i pakiety ruchu używają podpól.

### Statystyki, doświadczenie, ekwipunek i panele

| Adres | Znaczenie |
| --- | --- |
| `0x621C90` | `stdcall(Unit*, statId, layer)` - wartość statystyki z przedmiotami (Unit+0x5C lista statystyk) |
| `0x621B70` | `stdcall(StatList*, statId, layer)` - wartość bazowa |
| `0x43ADF0` | poziom trudności 0/1/2 (bajt) |
| `0x479E40` | `fastcall(Item*, wchar_t* buf, int size)` - pełna nazwa przedmiotu |
| `0x97E850` | wskaźnik na `experience.txt`: wiersze 32 bajty (7 klas + ExpRatio), wiersz 0 = MaxLvl, wiersz n = doświadczenie na poziom n |
| `0x798E00` | UI vars (`sub_440AD0`, 38 wpisów): 1 = ekwipunek, 2 = karta postaci |

Życie, mana i wytrzymałość są zapisane jako wartość << 8. Kara odporności: 0 / 40 / 100.

```
Inventory:  +00 0x01020304, +08 owner, +0C first item, +10 last item, +14 grids, +18 grid count,
            +20 item on cursor, +28 item count
Grid (0x10): +00 first, +04 last, +08 width, +09 height, +0C cell array (Item* per cell)
            grid 0 = body (index = body location), 1 = belt (16), 2 = inventory (10 x 4)
ItemData:   +44 body location, +45 page (0 inventory, 3 cube, 4 stash, FF none),
            +5C owner inventory, +60 prev, +64 next, +69 node (1 storage, 2 belt, 3 equipped)
Item path:  +0C x (grid column / belt slot), +10 y (grid row)
```

### Umiejętności

- `sgptDataTables` = `[0x73D020]`: +0xB8C skilldesc.txt (rekord 0x120), +0xB94 liczba, +0xB98 skills.txt (rekord 0x23C),
  +0xBA0 liczba, +0xBA4 wskaźnik na liczby umiejętności klas, +0xBA8 największa liczba, +0xBAC lista `short` (klasa * największa + i)
- skills.txt: +0x00 id, +0x0C klasa, +0x174 wymagany poziom, +0x17E trzy wymagane umiejętności, +0x194 indeks skilldesc, +0x1DC typ żywiołu
- skilldesc.txt: +0x02 zakładka 1..3, +0x03 wiersz, +0x04 kolumna, +0x08 nazwa (id tekstu), +0x0A krótki opis
- Unit+0xA8 -> +0x04 pierwsza umiejętność gracza; umiejętność: +0x00 rekord skills.txt, +0x04 następna, +0x28 wydane punkty
- `0x641B10` - bonus poziomów z przedmiotów (staty 127, 83 warstwa klasa, 188 warstwa zakładka + 8 * klasa - 1, 97, 126, 107)
- `0x499F70` - kliknięcie w drzewku; dodanie punktu to pakiet `0x3B` (word id umiejętności)
- `0x5207C0` - `fastcall(id)` tekst po numerze; `0x406290` - czy działa dodatek LoD

### Menu gry (Escape) i opcje

- UI var 4 = drzewko umiejętności, UI var 9 = menu Escape
- `[0x7B2F7C]` nagłówek bieżącego menu (+0 liczba pozycji), `[0x7B2F80]` pozycje (0x550 każda), `0x7B2F78` zaznaczona pozycja
- pozycja: +0 typ (-1 tytuł, 0 przycisk, 1 wybór, 2 suwak), +4 tylko dodatek, +12 nazwa grafiki, +272 warunek dostępności,
  +276 akcja `fastcall(pozycja, komunikat)`, +288 liczba wariantów, +292 wartość, +300 nazwy wariantów (po 260 bajtów)
- `0x46AF90` aktywuje zaznaczoną pozycję; oczekuje w `esi` komunikatu okna (+0 HWND), bo „Zapisz i wyjdź” (`0x46CCC0`) wysyła do niego WM_CLOSE
- menu: główne `0x70E5D0`, opcje `0x70E5E4`, dźwięk `0x711078`, obraz `0x716050`/`0x71108C`, automapa `0x71AAD8`/`0x716064`

### Opis przedmiotu (dymek) i mysz

- `0x79D0F0` / `0x79D0EC` - pozycja myszy X / Y (współrzędne ekranu gry), `0x70CD40` / `0x70CD44` - rozdzielczość
- `0x475100` - obsługa WM_MOUSEMOVE nad panelem ekwipunku; tylko ona wylicza najechany przedmiot `0x7B3234` (flagi `0x7B3224`/`0x7B3228`)
- `0x47BC70` - buduje pełny opis najechanego przedmiotu, `0x4F5B90` (`fastcall(text, x, …)`) kopiuje tekst dymka do `0x874978` (wchar 0x400)
- `0x4710E0` - układ paneli z inventory.txt (`[0x97F49C]`, rekord 0xF0, indeks = rekord klasy + 16 × flaga rozdzielczości `0x79B858`):
  siatka plecaka `0x7B31C8` (bajty kolumny/wiersze, left, right, top, bottom, bajty rozmiar pola),
  prostokąty miejsc na ciele (left, right, top, bottom, rozmiar) od `0x7B32E8` w kolejności kolumn inventory.txt
- Mod wysyła WM_MOUSEMOVE nad wybrane pole i czyta `0x874978`, gdy `0x7B3234` wskazuje ten przedmiot

### Śmierć

- Zwłoki gracza to jednostka typu gracz w trybie 0/17; pakiet `0x13` (typ 0, id) odzyskuje z nich ekwipunek
- Zapis przed odrodzeniem ma flagę „zginął” (bajt statusu 0x08 w .d2s) i życie 0; uzdrowiciel w mieście przywraca życie

### Menu główne i język

- przyciski menu głównego z tabeli `0x741D48` (rekord 48 bajtów: +0x18 id tekstu, +0x20 callback `stdcall(int)`):
  jeden gracz `0x688450`, Battle.net `0x6884B0`, brama `0x686DA0`, inne `0x6834F0`, twórcy `0x683B70`, filmy `0x683EB0`, wyjście `0x6844C0`
- `0x4EB520` - brak d2char.mpq (instalacja „spawn”), wtedy przyciski wieloosobowe są wyłączone
- `0x521090(char*, 0)` - kod języka gry („ENG”, „POL”, …), tabela języków `0x72AACC` (rekord 24 bajty)

### Pakiety klienta używane przez moda

- `0x01` / `0x03` - idź / biegnij do punktu (x, y word)
- `0x06` - lewa umiejętność na jednostkę (atak)
- `0x13` - interakcja z jednostką (obiekt, NPC, kafel przejścia)
- `0x16` - podniesienie przedmiotu (typ, id, akcja)
