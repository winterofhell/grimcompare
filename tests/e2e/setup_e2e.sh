#!/usr/bin/env bash
# End-to-end test of the release setup, non-interactive: every flow runs the packaged setup in --plain mode with its
# answers piped in, then checks the folder it changed.
#   linux: a reflink copy of the installed Steam game (never the real folder) in a fake $HOME holding copies of the
#          Steam user configs and cloud saves; install.sh and the static Linux setup.
#   wine:  a fake Windows Steam tree (fake exes, reflinked database and text) in a scratch WINEPREFIX; install.bat under
#          wine cmd and gdcompare-setup.exe. Headless: no DISPLAY, WINEDEBUG=-all.
# usage: tests/e2e/setup_e2e.sh [linux] [wine]      (both when none given)
# env:   DIST=dist/GrimCompare-<version> (scripts/package.sh), GD_DIR=<installed game>, E2E_DIR=build/e2e
set -uo pipefail
cd "$(dirname "$(realpath "$0")")/../.."
ROOT="$PWD"
DIST="$(realpath "${DIST:-$(ls -d dist/GrimCompare-*/ 2>/dev/null | sort -V | tail -1)}")"
REAL_STEAM="$HOME/.local/share/Steam"
GD_DIR="$(realpath "${GD_DIR:-$REAL_STEAM/steamapps/common/Grim Dawn}")"
E2E_DIR="$(realpath -m "${E2E_DIR:-$ROOT/build/e2e}")"
FLOWS=("${@:-linux wine}")
FLOWS=(${FLOWS[*]})

[[ -x "$DIST/bin/gdcompare-setup-linux" ]] || { echo "no release folder at $DIST: run scripts/package.sh first" >&2; exit 2; }
[[ -f "$GD_DIR/database/database.arz" ]] || { echo "no game at $GD_DIR: set GD_DIR" >&2; exit 2; }
case "$E2E_DIR/" in "$GD_DIR/"*|"$REAL_STEAM/"*) echo "E2E_DIR must be outside the game and Steam folders" >&2; exit 2 ;; esac

failures=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; failures=$((failures + 1)); }
check() { local what="$1"; shift; if "$@"; then pass "$what"; else fail "$what"; fi; }
# The log as one line: wrapped text joins again ("! " warning marks and indents dropped).
flat() { sed 's/^! //; s/^ *//' "$LOG" | tr '\r\n' '  ' | tr -s ' '; }
has() { flat | grep -qF -- "$1"; }
hashes() { (cd "$1" && find . -type f ! -path './GDCompare*' ! -name 'dinput8.*' -print0 | sort -z | xargs -0 sha256sum); }
hash_of() { sha256sum "$1" | cut -d' ' -f1; }
# Every file the manifest lists is there with that hash, and the shipped ones are the package's.
manifest_ok() {
  local dir="$1" name hash
  [[ -f "$dir/GDCompare.installed" ]] || return 1
  while IFS=$'\t' read -r name hash; do
    [[ "$(hash_of "$dir/$name")" == "${hash%$'\r'}" ]] || { echo "    $name differs from the manifest"; return 1; }
  done <"$dir/GDCompare.installed"
  for f in GDCompare.asi dinput8.dll dinput8.ini; do cmp -s "$dir/$f" "$DIST/payload/$f" || { echo "    $f is not the package's"; return 1; }; done
}

# run <name> <expected exit> <answers> <command...>: answers one per line on stdin, output to $LOG.
run() {
  local name="$1" want="$2" answers="$3"; shift 3
  LOG="$E2E_DIR/logs/$PLATFORM-$name.log"
  mkdir -p "$(dirname "$LOG")"
  printf '%s' "$answers" | timeout 120 "$@" >"$LOG" 2>&1
  local got=$?
  echo "$PLATFORM $name"
  check "exit $want (got $got, log $LOG)" test "$got" = "$want"
}

