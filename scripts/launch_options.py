#!/usr/bin/env python3
"""Grim Dawn (app 219990) Steam launch options with `dinput8=n,b` merged into / removed from
WINEDLLOVERRIDES. Reads the current options from every Steam user's localconfig.vdf and prints
the exact string to paste. Never writes Steam files (Steam overwrites them while running).
`add` with a game folder also drops DXVK_HDR without a renodx-*.addon64 (RenoDX HDR).
Every other override (e.g. `dxgi` for a ReShade/DLSS mod) is always kept as is.

usage: launch_options.py add|remove [steam_root] [game_folder]
"""
import re
import sys
from pathlib import Path

APP_ID = "219990"
DLL = "dinput8"
MODE = "n,b"
_OVR = re.compile(r'WINEDLLOVERRIDES=("[^"]*"|\S*)')
_HDR = re.compile(r'(?<!\S)DXVK_HDR=\S*\s*')


def _parse_overrides(value: str) -> list[tuple[list[str], str]]:
    """'a,b=n,b;c=b' → [(['a','b'], 'n,b'), (['c'], 'b')]. Entry without '=' keeps mode ''."""
    out = []
    for entry in filter(None, value.split(";")):
        dlls, _, mode = entry.partition("=")
        out.append(([d for d in dlls.split(",") if d], mode if "=" in entry else None))
    return out


def _format_overrides(entries) -> str:
    parts = []
    for dlls, mode in entries:
        if dlls:
            parts.append(",".join(dlls) + ("" if mode is None else "=" + mode))
    return ";".join(parts)


def _rewrite(opts: str, add: bool, present: set[str] | None = None) -> str:
    """present: lowercase file names in the game folder; None = don't drop stale mod options."""
    if present is not None and not any(f.startswith("renodx") and f.endswith(".addon64") for f in present):
        opts = _HDR.sub("", opts)
    m = _OVR.search(opts)
    entries = _parse_overrides(m.group(1).strip('"')) if m else []
    for dlls, _ in entries:
        dlls[:] = [d for d in dlls if d.lower() != DLL]
    if add:
        target = next((e for e in entries if e[1] == MODE and e[0]), None)
        if target:
            target[0].append(DLL)
        else:
            entries.append(([DLL], MODE))
    value = _format_overrides(entries)
    token = f'WINEDLLOVERRIDES="{value}"' if value else ""
    if m:
        opts = (opts[: m.start()] + token + opts[m.end():]).strip()
        opts = re.sub(r"  +", " ", opts)
    elif token:
        opts = f"{token} {opts}".strip()
    if add and "%command%" not in opts:
        opts = f"{opts} %command%".strip()
    return opts


def merge(opts: str, present: set[str] | None = None) -> str:
    return _rewrite(opts, add=True, present=present)


def remove(opts: str) -> str:
    return _rewrite(opts, add=False)


def _vdf_tokens(text: str):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "{}":
            yield c
            i += 1
        elif c == '"':
            j, buf = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 1
                buf.append(text[j])
                j += 1
            yield "".join(buf)
            i = j + 1
        elif text.startswith("//", i):
            i = text.find("\n", i) + 1 or n
        else:
            i += 1


def parse_vdf(text: str) -> dict:
    root, stack, key = {}, [], None
    cur = root
    for tok in _vdf_tokens(text):
        if tok == "{":
            child = {}
            cur[key.lower()] = child
            stack.append(cur)
            cur, key = child, None
        elif tok == "}":
            cur = stack.pop() if stack else root
        elif key is None:
            key = tok
        else:
            cur[key.lower()] = tok
            key = None
    return root


def current_options(localconfig: Path) -> str | None:
    d = parse_vdf(localconfig.read_text(encoding="utf-8", errors="replace"))
    node = d
    for k in ("userlocalconfigstore", "software", "valve", "steam", "apps", APP_ID):
        node = node.get(k) if isinstance(node, dict) else None
        if node is None:
            return None
    return node.get("launchoptions", "") if isinstance(node, dict) else None


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] not in ("add", "remove"):
        print(__doc__, file=sys.stderr)
        return 2
    steam = Path(argv[2]) if len(argv) > 2 else Path.home() / ".local/share/Steam"
    present = {f.name.lower() for f in Path(argv[3]).iterdir()} if len(argv) > 3 else None
    fn = (lambda o: merge(o, present)) if argv[1] == "add" else remove
    found = False
    for cfg in sorted(steam.glob("userdata/*/config/localconfig.vdf")):
        opts = current_options(cfg)
        if opts is None:
            continue
        found = True
        new = fn(opts)
        print(f"Steam user {cfg.parts[-3]}:")
        print(f"  current : {opts or '(empty)'}")
        print(f"  set to  : {new}" + ("   (already correct)" if new == opts else ""))
    if not found:
        print(f"No Steam user has Grim Dawn launch options yet. Set to:\n  {fn('')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
