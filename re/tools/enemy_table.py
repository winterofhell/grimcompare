# Enemy model per character level from the monster records (database.arz + GDX1..3, layered like gdc::arz).
# usage: python3 -I re/tools/enemy_table.py <game dir> > re/EnemyByLevel.tsv
# For every Monster record under records/creatures/enemies/ (Common, Champion, Hero) at charLevel = L:
#   level  = the record's charLevel equation
#   OA, DA = combatformulas.dbr offensive/defensiveAbilityEquation on the bio record's
#            (characterAttributeEquations) characterOffensiveAbility / DefensiveAbility / Dexterity / Strength
#   hit    = the largest flat direct damage (sum of offensive<Type>Min/Max averages, all types) of one of its
#            skills (skillName<n> at skillLevel<n>; the record or its buffSkillName record), and the physical
#            part of that hit, each x the combatformulas.dbr attribute factor on the bio's attributes
#            (physical / pierce x (dexterity/245 + 1), the rest x (intelligence/215 + 1)).
#            Weapon damage (weaponDamagePct x enemy gear), % damage bonuses and difficulty adjustments are
#            NOT included.
# Output: median over the monsters, per level.
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
def at(v, level):
    if isinstance(v, list): return float(v[min(level, len(v)) - 1]) if level > 0 and v else 0.0
    try: return float(v)
    except (TypeError, ValueError): return 0.0
def hit(r, level, dex, intel):
    total = phys = 0.0
    for t in TYPES:
        lo = at(r.get('offensive%sMin' % t, 0), level)
        if lo <= 0: continue
        hi = max(lo, at(r.get('offensive%sMax' % t, 0), level))
        v = (lo + hi) / 2 * ((dex / 245 + 1) if t in ('Physical', 'Pierce') else (intel / 215 + 1))
        total += v
        if t == 'Physical': phys += v
    return total, phys

mons = []
for n in index:
    if n.startswith('records/creatures/enemies/') and dbs[index[n]][2][n][0] == 'Monster':
        r = rec(n)
        if r.get('monsterClassification') in ('Common', 'Champion', 'Hero'): mons.append(r)

print('level\tmonsters\toa\tda\thit\thit_physical\tmonsters_with_skill_hit')
for L in [1] + list(range(5, 101, 5)):
    oa, da, hits, phys = [], [], [], []
    for r in mons:
        lvl = ev(r.get('charLevel', 'charLevel'), charLevel=L)
        bio = rec(r.get('characterAttributeEquations', '')) or {}
        b = lambda k: ev(bio.get(k, 0), charLevel=lvl)
        oa.append((b('characterOffensiveAbility') + lvl * 12 + b('characterDexterity') * 0.5) + 53)
        da.append((b('characterDefensiveAbility') + lvl * 12 + b('characterStrength') * 0.5) + 53)
        best = (0.0, 0.0)
        for i in range(1, 40):
            sk = rec(r.get('skillName%d' % i, ''))
            if not sk: continue
            sl = int(ev(r.get('skillLevel%d' % i, 1), charLevel=lvl))
            for s in (sk, rec(sk.get('buffSkillName', '')) if isinstance(sk.get('buffSkillName'), str) else None):
                if s: best = max(best, hit(s, max(sl, 1), b('characterDexterity'), b('characterIntelligence')))
        if best[0] > 0: hits.append(best[0]); phys.append(best[1])
    print('%d\t%d\t%.0f\t%.0f\t%.0f\t%.0f\t%d' % (L, len(mons), statistics.median(oa), statistics.median(da),
          statistics.median(hits) if hits else 0, statistics.median(phys) if phys else 0, len(hits)))
