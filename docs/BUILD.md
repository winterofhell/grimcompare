# Build

Everything runs inside `nix-shell` (see `shell.nix`: cmake, ninja, gcc for core; clang-cl 21 + lld-link for the adapter).

## One-time: MSVC CRT + Windows SDK via xwin
```sh
nix-shell --run 'xwin --accept-license --arch x86_64 --variant desktop --sdk-version 10.0.22621 \
  --cache-dir ~/.cache/xwin splat --output ~/.local/share/xwin'
```
`--sdk-version` is needed: xwin 0.9.0 fails on the default SDK 10.0.26100 ("unable to find x86_64 headers").
Override the location with `XWIN_DIR`.

## Core + tests (Linux)
```sh
nix-shell --run 'cmake --workflow --preset core'
```
`test_real_binaries` also checks the installed game (default Steam path or `GD_DIR`): our PE reader vs `re/Game.exports.tsv`, and every planned hook target in both `x64/` and `compat/`. Without the game it prints a skip message.

## Adapter (Windows x64 `.asi`)
```sh
nix-shell --run 'cmake --workflow --preset adapter'   # → build/adapter/adapter/GDCompare.asi
```
CRT: `/MD` (dynamic), because Game.dll/Engine.dll import MSVCP140/VCRUNTIME140/ucrt. The prefix's `msvcp140.dll` is from 2016, so after changing STL usage check that every import of the `.asi` exists there (`winedump -j import`). Avoid `std::mutex` (newer headers need a newer msvcp140); use SRWLOCK.
The build fails if the `.asi` imports `_Mtx_*` / `_Cnd_*` (`cmake/check_imports.cmake`): `std::mutex` from current headers faulted on its first lock against that 2016 DLL (adapter 0.4.0, `arz::Database` cache).

## Deploy / uninstall
```sh
scripts/deploy.sh                # builds, installs dinput8.dll (Ultimate ASI Loader v9.7.4, hash-pinned) + dinput8.ini + GDCompare.asi into compat/
scripts/deploy.sh --loader-only  # loader + ini only; removes our .asi
scripts/uninstall.sh             # removes only what deploy installed and is unchanged
```
Both print the exact Steam launch options string. `GD_SUBDIR=x64` targets the non-compat launch entry. Scripts work from any cwd.

`adapter/ual/dinput8.ini` sets `dontloadfromdllmain=0`. Without it the loader crashes the game before the main menu (it defers loading the real `dinput8.dll`, and the game calls `DirectInput8Create` first; see the ini comment). Never deploy the loader without it.

### Safe-mode test (after every loader change, or any crash before the main menu)
1. `scripts/deploy.sh --loader-only`, start the game. Must reach the main menu. No `GDCompare.log` appears.
2. `scripts/deploy.sh`, start the game. Must reach the main menu. Send `compat/GDCompare.log`.
3. Only if step 1 crashes: OptiScaler/ReShade are already removed (2026-11-14); send the crash zip from the game root.

Kill switch: `compat/GDCompare.disable` exists, or launch option `GDCOMPARE_DISABLE=1` → the .asi does nothing (no log written).
