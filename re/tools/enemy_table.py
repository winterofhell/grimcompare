# Enemy model per character level and difficulty from the monster records (database.arz + GDX1..3, layered
# like gdc::arz).
# usage: python3 -I re/tools/enemy_table.py <game dir> > re/EnemyByLevel.tsv
#        python3 -I re/tools/enemy_table.py <game dir> --resists > re/EnemyResistByDifficulty.tsv
#        python3 -I re/tools/enemy_table.py <game dir> --penalty > re/PlayerResistByDifficulty.tsv
# For every Monster record under records/creatures/enemies/ (Common, Champion, Hero) at charLevel = L:
#   level  = the record's charLevel equation
#   OA, DA = combatformulas.dbr offensive/defensiveAbilityEquation on the bio record's
#            (characterAttributeEquations) characterOffensiveAbility / DefensiveAbility / Dexterity / Strength
#   hit    = the largest flat direct damage (sum of offensive<Type>Min/Max averages, all types) of one of its
#            skills (skillName<n> at skillLevel<n>; the record or its buffSkillName record), and the physical
#            part of that hit, each x the combatformulas.dbr attribute factor on the bio's attributes
#            (physical / pierce x (dexterity/245 + 1), the rest x (intelligence/215 + 1)).
#            Weapon damage (weaponDamagePct x enemy gear) and the monster's own % damage bonuses are NOT included.
#   rr     = what its debuffs take off a player's resist: -defensive<Type> (and -defensiveElementalResistance
#            for Cold / Fire / Lightning) of the same skill records, summed per resist. No monster skill in
#            this set carries offensive*ResistanceReduction* fields.
# Difficulty (Monster::ContributeGameBalance*Attributes, Game.dll 0x37B710..0x37BE10): every monster gets
# gameengine.dbr monsterAttributePak at the game balance level and, while the difficulty ramp is above 0,
# GameEngine::GetChallengeAdjustment (0x2F1C20) on top: gameengine.dbr challengeAdjustment on Normal and Elite
# (Veteran), gameascendant.dbr ultimateChallangeAdjustment on Ultimate (Ascendant). The pak's 12 entries are
# 3 difficulties x 4 player counts (characterLifeMultModifier 0 / 90 / 180 / 270 repeats per player); the
# single-player entry is used. Applied here: character{Dexterity,Intelligence,Strength}Modifier (%),
# character{Offensive,Defensive}Ability (flat) and ...Modifier (%), offensiveTotalDamageModifier +
# offensive<Type>Modifier (% damage, added together); crit_damage = offensiveCritDamageModifier.
# Ascendant altar modifiers depend on the altars activated in play and are not applied.
# Output: per difficulty and level the median over the monsters; rr_share = mean over the nine base resists of
# the share of monsters that lower it, rr = mean (weighted by those shares) of the median amount among them.
# boss_rr / boss_rr_share: the same over the Quest, Boss (Nemesis included) and SuperBoss records.
# --penalty: gameengine.dbr playerAttributePak defensive<Type> (single-player entry), as a positive number.
import re, statistics, sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
import arz

gd = sys.argv[1].rstrip('/') + '/'
dbs = [arz.load(gd + f) for f in ('database/database.arz', 'gdx1/database/GDX1.arz', 'gdx2/database/GDX2.arz', 'gdx3/database/GDX3.arz')]
index = {}
for i, db in enumerate(dbs):
    for name in db[2]: index[name] = i
cache = {}
def rec(name):
    if name not in cache:
        cache[name] = arz.fields(dbs[index[name]], name)[1] if name in index else None
    return cache[name]

SAFE = re.compile(r'^[0-9a-zA-Z_+\-*/^(). ]+$')
def ev(eq, **v):
    if isinstance(eq, (int, float)): return float(eq)
    if not isinstance(eq, str) or not SAFE.match(eq): return 0.0
    try: return float(eval(eq.replace('^', '**'), {'__builtins__': {}}, v))
    except Exception: return 0.0

TYPES = ['Physical', 'Pierce', 'Cold', 'Fire', 'Poison', 'Lightning', 'Life', 'Chaos', 'Aether', 'Elemental']
BASE = ['Pierce', 'Cold', 'Fire', 'Poison', 'Lightning', 'Life', 'Chaos', 'Aether', 'Bleeding']
DIFFICULTIES = ['Normal', 'NormalVeteran', 'Elite', 'EliteVeteran', 'Ultimate', 'Ascendant']  # base x 2 + ramp

engine = rec('records/game/gameengine.dbr')
pak = rec(engine['monsterAttributePak'])
ramped = [rec(engine['challengeAdjustment'])] * 2 + [rec(rec(engine['ascendantRecord'])['ultimateChallangeAdjustment'])]
def entry(p, field, base):
    v = p.get(field, 0.0)
    return float(v[base * 4]) if isinstance(v, list) else float(v)
