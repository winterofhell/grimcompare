# Extract the GameTextClass table (value -> name -> text style) from Game.dll and the database.
#
# Source: the unexported style loader (Game.dll 1.3.1.1 0x313190 and around) reads each <name> and <name>Indent field
# of records/game/gameengine.dbr (`lea rdx, "<name>"` then `lea rdx, "<name>Indent"`) and stores the class constant
# next to them: the first `mov dword ptr [rbp + d], imm` from the name read on (before or after the Indent read, as
# the compiler placed it; the indent itself is stored from a register). Default stores r14d, which is 0 there
# (xor r14d, r14d; 0x313242). GameEngine::GetGameTextStyleName (0x2F5700) looks the class up in that map.
# The style is gameengine.dbr <name>.
#
# usage: python3 -I game_text_class.py <Game.dll> <Grim Dawn dir>   (needs capstone)
import os
import re
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(__file__))
import arz
from gddis import PE

HEADER = """# GameTextClass value -> name -> text style (records/game/gameengine.dbr <name>, records/ui/styles/text/<style>.dbr).
# Values: the unexported style loader at Game.dll 1.3.1.1 0x313190 reads each <name> / <name>Indent field and
# stores the class constant next to it (Default = r14d = 0 at 0x313242); GameEngine::GetGameTextStyleName
# (0x2F5700) looks the class up in that map. Extracted by scanning lea <name> -> mov dword [rbp+..], imm."""
NAME = re.compile(r"[A-Za-z][A-Za-z0-9]*")


def lea_rdx_sites(pe):
    """(site rva, string) for every `lea rdx, [rip + rel32]` (48 8D 15) in .text that points at a C string."""
    d = pe.data
    for name, va, size, rptr, rsize in pe.secs:
        if name != ".text":
            continue
        p = d.find(b"\x48\x8d\x15", rptr, rptr + rsize)
        while p >= 0:
            rva = va + p - rptr
            target = rva + 7 + struct.unpack_from("<i", d, p + 3)[0]
            if pe.section(target) == ".rdata":
                s = pe.cstr(target, 64)
                if s and NAME.fullmatch(s):
                    yield rva, s
            p = d.find(b"\x48\x8d\x15", p + 1, rptr + rsize)


def class_value(pe, md, start, indent):
    o = pe.off(start)
    for ins in md.disasm(pe.data[o:o + indent - start + 0x100], start):
        if ins.mnemonic == "ret":
            break
        m = re.fullmatch(r"dword ptr \[rbp [+-] 0x[0-9a-f]+\], (0x[0-9a-f]+|\d+|r14d)", ins.op_str)
        if ins.mnemonic == "mov" and m:
            return 0 if m.group(1) == "r14d" else int(m.group(1), 0)
    return None


def main(dll, gd):
    pe = PE(dll)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    sites = list(lea_rdx_sites(pe))
    rows = {}
    for rva, s in sites:
        if not s.endswith("Indent"):
            continue
        name = [r for r, n in sites if n == s[:-6] and rva - 0x100 < r < rva]
        if not name:
            continue
        v = class_value(pe, md, name[-1], rva)
        if v is None:
            continue
        if rows.get(v, s[:-6]) != s[:-6]:
            sys.exit(f"class {v}: both {rows[v]} and {s[:-6]}")
        rows[v] = s[:-6]
    if not rows or sorted(rows) != list(range(len(rows))):
        sys.exit(f"class values not 0..n-1: {sorted(rows)}")
    dbs = [arz.load(os.path.join(gd, f)) for f in ("database/database.arz", "gdx1/database/GDX1.arz",
                                                    "gdx2/database/GDX2.arz", "gdx3/database/GDX3.arz")]
    engine = {}
    for db in dbs:
        if "records/game/gameengine.dbr" in db[2]:
            engine = arz.fields(db, "records/game/gameengine.dbr")[1]
    print(HEADER)
    for v in sorted(rows):
        style = engine.get(rows[v], "")
        if not isinstance(style, str) or not style:
            sys.exit(f"gameengine.dbr has no {rows[v]}")
        print(f"{v}\t{rows[v]}\t{os.path.splitext(os.path.basename(style.replace(chr(92), '/')))[0]}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: game_text_class.py <Game.dll> <Grim Dawn dir>")
    main(sys.argv[1], sys.argv[2])
