# ERRATA — `plague-garden.md` and `priorities.md` vs the game DB

Checked 2026-11-14 against the installed v1.3.1.1 DB: `database.arz` → `gdx1` → `gdx2` → `gdx3` merged in override order (82,744 records), English tags from the four `Text_EN.arc`. Reader: `re/tools/arz.py`, `re/tools/arc.py`. Record paths below are relative to the DB root.

Legend: **✔** DB confirms · **✘** DB contradicts · **~** partly right / needs a caveat · **?** engine rule, not in the DB (needs a cited source or an in-game test).

## 0. DB naming used below
Legacy field names, mapped through the UI tags in `Text_EN` (`DamagePoison` = "Acid Damage", `DamageDurationPoison` = "Poison Damage", `DamageLife` = "Vitality Damage", `DefensePoison` = "Poison & Acid Resistance"):

| DB field family | Game term |
|---|---|
| `offensivePoison*` | Acid |
| `offensiveSlowPoison*` | Poison (DoT) |
| `offensiveLife*` / `offensiveSlowLife*` | Vitality / Vitality Decay |
| `defensivePoison` | Poison & Acid resistance (one resist for both; "Acid RR" = "Poison RR") |
| `defensiveX` < 0 on a debuff | "−X% resistance" (RR category 1) |
| `offensiveTotalResistanceReductionPercent` | "% Reduced target's Resistances" (category 2) |
| `offensiveTotalResistanceReductionAbsolute` | "Reduced target's Resistances" flat (category 3) |
| Intelligence / Dexterity / Strength | Spirit / Cunning / Physique |

Whether `offensiveSlow*Min` is per second (tooltip shows total over the duration) is **?**. The guide's numbers (e.g. Bloom "1000 Poison / 5s" = 200 × 5) are consistent with per second. Phase 3 confirms with a tooltip read.

## 1. Engine mechanics the guide asserts (not checkable in the DB)
| Claim | Status | Plan |
|---|---|---|
| DoTs snapshot RR and crit at application ("cast Curse of Frailty first") | **community** | `docs/MECHANICS.md` §3. Per-tick RR re-check for Bloody Pox-type debuffs: single claim, unverified. |
| RR stacking: category 1 additive, category 2 highest-only, category 3 highest-only | **community** (the guide says category 2 stacks) | `docs/MECHANICS.md` §1. |
| Skill-local conversion (item skill modifiers) applies before global conversion | **community** | `docs/MECHANICS.md` §2. Same-level sum > 100 % unverified → calibrate on the skill tooltip. Phase 3 swap test with a conversion item. |
| DoTs can crit (OA vs DA) and crit damage applies to the whole DoT | **community** | `docs/MECHANICS.md` §3. |
| Bleeding cannot be converted | **✔ (DB side)** | No record in the merged DB uses Bleeding as `conversionInType`/`OutType`; the template enum is `Physical;Pierce;Elemental;Cold;Fire;Poison;Lightning;Life;Chaos;Aether;Stun` (`Poison` = Acid, the DoT moves with it). |
| A conversion also moves the type's DoT (Vitality→Acid takes Vitality Decay to Poison, Physical→X Internal Trauma) | **✔ engine** | `ConvertDamage` 0x119ED0 matches entries by type, direct and DoT; a DoT is not moved to Pierce / Chaos / Aether. `docs/MECHANICS.md` §2. |

