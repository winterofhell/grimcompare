# GrimCompare

Item comparison mod for Grim Dawn 1.3.1.2 (x64). Hover an item and the tooltip shows one number: how much better or worse your character gets with it. The mod reads your real stats from the running game and the game database. Hold Shift to see the breakdown.

```text
score = Damage^a × Survivability^b × Other^c        shown as (after / before - 1) × 100 %
```

- **Damage**: DPS of your main skills, including damage types, conversions, crit, OA vs enemy DA, resists and RR, attack/cast speed, cooldowns, procs, buffs and pets (experimental).
- **Survivability**: effective life against a hit at your level and difficulty, from resists, armor, DA, dodge, deflect, block and damage reduction.
- **Other**: run speed, regen, CC resists, retaliation, pet and racial bonuses.

Weights live in TOML presets that reload on save. `default.toml` works for any build. `chaos-conversion-caster`, `physical-melee`, `pet` and `retaliation` are picked automatically when your character matches. More in [docs/PRESETS.md](docs/PRESETS.md) and [docs/MECHANICS.md](docs/MECHANICS.md).

The mod only reads game memory, never writes it. If anything fails, the normal tooltip is left alone.

Unofficial fan project, not affiliated with or endorsed by Crate Entertainment. Use at your own risk.

## Install

