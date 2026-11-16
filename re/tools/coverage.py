# Maps every stat field set on an item, component, augment, affix or set record to its engine attribute and to
# what the score does with it.
# usage: python3 -I re/tools/coverage.py <game dir> > docs/COVERAGE.md
# Field -> attribute: re/CharAttributeType.tsv, re/CombatAttributeType.tsv (Load* tags), re/DefenseAttributeFields.tsv.
# Attribute -> score: the HOW tables below, which mirror core/src/score.cpp, utility.cpp, effects.cpp,
# stat_model.cpp, requirements.cpp, sets.cpp (keep them in step when those change).
import collections, csv, os, re, sys
here = os.path.dirname(__file__)
sys.path.insert(0, here)
import arz

gd = sys.argv[1].rstrip('/') + '/'
dbs = [arz.load(gd + f) for f in ('database/database.arz', 'gdx1/database/GDX1.arz', 'gdx2/database/GDX2.arz', 'gdx3/database/GDX3.arz')]
index = {}
for i, db in enumerate(dbs):
    for name in db[2]: index[name] = i

def tsv(name):
    return list(csv.reader(open(os.path.join(here, '..', name)), delimiter='\t'))[1:]

field = {}  # db field -> (kind, type, class, role)
for value, cls, f40, _ in tsv('CharAttributeType.tsv'):
    if f40: field[f40] = ('char', int(value), cls, 'modifier' if 'Mod_' in cls else 'value')
for row in tsv('CombatAttributeType.tsv'):
    if not row[0].isdigit(): continue
    value, cls, tags = row[0], row[1], row[2] if len(row) > 2 else ''
    for tag in tags.split():
        k, _, v = tag.partition('=')
        if k.startswith('Load') and v and v != 'Not':
            kind = 'retaliation' if cls.startswith('Retaliation') else 'damage'
            field[v] = (kind, int(value), cls, k[4:])
types = {r[1]: int(r[0]) for r in tsv('CombatAttributeType.tsv') if r[0].isdigit()}
for cls, chance, value in tsv('DefenseAttributeFields.tsv'):
    for f, role in ((chance, 'Chance'), (value, 'Value')):
        if f: field[f] = ('defense', types.get(cls, -1), cls, role)

