# MECHANICS — engine rules used by the stat model

Every rule carries a source. Tags:
- **DB** — read from `database.arz` (record + field cited).
- **community** — Crate forum consensus, collected 2026-11-14. Not a dev post. Confirm by swap/dummy test where cheap; the model may use it until a test contradicts it.
- **unverified** — single claim or no source. The model must not silently depend on it: flag it in the tooltip or calibrate it.

DB field names: `docs/build-ref/ERRATA.md` §0. Constants and formulas from the DB are added in Phase 3.

## 1. Resistance reduction (RR) — community

Three types, by DB field (ERRATA §0):

| Type | Shown as | DB field | Stacking |
|---|---|---|---|
| 1 | "−X% resistance" | `defensiveX` < 0 on a debuff | all sources add, unlimited |
| 2 | "X% Reduced target's Resistances" | `offensiveTotalResistanceReductionPercent` | highest only; multiplicative; cannot push a positive resist below 0 |
| 3 | "X Reduced target's Resistances" | `offensiveTotalResistanceReductionAbsolute` | highest only; additive |

Order 1 → 2 → 3, per damage type, starting from the target resist `R`:

```
r1 = R − Σ type1
r2 = r1 > 0 ? r1 × (1 − max type2 / 100) : r1
r3 = r2 − max type3
```

**Scoring an overlap.** Highest-only sources are weighted by uptime (§4): a weaker source counts only while every stronger one is down, i.e. (1 − uptime of stronger) × its value. Only that RR component is reduced; every other stat of the same item counts normally. Never drop an item for one overlapping stat.
Example: awakened Ring of Eternal Rot (`c053_ring.dbr`, proc 18 % type 2) next to Living Bloom (26 % type 2): the ring's 18 % counts for (1 − Bloom uptime); at Bloom uptime 1 it adds 0. Its +2 Vulnerability, +100 % Poison duration and 300 Poison/5 s proc count in full.