## 2. Skills (`records/skills/playerclass03` = Occultist, `playerclass06` = Shaman; `skillMaxLevel` / `skillUltimateLevel`)
| Claim | Status | Evidence |
|---|---|---|
| Vulnerability "10/10 (hard cap!)" | **✘** | `playerclass03/curse2.dbr`: max 10, ultimate 20. The guide's own gear adds +3 (Grava'Thul's Leggings) and +2 (awakened Ring of Eternal Rot). |
| Dreeg's Evil Eye 16/16 | **✘** | `evileye1.dbr`: max 12, ultimate 22. |
| Grasping Vines 16/16 | **✘** | `graspingvines1.dbr`: max 12, ultimate 22. |
| Curse of Frailty 10, Bloody Pox 12, Wasting 12, Black Death 12 (22 ult), Blood of Dreeg 16, Aspect of the Guardian 12, Possession 12, Entangling Vines 12, Mogdrogen's Pact 12, Heart of the Wild 10, Oak Skin 10, Wendigo Totem 12, Devouring Swarm 16 | **✔** | respective records |
| Vulnerability is the main Acid/Poison RR + DA shred | **✔** | `curse2.dbr`: `defensivePoison` −3 … −25 (lvl 10) … −35 (lvl 20); same for Elemental/Vitality/Chaos; DA −15 … −300. |
| Curse of Frailty "cuts resistances" | **~** | `curse1_buff.dbr` lowers only Physical (−5…−35) and Bleeding resist; nothing for Acid/Poison. Its value here is Vulnerability (modifier) and the Rumor binding. |
| Fevered Rage gives bosses +150% Total Speed | **~** | `pox1b.dbr`: `characterTotalSpeedModifier` 150, OA +25%, DA −25%, damage ×1.6, 13–19% current-life. Pox is a debuff (`pox1_buff.dbr`, `SkillBuff_Contageous`), so it lands on enemies; check the tooltip in-game. |
| Brute Force only works with a two-hander | **✔** | `passive01.dbr` weapon flags: Axe2h, Mace2h, Ranged2h, Spear2h, Sword2h. It gives Physical/Lightning, nothing for Acid/Poison. |
| Possession: damage absorption, cast-interrupt protection | **✔** | `possession1.dbr`: `damageAbsorptionPercent` 3…25, `defensiveDisruption` 100. |
| Devouring Swarm 16/16 by level 10 | **✔ feasible** | `playerlevels.dbr` gives 3 skill points per level (27 by L10, before quest rewards). DS is tier 1 (mastery 1); Grasping Vines is tier 2 (mastery 5 per `skillMasteryTierLevel`). 5 + 16 + 1 = 22 ≤ 27. |
| Grasping Vines damage types | — | `graspingvines1.dbr`: Physical + Bleeding DoT only; **no Vitality**. Base cooldown 1.2 s. |

## 3. Worldkeeper's Garden (`records/items/lootsets/itemset_d313.dbr`)
Members: Gaze `gearhead/d316_head`, Thicket `gearshoulders/d318_shoulder`, Vines `geartorso/d318_torso`, Heart `gearaccessories/medals/d316_medal`. Arrays are indexed by piece count (index 0 = 1 piece).

| Tier | Bonus (DB) |
|---|---|
| 2 | +120% Acid |
| 3 | +22% HP, +100% HP regen, +150% Poison duration, +200% Poison, +3 Black Death |
| 4 | 50% Vitality→Acid (global), +100 Poison (5 s duration), +3 Cadence (Soldier), +3 Entangling Vines, **Conjure Living Blossom**; set skill modifiers for Bloody Pox / Grasping Vines / Cadence (`itemSkillModifierControl` = [0,0,0,1], so presumably 4pc; **?**) |

| Claim | Status | Evidence |
|---|---|---|
| Set gives +150% Poison duration, +3 Entangling Vines, +3 Black Death | **✔** | above |
| Grasping Vines CD 1 s from the set | **✔** | `set_d313_graspingvines.dbr`: `skillCooldownTime` 1.0, damage ×+150%, mana −40%. |
| Bloody Pox 4 s, "+580 Poison/s from set + medal" | **✔** | `set_d313_bloodypox.dbr` 400 + `medal_d316_bloodypox.dbr` 180; duration 4; each also −180 / −90 DA. |
| Bloody Pox 100% Vitality→Acid from the medal | **✔** | `medal_d316_bloodypox.dbr` |
| Grasping Vines 100% Physical→Acid from Gaze **and** Mythical Bramblevine | **✔, redundant** | `head_d316_graspingvines.dbr` and `shield_d203_graspingvines.dbr` both 100%. Normalisation of >100% is a Phase 3 question. |
| "Global Vitality→Acid on set pieces" | **~** | Thicket 30% + Vines 30% + 4pc 50% = 110% Life→Acid global, plus Gaze 30% Physical→Acid global. Not "100% on everything" per piece. |
| Set is an Occultist + Shaman set | **~** | Gaze grants +1 Soldier, +1 Occultist, +1 Shaman; the set also supports Soldier's Cadence. Irrelevant for this build, but Auto must not read it as a Soldier signal. |

