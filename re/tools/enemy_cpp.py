# Rewrites the generated tables in core/src/enemy.cpp (kRows, kRaces) from re/EnemyByLevel.tsv and
# re/EnemyRaces.tsv, after re/tools/enemy_table.py regenerated them for a new game build.
# usage: python3 -I re/tools/enemy_cpp.py <repo root>
import re
import sys

root = sys.argv[1].rstrip('/') + '/'
DIFFS = ['Normal', 'NormalVeteran', 'Elite', 'EliteVeteran', 'Ultimate', 'Ascendant']
COLS = ['oa', 'da', 'hit', 'hit_physical', 'rr', 'rr_share', 'crit_damage', 'boss_rr', 'boss_rr_share']


def num(s):
    return '%g' % float(s)


def tsv(name):
    lines = open(root + 're/' + name, encoding='utf-8').read().splitlines()
    head = lines[0].split('\t')
    return [dict(zip(head, l.split('\t'))) for l in lines[1:] if l]


rows = tsv('EnemyByLevel.tsv')
blocks = []
for d in DIFFS:
    mine = [r for r in rows if r['difficulty'] == d]
    if not mine:
        sys.exit(f'EnemyByLevel.tsv: no rows for {d}')
    body = ''.join(f"        {{{num(r['level'])}, {{{', '.join(num(r[c]) for c in COLS)}}}}},\n" for r in mine)
    blocks.append(f'    {{  // {d}\n{body}    }},\n')
levels = {len([r for r in rows if r['difficulty'] == d]) for d in DIFFS}
if len(levels) != 1:
    sys.exit(f'EnemyByLevel.tsv: uneven level counts {levels}')

races = tsv('EnemyRaces.tsv')
race_body = ''.join(f"    {{\"{r['races']}\", {r['monsters']}}},\n" for r in races)

path = root + 'core/src/enemy.cpp'
src = open(path, encoding='utf-8').read()
src, n1 = re.subn(r'(constexpr int kLevels = )\d+;', rf'\g<1>{levels.pop()};', src)
src, n2 = re.subn(r'(constexpr Row kRows\[kDifficulties\]\[kLevels\] = \{\n).*?(\n\};)',
                  lambda m: m.group(1) + ''.join(blocks).rstrip('\n') + m.group(2), src, flags=re.S)
src, n3 = re.subn(r'(constexpr Races kRaces\[\] = \{\n).*?(\n\};)',
                  lambda m: m.group(1) + race_body.rstrip('\n') + m.group(2), src, flags=re.S)
if (n1, n2, n3) != (1, 1, 1):
    sys.exit(f'core/src/enemy.cpp: table markers not found ({n1}, {n2}, {n3})')
open(path, 'w', encoding='utf-8').write(src)
print(f'{len(rows)} level rows, {len(races)} race groups')