def adj(d, field):
    return entry(pak, field, d // 2) + (float(ramped[d // 2].get(field, 0.0)) if d % 2 else 0.0)

def at(v, level):
    if isinstance(v, list): return float(v[min(level, len(v)) - 1]) if level > 0 and v else 0.0
    try: return float(v)
    except (TypeError, ValueError): return 0.0
def hit(r, level, dex, intel, d):
    total = phys = 0.0
    for t in TYPES:
        lo = at(r.get('offensive%sMin' % t, 0), level)
        if lo <= 0: continue
        hi = max(lo, at(r.get('offensive%sMax' % t, 0), level))
        v = (lo + hi) / 2 * ((dex / 245 + 1) if t in ('Physical', 'Pierce') else (intel / 215 + 1))
        v *= max(0.0, 1 + (adj(d, 'offensiveTotalDamageModifier') + adj(d, 'offensive%sModifier' % t)) / 100)
        total += v
        if t == 'Physical': phys += v
    return total, phys
def reduction(r, level, out):
    for t in BASE:
        v = min(0.0, at(r.get('defensive' + t, 0), level))
        if t in ('Cold', 'Fire', 'Lightning'): v += min(0.0, at(r.get('defensiveElementalResistance', 0), level))
        if v < 0: out[t] = out.get(t, 0.0) - v

if '--resists' in sys.argv:
    kinds = ['Physical'] + BASE
    print('difficulty\t' + '\t'.join(k.lower() for k in kinds))
    for d, name in enumerate(DIFFICULTIES):
        print(name + '\t' + '\t'.join('%.0f' % adj(d, 'defensive' + k) for k in kinds))
    sys.exit(0)
if '--penalty' in sys.argv:
    player = rec(engine['playerAttributePak'])
    print('difficulty\t' + '\t'.join(k.lower() for k in BASE))
    for base, name in enumerate(DIFFICULTIES[::2]):
        print(name + '\t' + '\t'.join('%.0f' % (0 - entry(player, 'defensive' + k, base)) for k in BASE))
    sys.exit(0)

def lowers(r, lvl, out):
    for i in range(1, 40):
        sk = rec(r.get('skillName%d' % i, ''))
        if not sk: continue
        sl = max(int(ev(r.get('skillLevel%d' % i, 1), charLevel=lvl)), 1)
        for s in (sk, rec(sk.get('buffSkillName', '')) if isinstance(sk.get('buffSkillName'), str) else None):
            if s: reduction(s, sl, out)
def rr_of(lowered, count):
    n = sum(len(v) for v in lowered.values())
    return (sum(len(v) * statistics.median(v) for v in lowered.values() if v) / n if n else 0, n / len(BASE) / count)

bosses = []
mons = []
for n in index:
    if n.startswith('records/creatures/enemies/') and dbs[index[n]][2][n][0] == 'Monster':
        r = rec(n)
        if r.get('monsterClassification') in ('Common', 'Champion', 'Hero'): mons.append(r)
        if r.get('monsterClassification') in ('Quest', 'Boss', 'SuperBoss'): bosses.append(r)

print('difficulty\tlevel\tmonsters\toa\tda\thit\thit_physical\tmonsters_with_skill_hit\trr\trr_share\tcrit_damage\tbosses\tboss_rr\tboss_rr_share')
for d, name in enumerate(DIFFICULTIES):
    for L in [1] + list(range(5, 101, 5)):
        oa, da, hits, phys, lowered = [], [], [], [], {t: [] for t in BASE}
        for r in mons:
            lvl = ev(r.get('charLevel', 'charLevel'), charLevel=L)
            bio = rec(r.get('characterAttributeEquations', '')) or {}
            b = lambda k: ev(bio.get(k, 0), charLevel=lvl)
            dex = b('characterDexterity') * (1 + adj(d, 'characterDexterityModifier') / 100)
            intel = b('characterIntelligence') * (1 + adj(d, 'characterIntelligenceModifier') / 100)
            strength = b('characterStrength') * (1 + adj(d, 'characterStrengthModifier') / 100)
            oa.append((b('characterOffensiveAbility') + adj(d, 'characterOffensiveAbility') + lvl * 12 + dex * 0.5)
                      * (1 + adj(d, 'characterOffensiveAbilityModifier') / 100) + 53)
            da.append((b('characterDefensiveAbility') + adj(d, 'characterDefensiveAbility') + lvl * 12 + strength * 0.5)
                      * (1 + adj(d, 'characterDefensiveAbilityModifier') / 100) + 53)
            best, rr = (0.0, 0.0), {}
            for i in range(1, 40):
                sk = rec(r.get('skillName%d' % i, ''))
                if not sk: continue
                sl = max(int(ev(r.get('skillLevel%d' % i, 1), charLevel=lvl)), 1)
                for s in (sk, rec(sk.get('buffSkillName', '')) if isinstance(sk.get('buffSkillName'), str) else None):
                    if s:
                        best = max(best, hit(s, sl, dex, intel, d))
                        reduction(s, sl, rr)
            if best[0] > 0: hits.append(best[0]); phys.append(best[1])
            for t, v in rr.items(): lowered[t].append(v)
        by_boss = {t: [] for t in BASE}
        for r in bosses:
            rr = {}
            lowers(r, ev(r.get('charLevel', 'charLevel'), charLevel=L), rr)
            for t, v in rr.items(): by_boss[t].append(v)
        print('%s\t%d\t%d\t%.0f\t%.0f\t%.0f\t%.0f\t%d\t%.1f\t%.4f\t%.0f\t%d\t%.1f\t%.4f' % (
            (name, L, len(mons), statistics.median(oa), statistics.median(da),
             statistics.median(hits) if hits else 0, statistics.median(phys) if phys else 0, len(hits))
            + rr_of(lowered, len(mons)) + (adj(d, 'offensiveCritDamageModifier'), len(bosses)) + rr_of(by_boss, len(bosses))))