## 4. Other gear
| Claim | Status | Evidence |
|---|---|---|
| Conduit of Wild Whispers: Grasping Vines 100% Vitality→Acid | **✘** (re-checked 2026-11-15) | `necklaces/d113f_necklace.dbr` (GDX1 only; `itemNameTag` "Conduit of Wild Whispers", its `FileDescription` says "Eldritch" — a stale comment): +1 Shaman, +2 Heart of the Wild (`natureblessing1`, a `SkillBuff_Passive`: regen, energy, physical, pet bonus). No skill modifier, no conversion. Grasping Vines has no Vitality to convert anyway. |
| Mythical Bramblevine: 5% ADCtH on Vines, 15% ADCtH + 2 s CD on Evil Eye | **✔** | `shields/d203_shield.dbr` (style tag = "Mythical"); `shield_d203_evileye.dbr` also has 100% Vitality→Acid and 60% retaliation added. Shield also 15% Physical→Acid global. |
| Dung Divider level 101, wearable at 100 with Fewmet Grips | **✔ / ?** | `gearweapons/caster/c307_dagger.dbr` (both versions) `levelRequirement` 101; `maxPlayerLevel` 100. Grips: `characterLevelReqReduction` 1 = "−1 Player Level Requirement for **Items**" (generic, not DD-only). How the engine applies it (all equipped items? itself?) is **?** — deferred until the user owns the Grips. |
| Dung Divider 396 Cunning / 495 Spirit; Crane −10% Spirit | **✔** | `itemcostformulas_caster_legend.dbr` `daggerIntelligenceEquation` at itemLevel 94 = 494.5; `daggerDexterityEquation` = 0.8× = 395.6. Rounding rule **?**. Crane star 3: `characterWeaponIntelligenceReqReduction` 10 → 445. |
| Fewmet Grips: 2pc +120% Acid with Dung Divider | **~** | Only the **awakened Legendary** versions are set pieces: `awakened/lootsets/itemset_d300.dbr` "Armorum Excrementis", 5 pieces (`awakened/gearhead/c126_head`, `gearlegs/c019_legs`, `gearhands/c207_hands`, `gearweapons/focus/c208_focus`, `gearweapons/caster/c307_dagger`). 2pc +120% Acid **and** +120% Vitality; 3pc 100% Physical→Acid + 100% Fire→Acid; 4pc +200% Poison/Vitality Decay, +100% their durations; 5pc +1 all skills. The Epic Grips/Dagger have no set. |
| Widow's Bite: −Acid RR | **✔** | `itemskillsgdx3/legendary/widowbite_buff.dbr`: −8/−10% Poison & Acid, −10/−12% Vitality, −8/−10% Pierce, 5 s (category 1). **Not mentioned:** the ring carries 25% **Chaos→Vitality** global (`rings/d303_ring`, `d308_ring`). Converted damage does not re-convert (`docs/MECHANICS.md` §2, community), so this is not a chain: the converted Chaos stays Vitality and scores only through Vitality bonuses (≈0 for an Acid/Poison target). Its type-1 RR counts normally. |
| Awakened Ring of Eternal Rot: +Vulnerability, Poison DoT, % RR | **✔** | `awakened/gearaccessories/rings/c053_ring.dbr`: +2 Vulnerability, +100% Poison duration; proc `poisondotnova_01.dbr` 300 Poison/5 s + **18%** category-2 RR. |
| Bloom 26% (cat. 2) and Eternal Rot both count ("doesn't take the strongest") | **✘ if cat. 2 is highest-only** | Same field `offensiveTotalResistanceReductionPercent` (Bloom 26, ring 18). Under the community rule (`docs/MECHANICS.md` §1) only the ring's 18% is reduced, to (1 − Bloom uptime) × 18% (`docs/MECHANICS.md` §4); its other stats count. |
| Grava'Thul's Leggings: +Vulnerability, DA shred, resists | **~** | `gearlegs/b103e_legs.dbr` (Rare/MI): +3 Vulnerability, +42 DA, 32% elemental. No DA shred on the item. |
| Groble Vile Effigy: Vitality→Acid for Bloody Pox, −10% Poison RR | **✔** | `itemskillsgdx2/skillmodifiers/mi/focus_b202_bloodypox.dbr`: 100% Vitality→Acid, `defensivePoison` −10. Checked on `b202f` (level 94); the low-level variants `b202a–e` were not checked. |
| Deathstalker relic: extra −Acid RR aura | **✔** | Pet aura `petskill_deathstalker_auraofdarkness_buff.dbr`: −10% Poison & Acid, −8% Physical, −10% Bleeding. |
| Symbol of Solael: Physical→Chaos | **✔** | `materia/compb_symbolofsolael.dbr` 10%. |
| Titan Plating 33% reflect reduction, +4% Physique | **✔** | `materia/compb_titanplating.dbr` |