# The flows, the same on both platforms. $SETUP runs setup, $LAUNCH the launcher; $TARGET is the folder the mod
# goes into; $PROTECTED a file there that is not ours (must never change).
flows() {
  local before_hashes payload_asi
  before_hashes="$(hashes "$TARGET")"
  payload_asi="$(hash_of "$DIST/payload/GDCompare.asi")"

  if [[ -f "$TARGET/GDCompare.installed" ]]; then
    # A dev deploy (scripts/deploy.sh) is there: setup offers update, writes only what changed.
    run update 0 $'1\ny\nn\n\n5\n' "${SETUP[@]}"
    check "offers update" has "Update or repair GrimCompare"
    check "files match the package" manifest_ok "$TARGET"
    run uninstall-dev 0 $'3\ny\n\n4\n' "${SETUP[@]}"
  fi

  run install 0 $'1\ny\nn\n\n5\n' "${SETUP[@]}"
  check "offers install" has "Install GrimCompare"
  check "plan lists the new files" has "GDCompare.asi - new"
  check "files match the package" manifest_ok "$TARGET"
  check "shows the next step" has "the tooltip shows the score"

  run again 0 $'1\nn\n\n5\n' "${SETUP[@]}"
  check "nothing to do when up to date" has "Everything is already in place."

  printf 'x' >>"$TARGET/GDCompare.asi"
  rm -f "$TARGET/dinput8.ini"
  printf '[broken\n' >"$TARGET/GDCompare.presets/pet.toml"
  run repair 0 $'1\ny\nn\n\n5\n' "${SETUP[@]}"
  check "damaged mod file replaced and backed up" has "GDCompare.asi - not from this setup, replaced; the old file is backed up"
  check "missing file restored" has "dinput8.ini - new"
  check "broken preset replaced" has "GDCompare.presets/pet.toml - has an error"
  check "files match the package" manifest_ok "$TARGET"
  check "backup kept" bash -c "ls '$TARGET'/GDCompare.backup/*/GDCompare.asi >/dev/null"

  # Wizard: the first character, its masteries, the first catalog build of the first category, saved.
  run wizard 0 $'1\n1\n1\n1\n1\ny\n\ny\n' "${SETUP[@]}" --wizard
  check "lists saved characters" has "Which character is this for?"
  check "saves the preset" has "Saved "
  local preset
  preset="$(grep -l '^# Written by gdcompare-setup' "$TARGET"/GDCompare.presets/*.toml 2>/dev/null | head -1)"
  check "preset written into the run folder" test -f "$preset"
  check "preset bound to the save folder" grep -q 'saves = \[' "$preset"

  # The same character again keeps its preset; the back answers (0) end in the main menu, then quit.
  run wizard-keep 0 $'1\n1\n3\n' "${SETUP[@]}" --wizard
  check "offers to keep the preset" has "in use for"
  check "keeps it" has "stays in use for"
  run back 0 $'2\n0\n5\n' "${SETUP[@]}"
  check "back from the wizard to the menu" test "$(grep -c 'What would you like to do?' "$LOG")" -ge 2

  # Cancel: input ends in a menu.
  run cancel 130 $'' "${SETUP[@]}"
  check "cancel says nothing changed" has "Cancelled. Nothing more was changed."

  # A dinput8.dll of another mod blocks the install and stays.
  run uninstall 0 $'3\ny\n\n4\n' "${SETUP[@]}"
  check "removes the mod files" test ! -e "$TARGET/GDCompare.asi" -a ! -e "$TARGET/dinput8.dll" -a ! -e "$TARGET/GDCompare.installed"
  check "keeps the preset made by the wizard" test -f "$preset"
  check "keeps backups" bash -c "ls '$TARGET'/GDCompare.backup/*/GDCompare.asi >/dev/null"
  cp "$TARGET/$PROTECTED" "$TARGET/dinput8.dll"
  local foreign
  foreign="$(hash_of "$TARGET/dinput8.dll")"
  run conflict 0 $'1\n\n4\n' "${SETUP[@]}"
  check "names the conflict" has "belongs to another mod"
  check "nothing written" has "Nothing was changed."
  check "foreign dinput8.dll untouched" test "$(hash_of "$TARGET/dinput8.dll")" = "$foreign"
  rm -f "$TARGET/dinput8.dll"

  # Through the launcher: install, then uninstall; the launcher waits for Enter at the end.
  run launcher-install 0 $'1\ny\nn\n\n5\n\n' "${LAUNCH[@]}"
  check "launcher runs setup" has "GrimCompare 1.0.1 setup"
  check "files match the package" manifest_ok "$TARGET"
  run launcher-uninstall 0 $'3\ny\n\n4\n\n' "${LAUNCH[@]}"
  check "uninstalled" test ! -e "$TARGET/GDCompare.installed"

  run bad-folder 1 $'' "${SETUP[@]}" --game "$BAD_GAME"
  check "explains a wrong folder" has "is not a Grim Dawn"

  check "game files and other mods untouched" test "$(hashes "$TARGET")" = "$before_hashes"
  check "package's mod was the one installed" test "$payload_asi" = "$(hash_of "$DIST/payload/GDCompare.asi")"
}

