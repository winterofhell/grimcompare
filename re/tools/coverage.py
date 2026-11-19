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
# Retaliation the Damage term reads under [damage] retaliation (gdc::fields::retaliation()).
RETALIATION_DAMAGE = re.compile(r'retaliation(Physical|Pierce|Cold|Fire|Poison|Lightning|Life|Chaos|Aether|Elemental)(Min|Max|Chance|Modifier)$|retaliationTotalDamageModifier$')
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
    if f == 'skillComboChargeSpendReduction':
        return 'scored', 'Damage: chance to keep combo charges for cooldown main skills (Character::SpendComboCharges 0x603F0); not in Other'
    if f == 'skillProjectileSpeedModifier':
        return 'default', 'Other `default` weight per 100, named in Shift (skill attribute)'
    if f == 'offensivePierceRatioMin':
        return 'default', 'Other `default` weight per 100, named in Shift (armor piercing, item offense type 3; weapons are not compared)'
    if f == 'bonusTableName':
        return 'scored', 'completion bonus: moves with the component; its stats are in the item\'s engine totals'
    if re.match(r'offensiveSlow\w+DurationModifier$', f):
        return 'scored', 'Damage: +% DoT duration of that final type, from the records of the equipped items, the worn set tiers and the learned passives, toggles and devotions with their modifiers (record values; timed buffs not counted)'
    if f in ('racialBonusPercentDamage', 'racialBonusPercentDefense', 'racialBonusRace'):
        return 'scored', 'Other `racial_damage` / `racial_defense`: % x share of monsters with any listed race (re/EnemyRaces.tsv; record values)'
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
        if RETALIATION_DAMAGE.match(f):
            return 'scored', 'Other `retaliation` / `retaliation_modifier` (weight 0 in plague-garden by design: guide §1); Damage instead under `[damage] retaliation = true` (preset `retaliation`)'
        return 'scored', 'Other `retaliation` / `retaliation_modifier` (weight 0 in plague-garden by design: guide §1); under `[damage] retaliation = true` not counted (shortcut: retaliation DoT and crowd control)'
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

# Item skill modifiers: the records items and sets name in modifierSkillName<N>. Field lists come from the core
# tables they mirror (db_fields.cpp, score_internal.h, effects.cpp); the rules mirror score_skills.cpp add_record,
# stat_model_effects.cpp modifier_effect, effects.cpp / effects_pets.cpp.
src = os.path.join(here, '..', '..', 'core', 'src')
def cpp_names(path, array):
    text = open(os.path.join(src, path)).read()
    body = text[text.index(array + '[] = {'):]
    return set(re.findall(r'"(\w+)"', body[:body.index('};')]))
SHEET = cpp_names('db_fields.cpp', 'kTable')
PCT = cpp_names('db_fields.cpp', 'kDamageMods')
FLAT = cpp_names('db_fields.cpp', 'kFlat')
DOT_DURATION = cpp_names('score_internal.h', 'kDotDuration')
RR = cpp_names('effects.cpp', 'kOffensiveFields')
DEBUFF = cpp_names('effects.cpp', 'kDebuffFields')
VISUAL = re.compile(r'^(templateName|Class|skillMaxLevel|characterBaseAttackSpeedTag|charFxPakOtherNames|fxChanges|'
                    r'\w*(FX|Fx|FxPak|Effect\d?)Override|lightningOverride|particleEffect\d+Override|shapeshiftMesh\w+|'
                    r'warmUpEffectName|skillSpecialAnimationName|skillTier|skillUltimateLevel)$')
WEAPON_FLAGS = {'Axe', 'Axe2h', 'Dagger', 'Mace', 'Mace2h', 'Offhand', 'Ranged1h', 'Ranged2h', 'Scepter', 'Shield',
                'Spear', 'Spear2h', 'Staff', 'Sword', 'Sword2h', 'dualRangedOrAllRangedOnly'}