Download `GrimCompare-1.0.1.zip` from [GitHub Releases](https://github.com/winterofhell/grimcompare/releases) or Nexus (the same file) and unzip it anywhere.

- **Windows**: double-click `install.bat`.
- **Linux**: double-click `install.sh`, or run `./install.sh` in a terminal.

Setup finds the game (Steam, including Flatpak, GOG, Heroic, or a folder you type in), shows what it will write and asks before writing. Nothing else needs to be installed. Run it again to update, repair, uninstall or set up a build preset. It never touches your saves or the game's own files. It refuses to write while the game is running.

Menus work with the arrow keys: Enter picks the highlighted entry, Esc goes back. `--plain` (or a terminal that can't do it) gives numbered menus instead, where Enter takes the default shown and 0 goes back. Set `NO_COLOR=1` for no colors. `--game <folder>` skips the search, `--wizard` goes straight to the build presets.

- If a `dinput8.dll` from another mod is already there, setup explains the conflict and stops. It never overwrites that file. An existing Ultimate ASI Loader x64 is reused.
- Files it replaces that it didn't install go to `GDCompare.backup/<date-time>/` first. A preset you edited is kept.
- What it installed is listed in `GDCompare.installed`. Uninstall removes only those files, and only if you haven't changed them.

**Steam on Linux**: in Steam → Grim Dawn → Properties → Launch Options, put `WINEDLLOVERRIDES="dinput8=n,b" %command%` in front of what is there. If `WINEDLLOVERRIDES` is already set, add `dinput8=n,b` to it with a `;`. Start the game with the "Compatibility Mode" entry (it runs from `compat/`). **Heroic, Lutris**: add the environment variable `WINEDLLOVERRIDES=dinput8=n,b`. **Windows**: nothing else to do.

The Windows setup is tested end to end under Wine but has not been tried on a Windows PC yet. Please report how it goes.

**Antivirus**: `gdcompare-setup.exe`, `GDCompare.asi` and the loader's `dinput8.dll` are not code-signed, and a DLL that loads into a game process looks like a cheat or injector to some scanners, so Windows Defender or another antivirus may flag or quarantine them. Check the zip against the `.sha256` on the Release page, then allow the files (Windows Security → Virus & threat protection → Protection history → Allow, or add the game folder as an exclusion). If you would rather not, build from source (below) or don't use the mod.

### Build presets

Choose "Set up a build preset" in setup:

1. Pick your character. Name, class and level are read from your saves; you can also type a name.
2. Check the masteries (preselected from the save).
3. Pick one of 55 common builds (102 with their variants), filtered to your class, with search and a details panel. Or answer a few questions about your own build: main skills, damage type and conversion, playstyle (balanced, tanky, glass cannon), pets, retaliation, shield, and optional advanced numbers.
4. Read "Your top priorities" and save.

The preset is saved as `GDCompare.presets/<character>-<build>.toml` and applies only to that character's save (Steam account, campaign or custom game). Run setup again to change it or use it for another character.

<!-- screenshot: character list -->
<!-- screenshot: catalog with the details panel -->
<!-- screenshot: top priorities -->

### By hand

Copy into the folder with the `Grim Dawn.exe` you launch (`x64\` on Windows, `compat/` for Steam's Compatibility Mode):

- the loader's `dinput8.dll` ([Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) x64 v9.7.4);
- `dinput8.ini` (the game crashes before the main menu without it);
- `GDCompare.asi`;
- `presets/default.toml`, as `GDCompare.preset.toml`;
- the other `presets/*.toml`, into `GDCompare.presets/`.

On Linux, also set the launch options above.

For development, `scripts/deploy.sh` builds and installs into `compat/`, and `scripts/uninstall.sh` removes it again.

## Usage

- Hover an item to see the score, hold Shift for details.
- F9 saves a snapshot of your character.
- F10 saves the current weights as `GDCompare.presets/frozen-<character>-<Steam account>.toml`.
- Keys are set in `GDCompare.ini`. The log is `GDCompare.log`, both next to the `.asi`.
- To turn the mod off, create an empty file `GDCompare.disable` next to the `.asi`, or set `GDCOMPARE_DISABLE=1`.

## Build

Builds on Linux with `nix-shell`. The adapter is cross-compiled with clang-cl and xwin, setup in [docs/BUILD.md](docs/BUILD.md). Code layout and data flow: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

```sh
nix-shell --run 'cmake --workflow --preset core'      # core library and tests
nix-shell --run 'cmake --workflow --preset adapter'   # build/adapter/adapter/GDCompare.asi
scripts/package.sh                                    # dist/GrimCompare-<version>.zip (both setups, payload)
tests/e2e/setup_e2e.sh                                # setup end to end: a reflink copy of the game, and Wine
```

`setup_e2e.sh` runs the packaged setup non-interactively (answers piped into `--plain`) through install, update, repair, the wizard, back and cancel, a `dinput8.dll` conflict, uninstall and both launchers. On Linux it works on a reflink copy of the installed game in a fake home, so it needs the game on a reflink-capable filesystem (XFS, Btrfs); the Wine half uses a scratch prefix and a fake Windows Steam tree.

Pushing a `v*` tag builds the same zip and attaches it to the GitHub Release (`.github/workflows/release.yml`).

## Limitations

- Checked against Grim Dawn 1.3.1.2 and 1.3.1.1 x64. On other versions it logs a warning and turns off any hook it can't find.
- Not tested with Grimarillion or any other mod that adds classes or changes the game database. Scores and the build wizard there may be wrong or missing.
- The block is English only, on any game language: its own text and the stat, skill, item and set names it shows come from the game's English text (`Text_EN.arc`). Line widths are measured with the game's English fonts, so a translation pack with its own fonts may wrap lines differently. Not tested with a translation.
- The score is a model, not a fight simulation: one target, no positioning. The enemy is the median monster for your level and difficulty, the mean boss for resists.
- Pets are experimental. With pets summoned, their attack rate comes from the game's own speed and animation data; otherwise from their records.
- Not scored: energy cost reduction, pet bonuses from buffs and set bonuses, flat racial damage. Full list in [docs/COVERAGE.md](docs/COVERAGE.md).
- Anything the mod can't value shows as `+? unscored: <effect>` instead of a guess.
- Catalog builds score their attack skills. Summons, totems and traps are counted on top of those. A damage aura (Cold Aura Walker) is not a main skill, so its own damage is not in the score.
- The "shield" answer only adds a note: block is always part of Survivability.
- The mod reads at most 32 files from `GDCompare.presets/`.
- Potions and other consumables are left out: their buffs never count as yours.

## License

MIT, see [LICENSE](LICENSE). Third-party code: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
