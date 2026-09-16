# D2 Access – user guide

D2 Access is an accessibility mod for classic Diablo II (Game.exe version 1.14b) for blind and low-vision players. It reads menus, the character, the inventory, the skill tree and the surroundings aloud. It also guides you to chosen targets, like the Diablo Access mod for the first Diablo.

Speech goes through NVDA. When NVDA is not running, the mod uses the Windows speech synthesizer (SAPI).

## Installing Diablo II

The mod needs the original Diablo II with the Lord of Destruction expansion, version 1.14b. Both can be bought in the Battle.net shop as "Diablo II" and "Diablo II: Lord of Destruction"; this is not Diablo II: Resurrected. After buying, Blizzard provides a downloader that fetches the installer into a folder such as `D2-1.14b-Installer-enUS` with `Installer.exe` and `Installer Tome.mpq`.

Blizzard's installer has a graphical menu and a licence agreement window that cannot be accepted with the keyboard alone. That is why the mod comes with the installation assistant `D2AccessSetup.exe`. It guides you through installing the game and then downloads and installs the mod.

### Installing with the assistant

1. Download `D2AccessSetup.exe` from the newest release on the [Releases](https://github.com/mojsior/diablo-2-access/releases) page. It is a single file, nothing to extract.
2. Run `D2AccessSetup.exe`. If Windows asks for administrator rights, allow it; Blizzard's installer and copying into the game folder require them.
3. The assistant looks for the game installer in your Downloads and Desktop folders. If it finds several, it asks you to pick one with a number; if it finds none, it opens a file dialog where you select `Installer.exe`. You can also pass the path: `D2AccessSetup.exe "D:\Downloads\D2-1.14b-Installer-enUS"`.
4. The assistant starts the installer and chooses to install the game in the graphical menu.
5. At the licence agreement the assistant asks: Enter accepts the agreement, Escape declines it. After you accept, it scrolls the agreement and presses "Agree" for you.
6. Every following window (CD-key, install folder, DirectX, error messages) is read aloud and focused. You type the key yourself: the owner's name, then Tab and the 26-character key, then Enter.
7. While files are copied, the assistant reports progress every 10%, then says the game installation is complete and closes the installer.
8. The assistant asks whether to install the mod. After Enter it downloads the newest Diablo 2 Access release from GitHub, extracts it and copies it into the folder where the game was installed (usually `C:\Program Files (x86)\Diablo II`).
9. If you have Lord of Destruction, run the assistant again and select the expansion installer.

### Updating the mod

When Diablo II is already installed, the assistant notices it right after starting. Enter then installs or updates only the mod to the newest version from GitHub, Escape runs the game installer anyway. Close the game before updating.

The assistant was tested with the Polish and English Diablo II installers up to the CD-key window, and downloading and installing the mod into the game folder was tested too. Choosing the folder, the progress and the end of the game installation, and the Lord of Destruction installer, have not been tested in practice yet. If something goes wrong, attach `D2AccessSetup.log`, written next to the assistant, to your report.

### Installer shortcuts without the assistant

The graphical installer menu responds to keys even though the screen reader cannot see it:

- D: install the game;
- P (G in the Polish version): play, when the game is already installed;
- U (O in the Polish version): uninstall;
- B (W in the Polish version): back;
- X (Z in the Polish version): exit the installer.

The CD-key, folder and DirectX windows are standard Windows dialogs used with Tab and Enter. Only the licence agreement cannot be accepted with the keyboard, because "Agree" becomes available only after the text was scrolled with the mouse.

## Starting the game

1. Run `D2AccessLauncher.exe` from the game folder the assistant installed the mod into. The launcher uses the `Game.exe` next to it; if there is none, it looks up the game in the registry and finally in `C:\Program Files (x86)\Diablo II`.
2. You can also pass the game path, for example: `D2AccessLauncher.exe "D:\Games\Diablo II\Game.exe"`.
3. After a moment you hear "Main menu" and the selected option.

The game window must be in the foreground, otherwise the game ignores keys.

### Keep the game on version 1.14b

The mod hooks Diablo II 1.14b (file version 1.14.1.68) at fixed addresses, so the launcher checks the version of `Game.exe` and refuses to start any other build instead of crashing the game.

**Do not choose BATTLE.NET in the main menu.** It starts Blizzard's updater, which deletes `patch_d2.mpq`, then fails on `binkw32.dll` and leaves an installation that crashes at start-up with "Diablo II Exception: ACCESS_VIOLATION". Playing on Battle.net needs version 1.14d anyway, which the mod does not support. Running `BNUpdate.exe` by hand does not help either: Blizzard stopped serving those patch files, so the updater only reports a missing file.

After installing the mod, `D2AccessSetup.exe` renames `BNUpdate.exe` to `BNUpdate.exe.disabled` so the game cannot break itself. Remove the `.disabled` ending if you ever want the updater back.

If the updater already ran and the game stopped starting, copy `patch_d2.mpq` (about 8 MB) back into the game folder from your installation media or another copy of the game.

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
- F: attack the nearest monster, with the weapon or with the chosen combat skill.
- S: next combat skill. Shift + S: previous one. Control + S: back to the normal attack.
- Slash (the `/` key): the way to the nearest unexplored space.

### Exploring an area (slash)

The mod remembers where the character has already been. Slash reads the way to the nearest place you have not explored yet, counted in steps exactly as Home counts the way to a target, for example "Nearest unexplored space: north 6, east 3". When no path can be worked out, the mod gives the plain direction and the distance in steps.

The search walks over cells the character can actually cross, so the place it names is always reachable rather than cut off by a wall. It works the same in dungeons and in open areas such as the Blood Moor. When everything around is explored you hear "No unexplored areas found" — walk on a little and press slash again. The record of explored places is cleared whenever you change area.

### Combat skills (S)

S walks through the skills the character already knows and puts the chosen one into the attack. The mod reads its name, level and place in the list, for example "Bash, level 1. 1 of 2. F uses this skill." Every press of F then attacks the selected monster with it, and Control + S goes back to the plain weapon attack.

The list holds only skills that have a point in them (or come from items) and that the game lets you hold in a hand. Passive skills and auras, such as weapon masteries or paladin auras, work on their own and cannot be chosen, so the mod leaves them out. Points are spent in the skill tree under T.

When the character knows no skill yet, the mod says "You have no skills to choose yet."

When an item appears near the character, for example from a chest or a monster, the mod says "On the ground:" and its name. These items are in the "items" category.

Monsters are announced the same way: when one shows up nearby, the mod names it at once, for example "Monsters: Fallen, Zombie". For a larger group it names the first three and adds how many others there are. So there is no need to press Page Down to learn that something is coming.

The range is the game's own: monsters only exist once the part of the level they stand in is loaded, which is roughly twenty-odd steps away. The surrounding sounds carry less far, twelve steps, so the mod usually names a monster before you hear it.

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
- Shift + Enter: use an item, for example drink a potion, or equip a weapon or a piece of armor straight from the backpack.
- Space: the full item description, the same as the game's tooltip. It includes defense or damage, durability, requirements and magic properties. It works in the backpack and on equipped items while nothing is held on the cursor.
- Tab: switch to the character sheet, if it is open.

The mod confirms actions: "Holding:", "Placed.", "Equipped.", "Used:" or "Cannot do that.".

### How to equip an item

The short way: select the item in the backpack and press Shift + Enter. The game puts it in the slot it belongs to and the mod says "Equipped:" and its name. Rings, amulets and second weapons still go the long way, because the game picks the slot itself.

The long way, which lets you choose the slot:

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

The options sub-menu holds sound, video, automap and control options. The mod reads the setting name, its value (for example "on" or "60%") and its position in the list.

### Configure controls

"Configure controls" opens the list of every action of the game together with its keys.

- Up and down arrows: choose an action. The mod reads its name, the primary and the secondary key, and the position in the list. Headings are skipped.
- Left and right arrows: switch between the primary and the secondary key.
- Enter: assign a new key. The mod says "Press the new key"; press the key you want, or Escape to cancel. Afterwards the mod reads the action with its new key.
- Tab: move to the buttons at the bottom (default settings, accept, cancel) and back to the list. There the arrows choose a button and Enter presses it.
- Space: read the selected action again.

## Death and corpses

- When your character dies, the mod says so. The equipped items stay on the corpse.
- At the moment of death the game itself saves the character with zero life.
- Press Escape to respawn in town with full life. That life reaches the save file only with the next save, so leave the game through the menu (Escape, "Save and exit game"), not by closing the window.
- The corpse stays where the character died, even after leaving and reloading the game. If that was a dungeon, you have to go back there for the equipment: the mod only lists targets from the area you are in. The corpse is in the "players" category as "Corpse:" followed by the character name. Select it, walk there (Shift + Home) and press E.
- If a loaded character has zero life, the mod warns you right after entering the game. Talk to the healer in town (Akara in act one) to restore full life.

## Quest log (Q)

Q opens and closes the quest log. The mod reads the act and the selected quest with the text the log shows for it, for example that the quest is still waiting or already done.

- Up and down arrows: previous or next quest of the act.
- Left and right arrows: previous or next act. Acts you have not reached yet hold no quests.
- Space: read the selected quest again.
- Q: close the log.

## Talking to NPCs

When an NPC menu opens, the mod reads the mode and the selected option.

- Up and down arrows: previous or next option. While the menu is open the arrows no longer move the character.
- Enter: choose the option.
- Escape: close the menu.

## Mod files

- `D2AccessSetup.exe` – assistant for installing the game and the mod, and for updating the mod.
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