MAIN = 'Damage, main skills only: '
def mod_how(f):
    if f in FLAT and f.endswith('Chance'):
        return 'scored', MAIN + 'chance of the flat damage it belongs to (expected value)'
    if f in FLAT and f.endswith('DurationMin'):
        return 'scored', MAIN + 'DoT duration of the flat damage it belongs to (damage per second x seconds)'
    if f in FLAT:
        return 'scored', MAIN + 'flat damage in the skill\'s own pool, converted with it, skill % and sheet % added (Skill::CollectCombatParameters 0x499570 -> AddModifierOffensiveDamageAttributes 0x493480)'
    if f in PCT:
        return 'scored', MAIN + 'skill-local % of that type, added to the sheet % (AddModifierOffensiveModifierAttributes 0x4935B0)'
    if f in DOT_DURATION:
        return 'scored', MAIN + '+% DoT duration of that final type, added to the gear\'s'
    if re.match(r'conversion(InType|OutType|Percentage)2?$', f):
        return 'scored', MAIN + 'skill conversion, before the global one'
    if f in ('weaponDamagePct', 'offensiveCritDamageModifier', 'skillCooldownTime'):
        return 'scored', MAIN + {'weaponDamagePct': '% weapon damage', 'offensiveCritDamageModifier': 'crit damage',
                                 'skillCooldownTime': 'cooldown (uses per second); procs\' recharge too'}[f]
    if f == 'offensiveLifeLeechMin':
        return 'scored', 'Other `life_leech`, main skills only'
    if f.startswith('offensiveSlowOffensiveAbility'):
        return 'scored', 'Survivability: lowers the enemy\'s OA (its hit chance) at the modified skill\'s uptime (gdc::effects)'
    if f.startswith('offensiveSlowDefensiveAbility'):
        return 'scored', 'Damage: lowers the enemy\'s DA (hit and crit chance) at the modified skill\'s uptime (gdc::effects)'
    if f.startswith('offensiveTotalDamageReductionPercent'):
        return 'scored', 'Survivability: enemy damage reduction at the modified skill\'s uptime, strongest only (gdc::effects)'
    if f in RR:
        return 'scored', 'Damage: resistance reduction of the modified skill through gdc::effects (uptime, highest-only)'
    if f == 'skillChanceWeight':
        return 'scored', 'Damage: weapon pool chance of the modified Skill_WPAttack_* (weights add, Skill_WPAttack::GetChanceWeight 0x5160E0; past 100 in all, each w / sum, SkillManager::SelectWeaponPoolSkill 0x5364C0)'
    if f == 'damageAbsorptionPercent':
        return 'scored', 'Survivability: % absorption of the modified buff while it is up'
    if f in DEBUFF:
        return 'scored', 'Damage: resistance reduction when the modified skill is a debuff (negative values); else sheet stat while the skill is active'
    if f in SHEET:
        return 'scored', 'sheet stat while the modified skill is active (passive, toggle, running buff); stat model'
    if f in ('petLimit', 'spawnObjectsTimeToLive'):
        return 'scored', 'Damage: pets alive (GetSpawnObjectTimeToLive 0x493660)'
    if re.match(r'modSpawnObjects\d*$|modSpawnObjectsTimeToLive$', f):
        return 'scored', 'Damage: the summoned pet and its time to live (Skill_SpawnPetTransmuter; AddModifierPetChanges 0x494CE0)'
    if f == 'petSkillName':
        return 'scored', 'Damage: pet skill added to the pet (SkillSecondary_PetModifier)'
    if f in WEAPON_FLAGS:
        return 'zero', '0 by design: weapon requirement of the modified skill (the character already uses it)'
    if f == 'skillActiveDuration':
        return 'no', 'not scored: buff / debuff duration of a modifier is not read (uptimes use the skill record)'
    if re.match(r'(refreshCooldown|refreshDuration)\w+$', f):
        return 'no', 'not scored: triggered cooldown / duration refresh is not modeled'
    if f == 'skillComboChargeLevel':
        return 'scored', 'Damage: combo charges the modified skill builds, for cooldown main skills (SkillManager::IncrementComboCharge 0x53F990, Skill::CollectComboMultiplier 0x4993D0)'
    if f == 'skillComboChargeDuration':
        return 'no', 'not scored: combo charge expiry is not modeled'
    if re.match(r'(projectile\w+|skillTarget\w+|beam\w+|wave\w+|explosionRadius|spark\w+|targetingMode|'
                r'overwriteBaseSkill|petBurstSpawn|petChanges)$', f):
        return 'no', 'not scored: area, targets, projectiles or behaviour; the model is one target, one use'
    if f == 'offensiveDamageMultModifier':
        return 'scored', MAIN + '"Total Damage Modified by": multiplies the skill\'s damage after its additive %; several compound (ModifyDamage 0x119280, CombatAttribute_DamageMultiplier::Execute 0x117E50); on a timed buff, every hit of the player\'s while it is up'
    if f in ('skillLifePercent', 'skillLifeBonusBuffDuration', 'skillLifePercentBuffDuration'):
        return 'no', 'not scored: heals are read from the skill record only'
    if f in ('skillCooldownReduction', 'skillCooldownReductionChance', 'skillManaCostReduction') or f.startswith('racialBonus'):
        return 'no', 'not scored: read from items only'
    if f in ('cooldownCharges', 'cooldownTime'):
        return 'no', 'not scored: skill charges are not modeled'
    if f.startswith(('defensive', 'character')):
        return 'no', 'not scored: no stat-model target (a swap that changes a defensive / character one warns "not modeled")'
    if f == 'retaliationDamagePct' or RETALIATION_DAMAGE.match(f):
        return 'scored', 'Damage, only under `[damage] retaliation = true`: the modified skill\'s % Retaliation Added to Attack, or its retaliation while the skill is up'
    if f.startswith('retaliation'):
        return 'no', 'not scored: retaliation from a skill modifier is not read'
    if re.match(r'offensive\w+', f):
        return 'no', 'not scored: crowd control / debuff of the skill without a damage or RR model'
    return 'no', 'not scored: not read for skill modifiers'

