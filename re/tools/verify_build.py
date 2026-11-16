# Checks every layout constant the adapter hardcodes (adapter/src/snapshot.cpp, tooltip.cpp, core/include/gdc/
# code_scan.h) against a Game.dll build: each check disassembles the function the constant was read from and
# requires the instruction that shows it. Run after every game patch; any FAIL means that constant needs a re-trace
# before the adapter can be trusted on the new build (the adapter's own init checks cover the vtable slots it
# compares against exports, not these).
#
# usage: nix-shell -p 'python3.withPackages(p: [p.capstone])' --run \
#          "python3 -I re/tools/verify_build.py <Game.dll> re/Game.exports.tsv"
# Exit: 0 all pass, 1 any FAIL, 2 usage.
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capstone  # noqa: E402
import gddis  # noqa: E402

# (constant(s) guarded, demangled export (exact suffix after the return type), regexes over "mnemonic op_str")
CHECKS = [
    ('GetCloudStorage GameEngine+0x375EA', 'GAME::GameEngine::GetCloudStorage(void) const', [r'movzx eax, byte ptr \[rcx \+ 0x375ea\]']),
    ('kBackupFolder Player+0x4BDD', 'GAME::GameEngine::GetPlayerFolder(class GAME::Player *) const',
     [r'movzx r10d, byte ptr \[rbx \+ 0x4bdd\]', r'movzx r9d, byte ptr \[rbx \+ 0x4b48\]']),
    ('save folder: main\\ for GameInfo::GetMode 1 or the Player+0x4B48 byte, else user\\',
     'GAME::GameEngine::GetPlayerFolder(std::string const &, bool, bool) const', [r'call qword ptr \[rip \+ 0x[0-9a-f]+\]', r'cmp eax, 1']),
    ('save folder: cloud drops the base, backup picks "__"', 'GAME::GameEngine::GetUserSaveDataFolder(void) const',
     [r'cmp byte ptr \[rcx \+ 0x375ea\], 0']),
    ('Character+0x5D0 attached items', 'GAME::Character::GetAttachedItems(void) const', [r'lea rax, \[rcx \+ 0x5d0\]']),
    ('kReplica 0x538', 'GAME::Item::GetItemReplicaInfo(struct GAME::ItemReplicaInfo &) const', [r'lea rdx, \[rcx \+ 0x538\]']),
    ('relic +0x1558', 'GAME::ItemEquipment::GetRelic(void) const', [r'mov rax, qword ptr \[rcx \+ 0x1558\]']),
    ('augment +0x1560', 'GAME::ItemEquipment::GetEnchantment(void) const', [r'mov rax, qword ptr \[rcx \+ 0x1560\]']),
    ('PetPen Character+0x2050', 'GAME::Character::GetPetPen(void)', [r'lea rax, \[rcx \+ 0x2050\]']),
    ('kPetList 8, kPetNodeId 0x10', 'GAME::PetPen::GetPetOwner(unsigned int) const',
     [r'mov rcx, qword ptr \[rcx \+ 8\]', r'cmp dword ptr \[rax \+ 0x10\], edx', r'mov eax, dword ptr \[rax \+ 0x14\]']),
    ('kPetNodeSkill 0x14, kPetNodeReleased 0x1C', 'GAME::Skill_SpawnPet::KillExtraPets(class GAME::Character &)',
     [r'cmp byte ptr \[r.. \+ 0x1c\], 0', r'cmp dword ptr \[r.. \+ 0x14\], eax', r'mov edx, dword ptr \[r.. \+ 0x10\]',
      r'mov byte ptr \[r.. \+ 0x1c\], 1']),
    ('Skill+0x158 item skill modifiers', 'GAME::Skill::GetItemSkillModifiers(void) const', [r'qword ptr \[rcx \+ 0x158\]']),
    ('Skill+0x158 stride 0x30, +0x28', 'GAME::Skill::AddToItemSkillModifiers(unsigned int)',
     [r'lea rcx, \[rsi \+ 0x158\]']),
    ('kSkillModifiers 0x140', 'GAME::Skill::GetModifiers(void) const', [r'lea rax, \[rcx \+ 0x140\]']),
    ('EquipManagerContainer stride 12', 'GAME::EquipManager::GetItemId(enum GAME::EquipLocations) const',
     [r'movabs rbx, 0x2aaaaaaaaaaaaaab', r'sar rdx, 1']),
    ('SkillManager+0x258 item skills', 'GAME::SkillManager::GetItemSkillList(void) const', [r'lea rax, \[rcx \+ 0x258\]']),
    ('char acc values @0', 'GAME::CharAttributeAccumulator::AddValue(enum GAME::CharAttributeType, float)',
     [r'addss xmm2, dword ptr \[rcx \+ rax\*4\]$']),
    ('char acc modifiers @0xE8', 'GAME::CharAttributeAccumulator::GetModifier(enum GAME::CharAttributeType) const',
     [r'dword ptr \[rcx \+ rax\*4 \+ 0xe8\]']),
    ('skill acc value @0x08', 'GAME::SkillAttributeAccumulator::AddValue(enum GAME::SkillAttributeType, float)',
     [r'dword ptr \[rcx \+ rax\*4 \+ 8\]']),
    ('skill acc chance value @0x1C', 'GAME::SkillAttributeAccumulator::AddChanceValue(enum GAME::SkillAttributeType, float)',
     [r'dword ptr \[rcx \+ rax\*4 \+ 0x1c\]']),
    ('skill acc modifier @0x30', 'GAME::SkillAttributeAccumulator::AddModifier(enum GAME::SkillAttributeType, float)',
     [r'dword ptr \[rcx \+ rax\*4 \+ 0x30\]']),
    ('skill acc chance modifier @0x44', 'GAME::SkillAttributeAccumulator::AddChanceModifier(enum GAME::SkillAttributeType, float)',
     [r'dword ptr \[rcx \+ rax\*4 \+ 0x44\]']),
    ('combat acc vectors @0x20/0x38/0x50', 'GAME::CombatAttributeAccumulator::~CombatAttributeAccumulator(void)',
     [r'qword ptr \[rbx \+ 0x20\]', r'qword ptr \[rbx \+ 0x38\]', r'qword ptr \[rbx \+ 0x50\]']),
    ('CombatAttribute vt+0x10 GetType', 'GAME::CombatAttributeAccumulator::GetTotalDefenseType(enum GAME::CombatAttributeType)',
     [r'call qword ptr \[rax \+ 0x10\]']),
    ('display acc +0x84/+0x85/+0x88, size', 'GAME::CombatDisplayAccumulator::CombatDisplayAccumulator(bool)',
     [r'mov byte ptr \[rcx \+ 0x84\], dl', r'mov byte ptr \[rcx \+ 0x85\], r8b', r'mov dword ptr \[rcx \+ 0x88\], r8d']),
    ('kGlobalChance 0x88 read', 'GAME::CombatDisplayAccumulator::GetChanceMultiplier(float)',
     [r'dword ptr \[rcx \+ 0x88\]', r'byte ptr \[rcx \+ 0x85\]', r'byte ptr \[rcx \+ 0x84\]']),
    ('kGlobalChance 0x88 write', 'GAME::CombatDisplayAccumulator::SetGlobalChance(float)',
     [r'movss dword ptr \[rcx \+ 0x88\], xmm1']),
    ('BasePhysical GetValues +0x20/24/28/48/2C', 'GAME::CombatAttributeDamage_BasePhysical::GetValues(float &, float &, float &, float &, float &)',
     [r'\[rcx \+ 0x20\]', r'\[rcx \+ 0x24\]', r'\[rcx \+ 0x28\]', r'\[rcx \+ 0x48\]', r'\[rcx \+ 0x2c\]']),
    ('kAbsorptionProtection 38', 'GAME::CombatAttributeDefense_AbsorptionProtection::GetType(void) const',
     [r'mov eax, 0x26']),
    ('requirement slots 0x530/0x538/0x540, reductions', 'GAME::ItemEquipment::AreRequirementsMet(class GAME::Character const *) const',
     [r'call qword ptr \[rax \+ 0x530\]', r'call qword ptr \[rax \+ 0x538\]', r'call qword ptr \[rax \+ 0x540\]'] +
     [rf'mov edx, dword ptr \[rbx \+ {o:#x}\]' for o in (0xDF8, 0xDEC, 0xE04, 0xDFC, 0xDF0, 0xE08, 0xE00, 0xDF4, 0xE0C)]),
    ('kSlotReqLevel 0x528', 'GAME::Item::AreRequirementsMet(class GAME::Character const *) const',
     [r'call qword ptr \[rax \+ 0x528\]']),
    ('kPthTable: CombatManager at Character+0x530', 'GAME::Character::GetCombatManager(void)', [r'lea rax, \[rcx \+ 0x530\]']),
    ('kPthTable: +0x278 pthMinimum .. +0x28C', 'GAME::CombatManager::LoadRecord(std::string const &)',
     [r'movss dword ptr \[r.. \+ 0x278\], xmm0', r'movss dword ptr \[r.. \+ 0x280\], xmm0',
      r'movss dword ptr \[r.. \+ 0x284\], xmm0', r'movss dword ptr \[r.. \+ 0x28c\], xmm0']),
    ('PthTable +0x27C = 100', 'GAME::CombatManager::CombatManager(void)', [r'qword ptr \[rbx \+ 0x27c\], 0x42c80000']),
    ('kSlotHitCallbackPoint 0x6E0', 'GAME::Player::CalculateSkillDps(unsigned int, std::string &, struct GAME::SkillAttackInfo &, float &, class GAME::CombatAttributeAccumulator &, class GAME::CombatAttributeAccumulator &, ',
     [r'call qword ptr \[rax \+ 0x6e0\]']),
    ('GameTextLine 0x40: +0 class, +8 u16string, +0x28/+0x30/+0x38',
     'GAME::GameTextLine::GameTextLine(enum GAME::GameTextClass, std::u16string const &, bool, class GAME::GraphicsTexture const *, float)',
     [r'mov dword ptr \[rcx\], edx', r'add rcx, 8', r'mov byte ptr \[rdi \+ 0x28\], bl', r'mov qword ptr \[rdi \+ 0x30\], rax',
      r'movss dword ptr \[rdi \+ 0x38\], xmm0']),
    ('ItemReplicaInfo string offsets', 'GAME::ItemReplicaInfo::StreamProperties(class GAME::IOStream *, unsigned int)',
     [rf'lea r8, \[rdi \+ {o:#x}\]' for o in (0x28, 0x48, 0x68, 0x70, 0x90, 0xB0, 0xD0, 0x160)]),
]

