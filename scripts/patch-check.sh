#!/usr/bin/env bash
# After a game patch: checks the adapter against the installed build and regenerates every table derived from the
# game files into a scratch directory, then prints what changed and what broke.
#
#   scripts/patch-check.sh           check only, the repository is not touched
#   scripts/patch-check.sh --write   also copy the regenerated tables into the repository, then run the tests on them
#
# Game directory: $GD_DIR, else the default Steam path. Needs winedump (wine) on PATH; enters nix-shell for the rest.
# Every line starts with OK, CHANGED (a regenerated table differs: review the diff, then --write) or FAIL (the
# adapter or a test no longer matches the build). Exit 1 if anything FAILed.
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
ROOT="$PWD"
if [[ -z "${GDC_PATCH_SHELL:-}" ]]; then
  exec env GDC_PATCH_SHELL=1 nix-shell -p 'python3.withPackages(p: [p.capstone])' llvmPackages_21.llvm \
    --run "bash '$ROOT/scripts/patch-check.sh' ${1:-}"
fi

WRITE=0
case "${1:-}" in
  "") ;;
  --write) WRITE=1 ;;
  *) echo "usage: scripts/patch-check.sh [--write]" >&2; exit 2 ;;
esac
GD="${GD_DIR:-$HOME/.local/share/Steam/steamapps/common/Grim Dawn}"
[[ -f "$GD/x64/Game.dll" && -f "$GD/compat/Game.dll" ]] || { echo "FAIL no Grim Dawn at $GD (set GD_DIR)"; exit 1; }
command -v winedump >/dev/null || { echo "FAIL winedump not on PATH (wine)"; exit 1; }
W="$(mktemp -d /tmp/gdc-patch.XXXXXX)"  # not nix-shell's TMPDIR: that one goes with the shell
FAILED=0
ok() { echo "OK      $*"; }
changed() { echo "CHANGED $*"; }
fail() { echo "FAIL    $*"; FAILED=1; }
py() { python3 -I "$@"; }

# Regenerated file vs the repository's copy; with --write the copy is replaced.
compare() {  # <label> <new file> <repository path>
  if cmp -s "$2" "$ROOT/$3"; then ok "$1 ($3)"; return; fi
  changed "$1 ($3): $(diff "$ROOT/$3" "$2" | grep -c '^[<>]') lines differ, diff -u $3 $2"
  if ((WRITE)); then cp "$2" "$ROOT/$3"; fi
}
# Runs a generator into the scratch directory, then compares; a generator that fails is a FAIL.
gen() {  # <label> <repository path> <generator args...>
  local label="$1" path="$2" out="$W/$(basename "$2")"
  shift 2
  if py "$@" >"$out" 2>"$out.err"; then compare "$label" "$out" "$path"
  else fail "$label: $* exited $? ($(tail -n1 "$out.err"))"
  fi
}

echo "== build"
version="$(grep -a -o -m1 -E 'v[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+ \(x64\)' "$GD/x64/Engine.dll" | head -n1 || true)"
manifest="$GD/../../appmanifest_219990.acf"
buildid="$([[ -f "$manifest" ]] && sed -n 's/.*"buildid"[[:space:]]*"\([0-9]*\)".*/\1/p' "$manifest" || true)"
echo "        ${version:-version string not found}, Steam build ${buildid:-unknown}, scratch $W"
for f in x64/Game.dll compat/Game.dll x64/Engine.dll compat/Engine.dll; do
  echo "        $(sha256sum "$GD/$f" | cut -c1-16)  $f"
done

echo "== exports"
# Dumped from compat/ like re/*.exports.tsv; the x64/ variant is checked by the target resolution test below.
for m in Game:compat/Game.dll Engine:compat/Engine.dll "Exe:x64/Grim Dawn.exe"; do
  name="${m%%:*}" file="${m#*:}"
  if ! py re/tools/demangle_exports.py "$GD/$file" "$W/$name.exports.tsv" >"$W/$name.exports.err" 2>&1; then
    fail "$name exports: $(tail -n1 "$W/$name.exports.err")"
    continue
  fi
  old="$ROOT/re/$name.exports.tsv"
  added="$(comm -13 <(cut -f2 "$old" | sort) <(cut -f2 "$W/$name.exports.tsv" | sort) | wc -l)"
  removed="$(comm -23 <(cut -f2 "$old" | sort) <(cut -f2 "$W/$name.exports.tsv" | sort) | wc -l)"
  moved="$(join -t $'\t' -1 2 -2 2 <(sort -t $'\t' -k2,2 "$old") <(sort -t $'\t' -k2,2 "$W/$name.exports.tsv") | awk -F'\t' '$2 != $4' | wc -l)"
  if ((added + removed + moved == 0)); then ok "$name exports unchanged"
  else changed "$name exports: +$added -$removed, $moved moved (comm on column 2 of re/$name.exports.tsv and $W/$name.exports.tsv)"
  fi
  if ((WRITE)); then cp "$W/$name.exports.tsv" "$old"; fi