## 5. Pets
| Claim | Status | Evidence |
|---|---|---|
| Living Bloom (helm): up to 4, 200 Acid + 1000 Poison/5 s, 26% Reduced | **✔** | `conjurelivingbud.dbr` petLimit 4, TTL 12 s, 20% on attack (`d316_head`); attack `petskill_livingplant_poisonbloom.dbr` 200 Acid, 200 Poison × 5 s, 26% cat. 2 for 5 s, 15% fumble. |
| Living Blossom (4pc): up to 6, 200–350 Acid + 1500 Poison/5 s, 28 flat RR 5 s | **✔** | `conjurelivingbloom.dbr` petLimit 6, TTL 24 s; `petskill_livingplant_venomousseed.dbr` 200–350 Acid, 300 Poison × 5 s, 28 cat. 3 for 5 s. |
| Bloom and Blossom scale with the player, pet bonuses do nothing for them | **✔ engine** | Spawned records `itempet_livingplantbulb_01` / `itempet_livingplantbloom_01`: `Class = PetPlayerScaling`. No pet bonus, the player's combat parameters (`docs/MECHANICS.md` §5). |
| Blossom 28 flat makes Manticore unnecessary | **✔ if cat. 3 is highest-only** | Acid Spray `tier2_18f_skill.dbr`: flat RR 10 (lvl 1) … 28 (lvl 20), same field. |

## 6. Devotions (`records/ui/skills/devotion/constellations/*.dbr`, star skills `records/skills/devotion/*`)
| Claim | Status | Evidence |
|---|---|---|
| Affinity requirements/rewards of every constellation on the route; 55 points total | **✔** | Arithmetic of steps 1–12 matches the constellation records; `maxDevotionPoints` 55 (`playerlevels.dbr`, gdx2). |
| Step 13: Crossroads (Order) can be refunded because Crane "self-supports" (O5 ≥ O1) | **? / inconsistent** | Step 10 justifies refunding Crossroads (Chaos) with affinity from *other* constellations. Step 13 relies on Crane counting its own reward. The refund rule is not in the DB. If self-support is not allowed, the route needs 56 points. **Not modelled**: the adapter reads actual devotions. |
| Final affinities C9 · E23 · P6 · O5 | **~** | True only for last-two-points option A (Crossroads Chaos + Eldritch). Option B (two single stars) gives no affinity: the DB stores affinity per constellation, not per star. |
| Option B: Ghoul star 1 = +4% ADCtH, Jackal star 1 = +15% all damage | **✘** | Ghoul star 1 (`tier1_25a`) = +8 DA, +15 Physique (ADCtH 5% is star 4). Jackal star 1 (`tier1_38a`) = +10% energy regen (+15% damage is star 3). |
| Rumor (Murmur) −30% Acid RR | **~** | `tier2_32d_skill_buff.dbr` `defensivePoison` −6 (lvl 1) … −30 (lvl 20, last entry of a 20-level XP table). Also −5…−23% Cold resist, −16…−96 OA. Value depends on the power's level. |
| Murmur + Yugol "both mandatory for −30% Acid RR" | **✘** | Yugol has no RR. The −30% is Rumor alone. Yugol (`tier3_21a–f`): +50 OA ✔, +180% Acid ✔, +300 HP ✔, +25% Vitality res ✔, 10% reflect ✔, 8% ADCtH ✔. Black Blood aura: −6% (lvl 1) … −20% (lvl 15) enemy damage, flat Cold + Acid (not Poison). |
| Crane 15% reflect reduction; total 15 + 10 + 33 = 58% | **✘** | Crane star 5 (`tier1_31e`) `defensivePercentReflectionResistance` = **22**. Total = 22 + 10 + 33 = **65%** if additive (stacking **?**). |
| Rat +30% Poison duration, Poison res | **✔** | `tier1_17d` 30%, `tier1_17c` 10% res. |
| Poison duration "set 150% + Rat 30%" | **~** | Akeron's Scorpion star 4 (`tier1_02d`) adds another 30%. Total on the route: 150 + 30 + 30 = 210% (+ ring 100% with Eternal Rot). |
| Abomination poison branch `1-2-3-4a-5a-6a` (6 points) → Tainted Eruption | **✔** | `constellation67` has 8 stars and two branches. Stars 1-2-3-6-7-8 end in Tainted Eruption (`tier3_02h_skill`: 130…472 Poison / 5 s, −2…−16% enemy damage, CD 3 s, 15% on attack). The other branch (stars 4-5) ends in **Abominable Might: 20–50% Physical→Chaos** → red warning in this preset. |
| Scorpion Sting: Poison proc + −DA | **✔** | `tier1_02e_skill`: −50…−150 DA for 5 s, Poison 25…225 / 5 s, 25% on attack. |
| Twin Fangs gets converted to Acid by the set | **~** | `tier1_01e_skill`: Vitality + **Pierce** + lifesteal. Only the Vitality part is affected by the set's global Vitality→Acid. |
| Sailor's Guide: Physical res, slow/freeze res, move speed | **✔** | `tier1_07a–d`: 3% Physical, 18% slow, 18% freeze, +10% move. |
| Crossroads Chaos +5% HP, Crossroads Eldritch +18 OA | **✔** | `tier1_15b`, `tier1_15d` |
| Giant's Blood (Behemoth) | **✔** | `tier2_28f_skill`: heal + regen buff, 15% on any hit, CD 25 s. |

