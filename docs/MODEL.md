# Swap stat model (`gdc::model`, Slice A)

`model::swap(snapshot, candidate, location, db)` removes the old item's own getter output, adds the candidate's, and re-derives the dependent numbers. `model::sheet()` reads the compared numbers from a real or a predicted snapshot. Item output already includes components, affixes and stat rolls (`docs/SNAPSHOT.md`).

## Rules and evidence

All checks below are within float rounding: fixture pair `lvl96-before/after-ring-swap` (finger 2: `c031_ring` → `b201e_ring`) and pair `lvl96-phase21-axe-hover` → `lvl96-swap-poison-ring-chest` (finger 2 + chest).

| Stat | Rule | Source / check |
|---|---|---|
| char attributes | `total = flat × (1 + modifier%)`, so flat = total / (1 + mod%); swap adds the items' `value` to flat and their `modifier` to mod | both pairs, types 1–7, 14, 21, 23 |
| Life / Energy from attributes | flat Life += 2.5 / Physique, 1 / Cunning, 1.5 / Spirit; flat Energy += 2 / Spirit, on attribute **totals** | `playerlevels.dbr` lifeIncrement 20, lifeIncrementDexterity 8, lifeIncrementIntelligence 12, manaIncrement 16 per 8 points. Spirit +12.1 checked; Physique/Cunning ratios not yet exercised by a fixture |
| regen, OA, DA | `formulas::*` on the predicted totals | `combat_formulas.h` |
| resists | raw = Σ; shown = min(raw + \|raw\|·mod%, defense_cap + cap_bonus) for types with a `DefenseAttributeDefenseCap_*` class | `CombatAttributeAbsDefense::Process` 0x115370; Poison 105 → 91, Disruption/Stun 80 |
| Absorption (39) | mean of positive per-entry absorption; modifiers sum | `GetTotalDefenseType` 0x11A90E..0x11A954; 70 / 70 → 70 |
| armor (sheet) | (Σ armor-slot item armor × region % + waist armor) × (1 + Protection mod%) | region % from `combatformulas.dbr`; 1407.78 vs sheet 1408. Waist term is a fit to that single value |
| run speed | type 14 total | cap (`Player::CapRunSpeed`) not modeled |
| item skill grants | Δ augmented level per skill: `augmentSkillName*` by record (`SetSkillLevelAugment` 0x5375B0), `augmentMasteryName*` to the mastery skill and every skill with that `mastery_id` (`SetMasteryAugment` 0x5373D0), `augmentAllLevel` to all (`SetAllSkillAugment` 0x537230; its one exclusion not traced → warning). Learned skills only: current = min(level + augmented, max(skillUltimateLevel, skillMaxLevel)) (`GetCurrentLevel` 0x4909C0, `SkillProfile::LoadProfile` 0x545A27). The skill's stats at the old and new level come from its record (per-level arrays at level − 1, one value = every level), mapped by `gdc::fields` (sources in `db_fields.h`). Only skills that contribute now: `skills[].passive` non-empty (schema 4), and that capture must equal the record at the current level or the skill is skipped with a warning. Schema < 4: `Skill_Passive` and `Skill_BuffSelfToggled` assumed on (warning) | ring pair: Possession 21 → 19, Chaos 34 → 32. Poison pair: `wpattack0` 5 → 2, Protection mod 9 → 5, sheet armor 1386 exact; Blood of Dreeg (`Skill_BuffRadius`, buff not running) 12 → 9 leaves OA unchanged |
| skill attachments | A skill's engine passive = its record at the current level + its **learned modifiers** (`skills[].modifier_ids`, Skill+0x140, schema 5) at their current levels + its **item skill modifiers** (`modifierSkillName<n>` / `modifiedSkillName<n>` on the item record, one `Skill_Modifier` record of one level each). The engine check above compares against that sum. A swap adds/removes the items' item skill modifiers (∓ the modifier record's mapped fields) and moves `item_skill_modifiers` for the slot. A learned modifier's level change applies its record delta while its parent contributes, and updates the parent's passive. A linked non-`Skill_Modifier` (transmuter) with mapped stats → warning (`Skill::AddModifierDefenseAttributes` 0x494970 adds only `Skill_Modifier` objects; transmuter class relation not traced) | Possession's AS mod 16 = two dual-wielded b013e swords (`sword1h_b013_possession` 8 each); Witchfire reserve 201 / Life 28 = witchfire1@17 (155) + witchfire2@15 (46, Life 28) |
| sets | piece count per `GetItemSetName` before/after | — |

## Warnings ("model inaccurate")

- **Skill level changes not modeled**, per skill: a `Skill_Modifier` whose parent is unknown (schema < 5) or missing, a changed `defensive*` / `character*` field outside `gdc::fields`, a level past the record's array, engine values that differ from the record, a current level that does not follow from level + bonus.
- **Skill activity not in snapshot** (schema < 4): passives and toggles assumed on.
- **All-skills grant changes.**
- **Max resist changes:** `defensive*MaxResist` fields differ. These are `CombatAttributeDefenseCap` entries, which item stats do not show.
- **Set bonuses change:** any piece count changes.
- **No database:** grants and max resist are not checked.

## Calibration

`gdc_calibrate [DIR] [--char NAME] [--verbose] | --pair A B` (`tools/calibrate`) turns every equipment-only change between two consecutive auto-snapshot records (`store::read`) into a swap test: each changed slot (id, record, component, augment) is swapped in order, then sheet values and each resist's raw / shown are compared with the actual record (integers ±1, resists and regen ±0.1, run speed ±0.001). Pairs with a changed level, difficulty, spent points, an emptied slot or a partial capture are skipped. Exit 0 all pass, 1 a pair fails, 2 usage / read error. Store 20261114-123319-910: ring (b201e → c031) and chest (a11_torso004 → b002e) PASS; fixtures `lvl96-s4-*`.

## Not modeled yet

- Requirements: an item with unmet requirements, or a swap that breaks another item's requirements (`itemcostformulas_*`).
- Conditional stats; skill stats other than through item skill grants (buff durations, procs).
- Run speed cap.
- Rings: the caller picks the finger.
- Unequipping (an emptied slot): calibration skips it.
