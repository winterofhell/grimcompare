# PRESETS — design choices

## Stage thresholds (decision 2026-11-14)
No source gives thresholds for stages below endgame, so none are derived or scaled.
- Every `[[stage]]` block lists its own threshold values explicitly.
- Resist target is 80 % at every stage (`gameengine.dbr` `playerDefenseCap` [80,80,80]).
- Other thresholds for lower stages are chosen per preset and commented as design choices, not mechanics.

### Threshold curve (decision 2026-11-14)
`[thresholds]` name → target (settings table, per `[[stage]]`; `0` drops one). Survivability × Π f, with u = value / target − 1:
`ln f = above·u − (below − above)·width·ln(1 + e^(−u/width))` (`score::threshold_factor`). Slope `below` well under the target, `above` well over it, a smooth bend of relative `width` between: no cliffs. Defaults `threshold_below 1.0`, `threshold_above 0.05`, `threshold_width 0.05` ([score], design guesses). The 9 base resists (Pierce … Bleeding) share one term (geometric mean of their factors).
Values: `resist` sheet value (capped, minus `resist_reduction`; the 9 base resists only), `resist_overcap` uncapped − cap, `physical_resist`, `stun_resist` (42), `freeze_resist` (45) as `resist`; `absorption`, `armor`, `life`, `da` from the sheet model; `slow_resist` = defense type 16 "TotalSpeed" and `reflect_reduction` = type 57 "Reflect", each defense × (1 + modifier %) (**unverified** that these are the sheet's Slow Resistance / Reflected Damage Reduction). The log's Survivability inputs list each `name value/target xfactor`.
`plague-garden`: resist 80 at every stage, scored under `[survival] resist_reduction = 25` (decision 2026-11-14: the guide's "caps + 25-30% overcap first", priorities.md §3 / §2, so a base resist counts fully up to 105 uncapped; adapter 0.7.0 scored a +28 Physique / +8% Life belt +2.3% over 15 Pierce + 20 Fire + 9 Cold / Lightning res, now -5.1%, `tests/test_presets.cpp`); 35-93 adds physical 10, absorption 90, stun 50 (design choices); 94+ uses `docs/build-ref/priorities.md` §2 target minimums (overcap via the RR above, physical 20, absorption 100, armor 2200, stun 75, freeze 50, slow 50, reflect 55, life 16000, DA 2750).

### `target_sets` (2026-11-14)
`[preset] target_sets = ["records/items/lootsets/…dbr"]`. Hovering a member (set record `setMembers`) adds `target set <name>: <worn>/<n> worn` and `missing: <piece> (this) / (in bag)` (bag = the inventory of the last manual F9 snapshot; auto snapshots do not read it). Shown whatever the item's level requirement. `plague-garden`: Worldkeeper's Garden (`itemset_d313`).

## Uptime fallback (decision 2026-11-14)
Last resort only: `0`, so a guess never inflates a score. Never silent: the tooltip shows `+? unscored: <effect>` next to the total, the Shift view marks it `default ⚠`. The preset sets the real value (`[uptime]`, per stage if needed). Pets use the preset's `pet_attack_rate` (not in the DB); `plague-garden` 94+ sets 0.5 (design guess). Mechanism: `docs/MECHANICS.md` §4.

## Threat profile: `incoming_hits_per_sec` (decision 2026-11-14)
Drives "on being hit" uptimes. Design guesses, no source; tune from play, override per preset/stage.

| Stage (plague-garden) | Default |
|---|---|
| 1–10 | 1.0 |
| 10–35 | 1.5 |
| 35–93 | 2.0 |
| 94+ | 3.0 |

## Score preset (`presets/default.toml`, decision 2026-11-14)
Exponents a / b / c, threat shares, Other weights, `melee_share`, `damage_reduction_uptime` and `filler_weight` are design guesses with no source. Enemy DA / OA / hit size default to the monster table (`re/EnemyByLevel.tsv`, `docs/MECHANICS.md` §9); a per-level number in the preset overrides it.

## Named presets, Auto and stages (2026-11-14)
- `GDCompare.preset.toml` (yours, from `presets/default.toml`) is the fallback. Every other `presets/*.toml` is deployed to `compat/GDCompare.presets/` (a copy you edited is kept on the next deploy).
- Auto (`[preset] auto = true`, default): the first named preset whose `[match]` fits the character scores the block. Order (`score::tried_first`): presets with `[match] character` first, then fewer `masteries` (a build's own preset before a template listing all ten), ties by file name. Fits = the character has points in at least one mastery (learned skill whose record has `Class = Skill_Mastery`, e.g. `_classtraining_class06.dbr`), every such mastery is listed in `masteries`, `archetype` (when set) equals `score::archetype` (below), and, when `damage` is set, those final damage types are at least `min_share` (default 0.5, design choice) of the main skills' damage (`score::damage_profile`: the top `auto_skills` class attack skills by points, after skill and gear conversions, with the character's % per type, weighted by points). No match → the generic `GDCompare.preset.toml`. The log line `tooltip: scoring with <file>[, stage <s>] (<each named preset: fits / not used: why>)` is written when the choice or a reason changes.
- `[match] damage` names: `physical`, `internal_trauma`, `pierce`, `cold`, `frostburn`, `fire`, `burn`, `acid`, `poison`, `lightning`, `electrocute`, `vitality`, `vitality_decay`, `chaos`, `aether`, `bleeding` (acid = Poison type direct, poison = its duration damage, ERRATA §0).
- `[[stage]]`: `name`, `min_level`, `max_level`, `requires` (skill or mastery records with points) and `[stage.score]` / `[stage.damage]` / `[stage.survival]` / `[stage.other]` over the preset's own values. The last stage in file order that applies wins. The block heading reads `GD Compare - <name> - stage <stage>` (` - `, not ` | ` or `·`: the game font's glyph coverage is not verified).
- `[damage] skill_weights = "points"`: main skills weigh by points spent, so a 1-point skill does not count like a maxed one.

### `plague-garden` stages (design choices from the guide's levelling plan, `docs/build-ref/plague-garden.md` §4)
| Stage | Applies when | Main skills |
|---|---|---|
| 1-10 | Shaman | Devouring Swarm, Grasping Vines |
| 10-35 | level >= 10, Shaman + Occultist | + Bloody Pox, Dreeg's Evil Eye |
| 35-93 | level >= 35, both | Grasping Vines, Bloody Pox, Dreeg's Evil Eye |
| 94+ | level >= 94, both | same + procs and set pets (`pet_attack_rate` 0.5) |

## Enemy model and uptimes (2026-11-14)
- `[enemy]`: the target's resist per type (names as `[survival.threat]`; `poison` = Acid and Poison). Default from the DB (mean over Boss records, `presets/default.toml`); RR from gear, skills, devotions, procs and pets lowers it (`docs/MECHANICS.md` §1, §4) and damage scales by `1 - resist/100`.
- `[uptime]`: effect record → uptime, over the derived value. `incoming_hits_per_sec` ([survival]) drives "when hit" / "on block" procs, `low_health_per_sec` (default 0.05, design guess) "at x% health" procs (× the threshold %, `docs/MECHANICS.md` §10), `kills_per_sec` ([damage], default 0.5, design guess) "on kill" procs, `cc_share` (default 0.1, design guess) is what a proc's stun / freeze is worth (`docs/MECHANICS.md` §10), `pet_attack_rate` ([damage]) pet RR (0 = from the pet controller). All three can be set per `[[stage]]`.
- Shift lists the RR a swap adds (green) or loses (red) with its uptime and origin; a proc whose uptime falls back to 0 is named in `+? unscored: <skill> (uptime ?)` (the log says why).

## Conversion warnings (2026-11-14)
`[damage] conversion_warn = ["from->to", ...]` (`*` = any; names in `presets/default.toml`). On hover, every conversion the item as hovered adds (its own, its components', a set tier it completes: snapshot conversions after vs before; and item skill modifiers' `conversionInType/OutType/Percentage` at level 1) that matches an entry and whose output is not one of the preset's `[match] damage` types shows a red line `! converts away: [<skill>] 25% Vitality -> Fire`. Informational: the score already counts the conversion's damage effect. `acid` and `poison` name the same conversion type (DB enum `Poison` = Acid; its DoT moves along, `docs/MECHANICS.md` §2). `plague-garden`: `acid->*`, `vitality->*`, `physical->*` (design choice).

## Freeze (2026-11-14)
**The freeze key** (F10 by default, `GDCompare.ini [freeze] key`, 0 = off; docs/SNAPSHOT.md) writes `GDCompare.presets\frozen-<character>.toml` (`score::freeze`; name: letters, digits, `-`, `_`, others `_`) from the last published snapshot: the settings of the preset and stage scoring now, the main skills written out (`skill_weights = "points"` when they were picked automatically; the default-attack filler, if Auto added one, becomes a plain skill), no stages, `[match] character = "<name>"` + this character's masteries (+ the chosen preset's `damage` / `min_share`). Auto then uses it for that character (a `[match] character` preset is tried first). Freezing again overwrites it; deploy never touches it. The log line `freeze: wrote ...` confirms.
`[match] character`: the preset fits only the character with that name (`Player::GetPlayerName`).
`[match] characters = ["name", ...]` (0.9.5): the preset applies to these characters at every level, with no mastery, archetype or damage-share check (log: `[match] characters lists <name>`); other characters still go through the checks. Tried first, before `character`. `plague-garden` binds `winterpoison`, so it does not switch off while levelling (its `min_share` 0.5 fails before level 35).

## Other: utility weights (decision 2026-11-14)
`[other]` has one key per utility stat, each commented with its unit in `presets/default.toml` (values and sources: `docs/MECHANICS.md` §6). Other = 1 + Σ weight × value + `default` × every unlisted stat / 100. A key is a number or `{ weight = w, when = "<condition>" }`; the weight counts only while the condition holds:
- `shield` (L Hand holds a `WeaponArmor_Shield`), `offhand` (`WeaponArmor_Offhand`), `pets` (a skill with points that summons a `Class = Pet` record: `Skill_SpawnPet` / `Skill_TargetedSpawnPet`; `PetPlayerScaling` summons take no pet bonus and do not count), `weapon:<Class>` (R Hand record class, e.g. `weapon:WeaponMelee_Sword`), `stage:<name>`; a leading `!` negates. An unknown key or condition is a preset error (line shown).
- Default weights are design guesses: life leech 0.3, stun 0.1, the other crowd-control resists 0.05, `default` 0.05. Dodge / deflect, block and the damage-reduction debuff left `[other]` in 0.9.0: they are stages of Survivability (`docs/MECHANICS.md` §9); a preset that still lists `avoid_melee`, `avoid_projectiles`, `block` or `damage_reduction` is rejected with `unknown key`. `cooldown_reduction` and `skill_cost_reduction` left in 0.9.6 (likewise rejected): CDR is scored in Damage (cooldown main skills used more often, up to attack / cast speed) and buff uptime; energy cost reduction is not scored (no energy model).
- `plague-garden` (from `docs/build-ref/plague-garden.md` priorities): life regen 0.05 (§3.6 % regen scales hard), life leech 1.0 (§2 ADCtH 12%+), retaliation and retaliation % 0 (§1); the rest default (comments in the file).
- `[stage.other]` overrides per stage. Freeze writes the resolved table: a `stage:` condition that holds is dropped, one that does not becomes weight 0.
- Deploy replaces `GDCompare.preset.toml` when it is an unedited copy of an earlier shipped default (its old `cc_resist` key no longer parses); an edited copy is kept.

## Skill levels in the score (decision 2026-11-14)
- +levels to a skill with 0 points score 0 and show grey `(unused, 0 points)`.
- Timed buffs you cast (Blood of Dreeg): `[score] recast_buffs = true` (default) assumes a recast whenever the recharge allows, uptime = min(1, duration / (recharge × (1 − CDR))); `false` takes the `[uptime]` entry of the skill's record where there is one. Below 100% the score is a mix of the buff's on and off states (`docs/MECHANICS.md` §6). `[score] buff_mode = "average"` (default) or `"live"`: your timed buffs exactly as they are right now (procs stay averaged). Shift lists every buff as `<name> up X%, while up:` with the sheet values it changes.

## Shields and caster off-hands (decision 2026-11-14)
Item type 8 (shield / off-hand) compares in L Hand with every stat but base damage and weapon DPS (`docs/MECHANICS.md` §5). Next to a two-handed R-hand weapon (record class ending `2h`) the block reads `not compared: two-handed weapon in R Hand`. Replacing a dual-wielded L-hand weapon shows a warning. Weapons themselves stay excluded.

## Archetype and templates (2026-11-14)
`[match] archetype = "pet" | "retaliation" | "melee" | "ranged" | "caster"` (`score::archetype`). Every learned class skill (not a devotion, not a mastery) falls into one bucket by its record or its `buffSkillName` record:
- **pet**: `Skill_SpawnPet` / `Skill_TargetedSpawnPet` whose `spawnObjects` record has `Class = Pet` (Hellhound, Raven, Briarthorn, Manticore, Blight Fiend, Skeletons). `PetPlayerScaling` summons (Mortar Trap, Thermite Mines, Storm Totem, Wind Devils, Inquisitor seals and runes, Blade Spirit, Celestial Guardian) are not pets here.
- **attack**: `Skill_Attack*` with damage (as `main_skills`), `Skill_WeaponPool_*`, `Skill_WPAttack_*`. **Weapon attack** = such a skill with `weaponDamagePct > 0`; every weapon pool and pool attack record in playerclass01-10 has it.
- **retaliation**: a non-zero `retaliation*` field (Presence of Virtue's buff, Counter Strike, Ascension, Overguard, Blood of Dreeg's buff, ...).
- A skill listed in another learned skill's `modifier_ids` (schema 5) counts for its parent's bucket. Unlinked modifiers (schema < 5) count for nothing.

Points per bucket = points spent. Decision (design thresholds, no source):
1. pet ≥ attack → `pet`;
2. retaliation ≥ attack → `retaliation`;
3. no attack points → none;
4. weapon attack ≥ half of attack → `ranged` when R Hand is `WeaponHunting_*`, else `melee`;
5. otherwise `caster`.

The log shows the counts: `pet 0, retaliation 1, attack 19 (weapon 0)`.

Templates (`presets/*.toml`; all ten masteries; every weight is a design choice, made offline from the DB and the current scoring sources, not tuned in play):

| File | `[match]` | Weights over default |
|---|---|---|
| `chaos-conversion-caster` | caster, Chaos ≥ 50% | `conversion_warn = ["*->*"]`, retaliation 0 |
| `physical-melee` | melee, Physical + Internal Trauma ≥ 50% | life leech 0.6, `physical->*` warns, retaliation 0 |
| `pet` | pet | damage exponent 0.3, other 0.5, pet bonus 2.0 when pets |
| `retaliation` | retaliation | retaliation 0.5, retaliation % 0.5 |

`plague-garden` adds `archetype = "caster"`. Tests (`tests/test_presets.cpp`, DB-backed): each template is picked for a synthetic character. Trap skills do not count as pets. Without `modifier_ids`, a pet modifier does not count. winterpoison (level 15, store #0021) gets `plague-garden` by name (0.9.5); under another name, and winterofhell (level 96), get no template.

Limitations:
- Weapon pool skills count toward the archetype but are not main skills, so a build whose only attack is one has no main damage skill and fails `[match] damage`.
- Pet damage is scored only next to main skills and only with `pet_attack_rate` > 0. The DB does not give that rate, so `pet` leaves it at 0: pet damage and RR show as `+? unscored`.
- No ranged template.
