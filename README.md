# GD Compare

Item comparison mod for Grim Dawn 1.3.1.1 (x64). Hover an item and the tooltip shows one number: how much better or worse your character gets with it. The mod reads your real stats from the running game and the game database. Hold Shift to see the breakdown.

```text
score = Damage^a × Survivability^b × Other^c        shown as (after / before - 1) × 100 %
```

- **Damage**: DPS of your main skills, including damage types, conversions, crit, OA vs enemy DA, resists and RR, attack/cast speed, cooldowns, procs, buffs and pets (experimental).
- **Survivability**: effective life against a hit at your level and difficulty, from resists, armor, DA, dodge, deflect, block and damage reduction.
- **Other**: run speed, regen, CC resists, retaliation, pet and racial bonuses.

Weights live in TOML presets that reload on save. `default.toml` works for any build. `chaos-conversion-caster`, `physical-melee`, `pet` and `retaliation` are picked automatically when your character matches. More in [docs/PRESETS.md](docs/PRESETS.md) and [docs/MECHANICS.md](docs/MECHANICS.md).

The mod only reads game memory, never writes it. If anything fails, the normal tooltip is left alone.

## Install

You need [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) x64 v9.7.4 and the mod files: `GDCompare.asi`, `dinput8.ini` and the `presets/` folder.

### Windows 10/11

1. Open the folder with the `Grim Dawn.exe` you launch (usually `Grim Dawn\x64\`).
2. Copy the loader's `dinput8.dll` there.
3. Copy `adapter/ual/dinput8.ini` next to it. Don't skip this, the game crashes before the main menu without it.
4. Copy `GDCompare.asi` there.
5. Copy `presets/default.toml` as `GDCompare.preset.toml`, and the other `presets/*.toml` into a `GDCompare.presets\` folder.

No launch options needed.

### Linux (Steam + Proton)

```sh
scripts/deploy.sh      # build and install into "Grim Dawn/compat/"
scripts/uninstall.sh   # remove what deploy installed
```

`deploy.sh` sets the Steam launch options for you. To install by hand, copy the same files as on Windows and set this launch option so Wine loads the loader:

```sh
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

Start the game with Steam's "Compatibility Mode" entry, which runs from `compat/`. Set `GD_SUBDIR=x64` to install into `x64/` instead.

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
```

## Limitations

- Only checked against Grim Dawn 1.3.1.1 x64. On other versions it logs a warning and turns off any hook it can't find.
- The score is a model, not a fight simulation: one target, no positioning. The enemy is the median monster for your level and difficulty, the mean boss for resists.
- Pets are experimental. With pets summoned, their attack rate comes from the game's own speed and animation data; otherwise from their records.
- Not scored: energy cost reduction, pet bonuses from buffs and set bonuses, flat racial damage. Full list in [docs/COVERAGE.md](docs/COVERAGE.md).
- Anything the mod can't value shows as `+? unscored: <effect>` instead of a guess.

## License

MIT, see [LICENSE](LICENSE). Third-party code: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
