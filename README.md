# GD Compare

An item comparison mod for Grim Dawn 1.3.1.1 (x64). Hover an item, and the tooltip gets one extra block: how much better or worse your character is with it, as one score. The score is worked out from your character's real stats, which the mod reads from the running game, and the game's database. Hold Shift for the breakdown: sheet values before and after, and which stats moved the score.

```
score = Damage^a × Survivability^b × Other^c        shown: (after / before − 1) × 100 %
```

- **Damage**: your main skills' damage per second from their stats. It covers damage types and conversions, crit, OA against the enemy's DA, enemy resists and your RR, attack or cast speed by skill class, cooldowns, procs and buffs, and pets (experimental). Weapon DPS is not recomputed, because the game already shows it. A weapon counts through its stats like any other item.
- **Survivability**: effective life against an incoming hit on your level and difficulty. It counts resists (with caps and overcap against enemy RR), armor, DA against the enemy's OA, dodge, deflect, block and damage reduction.
- **Other**: small weighted extras, such as run speed, regeneration, crowd-control resists, retaliation, pet bonuses and racial bonuses.

The weights are presets (TOML files, hot-reloaded). `default.toml` fits any build. The templates `chaos-conversion-caster`, `physical-melee`, `pet` and `retaliation` are picked automatically for a matching character. See [docs/PRESETS.md](docs/PRESETS.md) and [docs/MECHANICS.md](docs/MECHANICS.md).

The mod is read-only. It never writes game memory, and on any error it leaves the game's tooltip untouched. The kill switch is a file named `GDCompare.disable` next to the `.asi`, or the launch option `GDCOMPARE_DISABLE=1`.

## Build

Everything builds on Linux inside `nix-shell`. The adapter is cross-compiled with clang-cl and xwin; the one-time setup is in [docs/BUILD.md](docs/BUILD.md).

```sh
nix-shell --run 'cmake --workflow --preset core'      # core library + tests (needs no game; DB tests skip without it)
nix-shell --run 'cmake --workflow --preset adapter'   # → build/adapter/adapter/GDCompare.asi
```

## Install

### Linux (Steam + Proton)

```sh
scripts/deploy.sh      # builds, installs into "Grim Dawn/compat/", prints the Steam launch options
scripts/uninstall.sh   # removes only what deploy installed and is unchanged
```

`deploy.sh` installs Ultimate ASI Loader v9.7.4 as `dinput8.dll` (pinned by hash) with its `dinput8.ini`, plus `GDCompare.asi`, `GDCompare.preset.toml` and `GDCompare.presets/`. It also adds `dinput8=n,b` to `WINEDLLOVERRIDES` in the game's launch options and keeps any overrides already there. Start the game with Steam's "Compatibility Mode" launch entry, which runs the game from `compat/`. To use `x64/` instead, set `GD_SUBDIR=x64`.

### Windows

The install is the same, done by hand. The adapter still has to be built on Linux; there is no Windows build script and no release download.

1. Download Ultimate ASI Loader x64 v9.7.4 (github.com/ThirteenAG/Ultimate-ASI-Loader). Copy its `dinput8.dll` into the folder of the `Grim Dawn.exe` you launch, normally `Grim Dawn\x64\`.
2. Copy `adapter/ual/dinput8.ini` next to it. It is required: without `dontloadfromdllmain=0`, the loader crashes the game before the main menu.
3. Copy `GDCompare.asi` there. Also copy `presets/default.toml` as `GDCompare.preset.toml`, and the other `presets/*.toml` files into a `GDCompare.presets\` folder.

No launch option is needed on Windows.

### Use

Hover an item to see the block; hold Shift for detail. F9 writes a snapshot of your character and F10 freezes the current scoring as your own preset (both keys are set in `GDCompare.ini`). The log is `GDCompare.log` next to the `.asi`.

## Limitations

- **Built and checked against Grim Dawn 1.3.1.1 x64 only.** On another build the adapter warns in its log (unknown hash). It turns itself off only when a required export is missing; unexported code it hooks is matched by bytes and skipped when that fails.
- **Tested only on Linux/Proton.** Native Windows uses the same binaries, but it has not been tried.
- **The score is a model, not a combat simulation.** It covers one target, no positioning and no per-monster fights. The enemy is the median monster of your level and difficulty from the game database; for resists, it is the mean boss. The weights in the presets are design choices for tuning, not game rules.
- **Pets are experimental, and the tooltip says so.** Their attack rate is estimated from the pet records, not from animations. Summons without a lifetime in the database (Blade Spirit, Squall, Celestial Guardian) stay unscored.
- **Racial bonuses are a flat share.** Each race listed counts for 10% of enemies; there is no race simulation.
- **Some stats are not scored:**
  - skill energy cost reduction (there is no energy model);
  - pet bonuses from timed buffs and set bonuses;
  - absolute racial damage (2 records);
  - the armor chance field (1 record).

  [docs/COVERAGE.md](docs/COVERAGE.md) lists every item field and how it is scored.
- **Anything the model cannot value is marked**, never guessed: `+? unscored: <effect>`.

## License

MIT, see [LICENSE](LICENSE). Third-party code: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
