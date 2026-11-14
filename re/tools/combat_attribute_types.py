# Extract the CombatAttributeType enum (value -> classes, DB field tags) from Game.dll.
#
# Source: exported `enum GAME::CombatAttributeType <Class>::GetType(void) const` methods whose body is
# `movl $N, %eax; retq` (e.g. DamageAttributeAbs_Fire::GetType 0x2DE70), and the same class's
# exported `char const * <Class>::Get<...>Tag(void) const` getters (`leaq str(%rip), %rax; retq`,
# e.g. DamageAttributeAbs_Fire::GetLoadValueMinTag 0x19A320 -> "offensiveFireMin").
# GetType bodies that are not a constant (base classes reading a member) are reported as '?'.
#
# usage: python3 -I combat_attribute_types.py <Game.dll> <Game.exports.tsv>   (needs llvm-objdump on PATH)
import collections
import re
import subprocess
import sys

sys.path.insert(0, __import__("os").path.dirname(__file__))  # -I drops the script dir
from char_attribute_types import BASE, cstr, sections  # noqa: E402


def first_insns(dll, rva, n=0x10):
    out = subprocess.run(
        ["llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={BASE + rva:#x}",
         f"--stop-address={BASE + rva + n:#x}", dll], capture_output=True, text=True, check=True).stdout
    return [l.split("\t", 1)[1].strip() for l in out.splitlines() if re.match(r"\s*[0-9a-f]+:\s", l) and "\t" in l]


def main(dll, tsv):
    data = open(dll, "rb").read()
    secs = sections(data)
    gettype, tags = {}, collections.defaultdict(dict)
    for line in open(tsv):
        rva, _, dem = line.rstrip("\n").split("\t")[:3]
        rva = int(rva, 16)
        m = re.fullmatch(r"enum GAME::CombatAttributeType GAME::(\w+)::GetType\(void\) const", dem)
        if m:
            gettype[m.group(1)] = rva
            continue
        m = re.fullmatch(r"char const \* GAME::(\w+)::(Get\w*Tag)\(void\) const", dem)
        if m:
            tags[m.group(1)][m.group(2)] = rva
    by_value = collections.defaultdict(list)
    for cls, rva in sorted(gettype.items()):
        ins = first_insns(dll, rva)
        val = None
        if len(ins) > 1 and ins[1].startswith("ret"):
            if m := re.fullmatch(r"movl\s+\$0x([0-9a-f]+), %eax", ins[0]):
                val = int(m.group(1), 16)
            elif re.fullmatch(r"xorl\s+%eax, %eax", ins[0]):
                val = 0
        fields = {}
        for name, trva in sorted(tags[cls].items()):
            t = first_insns(dll, trva)
            lm = re.search(r"# 0x([0-9a-f]+)", t[0]) if t and t[0].startswith("leaq") else None
            if lm:
                fields[name] = cstr(data, secs, int(lm.group(1), 16) - BASE)
        by_value["?" if val is None else val].append((cls, fields))
    print("value\tclass\ttags")
    for val in sorted(by_value, key=lambda v: (isinstance(v, str), v if isinstance(v, int) else 0)):
        for cls, fields in by_value[val]:
            print(f"{val}\t{cls}\t" + " ".join(f"{k.removeprefix('Get').removesuffix('Tag')}={v}" for k, v in fields.items()))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
