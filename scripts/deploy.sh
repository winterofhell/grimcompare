#!/usr/bin/env bash
# Builds the adapter, installs it with Ultimate ASI Loader into the game folder, prints Steam launch options.
# Never overwrites a file it did not install.
#
# usage: deploy.sh [--loader-only]
#   --loader-only  install only the loader + ini and remove our .asi (safe-mode test, docs/BUILD.md)
# env: GD_DIR     game root (default: found via Steam libraryfolders.vdf)
#      GD_SUBDIR  folder the game runs from: compat (Steam "Compatibility Mode", default) or x64
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
ROOT="$PWD"

LOADER_ONLY=0
case "${1:-}" in
  "") ;;
  --loader-only) LOADER_ONLY=1 ;;
  *) echo "usage: $0 [--loader-only]" >&2; exit 2 ;;
esac
STEAM="${STEAM_ROOT:-$HOME/.local/share/Steam}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/gdcompare"
MANIFEST_NAME="GDCompare.installed"

# shellcheck source=ual.sh
. "$ROOT/scripts/ual.sh"

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

ASI="$ROOT/build/adapter/adapter/GDCompare.asi"
UAL_INI="$ROOT/adapter/ual/dinput8.ini"
SETUP="$ROOT/build/core/installer/gdcompare-setup"  # prints the launch options
build() {
  if [[ -z "${IN_NIX_SHELL:-}" ]]; then nix-shell "$ROOT/shell.nix" --run "$1"; else bash -c "$1"; fi
}
cmds="cmake --preset core >/dev/null && cmake --build build/core --target gdcompare-setup"
[[ $LOADER_ONLY == 0 ]] && cmds="cmake --workflow --preset adapter && $cmds"
build "$cmds"
[[ $LOADER_ONLY == 1 || -f "$ASI" ]] || die "build produced no $ASI"
[[ -x "$SETUP" ]] || die "build produced no $SETUP"

GAME="$(find_game)"
TARGET="$GAME/${GD_SUBDIR:-compat}"
[[ -f "$TARGET/Grim Dawn.exe" ]] || die "no 'Grim Dawn.exe' in $TARGET"
# wine names the process after the exe; its cmdline starts with a Windows path (S:\...\Grim Dawn.exe)
if pgrep -x 'Grim Dawn.exe' >/dev/null || pgrep -f '^[A-Za-z]:\\.*Grim Dawn\.exe' >/dev/null; then die "Grim Dawn is running; quit it first"; fi

fetch_ual "$CACHE" || die "could not get the ASI loader"

# A destination may be replaced only if absent, identical, or in our manifest.
MANIFEST="$TARGET/$MANIFEST_NAME"
declare -A OWNED=()
if [[ -f "$MANIFEST" ]]; then
  while IFS=$'\t' read -r name hash; do [[ -n "$name" ]] && OWNED["$name"]="${hash:-?}"; done < "$MANIFEST"
fi
install_file() {
  local src="$1" dst="$TARGET/$2"
  if [[ -e "$dst" && -z "${OWNED[$2]:-}" && "$(sha "$dst")" != "$(sha "$src")" ]]; then
    die "$dst exists and was not installed by us; not overwriting"
  fi
  cp -f "$src" "$dst.gdc-tmp" && mv -f "$dst.gdc-tmp" "$dst"  # rename: never rewrite a mapped file in place
  printf '%s\t%s\n' "$2" "$(sha "$dst")" >> "$MANIFEST.new"
  echo "installed $dst"
}
install_preset() {  # keeps a copy edited since install
  local src="$1" dst="$TARGET/$2"
  if [[ -e "$dst" && "$(sha "$dst")" != "$(sha "$src")" && "${OWNED[$2]:-}" != "$(sha "$dst")" ]]; then
    echo "kept $dst (edited; shipped version: $src)"
    if [[ -n "${OWNED[$2]:-}" ]]; then printf '%s\t%s\n' "$2" "${OWNED[$2]}" >> "$MANIFEST.new"; fi
    return 0
  fi
  install_file "$src" "$2"
}
: > "$MANIFEST.new"
install_file "$UAL_DLL" dinput8.dll
install_file "$UAL_INI" dinput8.ini
if [[ $LOADER_ONLY == 0 ]]; then
  install_file "$ASI" GDCompare.asi
  # The score preset is the user's once copied. Replaced only while byte-identical to a default.toml some commit
  # shipped; an old default may no longer parse.
  preset="$TARGET/GDCompare.preset.toml"
  shipped_default() {
    local have c
    have="$(sha "$preset")"
    for c in $(git -C "$ROOT" log --format=%H -- presets/default.toml 2>/dev/null); do
      [[ "$(git -C "$ROOT" show "$c:presets/default.toml" 2>/dev/null | sha256sum | cut -d' ' -f1)" == "$have" ]] && return 0
    done
    return 1
  }
  if [[ ! -e "$preset" ]]; then
    cp "$ROOT/presets/default.toml" "$preset"
    echo "installed $preset (default preset)"
  elif [[ "$(sha "$preset")" != "$(sha "$ROOT/presets/default.toml")" ]] && shipped_default; then
    cp "$ROOT/presets/default.toml" "$preset"
    echo "updated $preset (it was an unedited earlier default)"
  else
    echo "kept $preset (defaults: presets/default.toml)"
  fi
  mkdir -p "$TARGET/GDCompare.presets"
  for src in "$ROOT"/presets/*.toml; do
    [[ "$(basename "$src")" == default.toml ]] || install_preset "$src" "GDCompare.presets/$(basename "$src")"
  done
elif [[ -e "$TARGET/GDCompare.asi" ]]; then
  [[ -n "${OWNED[GDCompare.asi]:-}" ]] || die "$TARGET/GDCompare.asi exists and was not installed by us"
  rm -f "$TARGET/GDCompare.asi" "$TARGET/GDCompare.log"
  echo "removed $TARGET/GDCompare.asi (loader-only)"
fi
mv -f "$MANIFEST.new" "$MANIFEST"

echo
echo "Steam → Grim Dawn → Properties → Launch Options:"
"$SETUP" launch-options add --steam "$STEAM" --game-folder "$TARGET"
echo
echo "Launch with the 'Compatibility Mode' entry (runs compat/Grim Dawn.exe)."
if [[ $LOADER_ONLY == 0 ]]; then
  echo "Log: $TARGET/GDCompare.log"
  echo "Kill switch: touch \"$TARGET/GDCompare.disable\" (or launch option GDCOMPARE_DISABLE=1)"
fi