done

echo "== adapter offsets (x64/ and compat/ Game.dll, against the new exports)"
for v in x64 compat; do
  if py re/tools/verify_build.py "$GD/$v/Game.dll" "$W/Game.exports.tsv" >"$W/verify-$v.txt" 2>&1; then
    ok "verify_build $v: $(tail -n1 "$W/verify-$v.txt")"
  else
    fail "verify_build $v: $(grep '^FAIL' "$W/verify-$v.txt" | head -n5 | tr '\n' ';') (full: $W/verify-$v.txt)"
  fi
done

echo "== tables from Game.dll"
dll="$GD/x64/Game.dll"
gen "class tree" re/GameClassTree.tsv re/tools/class_tree.py "$dll" "$W/Game.exports.tsv"
gen "class tree include" core/src/class_tree.inc re/tools/class_tree.py --inc "$W/GameClassTree.tsv"
gen "CharAttributeType" re/CharAttributeType.tsv re/tools/char_attribute_types.py "$dll" "$W/Game.exports.tsv"
gen "CombatAttributeType" re/CombatAttributeType.tsv re/tools/combat_attribute_types.py "$dll" "$W/Game.exports.tsv"
gen "defense fields" re/DefenseAttributeFields.tsv re/tools/defense_fields.py "$dll" "$W/Game.exports.tsv"
gen "text classes" re/GameTextClass.tsv re/tools/game_text_class.py "$dll" "$GD"

echo "== tables from the database and resources"
gen "enemies by level" re/EnemyByLevel.tsv re/tools/enemy_table.py "$GD"
gen "enemy resists" re/EnemyResistByDifficulty.tsv re/tools/enemy_table.py "$GD" --resists
gen "player resist penalty" re/PlayerResistByDifficulty.tsv re/tools/enemy_table.py "$GD" --penalty
gen "enemy races" re/EnemyRaces.tsv re/tools/enemy_table.py "$GD" --races
# enemy_cpp.py rewrites core/src/enemy.cpp in place: run it on a copy fed with the new tables.
mkdir -p "$W/root/re" "$W/root/core/src"
cp "$W/EnemyByLevel.tsv" "$W/EnemyRaces.tsv" "$W/root/re/"
cp "$ROOT/core/src/enemy.cpp" "$W/root/core/src/"
if py re/tools/enemy_cpp.py "$W/root" >"$W/enemy_cpp.err" 2>&1; then compare "enemy tables" "$W/root/core/src/enemy.cpp" core/src/enemy.cpp
else fail "enemy tables: $(tail -n1 "$W/enemy_cpp.err")"
fi
gen "tooltip fonts" core/src/font_tables.inc re/tools/font_tables.py "$GD/resources/Fonts.arc"
gen "coverage" docs/COVERAGE.md re/tools/coverage.py "$GD"

echo "== tests (signatures, target resolution, catalog, database checks; tables as in the repository$( ((WRITE)) && echo ', now regenerated'))"
if GD_DIR="$GD" nix-shell "$ROOT/shell.nix" --run "cmake --preset core >/dev/null && cmake --build build/core >/dev/null && build/core/tests/gdc_tests" >"$W/tests.txt" 2>&1; then
  ok "gdc_tests: $(grep -m1 'test cases:' "$W/tests.txt" | tr -s ' ')"
else
  fail "gdc_tests: $(grep -m1 'test cases:' "$W/tests.txt" | tr -s ' ')"
  awk '/^TEST CASE:/ { tc = $0 } /(ERROR|FATAL ERROR):/ && tc != "" { print "          " tc; tc = "" }' "$W/tests.txt"
  echo "          full output: $W/tests.txt"
fi

echo "== not checked here: the in-game test plan"
exit "$FAILED"