BASE = {2, 4, 5, 6, 7, 8, 9, 10, 11, 15}
def how(f):
    """(status, text). status: scored | default | zero | no."""
    if re.match(r'augment(Skill|Mastery)(Name|Level)\d|augmentAllLevel', f):
        return 'scored', '+skills: the skill records at the old / new level (stat_model.cpp); 0-point skills score 0 by design (GetCurrentLevel stays 0), listed grey'
    if re.match(r'conversion(InType|OutType|Percentage)2?$', f):
        return 'scored', 'Damage: conversion of the main skills\' damage (snapshot conversions; score.cpp convert)'
    if re.match(r'itemSkill(Name|LevelEq|AutoController)$', f):
        return 'scored', 'granted skill: procs through gdc::effects (damage, RR, crowd control, self buffs, absorb / heal at their uptime); ungated passives through the record; the rest "+? unscored" with the reason'
    if re.match(r'(modifierSkillName|modifiedSkillName)\d+$', f):
        return 'scored', 'item skill modifier: its record added to the modified skill (Damage, sheet stats)'
    if f in ('petBonusName',):
        return 'scored', 'Damage: Class Pet damage (% damage, attack / cast speed, OA of the pet bonus record; experimental); the rest Other `pet_bonus`, only with pets'
    if f in ('itemSetName', 'setMembers'):
        return 'scored', 'set bonus at the old / new piece count (sets.cpp)'
    if re.match(r'character\w*ReqReduction$', f) or f in ('characterLevelReqReduction', 'characterGlobalReqReduction'):
        return 'scored', 'requirements: lowers the requirement the engine checks (requirements.cpp); items it leaves unequippable are scored as removed'
    if f in ('defensiveBlock', 'defensiveBlockChance', 'blockAbsorption', 'blockRecoveryTime'):
        return 'scored', 'Survivability: shield block stage'
    if re.match(r'defensive\w+MaxResist$', f) or f == 'defensiveAllMaxResist':
        return 'default', 'max resist: the cap in the sheet follows it only through engine totals; a change on swap is a model warning'
    if f in SLOTS:
        return 'scored', 'component / augment slot fit ("your <component> moved" / "does not fit")'
    if f == 'defensiveProtection':
        return 'scored', 'Survivability: armor per hit region (the item\'s engine armor entry, type 38)'
    if f == 'defensiveBonusProtection':
        return 'scored', 'Survivability: armor; DefenseAttribute_BaseProtectionAbsorption::AddToAccumulator 0x204BF0 adds it to defensiveProtection in the one type-38 entry'
    if f == 'defensiveProtectionChance':
        return 'no', 'NOT VERIFIED: where the engine reads it is not traced (AddToAccumulator 0x204BF0 builds the armor entry from protection, bonus protection and the absorption; 1 record)'
    if f in ('offensivePhysicalMin', 'offensivePhysicalMax'):
        return 'scored', 'Damage: flat physical damage of weapon attacks; skills\' and procs\' own flat damage'
    if f == 'offensiveGlobalChance':
        return 'scored', 'chance / grouping of the stats it belongs to (engine totals are expected values)'
    if f in ('skillCooldownReduction', 'skillCooldownReductionChance'):
        return 'scored', 'Other `cooldown_reduction` (items only)'
    if f == 'skillManaCostReduction':
        return 'scored', 'Other `skill_cost_reduction` (items only)'
    if f in ('skillProjectileSpeedModifier', 'skillComboChargeSpendReduction'):
        return 'default', 'Other `default` weight per 100, named in Shift (skill attribute)'
    if f == 'offensivePierceRatioMin':
        return 'default', 'Other `default` weight per 100, named in Shift (armor piercing, item offense type 3; weapons are not compared)'
    if f == 'bonusTableName':
        return 'scored', 'completion bonus: moves with the component; its stats are in the item\'s engine totals'
    if re.match(r'offensiveSlow\w+DurationModifier$', f):
        return 'scored', 'Damage: +% DoT duration of that final type, from the equipped items\' records (shortcut: record values, skills\' part not seen)'
    if f in ('racialBonusPercentDamage', 'racialBonusPercentDefense', 'racialBonusRace'):
        return 'scored', 'Other `racial_damage` / `racial_defense`: % x 10% of enemies per race listed (design share, no race simulation; record values)'
    if f == 'racialBonusAbsoluteDamage':
        return 'zero', '0 by design: flat damage vs a race (2 records; no race simulation)'
    if f not in field:
        return 'no', 'NOT READ: no engine attribute class found for this field in the TSVs'
    kind, t, cls, role = field[f]
    if role in ('Chance', 'Global', 'Xor'):
        return 'scored', 'chance / grouping of the stat it belongs to (engine totals are expected values)'
    if kind == 'char':
        table = {
            1: 'Survivability (Life 2.5 / point, DA 0.5 / point, life regen); requirements',
            2: 'Damage (OA, physical / pierce / bleeding factor); Life 1 / point; requirements',
            3: 'Damage (magical factor); Life 1.5 / point, Energy; requirements',
            4: 'Survivability: Life', 5: 'Other `energy_regen` only through regen; Energy itself 0 by design (no energy model)',
            6: 'Other `life_regen`', 7: 'Other `energy_regen`',
            13: 'Damage: total speed added to attack / cast speed (unverified)', 14: 'Other `run_speed`',
            15: 'Damage: attack speed of attack-class skills (Player::CalculateSkillDps order) without a cooldown; their cooldown cap',
            16: 'Damage: cast speed of cast-class skills (CalculateSkillDps order) without a cooldown; their cooldown cap',
            17: 'Damage: attack speed', 21: 'Damage: OA -> hit / crit chance', 22: 'Damage: OA %',
            23: 'Survivability: DA -> enemy hit / crit chance', 24: 'Survivability: DA %',
            25: 'Survivability: block recovery', 27: 'Survivability: dodge', 28: 'Survivability: deflect',
            55: 'Other `experience` (0 at max level)',
        }
        if t == 8: return 'zero', '0 by design: reserved energy (more is worse, no energy model)'
        if t in table: return 'scored', table[t]
        return 'default', 'Other `default` weight per 100, named in Shift (%s)' % cls
    if kind == 'defense':
        if t in BASE: return 'scored', 'Survivability: resist (capped, under `resist_reduction`), threshold `resist`' + ('; physical: before armor' if t == 2 else '')
        table = {38: 'Survivability: armor per hit region', 39: 'Survivability: armor absorption', 40: 'Survivability: armor %',
                 42: 'Other `stun_resist`, threshold', 43: 'Other `sleep_resist`', 44: 'Other `trap_resist`',
                 45: 'Other `freeze_resist`, threshold', 46: 'Other `petrify_resist`', 48: 'Other `knockdown_resist`',
                 13: 'Other `disruption_resist`', 16: 'Other `slow_resist`, threshold', 55: 'Survivability: block chance',
                 56: 'Survivability: block amount', 57: 'threshold `reflect_reduction` only (else 0 by design: no reflect model)',
                 58: 'Survivability: elemental resists (spread by the engine)', 60: 'Survivability: all resists (spread by the engine)'}
        if t in table: return 'scored', table[t]
        return 'default', 'Other `default` weight per 100, named in Shift (%s)' % cls
    if kind == 'retaliation':
        return 'scored', 'Other `retaliation` / `retaliation_modifier` (weight 0 in plague-garden by design: guide §1)'
    if role in ('DurationMin', 'DurationMax'): return 'scored', 'Damage: DoT duration of the flat damage it belongs to'
    if t in BASE or t in (1, 58, 60):
        if role == 'Modifier': return 'scored', 'Damage: % of that final type after conversions (direct and duration separately: engine)'
        return 'scored', 'Damage: flat damage of weapon attacks; skills\' and procs\' own flat damage'
    if t == 59: return 'scored', 'Damage: crit damage, added to every crit tier'
    if t == 20: return 'scored', 'Other `life_leech`'
    if t == 28: return 'scored', 'Survivability: enemy damage reduction x `damage_reduction_uptime` (strongest only)'
    if t in (32, 33, 34, 35, 36, 37): return 'scored', 'Damage: resistance reduction through gdc::effects (uptime, highest-only)'
    return 'default', 'Other `default` weight per 100, named in Shift (%s)' % cls

