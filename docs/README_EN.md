# D2 Access – user guide

D2 Access is an accessibility mod for classic Diablo II (Game.exe version 1.14b) for blind and low-vision players. It reads menus, the character, the inventory, the skill tree and the surroundings aloud. It also guides you to chosen targets, like the Diablo Access mod for the first Diablo.

Speech goes through NVDA. When NVDA is not running, the mod uses the Windows speech synthesizer (SAPI).

## Installing Diablo II

The mod needs the original Diablo II with the Lord of Destruction expansion, version 1.14b. Both can be bought in the Battle.net shop as "Diablo II" and "Diablo II: Lord of Destruction"; this is not Diablo II: Resurrected. After buying, Blizzard provides a downloader that fetches the installer into a folder such as `D2-1.14b-Installer-enUS` with `Installer.exe` and `Installer Tome.mpq`.

Blizzard's installer has a graphical menu and a licence agreement window that cannot be accepted with the keyboard alone. That is why the mod includes the installation assistant `D2AccessSetup.exe`.

### Installing with the assistant

1. Run `D2AccessSetup.exe` from the mod folder. If Windows asks for administrator rights, allow it; Blizzard's installer requires them.
2. The assistant looks for the installer in your Downloads and Desktop folders. If it finds several, it asks you to pick one with a number; if it finds none, it opens a file dialog where you select `Installer.exe`. You can also pass the path: `D2AccessSetup.exe "D:\Downloads\D2-1.14b-Installer-enUS"`.
3. The assistant starts the installer and chooses to install the game in the graphical menu.
4. At the licence agreement the assistant asks: Enter accepts the agreement, Escape declines it. After you accept, it scrolls the agreement and presses "Agree" for you.
5. Every following window (CD-key, install folder, DirectX, error messages) is read aloud and focused. You type the key yourself: the owner's name, then Tab and the 26-character key, then Enter.
6. While files are copied, the assistant reports progress every 10%, then says the installation is complete and closes the installer.
7. Install Lord of Destruction the same way with its own installer.

The assistant was tested with the Polish and English Diablo II installers up to the CD-key window. Choosing the folder, the progress and the end of the installation, and the Lord of Destruction installer, have not been tested in practice yet. If something goes wrong, attach `D2AccessSetup.log` from the mod folder to your report.

### Installer shortcuts without the assistant

The graphical installer menu responds to keys even though the screen reader cannot see it:

- D: install the game;
- P (G in the Polish version): play, when the game is already installed;
- U (O in the Polish version): uninstall;
- B (W in the Polish version): back;
- X (Z in the Polish version): exit the installer.

The CD-key, folder and DirectX windows are standard Windows dialogs used with Tab and Enter. Only the licence agreement cannot be accepted with the keyboard, because "Agree" becomes available only after the text was scrolled with the mouse.

## Starting the game

1. Run `D2AccessLauncher.exe` from the mod folder. It finds the game in `C:\Program Files (x86)\Diablo II\Game.exe`.
2. If the game is installed elsewhere, pass its path, for example: `D2AccessLauncher.exe "D:\Games\Diablo II\Game.exe"`.
3. After a moment you hear "Main menu" and the selected option.

The game window must be in the foreground, otherwise the game ignores keys.

## Language

The mod speaks Polish or English. By default it follows the installed game: a Polish game gives Polish speech, an English game gives English speech. Names that come from the game itself, such as items, areas and skills, are always in the game's language.

To force a language, create `D2Access.ini` in the mod folder (next to `D2AccessHook.dll`):

```
[General]
Language=en
```

Allowed values:

- `auto` – follow the game language (default)
- `pl` – always Polish
- `en` – always English

## Main menu

- Up and down arrows: choose an option. The mod reads the name and position, for example "SINGLE PLAYER, 1 of 7".
- Enter or Space: confirm the option.
- 1: start single player directly.

The options, top to bottom, are: single player, Battle.net, Battle.net gateway, other multiplayer, credits, cinematics and exit. An option that this installation cannot use is marked "unavailable".

## Character selection and creation

Character selection:

- Arrows: choose a character. The mod reads the name, class, level and position in the list.
- Enter: play.
- N: create a new character.
- Delete: delete the selected character. The mod asks for confirmation: Enter deletes, Escape cancels. Deletion cannot be undone.

Character creation:

- Left and right arrows (or up and down): change class.
- Keys 1 to 5: pick a class directly.
- Type the name on the keyboard.
- Enter: create the character. The mod tells you when the name is invalid or already taken.

## Moving around

- Arrows or the numeric keypad: move the character. One press is one step (one tile), holding a key walks step by step. Two arrows together make a diagonal step. When the way is blocked, the character stays and the mod says "Wall".
- F1: help with the list of keys.
- L: area name.
- K: character coordinates.
- G or F2: level scan, which tells how many targets of each category are nearby.

## Targets and guidance

The mod sorts the world into categories: items, chests, doors, shrines, objects, breakable objects, monsters, NPCs, players, exits, waypoints and portals.

- Control + Page Down / Control + Page Up: next or previous category.
- Page Down / Page Up: next or previous target in the category, nearest first.
- Home: describe the path to the target in steps, for example "north 3, east 2". That means three presses of the up arrow, then two of the right arrow (north is up, east is right). After walking part of the way, press Home again. H and numpad 5 do the same.
- Shift + Home: walk to the target automatically. Press again to stop. At exits the mod takes you into the next area.
- Control + Home: clear the selected target.
- E: interact with the target, for example open a chest, talk to an NPC, pick up an item or use a waypoint.
- F: attack the nearest monster.

When an item appears near the character, for example from a chest or a monster, the mod says "On the ground:" and its name. These items are in the "items" category.

