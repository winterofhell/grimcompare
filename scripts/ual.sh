# Sourced by deploy.sh and package.sh: Ultimate ASI Loader x64, pinned
# (github.com/ThirteenAG/Ultimate-ASI-Loader, release 2026-08-16). fetch_ual sets UAL_DLL to a verified dinput8.dll.
UAL_VERSION="v9.7.4"
UAL_URL="https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/$UAL_VERSION/Ultimate-ASI-Loader_x64.zip"
UAL_ZIP_SHA256="8272d83b2692662098746f2d0ad0e2d85f3c8358ab1d63f75fbe835c2c8135fd"
UAL_DLL_SHA256="fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7"  # = installer/src/install.h kUalSha256

fetch_ual() {  # $1: cache dir. Download once, verify both hashes.
  local cache="$1" zip
  mkdir -p "$cache"
  zip="$cache/Ultimate-ASI-Loader_x64-$UAL_VERSION.zip"
  UAL_DLL="$cache/dinput8-$UAL_VERSION.dll"
  if [[ ! -f "$UAL_DLL" ]]; then
    if [[ ! -f "$zip" ]]; then
      curl -fsSL "$UAL_URL" -o "$zip.part"
      mv -f "$zip.part" "$zip"
    fi
    [[ "$(sha256sum "$zip" | cut -d' ' -f1)" == "$UAL_ZIP_SHA256" ]] || { rm -f "$zip"; echo "ASI loader zip hash mismatch" >&2; return 1; }
    python3 -I -c 'import sys,zipfile; open(sys.argv[2],"wb").write(zipfile.ZipFile(sys.argv[1]).read("dinput8.dll"))' "$zip" "$UAL_DLL"
  fi
  [[ "$(sha256sum "$UAL_DLL" | cut -d' ' -f1)" == "$UAL_DLL_SHA256" ]] || { rm -f "$UAL_DLL"; echo "dinput8.dll hash mismatch" >&2; return 1; }
}
