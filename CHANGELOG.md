# Changelog

## 1.0.3 (2026-11-19)

- Item and set skill modifiers that were shown as unscored now count: enemy OA and DA reduction, enemy damage reduction, damage absorption, block and resistances on buffs, and "+x% Chance to be Used" on weapon pool attacks.
- Devotion bonuses to DoT duration (Rat, Scorpion and others) now lengthen DoTs.
- "Total Damage Modified" from timed buffs (Mark of Torment and similar) applies while the buff is up.
- Enemy damage reduction from devotion procs and pet auras (Tainted Eruption, Black Blood of Yugol) counts in Survivability.
- Weapon pool attacks (Zolhan's Technique, Amarasta's Quick Cut and others) replace a share of the default attack, Cadence or Savagery by their chance instead of adding on top.
- A proc granted by several items or sets (Dung Divider and Fewmet Grips: Noxious Fumes) is one effect at the summed rate: its buff, debuff or DoT is no longer counted twice.
- A proc's DoT no longer stacks with itself: uses faster than its duration add nothing (procs with long DoTs were overvalued on fast attackers).
- Weapon pool attacks with a cooldown replace only the uses they actually fire on.
- Cadence: weapon pool attacks replace only its two plain hits, never the finale, and the finale's damage is averaged over the cycle.
- DoT duration from passives and devotions counts on every capture; from a toggle only while it is on.
- Set bonuses that convert damage on a form or toggle (Fenris 5/5 on Werewolf Form) convert all the character's damage while it is on.
- Tests: one endgame character per build family (casters, melee, dual wield, sword and board, guns, weapon pool, retaliation, pets, shapeshift, totems and traps) built from the database, with the expected sign of common swaps.
- `scripts/patch-check.sh`: one command after a game patch to check the hooks against the new build and regenerate the tables read from the game files, the text style classes included.

## 1.0.2 (2026-11-18)

- Damage scoring fixes. A main skill's weight counts the points in its learned modifiers and transmuter (Bloody Pox 1 + Black Death 12 = 13), and their damage counts toward the skill.
- "Total Damage Modified by x%" (transmuters, item and set skill modifiers) now multiplies the skill's damage instead of adding to its % bonus.
- Swaps that change attack or cast speed now move them in the prediction, so on-attack procs follow.
- Procs and pets: their share of Damage no longer shifts when only a lightly weighted main skill changes.
- Presets: `offtarget_weight` for damage types no conversion can turn into the build's type; second-tier survival targets (`threshold_secondary_weight`, `threshold_level`).
- Cleaner code and smaller math fixes throughout.

## 1.0.1 (2026-11-17)

- The mod is now called GrimCompare: tooltip heading, setup and the release zip (`GrimCompare-<version>.zip`). Installed file names stay `GDCompare.*`, so existing installs and presets keep working.
- The release zip has the license texts of the bundled third-party code in `licenses/`, Ultimate ASI Loader included.
- Windows: still beta (tested under Wine only).

## 1.0.0 (2026-11-17)

First release.

- Windows: beta (tested under Wine only).
- Not tested with Grimarillion or other mods that add classes. The tooltip block is English only.

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
- Shows the one Steam launch option Linux needs: `WINEDLLOVERRIDES="dinput8=n,b" %command%`.
- Build wizard: pick a character from your saves and one of 55 catalog builds (102 with variants), or describe your own build. The preset is bound to that character's save, so same-named characters on other Steam accounts or in a custom game keep their own.
- Arrow-key menus with search and a details panel, or numbered menus with `--plain`. Readable at 80 columns; `NO_COLOR` turns colors off.
