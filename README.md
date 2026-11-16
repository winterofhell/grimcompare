# GD Compare

Item comparison mod for Grim Dawn 1.3.1.2 (x64). Hover an item and the tooltip shows one number: how much better or worse your character gets with it. The mod reads your real stats from the running game and the game database. Hold Shift to see the breakdown.

```text
score = Damage^a × Survivability^b × Other^c        shown as (after / before - 1) × 100 %
```

- **Damage**: DPS of your main skills, including damage types, conversions, crit, OA vs enemy DA, resists and RR, attack/cast speed, cooldowns, procs, buffs and pets (experimental).
- **Survivability**: effective life against a hit at your level and difficulty, from resists, armor, DA, dodge, deflect, block and damage reduction.
- **Other**: run speed, regen, CC resists, retaliation, pet and racial bonuses.

Weights live in TOML presets that reload on save. `default.toml` works for any build. `chaos-conversion-caster`, `physical-melee`, `pet` and `retaliation` are picked automatically when your character matches. More in [docs/PRESETS.md](docs/PRESETS.md) and [docs/MECHANICS.md](docs/MECHANICS.md).

The mod only reads game memory, never writes it. If anything fails, the normal tooltip is left alone.

## Install

Download `GDCompare-1.0.0.zip` from [GitHub Releases](https://github.com/winterofhell/grimcompare/releases) or Nexus (the same file) and unzip it anywhere.

- **Windows**: double-click `install.bat`.
- **Linux**: double-click `install.sh`, or run `./install.sh` in a terminal.

Setup finds the game (Steam, including Flatpak, GOG, Heroic, or a folder you type in), shows what it will write and asks before writing. Nothing else needs to be installed. Run it again to update, repair, uninstall or set up a build preset. It never touches your saves or the game's own files. It refuses to run while the game is running.

- If a `dinput8.dll` from another mod is already there, setup explains the conflict and stops. It never overwrites that file. An existing Ultimate ASI Loader x64 is reused.
- Files it replaces that it didn't install go to `GDCompare.backup/<date-time>/` first. A preset you edited is kept.
- What it installed is listed in `GDCompare.installed`. Uninstall removes only those files, and only if you haven't changed them.

**Steam on Linux**: setup prints the launch options to paste into Steam → Grim Dawn → Properties → Launch Options. It keeps your existing options and adds `dinput8=n,b` to `WINEDLLOVERRIDES`. Start the game with the "Compatibility Mode" entry, which runs from `compat/`. **Heroic, Lutris**: add the environment variable `WINEDLLOVERRIDES=dinput8=n,b`. **Windows**: nothing else to do.

Windows support is built and unit-tested but has not been tried on a Windows PC yet. Please report how it goes.

### Build presets

Choose "Set up a build preset" in setup:

1. Pick your character. Name, class and level are read from your saves; you can also type a name.
2. Check the masteries (preselected from the save).
3. Pick one of 54 common builds, filtered to your class, with search and a details panel. Or answer a few questions about your own build: main skills, damage type and conversion, playstyle (balanced, tanky, glass cannon), pets, retaliation, shield, and optional advanced numbers.
4. Read "Your top priorities" and save.

The preset is saved as `GDCompare.presets/<character>-<build>.toml` and applies only to that character. Run setup again to change it, or to use it for another character.

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
- F10 saves the current weights as your own preset.
- Keys are set in `GDCompare.ini`. The log is `GDCompare.log`, both next to the `.asi`.
- To turn the mod off, create an empty file `GDCompare.disable` next to the `.asi`, or set `GDCOMPARE_DISABLE=1`.

## Build

Builds on Linux with `nix-shell`. The adapter is cross-compiled with clang-cl and xwin, setup in [docs/BUILD.md](docs/BUILD.md).

```sh
nix-shell --run 'cmake --workflow --preset core'      # core library and tests
nix-shell --run 'cmake --workflow --preset adapter'   # build/adapter/adapter/GDCompare.asi
scripts/package.sh                                    # dist/GDCompare-<version>.zip (both setups, payload)
```

Pushing a `v*` tag builds the same zip and attaches it to the GitHub Release (`.github/workflows/release.yml`).

## Limitations

- Checked against Grim Dawn 1.3.1.2 and 1.3.1.1 x64. On other versions it logs a warning and turns off any hook it can't find.
- The score is a model, not a fight simulation: one target, no positioning. The enemy is the median monster for your level and difficulty, the mean boss for resists.
- Pets are experimental. With pets summoned, their attack rate comes from the game's own speed and animation data; otherwise from their records.
- Not scored: energy cost reduction, pet bonuses from buffs and set bonuses, flat racial damage. Full list in [docs/COVERAGE.md](docs/COVERAGE.md).
- Anything the mod can't value shows as `+? unscored: <effect>` instead of a guess.
- Build presets bind to a character by name and save folder (Steam Cloud or local, and the game's backup copy). Two Steam accounts with same-named characters on one PC, or a custom-game character named like a campaign one, still share a preset.
- Catalog builds score their attack skills. Summons, totems and traps are counted on top of those. A damage aura (Cold Aura Walker) is not a main skill, so its own damage is not in the score.
- The "shield" answer only adds a note: block is always part of Survivability.
- The mod reads at most 32 files from `GDCompare.presets/`.

## License

MIT, see [LICENSE](LICENSE). Third-party code: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