SLOTS = {'amulet', 'ring', 'medal', 'head', 'chest', 'shoulders', 'hands', 'legs', 'feet', 'waist', 'axe', 'axe2h', 'dagger',
         'mace', 'mace2h', 'offhand', 'ranged1h', 'ranged2h', 'scepter', 'shield', 'spear2h', 'sword', 'sword2h'}
GROUPS = [
    ('Items', lambda c, n: c.startswith(('Armor', 'Weapon'))),
    ('Components (ItemRelic)', lambda c, n: c == 'ItemRelic'),
    ('Augments (ItemEnchantment)', lambda c, n: c == 'ItemEnchantment'),
    ('Relics (ItemArtifact)', lambda c, n: c == 'ItemArtifact'),
    ('Affixes (LootRandomizer), incl. ascended / awakened', lambda c, n: c == 'LootRandomizer'),
    ('Sets (ItemSet)', lambda c, n: c == 'ItemSet'),
]
SKIP = re.compile(r'^(templateName|Class|FileDescription|itemLevel|levelRequirement|itemCost|itemCostScalePercent|'
                  r'attributeScalePercent|lootRandomizerCost|lootRandomizerJitter|itemClassification|bitmap|mesh|baseTexture|'
                  r'bumpTexture|glowTexture|shader|itemNameTag|itemQualityTag|itemStyleTag|description|itemText|'
                  r'(strength|dexterity|intelligence)Requirement|hidePrefixName|hideSuffixName|numRelicSlots|'
                  r'lootRandomizerName|quest|cannotPickUp|soulbound|untradeable|dropSound.*|itemCostName|'
                  r'characterBaseAttackSpeedTag|characterBaseAttackSpeed|castsShadows|scale|maxTransparency|'
                  r'physics.*|actor.*|outline.*|shadow.*|.*Sound.*|.*Fx.*|.*Anim.*|.*Bitmap.*|.*Tag|.*Texture.*|.*256|.*Mesh|'
                  r'DragUVs|FadeAlpha|MSFadeTime|TrailShrinkSpeed|.*Classification|attachPointName|attackEffect|'
                  r'.*ProjectileName|projectileName|completedRelicLevel|craftingMaterial|dlcRequirement|factionSource|'
                  r'hide\w+|lootRandomizerScale|marketAdjustmentPercent|medalVisible|onPickUp|preventMaleTransmute|'
                  r'useWeaponTint|weapon(Effect|Tint\w+|Trail))$', re.I)

use = collections.defaultdict(lambda: collections.Counter())
for name in index:
    cls = dbs[index[name]][2][name][0]
    for gi, (_, pred) in enumerate(GROUPS):
        if pred(cls, name):
            for k, v in arz.fields(dbs[index[name]], name)[1].items():
                if SKIP.match(k): continue
                vals = v if isinstance(v, list) else [v]
                if any(x not in (0, 0.0, '', '0', '0.0') for x in vals): use[k][gi] += 1
            break

rows = sorted(((f,) + how(f) for f in use), key=lambda r: ({'no': 0, 'default': 1, 'zero': 2, 'scored': 3}[r[1]], r[0]))
count = collections.Counter(r[1] for r in rows)
print('# Coverage audit: item stat fields -> score\n')
print('Generated by `re/tools/coverage.py` from the installed database (1.3.1.1 + GDX1..3). Every field that is set on at')
print('least one item, component, augment, relic, affix or set record. Records: I items, C components, A augments,')
print('R relics, X affixes (incl. ascended / awakened), S sets.\n')
print('%d fields: %d scored, %d at the `default` weight (named in Shift), %d zero by design, **%d not read**.\n' %
      (len(rows), count['scored'], count['default'], count['zero'], count['no']))
print('| Field | Records | Scored how |\n|---|---|---|')
for f, status, text in rows:
    recs = ' '.join('%s%d' % ('ICARXS'[g], n) for g, n in sorted(use[f].items()))
    print('| `%s` | %s | %s |' % (f, recs, ('**%s**' % text) if status == 'no' else text))