owners = collections.Counter()
for name in index:
    if not name.startswith(('records/items', 'records/sets')): continue
    for k, v in arz.fields(dbs[index[name]], name)[1].items():
        if k.startswith('modifierSkillName'):
            for x in (v if isinstance(v, list) else [v]):
                if isinstance(x, str) and x: owners[x.lower()] += 1
mod_use = collections.Counter()
classes = collections.Counter()
for m in owners:
    if m not in index: continue
    r = arz.fields(dbs[index[m]], m)
    classes[r[0]] += 1
    for k, v in r[1].items():
        vals = v if isinstance(v, list) else [v]
        if any(x not in (0, 0.0, '', '0', '0.0') for x in vals) and not VISUAL.match(k): mod_use[k] += 1
mrows = sorted(((f,) + mod_how(f) for f in mod_use), key=lambda r: ({'no': 0, 'zero': 1, 'scored': 2}[r[1]], r[0]))
mcount = collections.Counter(r[1] for r in mrows)
print('\n## Item skill modifier fields\n')
print('Every field set on at least one record that an item or set names in `modifierSkillName<N>` (%s). Visual and' %
      ', '.join('%d %s' % (n, c) for c, n in sorted(classes.items())))
print('identity fields are left out. "Main skills only": a learned skill that is not a main skill adds no damage of its')
print('own, so its modifier\'s damage is not scored. The Shift view lists each changed modifier per skill and marks')
print('parts not scored.\n')
print('%d fields: %d scored, %d zero by design, %d not scored.\n' % (len(mrows), mcount['scored'], mcount['zero'], mcount['no']))
print('| Field | Records | Scored how |\n|---|---|---|')
for f, status, text in mrows:
    print('| `%s` | %d | %s |' % (f, mod_use[f], text))
