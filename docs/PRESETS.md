# PRESETS — design choices

## Stage thresholds (decision 2026-11-14, scaling 2026-11-18)
No source gives thresholds for stages below endgame.
- Every `[[stage]]` block lists its own threshold values explicitly.
- Resist target is 80 % at every stage (`gameengine.dbr` `playerDefenseCap` [80,80,80]).
- Two tiers (decision 2026-11-18): the 9 base resists first (to the cap, then overcap), everything else second. Every other threshold term is raised to `threshold_secondary_weight` ([score], default 0.1), so one far under its target costs at most about 10% Survivability: it can tip a close call, never outweigh a base resist dropping under the cap. Before, a stun resist 12 points closer to 50 scored +24 points against −12 for Lightning 16 under the cap.
- `threshold_level` ([score], default 0 = off): below that character level, second-tier targets except absorption (a share of armor, not an amount that grows with level) are scaled by level / `threshold_level`, so a stage can state its endgame targets. The scaled target shows on the stat's own sheet line (`target 31`), and a threshold's points are merged into that line instead of a separate `Target …` line.

### Threshold curve (decision 2026-11-14)
`[thresholds]` name → target (settings table, per `[[stage]]`; `0` drops one). Survivability × Π f, with u = value / target − 1:
`ln f = above·u − (below − above)·width·ln(1 + e^(−u/width))` (`score::threshold_factor`). Slope `below` well under the target, `above` well over it, a smooth bend of relative `width` between: no cliffs. Defaults `threshold_below 1.0`, `threshold_above 0.05`, `threshold_width 0.05` ([score], design guesses). The 9 base resists (Pierce … Bleeding) share one term (geometric mean of their factors) raised to `threshold_resist_weight` ([score], default 1; 9 = each resist counts like one other threshold).
Values: `resist` sheet value (capped, minus `resist_reduction` and, for the lookahead share, the next difficulty's extra penalty; the 9 base resists only, `docs/MECHANICS.md` §9), `resist_overcap` uncapped − cap, `physical_resist`, `stun_resist` (42), `freeze_resist` (45) as `resist`; `absorption`, `armor`, `life`, `da` from the sheet model; `slow_resist` = defense type 16 "TotalSpeed" and `reflect_reduction` = type 57 "Reflect", each defense × (1 + modifier %) (**unverified** that these are the sheet's Slow Resistance / Reflected Damage Reduction). The log's Survivability inputs list each `name value/target xfactor`.
`plague-garden`: resist 80 at every stage. Below 94: `resist_reduction = "auto"` (the monster table, `docs/MECHANICS.md` §9) and `threshold_resist_weight = 4` (decision 2026-11-15: the fixed 25 of 2026-11-14 scored Hermit's Legguards ~0% at level 27 for overcap lost above the cap; now +20%, `tests/test_regress.cpp`). The weight is a design choice that keeps the guide's "caps first": a point under 80 costs about 0.7% Survivability at a 10% threat share, so the +28 Physique / +8% Life belt that drops Fire 7 and Pierce 15 under the cap stays negative (adapter 0.7.0 +2.3%, with the fixed 25 −5.1%, now −5.5%, `tests/test_presets.cpp`). `difficulty_lookahead` and `boss_share` stay at their defaults (on, 0.15): with weight 4 the next difficulty's 25 counts like the cap itself from level 65 on Normal and 84 on Elite (decision 2026-11-15: overcap for Elite / Ultimate before the switch, the guide's 75 / 94). 94+: `resist_reduction = 25` with `resist_reduction_from = "ultimate"`, weight 1 (the guide's "+25-35% overcap"; decision 2026-11-15: still on Normal / Elite at 94+ it is auto + lookahead, so the 25 and the next difficulty's 25 never stack); 35-93 states the 94+ physical 20 and stun 75, scaled by level / 94 (`threshold_level = 94`; stun 31 at level 39), and absorption 90 (design choice); 94+ uses the guide's endgame target minimums (overcap via the RR above, physical 20, absorption 100, armor 2200, stun 75, freeze 50, slow 50, reflect 55, life 16000, DA 2750).

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
Exponents a / b / c, threat shares, Other weights, `melee_share`, `damage_reduction_uptime` and `filler_weight` are design guesses with no source. Enemy DA / OA / hit size and resist reduction default to the monster table of the character's level and difficulty (`re/EnemyByLevel.tsv`, `docs/MECHANICS.md` §9); a per-level number (`resist_reduction`: a number) in the preset overrides it. `[enemy]` resists get the difficulty's bonus added (`re/EnemyResistByDifficulty.tsv`).

