#!/usr/bin/env bash
# Build the adapter, install it + Ultimate ASI Loader into the game folder, print the
# Steam launch options to paste. Never overwrites a file it did not install itself.
#
# env: GD_DIR     game root (default: found via Steam libraryfolders.vdf)
#      GD_SUBDIR  folder the game runs from: compat (Steam "Compatibility Mode", default) or x64
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STEAM="${STEAM_ROOT:-$HOME/.local/share/Steam}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/gdcompare"
MANIFEST_NAME="GDCompare.installed"

# Ultimate ASI Loader, pinned (github.com/ThirteenAG/Ultimate-ASI-Loader, release 2026-08-16).
UAL_VERSION="v9.7.4"
UAL_URL="https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/$UAL_VERSION/Ultimate-ASI-Loader_x64.zip"
UAL_ZIP_SHA256="8272d83b2692662098746f2d0ad0e2d85f3c8358ab1d63f75fbe835c2c8135fd"
UAL_DLL_SHA256="fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7"

die() { echo "deploy: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

find_game() {
  if [[ -n "${GD_DIR:-}" ]]; then echo "$GD_DIR"; return; fi
  local vdf="$STEAM/steamapps/libraryfolders.vdf" lib
  [[ -f "$vdf" ]] || die "no $vdf; set GD_DIR"
  while IFS= read -r lib; do
    if [[ -f "$lib/steamapps/appmanifest_219990.acf" && -d "$lib/steamapps/common/Grim Dawn" ]]; then
      echo "$lib/steamapps/common/Grim Dawn"; return
    fi
  done < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$vdf")
  die "Grim Dawn (219990) not found in any Steam library; set GD_DIR"
}

# 1. Build
if [[ -z "${IN_NIX_SHELL:-}" ]]; then
  nix-shell "$ROOT/shell.nix" --run "cmake --workflow --preset adapter"
else
  cmake --workflow --preset adapter
fi
ASI="$ROOT/build/adapter/adapter/GDCompare.asi"
[[ -f "$ASI" ]] || die "build produced no $ASI"

# 2. Locate game
GAME="$(find_game)"
TARGET="$GAME/${GD_SUBDIR:-compat}"
[[ -f "$TARGET/Grim Dawn.exe" ]] || die "no 'Grim Dawn.exe' in $TARGET"
# wine names the process after the exe; its cmdline starts with a Windows path (S:\...\Grim Dawn.exe)
if pgrep -x 'Grim Dawn.exe' >/dev/null || pgrep -f '^[A-Za-z]:\\.*Grim Dawn\.exe' >/dev/null; then die "Grim Dawn is running; quit it first"; fi

# 3. ASI loader (download once, verify both hashes)
mkdir -p "$CACHE"
UAL_ZIP="$CACHE/Ultimate-ASI-Loader_x64-$UAL_VERSION.zip"
UAL_DLL="$CACHE/dinput8-$UAL_VERSION.dll"
if [[ ! -f "$UAL_DLL" ]]; then
  if [[ ! -f "$UAL_ZIP" ]]; then
    curl -fsSL "$UAL_URL" -o "$UAL_ZIP.part"
    mv -f "$UAL_ZIP.part" "$UAL_ZIP"
  fi
  [[ "$(sha "$UAL_ZIP")" == "$UAL_ZIP_SHA256" ]] || { rm -f "$UAL_ZIP"; die "ASI loader zip hash mismatch"; }
  python3 -I -c 'import sys,zipfile; open(sys.argv[2],"wb").write(zipfile.ZipFile(sys.argv[1]).read("dinput8.dll"))' \
    "$UAL_ZIP" "$UAL_DLL"
fi
[[ "$(sha "$UAL_DLL")" == "$UAL_DLL_SHA256" ]] || { rm -f "$UAL_DLL"; die "dinput8.dll hash mismatch"; }

# 4. Install. A destination may be replaced only if absent, identical, or recorded in our manifest.
MANIFEST="$TARGET/$MANIFEST_NAME"
declare -A OWNED=()
if [[ -f "$MANIFEST" ]]; then
  while IFS=$'\t' read -r name _; do [[ -n "$name" ]] && OWNED["$name"]=1; done < "$MANIFEST"
fi
install_file() {  # src name
  local src="$1" dst="$TARGET/$2"
  if [[ -e "$dst" && -z "${OWNED[$2]:-}" && "$(sha "$dst")" != "$(sha "$src")" ]]; then
    die "$dst exists and was not installed by us; not overwriting"
  fi
  cp -f "$src" "$dst.gdc-tmp" && mv -f "$dst.gdc-tmp" "$dst"  # rename: never rewrite a mapped file in place
  printf '%s\t%s\n' "$2" "$(sha "$dst")" >> "$MANIFEST.new"
  echo "installed $dst"
}
: > "$MANIFEST.new"
install_file "$UAL_DLL" dinput8.dll
install_file "$ASI" GDCompare.asi
mv -f "$MANIFEST.new" "$MANIFEST"

# 5. Launch options
echo
echo "Steam → Grim Dawn → Properties → Launch Options:"
python3 -I "$ROOT/scripts/launch_options.py" add "$STEAM"
echo
echo "Launch with the 'Compatibility Mode' entry (runs compat/Grim Dawn.exe)."
echo "Log: $TARGET/GDCompare.log"