Cheap check: none in isolation (the ring's RR comes bundled with other stats). Covered by the Phase 3 ring swap test: model prediction for the whole swap vs the dummy.

## 2. Conversion — community

1. **Same level, additive:** skill transmuter conversion + item skill modifier conversion for the same skill, summed per `in → out` pair.
2. **Global, multiplicative:** gear/devotion conversion (`conversionInType`/`conversionOutType`) applies to the remainder that level 1 left unconverted.
3. Converted damage is never converted again. Consequence: Widow's Bite 25 % Chaos→Vitality + a Vitality→Acid conversion does **not** move the converted Vitality on to Acid; only original Vitality converts. This overrides the "chained conversion" reading in ERRATA §4.
4. Elemental converts before Fire / Cold / Lightning.
5. Skill-rank % bonuses (the skill's own % damage) apply before conversion, to the original type. Gear/sheet % bonuses apply to the final type.

**Same-level sum > 100 %: unverified.** No rule is assumed. When it occurs the model flags `⚠ conversion > 100 %` and the value is calibrated against the in-game skill tooltip, which shows post-conversion damage.

Cheap check: skill tooltip before/after equipping a conversion item (Phase 3 swap test "conversion item").

## 3. DoT — community, one part unverified

- **+% of a direct type does not boost its DoT — engine.** `CombatAttributeAccumulator::ModifyDamage` (0x1194C0) runs every modifier's `Execute` on every damage entry. `CombatAttributeAbsDamageMod::Execute` (0x115070) calls only the entry's `ModifyAbsoluteDamage` (vt+0x80), `CombatAttributeDurDamageMod::Execute` (0x1151B0) only `ModifyDurationDamage` (vt+0x88). `CombatAttributeDurDamage` has no `ModifyAbsoluteDamage` of its own (the base one, 0x15880, is empty), and `CombatAttributeAbsDamage` none for duration. So +% Acid never reaches Poison, +% Fire never Burn, and so on for Cold / Frostburn, Lightning / Electrocute, Physical / Internal Trauma, Vitality / Vitality Decay. The sheet agrees: `GetTotalDamageModifierType` (0x11A750) sums by type **and** damage kind, and `lvl16-wp-before-belt` shows Acid +59% next to Poison +68%. Community: [wiki Damage Types](https://grimdawn.fandom.com/wiki/Damage_Types) (+% Cold does not affect Frostburn). What both share: the resist on the target, conversion (a converted type takes its DoT along) and +% All Damage (type 60, spread over both kinds). `tests/test_sanity.cpp` cases 8 / 9.

- Crit is rolled at application; a critical DoT stays critical for its full duration.
- RR is snapshotted at application.
- **unverified** (single forum claim): debuffs that tick every second (Bloody Pox etc.) re-check RR on each tick. The steady-state model assumes all RR is up, so this matters only for the "apply RR first" order, which the model does not score.

## 4. Uptime of conditional effects — design choice

Every conditional effect (proc, pet, temporary buff/debuff, skill with a cooldown) has an `uptime` in [0, 1]. One mechanism for all; no per-item special cases.

- **Default, derived where possible:** `uptime = min(1, proc chance × trigger rate × duration)`. Proc chance and duration come from the DB record; trigger rate from the model (attack/cast rate of the triggering skill). For cooldown skills: `min(1, duration / cooldown)`.
- **On being hit:** trigger rate = threat profile `incoming_hits_per_sec` (per-stage defaults in `docs/PRESETS.md`).
- **Pets:** preset value until Phase 3 models pet attack rate (then: pet attack rate × active pets).
- **Last-resort fallback `0`** (`docs/PRESETS.md`). Never silent: if any effect on the compared items is at fallback, the tooltip shows `+? unscored: <effect>` next to the total.
- **Override:** per preset and per `[[stage]]`, keyed by the effect's DB record path.
- **Highest-only overlap** (RR types 2 and 3, any other highest-only stat): sort sources by value, strongest first. Source k is the active one with probability `p_k = u_k × Π_{j stronger} (1 − u_j)`; nothing is active with `Π (1 − u_j)`. Score the expected damage over these states, not damage at an averaged RR (type 2 is multiplicative, so the two differ). For two sources the weaker one gets weight `u_weak × (1 − u_strong)`, the rule above. **Design assumption: sources fire independently** (unverified; correlated procs, e.g. two on-hit effects from the same attack, would make this wrong).
- **Shift view** shows each assumed uptime and where it came from (`derived` / `preset` / `default ⚠`).

**Implementation (`core/src/effects.cpp`, 2026-11-14).** Sources and the rate each one gets:
- Learned class skills (not Passive / Modifier / Transmuter / Mastery): the debuff they apply (`buffSkillName` when that record is a debuff, else the skill) with their learned modifiers and item skill modifiers. No `skillCooldownTime` → **design choice:** kept up (uptime 1, e.g. Curse of Frailty `curse1.dbr`); else cast on cooldown, `uptime = min(1, duration / cooldown)`.
- Devotion powers with points: `templateAutoCast` on the skill or its `buffSkillName` record (DB: `tier2_32d_skill.dbr` has only `buffSkillName`; the buff record holds the controller and `skillCooldownTime`).
- Items and sets: `itemSkillName` + `itemSkillAutoController`, level `itemSkillLevelEq` (a number, or `itemLevel/4+1` floored: **unverified**); set skills at the piece count (`gdc::sets`).
- Proc rate: `per_sec = 1 / (cooldown + 1 / (chanceToRun/100 × trigger rate))` (**design choice:** the cooldown blocks the next roll). Trigger rate: `AttackEnemy` / `AttackEnemyCrit` → the main skills' attack or cast speed (snapshot `attack_speed` / `cast_speed` read as uses per second: **unverified**) × crit chance for crit triggers; `HitByEnemy` → `incoming_hits_per_sec`; other triggers → fallback 0.
- Pets (`spawnObjects`): alive = `min(petLimit, summons/s × spawnObjectsTimeToLive)`; the pet's `attackSkillName` is used `pets × pet_attack_rate` times per second, its other skills with a cooldown `pets / cooldown`. `pet_attack_rate` comes from the preset only: the controller's `minSwingPause` / `maxSwingPause` is the pause between attacks, and the attack animation's length is not a record field. Unset → fallback 0, listed unscored.
- RR "to all" (`offensiveTotal…`) lands on every type of the `[enemy]` model; `…Elemental…` and `defensiveElementalResistance` on Cold, Fire, Lightning.

## Not modelled
- Devotion refund / "self-support" (e.g. Crane counting its own affinity): irrelevant, the adapter reads the actual devotions.

## 5. Score v1 (`core/src/score.cpp`) — sources per rule

Weights and enemy numbers are design choices in `presets/default.toml` (`docs/PRESETS.md`). Only ratios after / before are shown, so constant factors cancel.

**Damage** per main skill, at its current level (record arrays indexed by level − 1, the last value past the end):
- Flat damage: the skill record's `fields::flat_damage()` fields (re/CombatAttributeType.tsv load fields), average of min/max; a record without any (Skill_AttackBuffRadius) uses its `buffSkillName` record. Plus learned modifiers (`Skill::modifier_ids`, schema 5) and item skill modifiers (DB records). **unverified:** DoT = per second × `…DurationMin`; `…Chance` < 100 scales by chance.
- Weapon skills (`weaponDamagePct` > 0) add pct × flat damage of equipped items (their getter output; type 1 BasePhysical as Physical) and of passive / toggled skills that are on. **community:** gear flat damage applies to weapon attacks only. Shortcuts: dual wield not modeled (L-hand weapon ignored); an L-hand shield / off-hand's base damage (`BasePhysical` and the record's `offensiveBase*Min/Max`) is taken out of weapon flat damage (base damage and weapon DPS are not compared for them, design choice); item DoT min/max taken as per second (**unverified**).
- Skill % fields apply to the original type before conversion (§2.5, community). Conversions: skill level, then the character's on the remainder; converted damage is not converted again (§2, community). Sums over 100% are scaled (**unverified**, noted). Elemental = a third each Cold / Fire / Lightning (**unverified**).
- × enemy resist multiplier of the final type: `[enemy]` resist after the RR of both sides (`effects::resist_multipliers`, §1 / §4). Default resists: mean base `defensive<Type>` of the 48 Boss enemy records (DB); difficulty scaling not modelled.
- × (1 + character % of the final type / 100) from `damage_modifiers` (engine rows; 58 / 60 already spread, not added again).
- × attribute factor, combatformulas.dbr: physical/pierce `dexterityDV/245 + 1`, physical duration (bleed, internal trauma) `/215`, magical `intelligenceDV/215 + 1`, magical duration `/200` (DB). **unverified:** applied as a separate multiplier (the equations multiply the damage value; the engine's % rows do not include it: aether 187% vs chaos 2324% at the same Spirit).
- × hit and crit: pth from `probabilityToHitEquation` (DB) with OA vs the enemy DA (monster table, §9, or `enemy_da_per_level × level`), then `formulas::expected_hit` (engine, §9 stage 2): misses below 100, glancing at or below 70, each crit tier by its own band, crit damage (type 59, a fraction, + the skill's `offensiveCritDamageModifier` / 100) on every tier above 1.0.
- × (1 + `offensiveSlow<Type>DurationModifier` / 100) for DoT of the final type, summed over the equipped items' records. **shortcut:** record values (no affix roll), skills' and devotions' parts not seen; applied to the final type (**unverified**).
- × speed: no cooldown → 1 + (AttackSpeed (15) or SpellCastSpeed (16) modifier + TotalSpeed (13) modifier) / 100 (**unverified:** TotalSpeed added, noted when it changes); cooldown skills 1 (CDR is an Other key, §6).
- **Procs and pets** (`extra_damage`): every non-learned effect of §4 with flat damage (its record or its `buffSkillName` record) adds `per_sec × one use`. A proc's use goes through the character's conversions, % per type, attribute factor, enemy resist, hit and crit like a main skill; a pet's use is its attack record × enemy resist only (**shortcut:** pet bonuses, the pet's OA / crit, `PetPlayerScaling` not modelled). Item skills without a controller count as cast on cooldown (design choice). To put them on the main skills' scale: main / s = Σ cooldown skills (one use / cooldown) + the others sharing the casts by weight at the snapshot's attack / cast speed (read as uses per second: **unverified**) — design choice. Damage = (main/s × skill ratio + extra after) / (main/s + extra before); without procs or pets it is the skill ratio. No attack / cast speed in the snapshot → procs and pets not scored, noted.
- Auto skills: learned `records/skills/playerclass*` skills of class `Skill_Attack*` with damage, top `auto_skills` by points, weighted by points; all on cooldown → default weapon attack fills in (`filler_weight`). Weapon pool procs (`Skill_WPAttack_BasicAttack`), secondary skills (`SkillSecondary_*`), pets: not scored.

**Survivability:** EHP = Life / damage taken per incoming attack, through the pipeline of §9 (dodge / deflect, enemy OA vs DA, block, enemy damage reduction, resists, armor per region), × the threshold curves. Resists: R_t = min(uncapped − `resist_reduction`, max) (sheet values, `model::sheet`); `resist_reduction` (a preset design choice: incoming enemy RR) lowers the 9 base resists only. Base absorption: every armor entry carries `GameEngine::GetArmorDefensiveAbsorption` (engine, traced: `DefenseAttribute_BaseProtectionAbsorption::LoadFromTable` 0x204420 copies GameEngine+0x1328, which `GameEngine::LoadFromDatabase` 0x2C8830 loads from `gameengine.dbr armorDefensiveAbsorption` = 70; `AddToAccumulator` 0x204BF0 builds one entry per piece with armor > 0; `GetTotalDefenseType(39)` 0x11A880 averages the positive entries), × (1 + absorption modifier %). Not scored: DoT vs hit split, stages 5 and 8 of §9.

**Other:** 1 + Σ weight × value over the preset's `[other]` keys whose `when` holds + `default` × Σ unlisted stats / 100 (`gdc::utility`, §6). Weights are design choices (`docs/PRESETS.md` "Other"). Item-granted skills other than passives (`docs/MODEL.md` granted skills), set-granted skills and set fields outside `gdc::fields` stay "+? unscored".

**Components:** base item = item stats − relic stats − augment stats (per accumulator row; absorption left alone), minus the components' flat damage from their DB records (matched by value). Slot fit: the record's `amulet` / `ring` / `medal` / `head` / `chest` / `shoulders` / `hands` / `legs` / `feet` / `waist` flag (DB: present on all 491 ItemRelic / ItemEnchantment records). Completion bonuses are not moved (noted).

## 6. Skill levels, buffs and utility stats (2026-11-14)

**+skills** (`model::swap`, `docs/MODEL.md` item skill grants). A skill's stats at the old and new level come from its record's per-level arrays, so every mapped field moves with the level: damage, RR, % bonuses, resists, flat stats of passives and auras. Main skills and their modifiers are scored at the new level in Damage; RR through `gdc::effects` (§4).
- **+mastery** goes to every skill with that `mastery_id` (`SetMasteryAugment` 0x5373D0). **The mastery bar itself does not rise** — store 20261114-172933-952 records 1 → 2 (Occultist +1 item): every Occultist skill's augmented level rose by 1, `_classtraining_class03.dbr` stayed 0, and the mastery record has no `skillUltimateLevel` (DB). Community agrees: item bonuses to a mastery do not raise its bar ([Steam: GD mechanics, "+skills"](https://steamcommunity.com/sharedfiles/filedetails/?id=1855311027)). Evidence is from one level-50 mastery only.
- **0-point skills:** +levels to a skill with no points do nothing (`GetCurrentLevel` 0x4909C0 returns 0 for an unlearned skill). Score 0; the tooltip lists them grey `X +n (unused, 0 points)`.
- **Timed buffs** that are not running at the snapshot (`SkillBuff_Passive` / `Skill_BuffSelfDuration` records, DB classes): their level delta counts × uptime = min(1, `skillActiveDuration` / `skillCooldownTime`) (§4 rule, DB fields at the level). Only in the tooltip's `Mode::Expected`; the engine-checked path (`Mode::Engine`, calibration) leaves them out because the engine's totals do not hold a buff that is down. **shortcut:** the "before" side also lacks the buff, so the ratio is slightly high.

**Utility values** (`gdc::utility::values`, one per `[other]` key; dodge / deflect, block and the damage-reduction debuff are Survivability stages since 0.9.0, §9):
| Key | Value | Source |
|---|---|---|
| `run_speed` | sheet run speed (1 = +100%) | `model::sheet` |
| `life_regen`, `energy_regen` | % of Life regenerated per s; Energy per s / 10 | `model::sheet`, `combat_formulas.h` |
| `life_leech` | items' offensive 20 (attack damage converted to health %) / 100 | item-only |
| `stun_resist`, `sleep_resist`, `trap_resist`, `freeze_resist`, `petrify_resist`, `knockdown_resist`, `disruption_resist`, `slow_resist` | sheet resist (capped) / 100 for defense 42, 43, 44, 45, 46, 48, 13; 16 read as Slow Resistance (**unverified**) | `model::sheet`, re/CombatAttributeType.tsv |
| `cooldown_reduction`, `skill_cost_reduction` | items' skill attribute 1 / 2 / 100 (type 2 keeps its % in `value`) | SkillAttributeType; item-only |
| `experience` | char 55 % / 100 × (1 − level / `maxPlayerLevel`) | DB `records/creatures/pc/playerlevels.dbr` maxPlayerLevel = 100 |
| `retaliation`, `retaliation_modifier` | items' retaliation flat (avg of min / max, / 1000) and the largest retaliation % / 100 | item-only |
| `pet_bonus` | Σ over items of the mean of their `petBonusName` record's mapped % fields, as a fraction | DB, e.g. `b012a_medal` → 0.525. **shortcut:** mean only |

Item-only stats have no character total in the snapshot, so skills' parts of them are not seen (**shortcut**, noted in code). Any char / defense / damage-% / item-offense type that no score term reads is **unlisted**: it counts at `default` per 100 of its value and is named (`Other: <stat> a -> b (w default)` in Shift, the log); char 8 ManaLimitReserve is left out (more reserved energy is worse). Which of defense / defense_modifier holds types 55 / 56: not traced.

## 7. Requirements — engine (Game.dll 1.3.1.1 disassembly)

- Raw values: the item's virtual getters (`ItemEquipment::GetLevel/Strength/Dexterity/IntelligenceRequirement`, vt+0x528..0x540); each is the max of the item, its relic (+0x1558) and its augment (+0x1560) (0x343AB0, 0x343BD0). The printed value comes from the `itemcostformulas_*` equations at item load; we read the result, not the equations.
- `Item::AreRequirementsMet` (0x326930): `level >= E(L, LevelReqReduction (54))` or L = 0.
- `ItemEquipment::AreRequirementsMet` (0x343C30): that, then per Physique / Cunning / Spirit (char attribute 1 / 2 / 3): `total >= E(R, red)` or R = 0, with `red` = the totals of the item's three reduction types (+0xDF8.. per attribute, the first always, the others when non-zero; e.g. Weapon / Melee / Shield / Armor / Jewelry `*ReqReduction`) + `GlobalReqReduction` (53) when > 0.
- `E(r, x) = trunc(r − r·x·0.01 + 0.5)` (float; 0.01 at 0x7A7FC8, 0.5 at 0x7A8024).
- Tooltip: the candidate's check uses the engine's call (`met`); core's formula only names what is missing and a mismatch is a model warning. After a swap, the formula runs on the predicted sheet for every other equipped item that met its requirements before; one that fails is shown red and taken off (scored as removed), which can break another (at most 4 rounds).
- Unverified: whether the engine checks the candidate with the old item still worn (assumed: yes, the current sheet).

## 8. Difficulty resistance penalty — DB, folded into the engine totals

`records/game/balancingadjustment_mp+difficulty_players01.dbr` (12-entry arrays, docs/RECON.md:115): Fire / Cold / Lightning / Pierce / Poison −25 from entry 5, −50 from entry 9; Aether / Bleeding / Chaos / Vitality −25 from entry 9 (3 difficulties × 4 entries; which axis the 4 is: unverified). The snapshot's `defense` totals already include it: in the lvl96 Ultimate fixtures (`lvl96-s4-before-ring-c031`, `lvl96-s5-after-ring-c031`) every base resist total = Σ item + skill contributions − 50 (the first five) or − 25 (the other four), exactly; in `lvl16-wp-before-belt` (Normal) the gap is 0. The model swaps item contributions on those totals and never adds a penalty, so nothing is counted twice. Elite (−25 / 0 by the record) is not in any fixture yet.

## 9. Incoming damage: one pipeline — engine (Game.dll 1.3.1.1), tail from the guide

`CombatManager::TakeAttack` (0x11D950) on the defender, in this order. Survivability (`score.cpp survival()`) follows it stage by stage.

| # | Stage | Engine | In the score |
|---|---|---|---|
| 0 | Attacker's damage reduction debuff (type 28 "x% reduced target's damage") | on the attacker when the attack is built (`CombatManager::ContributeTotalDamageReductionPercent` 0x120940; where it is applied is not traced), before any defense; [guide](https://www.grimdawn.com/guide/character/combat/): only the strongest applies | × (1 − strongest item debuff × `damage_reduction_uptime`). **shortcut:** items only (skill / devotion procs such as Yugol are not read); uptime is a preset number |
| 1 | Fumble, dodge, deflect | `CanIDodgeAttack` 0x11D750 (melee), `CanIDeflectProjectile` 0x11D850 (ranged), called at 0x11E07C / 0x11E4BC | × (1 − (`melee_share` × dodge + (1 − `melee_share`) × deflect)) |
| 2 | OA vs DA | `DesignerCalculateProbabilityToHit` 0x121790 (clamped at `pthMinimum` 55), `CalculateDamageModifier` 0x120B10: roll x ∈ [0, max(100, PTH)]; x > PTH misses; PTH ≤ 70: damage × `normalPTHEquation` (PTH / 70); else the modifier of the highest threshold below x (90 → 1.1, 105 → 1.2, 120 → 1.3, 130 → 1.4, 135 → 1.5, `combatformulas.dbr`) | `formulas::expected_hit`: misses, glancing hits and enemy crits by tier. Enemy OA: monster table. **shortcut:** monster crit damage bonus 0 |
| 3 | Crit damage | a modifier above 1 gets the attacker's crit bonus added (0x11ECBD), then `ProcessBluntDamageModifier` | my attacks: bonus on every tier |
| 4 | Shield block | 0x11EDAC: only while the recovery timer is 0; chance `meleeBlockEquation` = shield chance + type 55; `DamageReductionByShield` 0x1197A0 → `shieldDamageReductionEquationDGB / DLEB`; timer = recovery × (1 − char 25 %) | × (1 − blocked share), `utility::block` |
| 5 | % reduced damage from monster types | `ReduceAbsoluteDamage` 0x11EF5E ("Racial Bonus Percent Defense") | **not read** (`racialBonusPercentDefense`, `docs/COVERAGE.md`) |
| 6 | Resists | `ExecuteDefense` 0x11AC40 → `CombatAttributeAbsDefense::Execute` 0x115470 → the damage entry's `ReduceDamage`: × (1 − resist / 100) at once; RR through `ProcessDefense` 0x11AB40 | per threat type, capped, under `resist_reduction` |
| 7 | Armor | `CombatAttributeDefense_AbsorptionProtection::Execute` 0x118400 only **adds** its armor and absorption × armor to the physical entry (types 38 / 40), and only for the hit region (`PickRegion` 0x120D90) or a region-less entry. `CombatAttributeDamage_BasePhysical::Execute` 0x117260 then calls `DesignerCalculatePhysicalDamageDefense` 0x121430 on the damage the resist left: `physicalDamageDefenseEquationDGP` (armor × (1 − A)) + (D − armor) above the armor, `DLEP` D × (1 − A) below | physical: Σ over regions chance × (d − A × min(d, armor of the region)), d = hit × (1 − physical resist). Hit size: monster table |
| 8 | Flat reduced damage from monster types, % absorption, flat absorption | after `ExecuteDefense` (`ApplyDamage` 0x11D340, not traced further); order from the [guide](https://www.grimdawn.com/guide/character/combat/) | **not scored** (no such stat in the snapshot totals; absorption skills are conditional) |

**Armor before or after the physical resist?** After. `BasePhysical::ReduceDamage` (0x1175F0) multiplies the stored damage by (1 − resist) the moment the resist entry executes and merely accumulates armor; the armor equation runs later, in `Execute`, on the reduced damage. This matches the official guide's list (Resistances, then Armor) and contradicts the 2016 forum reading (armor first, [Order of defense seems inappropriate](https://forums.crateentertainment.com/t/order-of-defense-seems-inappropriate/34474)). `tests/test_score.cpp` "armor absorbs…".

**Enemy model** (`gdc::enemy`, `re/EnemyByLevel.tsv`, `re/tools/enemy_table.py`): median Common / Champion / Hero `Monster` record (2529 records) at the character's level. OA / DA: `combatformulas.dbr` ability equations on the monster's bio record (`characterAttributeEquations`), e.g. OA 500 / DA 413 at level 15, 2472 / 2283 at 100. Hit: the largest flat direct damage of one of its skills × the attribute factor (physical × (dex / 245 + 1), magical × (int / 215 + 1)): 298 (105 physical) at 15, 5337 (1907) at 100. **Not included:** weapon damage, % damage bonuses, the `charLevel` offset between area and player, difficulty adjustments, bosses. **unverified:** that the ability equations apply to monsters as to players (TakeAttack calls `DesignerCalculateDefensiveAbility` on whoever defends; the attacker's OA arrives precomputed in `ParametersCombat+0x14`).

**Crit for my damage** (§5): the same `expected_hit` against the table's DA. PTH past 135 only adds 1.5× crits; +crit damage adds to every tier above 1.0.

**Contributions** (`score::Result::contributions`, Shift line `top:`): every factor of Survivability has its own ln ratio (Life, DA, dodge / deflect, block, enemy damage reduction, each threshold); the damage-taken sum is split over the damage types, Damage over the main skills and procs, Other over its keys, each in proportion to its linear change. × the term's exponent × 100 = points of the total; the parts add up to ln(1 + total).

**Coverage:** `docs/COVERAGE.md` lists every item stat field and what the score does with it.