## Named presets, Auto and stages (2026-11-14)
- `GDCompare.preset.toml` (yours, from `presets/default.toml`) is the fallback. Every other `presets/*.toml` is deployed to `compat/GDCompare.presets/` (a copy you edited is kept on the next deploy).
- Auto (`[preset] auto = true`, default): the first named preset whose `[match]` fits the character scores the block. Order (`score::tried_first`): presets with `[match] character` first, then fewer `masteries` (a build's own preset before a template listing all ten), ties by file name. Fits = the character has points in at least one mastery (learned skill whose record has `Class = Skill_Mastery`, e.g. `_classtraining_class06.dbr`), every such mastery is listed in `masteries`, `archetype` (when set) equals `score::archetype` (below), and, when `damage` is set, those final damage types are at least `min_share` (default 0.5, design choice) of the main skills' damage (`score::damage_profile`: the top `auto_skills` class attack skills by points, after skill and gear conversions, with the character's % per type, weighted by points). No match → the generic `GDCompare.preset.toml`. The log line `tooltip: scoring with <file>[, stage <s>] (<each named preset: fits / not used: why>)` is written when the choice or a reason changes.
- `[match] damage` names: `physical`, `internal_trauma`, `pierce`, `cold`, `frostburn`, `fire`, `burn`, `acid`, `poison`, `lightning`, `electrocute`, `vitality`, `vitality_decay`, `chaos`, `aether`, `bleeding` (acid = Poison type direct, poison = its duration damage, `docs/MECHANICS.md` "DB names").
- `[[stage]]`: `name`, `min_level`, `max_level`, `requires` (skill or mastery records with points) and `[stage.score]` / `[stage.damage]` / `[stage.survival]` / `[stage.other]` over the preset's own values. The last stage in file order that applies wins. The block heading reads `GrimCompare - <name> - stage <stage>` (` - `, not ` | ` or `·`: the game font's glyph coverage is not verified).
- `[damage] skill_weights = "points"`: main skills weigh by points spent, so a 1-point skill does not count like a maxed one. A skill's points are its own plus its learned modifiers' and transmuter's (Bloody Pox 1 / Wasting 12 / Black Death 12 = 25), so +levels to a modifier score through the skill it modifies. Auto mode ranks skills by the same sum.
- `[damage] offtarget_weight` (decision 2026-11-18, default 1.0, per stage): damage of an off-target type counts × this, its flat damage and its % bonuses alike, in main skills, procs and pets. Off-target = not one of `[match] damage` and no conversion can turn it into one: every damage type but Bleeding is some record's `conversionInType` in the 1.3.1.1 database and converts into every other, nothing converts into Bleeding, a conversion keeps a part's DoT flag and never moves DoT into Pierce, Chaos or Aether (`docs/MECHANICS.md` §2). So for `plague-garden` (acid, poison) only Bleeding is off-target; Vitality counts in full because gear can convert it. Both sides of a swap use the same weights, so A → B and B → A stay exact opposites. No `[match] damage`: nothing is off-target.
- `[damage] skills`: only the listed skills with points are scored; while none has points (a levelling character), the main skills are picked as auto does (1.0.0).
- `[damage] retaliation` (default `false`): `true` counts retaliation damage (per incoming hit, `[survival] incoming_hits_per_sec`) and % Retaliation Added to Attack in Damage, and the `[other]` keys `retaliation` / `retaliation_modifier` then weigh nothing (`docs/MECHANICS.md` §5). The `retaliation` preset sets it.

### `plague-garden` stages (design choices from the Plague Garden guide's levelling plan)
| Stage | Applies when | Main skills | `offtarget_weight` (Bleeding) |
|---|---|---|---|
| 1-10 | Shaman | Devouring Swarm, Grasping Vines | 1.0: all real damage counts while levelling |
| 10-35 | level >= 10, Shaman + Occultist | + Bloody Pox, Dreeg's Evil Eye | 1.0 |
| 35-93 | level >= 35, both | Grasping Vines, Bloody Pox, Dreeg's Evil Eye | 0.5 |
| 94+ | level >= 94, both | same + procs and set pets (`pet_attack_rate` 0.5) | 0.2 |

## Enemy model and uptimes (2026-11-14)
- `[enemy]`: the target's resist per type (names as `[survival.threat]`; `poison` = Acid and Poison). Default from the DB (mean over Boss records, `presets/default.toml`); RR from gear, skills, devotions, procs and pets lowers it (`docs/MECHANICS.md` §1, §4) and damage scales by `1 - resist/100`.
- `[uptime]`: effect record → uptime, over the derived value. `incoming_hits_per_sec` ([survival]) drives "when hit" / "on block" procs, `low_health_per_sec` (default 0.05, design guess) "at x% health" procs (× the threshold %, `docs/MECHANICS.md` §10), `kills_per_sec` ([damage], default 0.5, design guess) "on kill" procs, `cc_share` (default 0.1, design guess) is what a proc's stun / freeze is worth (`docs/MECHANICS.md` §10), `pet_attack_rate` ([damage]) pet RR (0 = from the pet controller). All three can be set per `[[stage]]`.
- Shift lists the RR a swap adds (green) or loses (red) with its uptime and origin; a proc whose uptime falls back to 0 is named in `+? unscored: <skill> (uptime ?)` (the log says why).

## Conversion warnings (2026-11-14)
`[damage] conversion_warn = ["from->to", ...]` (`*` = any; names in `presets/default.toml`). On hover, every conversion the item as hovered adds (its own, its components', a set tier it completes: snapshot conversions after vs before; and item skill modifiers' `conversionInType/OutType/Percentage` at level 1) that matches an entry and whose output is not one of the preset's `[match] damage` types shows a red line `! converts away: [<skill>] 25% Vitality -> Fire`. Informational: the score already counts the conversion's damage effect. `acid` and `poison` name the same conversion type (DB enum `Poison` = Acid; its DoT moves along, `docs/MECHANICS.md` §2). `plague-garden`: `acid->*`, `vitality->*`, `physical->*` (design choice).

## Freeze (2026-11-14)
**The freeze key** (F10 by default, `GDCompare.ini [freeze] key`, 0 = off) writes `GDCompare.presets\frozen-<character>-<save>.toml` (`score::frozen_file`: `<save>` is the Steam account or `local`, plus `-custom` / `-backup`; just `frozen-<character>` when the save is unknown; name: letters, digits, `-`, `_`, others `_`) from the last published snapshot: the settings of the preset and stage scoring now, the main skills written out (`skill_weights = "points"` when they were picked automatically; the default-attack filler, if Auto added one, becomes a plain skill), no stages, `[match] character = "<name>"` and `saves = ["<its save>"]` + this character's masteries (+ the chosen preset's `damage` / `min_share`). Auto then uses it for that character (a `[match] character` preset is tried first). Freezing again overwrites it; deploy never touches it. The log line `freeze: wrote ...` confirms.
`[match] character`: the preset fits only the character with that name (`Player::GetPlayerName`).
`[match] characters = ["name", ...]` (0.9.5): the preset applies to these characters at every level, with no mastery, archetype or damage-share check (log: `[match] characters lists <name>`); other characters still go through the checks. Tried first, before `character`. `plague-garden` binds `winterpoison`, so it does not switch off while levelling (its `min_share` 0.5 fails before level 35).
`[match] saves = ["cloud/928303920/main/_name", ...]` (1.0.0): narrows `character` / `characters` to those saves, so same-named characters never share a preset. A key is the save folder as `GameEngine::GetPlayerFolder` builds it: `cloud/<Steam account>` (Steam Cloud, `userdata/<account>/219990/remote/save`) or `local` (`Documents/My Games/Grim Dawn/save`), then `main` (campaign) or `user` (custom game), then `_<name>`, or `__<name>` for the engine's backup copy. A name that some key mentions matches only a listed save; a name no key mentions matches by name. With the save unknown (`character.save` empty: an older snapshot, a missing optional target, no Steam user) names match as before. Case-insensitive, like the Windows folder. The wizard writes both lists.