# (constant, vftable export, slot offset, demangled export expected there (substring))
VTABLES = [
    ('vt+0x20 GetDamageType (DoT)', "const GAME::CombatAttributeDurDamage::`vftable'", 0x20, 'CombatAttributeDurDamage::GetDamageType'),
    ('vt+0x78 GetMinMaxValues', "const GAME::CombatAttributeAbsDamage::`vftable'", 0x78, 'CombatAttributeAbsDamage::GetMinMaxValues'),
    ('vt+0x78 GetMinMaxValues (DoT)', "const GAME::CombatAttributeDurDamage::`vftable'", 0x78, 'CombatAttributeDurDamage::GetMinMaxValues'),
    ('vt+0xF0 GetDuration', "const GAME::CombatAttributeDurDamage::`vftable'", 0xF0, 'CombatAttributeDurDamage::GetDuration'),
    ('vt+0x28 AbsDefense::Process', "const GAME::CombatAttributeAbsDefense::`vftable'", 0x28, 'CombatAttributeAbsDefense::Process'),
    ('kSlotGetValues 0x110', "const GAME::CombatAttributeDamage_BasePhysical::`vftable'", 0x110, 'CombatAttributeDamage_BasePhysical::GetValues'),
    ('kSlotChanceMultiplier 0x18', "const GAME::CombatDisplayAccumulator::`vftable'", 0x18, 'CombatDisplayAccumulator::GetChanceMultiplier'),
    ('SetGlobalChance vt+0x28', "const GAME::CombatDisplayAccumulator::`vftable'", 0x28, 'CombatDisplayAccumulator::SetGlobalChance'),
]

