# Extract the CharAttributeType enum (value -> class, strings) from Game.dll.
#
# Source: every exported default ctor `GAME::CharAttribute*::CharAttribute*(void)` stores its
# type as `movl $N, 0x8(%rcx)` and two string pointers at +0x40 / +0x48 (seen in
# CharAttributeMod_Dexterity::ctor 0xCE9A0: type 2, the base ctor 0xCAA60 zeroes all three).
# Strings are read from the file through the section table.
#
# usage: python3 -I char_attribute_types.py <Game.dll> <Game.exports.tsv>   (needs llvm-objdump on PATH)
import re
import struct
import subprocess
import sys

BASE = 0x180000000


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    out, p = [], pe + 24 + opt
    for _ in range(nsec):
        vsize, va, rsize, rptr = struct.unpack_from("<4I", data, p + 8)
        out.append((va, max(vsize, rsize), rptr))
        p += 40
    return out


def cstr(data, secs, rva):
    for va, size, rptr in secs:
        if va <= rva < va + size:
            off = rptr + rva - va
            end = data.index(b"\0", off)
            return data[off:end].decode("latin1")
    return None


def main(dll, tsv):
    data = open(dll, "rb").read()
    secs = sections(data)
    ctors = {}
    for line in open(tsv):
        rva, mangled, dem = line.rstrip("\n").split("\t")[:3]
        m = re.fullmatch(r"GAME::(CharAttribute\w*)::\1\(void\)", dem)
        if m:
            ctors.setdefault(int(rva, 16), m.group(1))
    rows = []
    for rva, cls in sorted(ctors.items()):
        dis = subprocess.run(
            ["llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={BASE + rva:#x}",
             f"--stop-address={BASE + rva + 0x80:#x}", dll],
            capture_output=True, text=True, check=True).stdout
        body = dis.split("ret")[0]
        typ = re.search(r"movl\s+\$0x([0-9a-f]+), 0x8\(%rcx\)", body)
        # lea <addr> into rax, then store rax at +0x40 / +0x48
        strs, last = {}, None
        for ln in body.splitlines():
            lm = re.search(r"leaq\s+-?0x[0-9a-f]+\(%rip\), %rax\s+# 0x([0-9a-f]+)", ln)
            if lm:
                last = int(lm.group(1), 16) - BASE
            sm = re.search(r"movq\s+%rax, 0x(40|48)\(%rcx\)", ln)
            if sm and last is not None:
                strs[sm.group(1)] = cstr(data, secs, last)
        if typ is None:
            continue
        rows.append((int(typ.group(1), 16), cls, strs.get("40") or "", strs.get("48") or ""))
    print("value\tclass\tfield_0x40\tfield_0x48")
    for r in sorted(rows):
        print(f"{r[0]}\t{r[1]}\t{r[2]}\t{r[3]}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