### Surrounding sounds

The sound cues work the same way as in Diablo Access:

- The mod plays at most three sounds at once, for the nearest things within 12 steps.
- Each sound repeats faster the closer you are: once a second from far away, four times a second up close, and up to ten times a second for monsters.
- Volume drops with distance.
- A sound moves left or right the way the thing lies on the screen relative to the character.
- Items have their own sounds: weapon, armor (shields included), gold, potions and scrolls. They play everywhere, in town too.
- Chests, doors, exits and monsters are heard only outside town.
- When an item, a chest or a door is right next to the character, the other sounds stop, the mod plays the interaction sound and reads the name.
- The surrounding sounds are silent while the inventory is open.

## Life, mana and experience

- Z: life percentage.
- Shift + Z: mana percentage.
- X: percentage of experience still missing to the next level.

## Character sheet (C)

- Up and down arrows: move through the fields. The fields are name and class, level, experience, next level, strength, dexterity, vitality, energy, stat points, skill points, gold, defense, life, mana, stamina and resistances.
- Enter on an attribute (strength, dexterity, vitality, energy): add one point.
- Shift + Enter on an attribute: add all free points.
- Space: read the field again.
- Tab: switch to the inventory, if it is open.

When points are waiting, the sheet opens on strength.

## Inventory (I)

The inventory has three areas: equipped items, the backpack (10 columns by 4 rows) and the belt.

- Arrows: move between slots. Up from the top backpack row goes to equipped items, and down from the bottom row goes to the belt.
- Enter: pick an item up onto the cursor, place it, equip it or swap it.
- Shift + Enter: use an item, for example drink a potion.
- Space: the full item description, the same as the game's tooltip. It includes defense or damage, durability, requirements and magic properties. It works in the backpack and on equipped items while nothing is held on the cursor.
- Tab: switch to the character sheet, if it is open.

The mod confirms actions: "Holding:", "Placed.", "Equipped.", "Used:" or "Cannot do that.".

### How to equip an item

1. In the backpack, select the item and press Enter. You hear "Holding:" and its name.
2. Press the up arrow on the top backpack row to reach the equipped items.
3. Use left and right arrows to choose the slot, for example "Right hand" for a weapon or "Armor".
4. Press Enter. You hear "Equipped.". If the slot already held an item, the two are swapped and the old one goes onto the cursor ("Holding:"). Place it on a free backpack slot with Enter.

If the character does not meet the item's requirements, the game does not equip it and the mod says "Cannot do that.".

## Skill tree (T)

The skill tree works like a tree view in Windows programs. The top level has the three class tabs, for example for the Barbarian: combat skills, combat masteries and warcries. Each tab expands into its ten skills.

- Up and down arrows: previous or next visible item.
- Right arrow: expand a tab; on an expanded tab, move to its first skill.
- Left arrow: from a skill, go back to its tab; on a tab, collapse it.
- Home and End: first and last item.
- Enter on a tab: expand or collapse it.
- Enter on a skill: add one skill point.
- Shift + Enter on a skill: add as many points as possible (up to 20).
- Space: skill description, required character level, required skills, and row and column in the tree.

When the tree opens, the mod says how many points you can spend. For every skill it reads:

- the name,
- the level (plus levels from items) or "not learned",
- the state:
  - "a point can be added" when all requirements are met and you have free points,
  - "available" when requirements are met but there are no free points,
  - "locked, requires: …" with the missing requirements,
  - "maximum level".

### How skills work in Diablo II

All 30 class skills are visible in the tree from the start, but you can add a point only when:

- the character has the required level: skill rows open at levels 1, 6, 12, 18, 24 and 30,
- every required earlier skill already has at least one point.

You get skill points for each new character level and for some quests. After a level up, open the tree (T): the mod immediately says how many points are waiting.

## Game menu (Escape)

Escape during play opens the menu: options, save and exit game, return to game.

- Up and down arrows: select an entry.
- Enter: confirm, for example open a sub-menu.
- Left and right arrows: change a setting or move a slider.
- Escape: close the whole menu and return to the game. The "Previous menu" entry goes back one level.

The options sub-menu holds sound, video and automap options. The mod reads the setting name, its value (for example "on" or "60%") and its position in the list. The key configuration screen is not accessible yet.

## Death and corpses

- When your character dies, the mod says so. The equipped items stay on the corpse.
- At the moment of death the game itself saves the character with zero life.
- Press Escape to respawn in town with full life. That life reaches the save file only with the next save, so leave the game through the menu (Escape, "Save and exit game"), not by closing the window.
- The corpse stays in town even after leaving and reloading, so the equipment has to be taken back. The corpse is in the "players" category as "Corpse:" followed by the character name. Select it, walk there (Shift + Home) and press E.
- If a loaded character has zero life, the mod warns you right after entering the game. Talk to the healer in town (Akara in act one) to restore full life.

## Talking to NPCs

When an NPC menu opens, the mod reads the mode and the selected option. Arrows change the option, Enter selects it, Escape closes the menu.

## Mod files

- `D2AccessLauncher.exe` – starts the game with the mod.
- `D2AccessHook.dll` – the mod itself.
- `audio` – sound cues.
- `D2Access.ini` – optional settings (language).
- `D2AccessHook.log` and `D2AccessLauncher.log` – logs, useful when reporting problems.

## For developers

Building (Visual Studio 2022, Win32):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

While the game runs, the mod serves an MCP server at `http://127.0.0.1:13450/mcp`. It lets tools query the game (status, targets, paths, speech events) and press keys, which makes automated testing easier. Game function addresses and structures are described in `docs/ida_notes.md`.