# (constant, function, offset from its start, regex the instruction there must match)
SITES = [
    ('kLinePushBackSite 0x264', 'GAME::ItemEquipment::GetUIDisplayText(class GAME::Character const *, class mem::vector<struct GAME::GameTextLine> &, bool) const',
     0x264, r'lea rdx, \[rbp - 0x59\]'),
    ('kSearchCall 0xB8', 'GAME::Item::GenerateSearchText(void)', 0xB8, r'call qword ptr \[rax \+ 0x458\]'),
]


def main(argv):
    if len(argv) != 3:
        print(__doc__ or 'usage: verify_build.py <Game.dll> <Game.exports.tsv>', file=sys.stderr)
        return 2
    pe = gddis.PE(argv[1])
    base = struct.unpack_from('<Q', pe.data, struct.unpack_from('<I', pe.data, 0x3C)[0] + 48)[0]
    by_name = {}
    starts = set()
    for line in open(argv[2], encoding='utf-8'):
        rva, _, dem = line.rstrip('\n').split('\t')
        by_name.setdefault(dem, int(rva, 16))
        starts.add(int(rva, 16))
    starts = sorted(starts)
    names = gddis.load_exports(argv[2])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    def find(sym):
        hits = [r for d, r in by_name.items() if d == sym or d.endswith(' ' + sym) or (sym.endswith(', ') and sym in d)]
        return hits[0] if len(set(hits)) == 1 else None

    def body(rva, limit=0x3000):
        nxt = next((s for s in starts if s > rva), rva + limit)
        o = pe.off(rva)
        return [f'{i.mnemonic} {i.op_str}'.strip() for i in md.disasm(pe.data[o:o + min(nxt - rva, limit)], base + rva)]

    fails = 0

    def report(ok, what, where, why=''):
        nonlocal fails
        fails += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  {what:48} {where}{'  ' + why if why else ''}")

    for what, sym, pats in CHECKS:
        rva = find(sym)
        if rva is None:
            report(False, what, sym[:60], 'export not found (or ambiguous)')
            continue
        lines = body(rva)
        missing = [p for p in pats if not any(re.search(p, ln) for ln in lines)]
        report(not missing, what, f'{rva:#08x}', 'missing: ' + ' | '.join(missing) if missing else '')
    for what, vt, off, want in VTABLES:
        rva = find(vt)
        if rva is None:
            report(False, what, vt, 'vftable not exported')
            continue
        slot = struct.unpack_from('<Q', pe.data, pe.off(rva + off))[0] - base
        got = names.get(slot, f'{slot:#x}')
        report(want in got, what, f'{rva:#08x}+{off:#x}', '' if want in got else f'holds {got}')
    for what, sym, off, pat in SITES:
        rva = find(sym)
        lines = body(rva + off, 0x10) if rva is not None else []
        ok = bool(lines) and re.search(pat, lines[0]) is not None
        report(ok, what, f'{(rva or 0) + off:#08x}', '' if ok else f'found {lines[:1]}')
    print(f'{len(CHECKS) + len(VTABLES) + len(SITES)} checks, {fails} failed')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
