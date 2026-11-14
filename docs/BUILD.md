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

## Deploy / uninstall
```sh
scripts/deploy.sh      # builds, installs dinput8.dll (Ultimate ASI Loader v9.7.4, hash-pinned) + GDCompare.asi into compat/
scripts/uninstall.sh   # removes only what deploy installed and is unchanged
```
Both print the exact Steam launch options string. `GD_SUBDIR=x64` targets the non-compat launch entry.