## Other: utility weights (decision 2026-11-14)
`[other]` has one key per utility stat, each commented with its unit in `presets/default.toml` (values and sources: `docs/MECHANICS.md` §6). Other = 1 + Σ weight × value + `default` × every unlisted stat / 100. A key is a number or `{ weight = w, when = "<condition>" }`; the weight counts only while the condition holds:
- `shield` (L Hand holds a `WeaponArmor_Shield`), `offhand` (`WeaponArmor_Offhand`), `pets` (a skill with points that summons a `Class = Pet` record: `Skill_SpawnPet` / `Skill_TargetedSpawnPet`; `PetPlayerScaling` summons take no pet bonus and do not count), `weapon:<Class>` (R Hand record class, e.g. `weapon:WeaponMelee_Sword`), `stage:<name>`; a leading `!` negates. An unknown key or condition is a preset error (line shown).
- Default weights are design guesses: life leech 0.3, stun 0.1, the other crowd-control resists 0.05, `default` 0.05. Dodge / deflect, block and the damage-reduction debuff left `[other]` in 0.9.0: they are stages of Survivability (`docs/MECHANICS.md` §9); a preset that still lists `avoid_melee`, `avoid_projectiles`, `block` or `damage_reduction` is rejected with `unknown key`. `cooldown_reduction` and `skill_cost_reduction` left in 0.9.6 (likewise rejected): CDR is scored in Damage (cooldown main skills used more often, up to attack / cast speed) and buff uptime; energy cost reduction is not scored (no energy model).
- `plague-garden` (from the Plague Garden guide's priorities): life regen 0.05 (§3.6 % regen scales hard), life leech 1.0 (§2 ADCtH 12%+), retaliation and retaliation % 0 (§1); the rest default (comments in the file).
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
| `pet` | pet | other 0.35, pet bonus 1.0 when pets (pet damage in Damage, experimental) |
| `retaliation` | retaliation | `[damage] retaliation = true`: retaliation and % Retaliation Added to Attack count in Damage |

`plague-garden` adds `archetype = "caster"`. Tests (`tests/test_presets.cpp`, DB-backed): each template is picked for a synthetic character. Trap skills do not count as pets. Without `modifier_ids`, a pet modifier does not count. winterpoison (level 15, store #0021) gets `plague-garden` by name (0.9.5); under another name, and winterofhell (level 96), get no template.

Limitations:
- Weapon pool skills (the default attack, Cadence, Savagery) are main skills. Pool attacks (`Skill_WPAttack_*`, e.g. Zolhan's Technique) count toward the archetype but are not: they take a share of those skills' uses and count in Damage like procs (docs/MECHANICS.md §4, §5), not in `[match] damage`.
- Pet damage is an experimental estimate from the pet records (docs/MECHANICS.md §4); the tooltip says so. Summons without a time to live (Blade Spirit, Squall, Celestial Guardian too) are kept at `petLimit`; with a live pet in the snapshot its engine speed and hit animation set the rate.
- No ranged template.
