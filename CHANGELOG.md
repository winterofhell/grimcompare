# Changelog

## 1.0.0 (2026-11-16)

First release.

### Mod
- Hover an item: the tooltip shows how much better or worse your character gets with it, one number from Damage, Survivability and Other. Hold Shift for the breakdown.
- Reads your real stats from the running game (read-only) and values the swap with a stat model checked against the game database: damage types, conversion, crit, OA/DA, resists and RR, attack and cast speed, cooldowns, procs, timed buffs, sets, skill bonuses and pets (experimental).
- Enemies per level and difficulty from the game data; endgame resist buffer on Ultimate.
- Weights in TOML presets that reload on save, picked per character. Anything the mod cannot value shows as `+? unscored` instead of a guess.
- Works on Grim Dawn 1.3.1.2 and 1.3.1.1 (x64). On another build it logs a warning and turns off any hook it cannot find; the normal tooltip is left alone.
- Plain hover about 2 ms, Shift about 3 ms on a level 96 character.

### Setup
- `install.bat` (Windows) and `install.sh` (Linux): finds the game in Steam (also Flatpak), GOG, Heroic or a folder you type, shows the plan and asks before writing.
- Install, update, repair and uninstall. Never overwrites another mod's `dinput8.dll`, reuses an existing Ultimate ASI Loader x64, backs up files it replaces and keeps presets you edited.
- Prints the Steam launch options on Linux, keeping the ones you have.
- Build wizard: pick a character from your saves and one of 54 catalog builds (96 with variants), or describe your own build. The preset is bound to that character's save folder.
- Arrow-key menus with search and a details panel, or numbered menus with `--plain`. Readable at 80 columns; `NO_COLOR` turns colors off.
