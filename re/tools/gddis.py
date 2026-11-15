# Disassemble one function of Game.dll/Engine.dll with call targets, IAT slots and string
# literals annotated. Function end = next export RVA (or --len bytes).
#
# usage: nix-shell -p 'python3.withPackages(p: [p.capstone])' --run \
#          "python3 -I re/tools/gddis.py <dll> <exports.tsv> <rva-hex | name-substring> [--len N] [--other dll:tsv]"
#   --other: a second module whose exports name IAT imports (e.g. Engine.dll for Game.dll)
#   --xrefs <rva-hex>: list functions in <dll> that call/jmp/lea that RVA instead
#   --vtable <rva-hex> [N]: print N (decimal, default 16) slots of the vtable at that RVA
import struct
import sys

import capstone

BASE = 0x180000000


class PE:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt = pe + 24
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.secs = []
        p = opt + optsz
        for _ in range(nsec):
            name = d[p:p + 8].rstrip(b"\0").decode()
            vsize, va, rsize, rptr = struct.unpack_from("<4I", d, p + 8)
            self.secs.append((name, va, max(vsize, rsize), rptr, rsize))
            p += 40
        imp_rva = struct.unpack_from("<I", d, opt + 112 + 8)[0]  # DataDirectory[1]
        self.imports = {}  # IAT slot rva -> "dll!name"
        p = imp_rva
        while True:
            ilt, _, _, name_rva, iat = struct.unpack_from("<5I", d, self.off(p))
            if not name_rva:
                break
            dll = self.cstr(name_rva)
            i = 0
            src = ilt or iat
            while True:
                e = struct.unpack_from("<Q", d, self.off(src + 8 * i))[0]
                if not e:
                    break
                nm = f"#{e & 0xFFFF}" if e >> 63 else self.cstr((e & 0x7FFFFFFF) + 2)
                self.imports[iat + 8 * i] = f"{dll}!{nm}"
                i += 1
            p += 20

    def off(self, rva):
        for _, va, size, rptr, rsize in self.secs:
            if va <= rva < va + size:
                return rptr + rva - va if rva - va < rsize else None
        return None

    def cstr(self, rva, limit=200):
        o = self.off(rva)
        if o is None:
            return None
        e = self.data.find(b"\0", o, o + limit)
        return self.data[o:e if e >= 0 else o + limit].decode("latin1")

    def section(self, rva):
        for name, va, size, _, _ in self.secs:
            if va <= rva < va + size:
                return name
        return None


def load_exports(tsv):
    out = {}
    for line in open(tsv, encoding="utf-8"):
        rva, mangled, demangled = line.rstrip("\n").split("\t")
        out.setdefault(int(rva, 16), demangled or mangled)
    return out


def literal(pe, rva):
    if pe.section(rva) not in (".rdata", ".data"):
        return None
    s = pe.cstr(rva, 120)
    if s and len(s) >= 3 and all(32 <= ord(c) < 127 for c in s):
        return repr(s)
    o = pe.off(rva)  # UTF-16
    if o is not None:
        raw = pe.data[o:o + 160]
        chars = []
        for i in range(0, len(raw) - 1, 2):
            c = raw[i] | raw[i + 1] << 8
            if c == 0:
                break
            if not 32 <= c < 127:
                return None
            chars.append(chr(c))
        if len(chars) >= 3:
            return "u" + repr("".join(chars))
    return None


def annotate(pe, exports, other, ins):
    notes = []
    for op in ins.operands:
        if op.type == capstone.x86.X86_OP_IMM and ins.group(capstone.CS_GRP_JUMP) or \
           op.type == capstone.x86.X86_OP_IMM and ins.group(capstone.CS_GRP_CALL):
            t = op.imm - BASE
            if t in exports:
                notes.append(exports[t])
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            t = ins.address + ins.size + op.mem.disp - BASE
            if t in pe.imports:
                imp = pe.imports[t]
                notes.append(f"[IAT {imp}]")
            elif t in exports:
                notes.append(f"&{exports[t]}")
            else:
                lit = literal(pe, t)
                notes.append(f"rva {t:#x}" + (f" {lit}" if lit else ""))
    return "   ; " + " | ".join(notes) if notes else ""


def disasm(pe, exports, other, rva, length, past_int3=False):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    o = pe.off(rva)
    code = pe.data[o:o + length]
    for ins in md.disasm(code, BASE + rva):
        r = ins.address - BASE
        tag = f"<{exports[r]}>" if r in exports and r != rva else ""
        if tag:
            print(tag)
        print(f"  {r:08x}  {ins.mnemonic:7} {ins.op_str}{annotate(pe, exports, other, ins)}")
        if ins.mnemonic == "int3" and r > rva and not past_int3:
            break


def xrefs(pe, exports, target):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = False
    starts = sorted(exports)
    import bisect
    hits = set()
    for name, va, size, rptr, rsize in pe.secs:
        if name != ".text":
            continue
        code = pe.data[rptr:rptr + rsize]
        # scan for E8/E9 rel32 and RIP-relative disp32 to target
        for i in range(len(code) - 5):
            b = code[i]
            if b in (0xE8, 0xE9):
                rel = struct.unpack_from("<i", code, i + 1)[0]
                if va + i + 5 + rel == target:
                    hits.add(va + i)
            # lea/mov rip-relative: disp at i, instruction end unknown; try common lengths
        for i in range(len(code) - 4):
            disp = struct.unpack_from("<i", code, i)[0]
            for tail in (0, 1, 4):
                if va + i + 4 + tail + disp == target:
                    hits.add(va + i)
    for h in sorted(hits):
        k = bisect.bisect_right(starts, h) - 1
        fn = exports[starts[k]] if k >= 0 else "?"
        print(f"{h:08x}  in {fn} (+{h - starts[k]:#x})")


def main(argv):
    dll, tsv, what = argv[1], argv[2], argv[3]
    pe = PE(dll)
    global BASE  # the module's ImageBase (DLLs 0x180000000, Grim Dawn.exe 0x140000000)
    BASE = struct.unpack_from("<Q", pe.data, struct.unpack_from("<I", pe.data, 0x3C)[0] + 24 + 24)[0]
    exports = load_exports(tsv)
    other = None
    length = None
    if "--len" in argv:
        length = int(argv[argv.index("--len") + 1], 0)
    if what == "--vtable":
        v = int(argv[4], 16)
        n = int(argv[5]) if len(argv) > 5 else 16
        print(f"== vtable {v:08x} {exports.get(v, '?')}")
        for i in range(n):
            t = struct.unpack_from("<Q", pe.data, pe.off(v + 8 * i))[0] - BASE
            print(f"  +{8 * i:#05x} {t:08x} {exports.get(t, '')}")
        return
    if what == "--xrefs":
        xrefs(pe, exports, int(argv[4], 16))
        return
    try:
        rva = int(what, 16)
    except ValueError:
        matches = [(r, n) for r, n in exports.items() if what in n]
        if len(matches) != 1:
            for r, n in sorted(matches)[:40]:
                print(f"{r:08x}  {n}")
            sys.exit(f"{len(matches)} matches")
        rva = matches[0][0]
    print(f"== {rva:08x} {exports.get(rva, '?')}")
    explicit = length is not None  # an explicit --len runs through int3 (a function with a noreturn call inside)
    if length is None:
        nxt = min((r for r in exports if r > rva), default=rva + 0x400)
        length = min(nxt - rva, 0x2000)
    disasm(pe, exports, other, rva, length, explicit)


if __name__ == "__main__":
    main(sys.argv)
