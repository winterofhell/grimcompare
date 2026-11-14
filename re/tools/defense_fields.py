# Extract DefenseAttribute* class -> database field names from Game.dll.
#
# Source: the defense store loaders (e.g. the one behind DefenseAttributeStore_Max::Load, 0x22A3D9..0x22A43E)
# build each attribute inline: `lea rcx, <Class>::vftable; ... mov [obj], rcx`, then store string literals
# into the new object: +0x10 the chance field ("defensiveChaosChance"), +0x50 the value field
# ("defensiveChaos"), +0x58 the display tag ("DefenseChaos"), and the type at +8 (0xA = Chaos). This sweeps
# .text linearly and, for 40 instructions after each DefenseAttribute* vftable lea, pairs `lea reg, str`
# with the next `mov [mem], reg`. Output: class, +0x10 field, +0x50 field (only classes with a +0x50 field).
#
# usage: nix-shell -p 'python3.withPackages(p: [p.capstone])' --run \
#          "python3 -I defense_fields.py <Game.dll> <Game.exports.tsv>"
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I drops the script dir
import capstone  # noqa: E402
import gddis  # noqa: E402

WINDOW = 40


def main(dll, tsv):
    pe = gddis.PE(dll)
    vft = {}
    for line in open(tsv):
        rva, _, dem = line.rstrip("\n").split("\t")[:3]
        m = re.fullmatch(r"const GAME::(DefenseAttribute\w+)::`vftable'", dem)
        if m:
            vft[int(rva, 16)] = m.group(1)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    rip = capstone.x86.X86_REG_RIP
    out = collections.defaultdict(dict)
    for name, va, _size, rptr, rsize in pe.secs:
        if name != ".text":
            continue
        code = pe.data[rptr:rptr + rsize]
        off, cls, left, pend = 0, None, 0, {}
        while off < len(code):
            progressed = False
            for ins in md.disasm(code[off:off + 0x4000], gddis.BASE + va + off):
                progressed = True
                off = ins.address - gddis.BASE - va + ins.size
                ops = ins.operands
                if ins.mnemonic == "lea" and ops[1].type == capstone.x86.X86_OP_MEM and ops[1].mem.base == rip:
                    tgt = ins.address + ins.size + ops[1].mem.disp - gddis.BASE
                    if tgt in vft:
                        cls, left, pend = vft[tgt], WINDOW, {}
                        continue
                    if cls:
                        s = pe.cstr(tgt)
                        if s:
                            pend[ins.reg_name(ops[0].reg)] = s
                elif (cls and ins.mnemonic == "mov" and len(ops) == 2 and ops[0].type == capstone.x86.X86_OP_MEM
                      and ops[1].type == capstone.x86.X86_OP_REG):
                    reg = ins.reg_name(ops[1].reg)
                    if reg in pend:
                        out[cls].setdefault(ops[0].mem.disp, pend.pop(reg))
                if cls:
                    left -= 1
                    if left <= 0:
                        cls, pend = None, {}
            if not progressed:
                off += 1  # undecodable byte (data in .text)
    print("class\tchance_field\tvalue_field")
    for cls in sorted(out):
        f = out[cls]
        if 0x50 in f:
            print(f"{cls}\t{f.get(0x10, '')}\t{f[0x50]}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