## 7. `priorities.md`
| Claim | Status | Note |
|---|---|---|
| Resist cap 80% | **✔** | `gameengine.dbr` `playerDefenseCap` [80,80,80]. |
| "Armor absorption toward 100%" | **?** | `gameengine.dbr` `armorDefensiveAbsorption` = 70 (base). How item/skill bonuses raise it is Phase 3. |
| Reflect reduction 58% (Crane 15) | **✘** | See §6: 65% with the same sources. |
| Poison duration "+200%: set 150 + Rat 30" | **~** | See §6 (Scorpion +30). |
| "Bleeding never converts, skip +Bleeding%" | **✔ / ~** | No Bleed conversion exists (§1). But Grasping Vines and Deathstalker deal real Bleeding, and Bramblevine gives +% Bleeding, so the model scores it from the math, not as a hard zero. |
| Numeric HP/OA/DA/armor targets | intent only | Taken as preset thresholds (level 94+ stage); not mechanics. |
| Component/augment list | not checked | Out of scope until Phase 5 scoring needs it. |

## 8. Verified record map for the `plague-garden` preset (Phase 5 input)
| Role | Record |
|---|---|
| Grasping Vines | `records/skills/playerclass06/graspingvines1.dbr` |
| Entangling Vines | `records/skills/playerclass06/graspingvines2.dbr` |
| Devouring Swarm (stage 1–10) | `records/skills/playerclass06/devouringswarm1.dbr` |
| Bloody Pox / Black Death / Wasting / Fevered Rage | `records/skills/playerclass03/pox1.dbr` / `pox3.dbr` / `pox2.dbr` / `pox1b.dbr` |
| Dreeg's Evil Eye / Focused Gaze | `records/skills/playerclass03/evileye1.dbr` / `evileye1b.dbr` |
| Curse of Frailty / Vulnerability | `records/skills/playerclass03/curse1.dbr` / `curse2.dbr` |
| Possession | `records/skills/playerclass03/possession1.dbr` |
| Living Bloom / Living Blossom (pets) | `records/skills/itemskillsgdx3/pets/itempet_livingplantbulb_01.dbr` / `itempet_livingplantbloom_01.dbr` |
| Scorpion Sting / Tainted Eruption / Twin Fangs | `records/skills/devotion/tier1_02e_skill.dbr` / `tier3_02h_skill.dbr` / `tier1_01e_skill.dbr` |
| Noxious Fumes (Dung Divider / Grips proc) | `records/skills/itemskillsgdx3/noxiousfumeslegendary_01.dbr` (awakened), `itemskillsgdx2/noxiousfumes_01.dbr` (epic) — contents not checked |
| Target set | `records/items/lootsets/itemset_d313.dbr` (Worldkeeper's Garden) |
