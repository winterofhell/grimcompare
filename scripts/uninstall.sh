#!/usr/bin/env bash
# Remove exactly the files deploy.sh installed (per GDCompare.installed), only if unchanged
# since install, plus our log. Prints the launch options with dinput8 removed.
# env: GD_DIR, GD_SUBDIR — as in deploy.sh
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
ROOT="$PWD"
STEAM="${STEAM_ROOT:-$HOME/.local/share/Steam}"
die() { echo "uninstall: $*" >&2; exit 1; }

if [[ -n "${GD_DIR:-}" ]]; then
  GAME="$GD_DIR"
else
  GAME=""
  while IFS= read -r lib; do
    [[ -f "$lib/steamapps/appmanifest_219990.acf" ]] && GAME="$lib/steamapps/common/Grim Dawn" && break
  done < <(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$STEAM/steamapps/libraryfolders.vdf")
  [[ -n "$GAME" ]] || die "Grim Dawn not found; set GD_DIR"
fi
TARGET="$GAME/${GD_SUBDIR:-compat}"
MANIFEST="$TARGET/GDCompare.installed"
[[ -f "$MANIFEST" ]] || die "nothing installed in $TARGET (no GDCompare.installed)"
# wine names the process after the exe; its cmdline starts with a Windows path (S:\...\Grim Dawn.exe)
if pgrep -x 'Grim Dawn.exe' >/dev/null || pgrep -f '^[A-Za-z]:\\.*Grim Dawn\.exe' >/dev/null; then die "Grim Dawn is running; quit it first"; fi

kept=0
while IFS=$'\t' read -r name hash; do
  [[ -n "$name" ]] || continue
  f="$TARGET/$name"
  if [[ ! -e "$f" ]]; then
    echo "already gone: $f"
  elif [[ "$(sha256sum "$f" | cut -d' ' -f1)" == "$hash" ]]; then
    rm -f "$f" && echo "removed $f"
  else
    echo "KEPT (changed since install): $f"; kept=1
  fi
done < "$MANIFEST"
rm -f "$TARGET/GDCompare.log"
if [[ $kept == 0 ]]; then rm -f "$MANIFEST"; fi

echo
echo "Steam → Grim Dawn → Properties → Launch Options:"
python3 -I "$ROOT/scripts/launch_options.py" remove "$STEAM"