linux() {
  PLATFORM=linux
  local w="$E2E_DIR/linux" home steam
  home="$w/home"
  steam="$home/.local/share/Steam"
  rm -rf "$w"
  mkdir -p "$steam/steamapps/common"
  # A reflink copy: the real game folder is never written.
  cp -a --reflink=always "$GD_DIR" "$steam/steamapps/common/" || { echo "reflink copy failed: E2E_DIR must be on the game's (reflink-capable) filesystem" >&2; exit 2; }
  cp "$REAL_STEAM/steamapps/appmanifest_219990.acf" "$steam/steamapps/" 2>/dev/null || printf '"AppState"\n{\n"appid" "219990"\n}\n' >"$steam/steamapps/appmanifest_219990.acf"
  for u in "$REAL_STEAM"/userdata/*/; do
    local id
    id="$(basename "$u")"
    [[ -f "$u/config/localconfig.vdf" ]] && mkdir -p "$steam/userdata/$id/config" && cp "$u/config/localconfig.vdf" "$steam/userdata/$id/config/"
    [[ -d "$u/219990/remote/save" ]] && mkdir -p "$steam/userdata/$id/219990/remote" && cp -a "$u/219990/remote/save" "$steam/userdata/$id/219990/remote/"
  done
  local saves_before
  saves_before="$(cd "$steam/userdata" && find . -type f -print0 | sort -z | xargs -0 sha256sum)"
  # Copy the release folder too: the launcher fixes its executable bit in place.
  cp -a "$DIST" "$w/release"
  local env=(env -i "HOME=$home" "PATH=/run/current-system/sw/bin:/usr/bin:/bin" "TERM=dumb" "LANG=C.UTF-8")
  SETUP=("${env[@]}" "$w/release/bin/gdcompare-setup-linux" --plain)
  LAUNCH=("${env[@]}" GDC_IN_TERMINAL=1 "$w/release/install.sh" --plain)
  TARGET="$steam/steamapps/common/Grim Dawn/compat"
  [[ -f "$TARGET/Grim Dawn.exe" ]] || TARGET="$steam/steamapps/common/Grim Dawn/x64"
  PROTECTED="Engine.dll"
  BAD_GAME="$w"
  flows
  # Saves and Steam's configs (launch options are printed, never set).
  check "saves and Steam configs untouched" test "$(cd "$steam/userdata" && find . -type f -print0 | sort -z | xargs -0 sha256sum)" = "$saves_before"
}

wine_flows() {
  PLATFORM=wine
  command -v wine >/dev/null || { echo "wine not found (nix-shell -p wineWowPackages.stable)"; failures=$((failures + 1)); return; }
  local w="$E2E_DIR/wine"
  rm -rf "$w"
  mkdir -p "$w"
  export WINEPREFIX="$w/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" WINEARCH=win64
  unset DISPLAY WAYLAND_DISPLAY
  wineboot -i >/dev/null 2>&1
  wineserver -w
  local c="$WINEPREFIX/drive_c" user
  user="$(ls "$c/users" | grep -v -e '^Public$' -e '^steamuser$' | head -1)"
  [[ -n "$user" ]] || { fail "wine prefix has no user folder"; return; }
  # Fake Steam tree: the exes are placeholders, the database and text are the real ones (reflinked).
  local game="$c/Program Files (x86)/Steam/steamapps/common/Grim Dawn"
  mkdir -p "$game/x64" "$game/compat"
  printf 'MZ fake' >"$game/Grim Dawn.exe"
  printf 'MZ fake' >"$game/x64/Grim Dawn.exe"
  printf 'MZ fake dxgi (a DLSS mod)' >"$game/x64/dxgi.dll"
  cp "$REAL_STEAM/steamapps/appmanifest_219990.acf" "$c/Program Files (x86)/Steam/steamapps/" 2>/dev/null ||
    printf '"AppState"\n{\n"appid" "219990"\n}\n' >"$c/Program Files (x86)/Steam/steamapps/appmanifest_219990.acf"
  for d in database resources gdx1/database gdx1/resources gdx2/database gdx2/resources gdx3/database gdx3/resources; do
    [[ -d "$GD_DIR/$d" ]] && mkdir -p "$game/$(dirname "$d")" && cp -a --reflink=auto "$GD_DIR/$d" "$game/$(dirname "$d")/"
  done
  # One character in the local (Documents) saves.
  local save
  save="$(ls -d "$REAL_STEAM"/userdata/*/219990/remote/save/main/_*/ 2>/dev/null | grep -v '/__' | head -1)"
  if [[ -n "$save" ]]; then
    mkdir -p "$c/users/$user/Documents/My Games/Grim Dawn/save/main"
    cp -a "$save" "$c/users/$user/Documents/My Games/Grim Dawn/save/main/"
  fi
  cp -a "$DIST" "$c/release"
  SETUP=(wine 'C:\release\bin\gdcompare-setup.exe' --plain)
  LAUNCH=(wine cmd /c 'C:\release\install.bat' --plain)
  TARGET="$game/x64"
  PROTECTED="dxgi.dll"
  BAD_GAME='C:\release'
  flows
  wineserver -k 2>/dev/null
}

for f in "${FLOWS[@]}"; do
  case "$f" in
    linux) linux ;;
    wine) wine_flows ;;
    *) echo "unknown flow $f (linux, wine)" >&2; exit 2 ;;
  esac
done
echo
if ((failures)); then echo "$failures check(s) failed"; exit 1; fi
echo "all checks passed"
