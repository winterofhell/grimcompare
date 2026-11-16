#!/usr/bin/env bash
# Builds the release zip that GitHub Releases and Nexus both ship: dist/GDCompare-<version>.zip.
#   install.bat / install.sh       launchers
#   bin/gdcompare-setup.exe        Windows x64, static CRT
#   bin/gdcompare-setup-linux      Linux x86-64, static
#   payload/                       GDCompare.asi, dinput8.dll (UAL v9.7.4), dinput8.ini, presets/, catalog/
# Runs inside nix-shell (re-enters it if needed); needs the xwin splat (docs/BUILD.md).
set -euo pipefail
cd "$(dirname "$(realpath "$0")")/.."
ROOT="$PWD"
if [[ -z "${IN_NIX_SHELL:-}" ]]; then exec nix-shell "$ROOT/shell.nix" --run "bash '$ROOT/scripts/package.sh'"; fi
. "$ROOT/scripts/ual.sh"

VERSION="$(sed -n 's/.*kVersion = "\(.*\)".*/\1/p' installer/src/main.cpp)"
[[ -n "$VERSION" ]] || { echo "package: no kVersion in installer/src/main.cpp" >&2; exit 1; }
NAME="GDCompare-$VERSION"
OUT="$ROOT/dist/$NAME"

cmake --workflow --preset adapter
cmake --workflow --preset setup-win
cmake --workflow --preset setup-linux
fetch_ual "${XDG_CACHE_HOME:-$HOME/.cache}/gdcompare"

rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/bin" "$OUT/payload/presets" "$OUT/payload/catalog"
cp installer/launchers/install.bat installer/launchers/install.sh README.md LICENSE THIRD_PARTY_NOTICES.md "$OUT/"
cp build/setup-win/installer/gdcompare-setup.exe "$OUT/bin/"
cp build/setup-linux/installer/gdcompare-setup "$OUT/bin/gdcompare-setup-linux"
cp build/adapter/adapter/GDCompare.asi "$OUT/payload/"
cp "$UAL_DLL" "$OUT/payload/dinput8.dll"
cp adapter/ual/dinput8.ini "$OUT/payload/"
cp presets/*.toml "$OUT/payload/presets/"
cp catalog/builds.toml "$OUT/payload/catalog/"

# The static Linux binary runs.
"$OUT/bin/gdcompare-setup-linux" --version
python3 -I - "$OUT" <<'PY'
import os, sys, zipfile
out = sys.argv[1]
base = os.path.dirname(out)
with zipfile.ZipFile(out + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk(out):
        for f in sorted(files):
            p = os.path.join(d, f)
            info = zipfile.ZipInfo.from_file(p, os.path.relpath(p, base))
            info.compress_type = zipfile.ZIP_DEFLATED
            exe = f in ("install.sh", "gdcompare-setup-linux")
            info.external_attr = ((0o755 if exe else 0o644) | 0o100000) << 16
            with open(p, "rb") as fh:
                z.writestr(info, fh.read())
PY
( cd "$ROOT/dist" && sha256sum "$NAME.zip" > "$NAME.zip.sha256" )
echo "dist/$NAME.zip"
