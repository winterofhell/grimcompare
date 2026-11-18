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

## 2. Conversion — community, order and sums from the engine

1. **Same level, additive:** skill transmuter conversion + item skill modifier conversion for the same skill, summed per `in → out` pair.
2. **Global, multiplicative:** gear/devotion conversion (`conversionInType`/`conversionOutType`) applies to the remainder that level 1 left unconverted.
3. Converted damage is never converted again. Consequence: Widow's Bite 25 % Chaos→Vitality + a Vitality→Acid conversion does **not** move the converted Vitality on to Acid; only original Vitality converts. This overrides the "chained conversion" reading in ERRATA §4.
4. Source types convert in the order they first appear, not Elemental first (**engine**, traced 2026-11-16, next paragraph; the community reading "Elemental before Fire / Cold / Lightning" holds only when the Elemental conversion comes first).
5. Skill-rank % bonuses (the skill's own % damage) apply before conversion, to the original type. Gear/sheet % bonuses apply to the final type.

**Sum > 100 % — engine:** `ConversionAttributeAccumulator::Weigh` 0x17D520 scales every pair of a source type by 100 / total, per level (the skill's, then the character's); the total includes DoT pairs that `ConvertDamage` then skips, so those keep their weighed share unmoved. The tooltip notes `conversion > 100% (weighed)`.

**Order and chaining — engine (Skill::CollectCombatParameters 0x499E40):** the skill's own conversions first (0x49A3B7), then the character's (0x49A5D7). Entries a conversion creates are flagged (byte +0xF, 0x119E44) and never converted again, in either level, which confirms rule 3: a Chaos→Fire item does nothing on a build whose Chaos all comes from conversions (`tests/test_templates.cpp`). Elemental damage is stored as a third each of Cold / Fire / Lightning (0x7A8010). **Rule 4 — engine:** `ConversionAttributeAccumulator::AddAttribute` 0x17D890 appends one `Data` per new source type and adds a known type's pairs to its `Data`; `Weigh` 0x17D520 only scales; `Apply` 0x17DB20 calls `ConvertDamage` 0x119ED0 once per `Data`, in that order. In each call every pair of the type converts its % of the same entry amount, then the entry keeps the remainder. So an Elemental → Chaos 50% seen before a Fire → Vitality 50% turns a Fire entry into 50 Chaos, 25 Vitality, 25 Fire; the other order gives 50 Vitality, 25 Chaos, 25 Fire. The character level's order is the one `SkillManager::CollectAvailableConversionAttributes` 0x536400 builds (skills, then `Character::GetEquipConversionAttributes`, then vt+0x708; `Skill::CollectCombatParameters` 0x49A588 calls the same three), which the snapshot's `conversions` list keeps. The skill level: `Skill::CollectLocalConversionAttributes` 0x496F00 adds its own profile pairs in field order (0x17DF90), then a tail call that adds the modifiers' (not traced further; the model takes the skill's record, then its modifiers in `modifier_ids` order). `core/src/score.cpp` `convert`, `tests/test_score.cpp`. **shortcut:** a swap appends the candidate's new source types at the end of the list (`model::swap`); the engine's order puts skills before equipment slots, so a swap that adds a source type the skills also carry can be ordered differently.

**Warnings:** a gear conversion is flagged (red line) when its pair is in `conversion_warn`, the build deals the source type itself (≥ 10% of the main skills' damage with the gear conversions taken out: converted damage is not converted again) and the target is not a `[match]` damage type of the preset (no `[match]` types: not one the build already deals).

Cheap check: skill tooltip before/after equipping a conversion item (Phase 3 swap test "conversion item").

**Engine (Game.dll 1.3.1.1, `CombatAttributeAccumulator::ConvertDamage` 0x119ED0), 0.9.5:** a conversion matches every damage entry of its source type (vt+0x10), direct and DoT alike, so Vitality→Acid also moves Vitality Decay to Poison, Physical→X moves Internal Trauma. Physical also matches BasePhysical (type 1); Elemental (58) also matches Cold / Fire / Lightning entries (types 5, 6, 8). A DoT entry (`DamageType` vt+0x20 = 2) is left alone when the target is Pierce, Chaos or Aether (4, 10, 11: no DoT form). Remaining = 100 − Σ % (const 100.0 at 0x7A8134). `core/src/score.cpp` `convert`, `tests/test_effects.cpp` (Sigil of Consumption).

## 3. DoT — community, one part unverified

- **+% of a direct type does not boost its DoT — engine.** `CombatAttributeAccumulator::ModifyDamage` (0x1194C0) runs every modifier's `Execute` on every damage entry. `CombatAttributeAbsDamageMod::Execute` (0x115070) calls only the entry's `ModifyAbsoluteDamage` (vt+0x80), `CombatAttributeDurDamageMod::Execute` (0x1151B0) only `ModifyDurationDamage` (vt+0x88). `CombatAttributeDurDamage` has no `ModifyAbsoluteDamage` of its own (the base one, 0x15880, is empty), and `CombatAttributeAbsDamage` none for duration. So +% Acid never reaches Poison, +% Fire never Burn, and so on for Cold / Frostburn, Lightning / Electrocute, Physical / Internal Trauma, Vitality / Vitality Decay. The sheet agrees: `GetTotalDamageModifierType` (0x11A750) sums by type **and** damage kind, and `lvl16-wp-before-belt` shows Acid +59% next to Poison +68%. Community: [wiki Damage Types](https://grimdawn.fandom.com/wiki/Damage_Types) (+% Cold does not affect Frostburn). What both share: the resist on the target, conversion (a converted type takes its DoT along) and +% All Damage (type 60, spread over both kinds). `tests/test_sanity.cpp` cases 8 / 9.

- Crit is rolled at application; a critical DoT stays critical for its full duration.
- RR is snapshotted at application.
- **unverified** (single forum claim): debuffs that tick every second (Bloody Pox etc.) re-check RR on each tick. The steady-state model assumes all RR is up, so this matters only for the "apply RR first" order, which the model does not score.

## 4. Uptime of conditional effects — design choice

Every conditional effect (proc, pet, temporary buff/debuff, skill with a cooldown) has an `uptime` in [0, 1]. One mechanism for all; no per-item special cases.

- **Default, derived where possible:** `uptime = min(1, proc chance × trigger rate × duration)`. Proc chance and duration come from the DB record; trigger rate from the model (attack/cast rate of the triggering skill). For cooldown skills: `min(1, duration / cooldown)`.
- **On being hit:** trigger rate = threat profile `incoming_hits_per_sec` (per-stage defaults in `docs/PRESETS.md`).
- **Pets:** pets alive × attacks per second per pet, estimated from the pet record (below; experimental).
- **Last-resort fallback `0`** (`docs/PRESETS.md`). Never silent: if any effect on the compared items is at fallback, the tooltip shows `+? unscored: <effect>` next to the total.
- **Override:** per preset and per `[[stage]]`, keyed by the effect's DB record path.
- **Highest-only overlap** (RR types 2 and 3, any other highest-only stat): sort sources by value, strongest first. Source k is the active one with probability `p_k = u_k × Π_{j stronger} (1 − u_j)`; nothing is active with `Π (1 − u_j)`. Score the expected damage over these states, not damage at an averaged RR (type 2 is multiplicative, so the two differ). For two sources the weaker one gets weight `u_weak × (1 − u_strong)`, the rule above. **Design assumption: sources fire independently** (unverified; correlated procs, e.g. two on-hit effects from the same attack, would make this wrong).
- **Shift view** shows each assumed uptime and where it came from (`derived` / `preset` / `default ⚠`).

**Implementation (`core/src/effects.cpp`, 2026-11-14).** Sources and the rate each one gets:
- Learned class skills (not Passive / Modifier / Transmuter / Mastery): the debuff they apply (`buffSkillName` when that record is a debuff, else the skill) with their learned modifiers and item skill modifiers. No `skillCooldownTime` → **design choice:** kept up (uptime 1, e.g. Curse of Frailty `curse1.dbr`); else cast on cooldown (**design choice**), `uptime = min(1, duration / recharge)`, summons/s = 1 / recharge.
- **Recharge** (0.9.7) = `skillCooldownTime` × (1 − CDR) for the character's own skills — learned class skills (cast, own-trigger passives) and devotion powers; item and set skills keep `skillCooldownTime`. **Engine:** `Skill::GetCooldownTime` (0x4917E0) applies CDR unless Skill+0x2F9 or +0x2FC is set. +0x2F9 is `Skill::IsItemSkill` (0x30AD10); its only writers are `SkillManager::AddItemSkills` (0x538BC0, writes 0x0101 to +0x2F9/+0x2FA), `Skill::SetAsItemSkill` / `SetAsItemSkillAuto` (no callers in Game.dll), `SkillManager::CreateSkillBuff` (buff replicas) and `SkillManager::CreateAndLoadSkill` when the manager's +0x412 (`SetAsItemSkills`, no callers) is set. +0x2FC is the record's `potionModifier` (`Skill::LoadResources`) and potion skills (`SetAsPotionSkill`, `Skill_ChargePotion`, `Skill_Potion*`). Devotion powers are in the character's own SkillManager and cast through `SkillAutoCastController::AppraiseAndExecute` (0x523440) → `GetCooldownTime` → `Skill::StartCooldown`, so CDR shortens them. Other callers of `GetCooldownTime`: `StartCooldown`, `UpdateCooldownCharges`, `Skill_OnHitAttackRadius::OnHitActivation`, `Skill_OnHitBuffSelf::Update`, `Skill_Shapeshift::ReceiveActiveUpdate`, `SkillActivatedBuffSelf::ApplyCost`, `HotSlotOptionSkill::GetNumberAvailable`. CDR ≥ 100% → kept up. The cooldown is the skill's plus its learned and item skill modifiers' `skillCooldownTime` (`GetCooldownTime` → `GetSkillModifierData`, above; 0.9.12 for procs and summons, the main-skill path already did). Pets' own skills keep their DB cooldown (the pet's attributes, not the character's: not traced).
- Devotion powers with points: `templateAutoCast` on the skill or its `buffSkillName` record (DB: `tier2_32d_skill.dbr` has only `buffSkillName`; the buff record holds the controller and `skillCooldownTime`).
- Items and sets: `itemSkillName` + `itemSkillAutoController`, level `itemSkillLevelEq` (a number, or `itemLevel/4+1` floored with the **base item's** `itemLevel`; in game: Brigandine Armor `a03_torso001b` (itemLevel 11) "of Balanced Steel" shows Ring of Steel 63 Piercing = level 3 (38) × (Cunning 159 / 245 + 1), screenshot 2026-11-14, one item); set skills at the piece count (`gdc::sets`).
- Proc rate: `per_sec = 1 / (recharge + 1 / (chanceToRun/100 × trigger rate))` (**design choice:** the recharge blocks the next roll). Trigger rate: `AttackEnemy` / `AttackEnemyCrit` → the main skills' attack or cast speed (snapshot `attack_speed` / `cast_speed` read as uses per second: **unverified**, and for spells the engine says otherwise — §5 "Uses per second") × crit chance for crit triggers; `HitByEnemy` → `incoming_hits_per_sec`; `HitByMelee` → that × `melee_share`; `Block` → that × the shield's blocked share (`utility::block_share`); `HitByProjectile` → `incoming_hits_per_sec` × (1 − `melee_share`); `HitByCrit` → `incoming_hits_per_sec` × the enemy's crit chance on you (§9); `LowHealth` → `low_health_per_sec`, the threshold from the controller's `triggerParam`; `OnKill` → `kills_per_sec` (preset, default 0.5, design guess); `OnEquip` → kept up; any other trigger → fallback 0. Passives with their own trigger (DB fields on the skill record): `Skill_PassiveOnLifeBuffSelf` (`lifeMonitorPercent` = the threshold) → `low_health_per_sec` (× the controller's chance if it has one); `Skill_PassiveOnHitBuffSelf` / `…BuffShield` → `onHitActivationChance` × `incoming_hits_per_sec`; `Skill_WPAttack_*` → `skillChanceWeight` (tooltip "x% Chance to be Used", read as a percent: **unverified**) × the weapon main skills' attack rate. What the effect then does besides damage and RR: §10.
- Pets (`spawnObjects`, **experimental estimate**, the tooltip says so). **How many — engine (traced 2026-11-16):** `petLimit`, `spawnObjectsTimeToLive` and `skillCooldownTime` are the skill's plus its learned and item skill modifiers' (`Skill::GetSkillModifierData` 0x49AF90 sums Skill+0x140 and +0x158 through `SkillProfile_Modifiers` 0x54ACF0, profile +0x74 time to live × 1000 at 0x54A8C4, +0x78 `petLimit`; read by `GetPetLimit` 0x493880, `GetSpawnObjectTimeToLive` 0x493660, `GetCooldownTime` 0x4917E0). The last modifier whose vt+0x6F0 is non-zero replaces the time to live: only `Skill_SpawnPetTransmuter` (0x509F40, `modSpawnObjectsTimeToLive[level − 1]`) has one, `Skill_Modifier`'s returns 0. A transmuter's `modSpawnObjects` replaces the pet record, indexed by the summon's level (`Skill::AddModifierPetChanges` 0x494CE0, min(level − 1, size − 1) at 0x50A631). `Skill_TargetedSpawnPet::SpawnPet` 0x50A560 calls `Monster::SetLifetime` 0x37B620 only for a time to live > 0, and every pet record's own `lifeTime` (`Monster::Load` 0x37482B) is 0; past the limit the oldest go (`Skill_SpawnPet::KillExtraPets` 0x509890). So alive = the limit without a time to live — Hellhound, Raven, Briarthorn, Skeletons, and the invincible Blade Spirit (2 / 3 / 4 by level), Squall (3), Celestial Guardian (2), which earlier versions left unscored — else min(limit, summons/s × time to live). Blight Fiend with its transmuter `summon_blightbeast1b` (limit 1 + 2, cooldown 18 − 13 = 5 s, 12 s, `blightbeast_<level>b`) is 12 / 5 = 2.4 pets without CDR. **How often, per pet:** `specialAttack<n>SkillName` at `specialAttack<n>Chance` / max(`specialAttack<n>Delay`, its cooldown) per second (**design reading of the DB fields**), other activated skills with a cooldown once per cooldown, each at most the pet's rate for it; the plain attack (`attackSkillName`, else the pet's own weapon: its `Skill_Passive` skills with flat damage, at 100%) at the pet's rate × (1 − the share of time the others take). The rate: with a live pet of that record in the snapshot (schema 12, `Snapshot::pets`: the adapter walks the player's PetPen, `docs/SNAPSHOT.md`) the pet's own `Character::GetAttackSpeed` / `GetSpellCastSpeed` (by the skill's class, §5) × `anim_frame_rate` / `anim_frames` of that skill's hit animation (`SkillActivated::GetHitCallbackPoint` called with the pet; the own-weapon attack takes its SkillManager's `GetDefaultSkillId(0)` skill), the same model the engine uses for the player's spells and that was checked in game for them (§5); not checked for pets. A swap shifts the captured speed by the change of pet bonus %: speed × (1 + (m + Δ) / 100) / (1 + m / 100), m = the pet's `GetTotalCharModifier` 15 / 16 at capture (`CapAttackSpeed` not re-applied). Without a live pet: the record's `characterAttackSpeed` / `characterSpellCastSpeed` × (1 + pet attack / cast speed % + total speed %) from pet bonuses (Class Pet only), read as uses per second (**unverified**: `Character::GetAttackSpeed` 0x692E0 is type 15 × (1 + type 17 `characterBaseAttackSpeed`, × the dual-wield factor at hand state 10) × (1 − 0.01 × the attack-speed slow), clamped by vt+0x688 — `Monster::CapAttackSpeed` 0x37C570: between Monster+0x4EEC % and +0x4EF0 % of type 15 — a speed multiplier, not attacks per second). A preset `pet_attack_rate` > 0 replaces the plain attack's rate. Pet skill levels: `skillName<n>` / `skillLevel<n>`, a number or an equation of `charLevel` (the player's level; `effects::equation`: + − × / ^ and parentheses), plus `SkillSecondary_PetModifier` → `petSkillName` at the modifier's level.
- RR "to all" (`offensiveTotal…`) lands on every type of the `[enemy]` model; `…Elemental…` and `defensiveElementalResistance` on Cold, Fire, Lightning.

## Not modelled
- Devotion refund / "self-support" (e.g. Crane counting its own affinity): irrelevant, the adapter reads the actual devotions.

## 5. Score v1 (`core/src/score.cpp`) — sources per rule

Weights and enemy numbers are design choices in `presets/default.toml` (`docs/PRESETS.md`). Only ratios after / before are shown, so constant factors cancel.

**Damage** per main skill, at its current level (record arrays indexed by level − 1, the last value past the end):
- Flat damage: the skill record's `fields::flat_damage()` fields (re/CombatAttributeType.tsv load fields), average of min/max; a record without any (Skill_AttackBuffRadius) uses its `buffSkillName` record. Plus learned modifiers (`Skill::modifier_ids`, schema 5) and item skill modifiers (DB records). **unverified:** DoT = per second × `…DurationMin`; `…Chance` < 100 scales by chance.
- Weapon skills (`weaponDamagePct` > 0) add pct × flat damage of equipped items (their getter output; type 1 BasePhysical as Physical) and of passive / toggled skills that are on. **community:** gear flat damage applies to weapon attacks only. **Weapon base damage is never compared** (design choice, 0.9.2): every hand item's own base (`BasePhysical` and the record's `offensiveBase*Min/Max`) is taken out of flat damage and both sides get one shared base = the mean of the two sides' R Hand weapon base, so a weapon swap is scored on its affixes, +skills, procs, conversions, RR and % only. Dual wield: compared against each hand; the L-hand weapon's base is not modeled (noted). Two-handed vs one-hand + off-hand: compared as a pair; item DoT min/max taken as per second (**unverified**).
- Skill % fields apply to the original type before conversion (§2.5, community). Conversions: skill level, then the character's on the remainder; converted damage is not converted again (§2, community). Sums over 100% are weighed to 100% and Elemental is a third each Cold / Fire / Lightning (**engine**, §2).
- × enemy resist multiplier of the final type: `[enemy]` resist after the RR of both sides (`effects::resist_multipliers`, §1 / §4). Default resists: mean base `defensive<Type>` of the 48 Boss enemy records (DB); difficulty scaling not modelled.
- × (1 + character % of the final type / 100 + attribute bonus) from `damage_modifiers` (engine rows; 58 / 60 already spread, not added again).
- Attribute bonus, combatformulas.dbr: physical/pierce `dexterityDV/245`, physical duration (bleed, internal trauma) `/215`, magical `intelligenceDV/215`, magical duration `/200` (DB), **added to** the type's % (one factor `1 + %/100 + attribute`), not multiplied. Evidence (in-game tooltips, winterpoison level 22, Spirit 198.2 → bonus 0.991 for DoT): Poison Nova level 4, 21 Poison/s × 5 s: tome on (Poison % 93) 21 × (1 + 0.93 + 0.991) × 5 = 306.7, tooltip 305; shield on (68%) 280.4, tooltip 280. Multiplying would give 403 / 351. Effigy proc (level 6, 27/s, DoT % 188, Spirit 245): 27 × 4.106 × 5 = 554, tooltip 555.
- × hit and crit: pth from `probabilityToHitEquation` (DB) with OA vs the enemy DA (monster table, §9, or `enemy_da_per_level × level`), then `formulas::expected_hit` (engine, §9 stage 2): misses below 100, glancing at or below 70, each crit tier by its own band, crit damage (type 59, a fraction, + the skill's `offensiveCritDamageModifier` / 100) on every tier above 1.0.
- × (1 + `offensiveSlow<Type>DurationModifier` / 100) for DoT of the final type, summed over the equipped items' records. **shortcut:** record values (no affix roll), skills' and devotions' parts not seen; applied to the final type (**unverified**).
- × speed: no cooldown → 1 + (AttackSpeed (15) or SpellCastSpeed (16) modifier + TotalSpeed (13) modifier) / 100 (**unverified:** TotalSpeed added, noted when it changes); cooldown skills (0.9.6) → uses per second = min(attack / cast speed, 1 / (cooldown × (1 − CDR))): cast whenever ready (**design choice**), the recharge shortened as the engine does (**engine:** `Skill::GetCooldownTime` 0x4917E0 = `skillCooldownTime` × 1000 × (1 − 0.01 × CDR), consts 0x7A8164 = 1000, 0x7A7FC8 = 0.01, 0x7A8054 = 1.0; skipped for item and potion skills, §4 "Recharge" — the sheet's text: "Skills granted by items do not benefit from Cooldown Reduction bonuses and effects"), at most one use per attack / cast: spells and self buffs with a captured hit animation (0.9.8, snapshot schema 10) `cast_speed` × `anim_frame_rate` / `anim_frames`, the engine's `SkillAttackInfo+0x2C` (next item); by class, as the engine: a spell whose modifiers add weapon damage % (Vile Eye on Dreeg's Evil Eye) stays on its animation rate and its cast speed %; without a capture (weapon skills, older snapshots) `attack_speed` for weapon-damage skills, `cast_speed` for the rest, read as uses per second (**unverified**); the log marks `(speed cap)` and `(anim Nf@R -> x/s)`. CDR = the character's total (`utility::cooldown_reduction`, §6). The cooldown is the skill's and its modifiers' / transmuters' `skillCooldownTime` (Dreeg's Evil Eye has none; its transmuter `evileye1b.dbr` 4 s). **Not applied** to item skills and item procs; applied (0.9.7) to devotion procs and learned skills cast on cooldown for their debuff or summons (§4 "Recharge").
- **Procs and pets** (`extra_damage`): every non-learned effect of §4 with flat damage (its record or its `buffSkillName` record) or `weaponDamagePct` (× the weapon and gear flat damage, as for a weapon skill) adds `per_sec × one use`. A `Skill_WPAttack_*` replaces a weapon attack, so only its `weaponDamagePct` − 100 counts (**shortcut:** the replaced attack taken as the default attack). There is no separate "build relevance" factor: a proc of a type the build has no % for is small because it gets no % bonus, not because it is zeroed. A proc's use goes through the character's conversions, % per type, attribute factor, enemy resist, hit and crit like a main skill; a `Class Pet`'s use (**experimental**) is its attack's flat damage (plus its weapon × `weaponDamagePct`) × per type (1 + (pet bonus % of that type, all-damage and Elemental, + the pet's own passives' %) / 100 + its Cunning / Spirit bonus by the player's combatformulas) × enemy resist × its expected hit at its OA against the enemy DA. Pet bonuses: the equipped items' (and affixes' / components') `petBonusName` records, learned skills' and devotions' `petBonusName` (own or on the `buffSkillName` record; timed buffs and sets left out, **shortcut**). The pet's Cunning / Spirit / OA: its `characterAttributeEquations` record's equations of the player's level, plus pet bonus flat and %. **shortcut:** the player's formulas taken for pets, no pet crit damage, DoT duration or conversions. With no attack of the player's own (a pure summoner, every state with pet or proc damage) Damage is the pets' and procs' damage per second alone. A `PetPlayerScaling` summon (Living Bloom / Blossom, totems, mines; `effects::summoned`) is scored like a proc (the character's conversions, %, attributes, hit, crit; label `(player-scaled pet)`): **engine** — its seven `ContributeMisc*` (0x3C8840..0x3C88B0) jump to `Character::ContributeMisc*`, skipping `Pet::ContributeMisc*` (0x3C7C10..), which add `GameEngine::ContributePetBonus*`, so no pet bonus; `Skill::CollectCombatParameters` (0x499E40) swaps such a caster for its owner when the owner is a Player. Item skills without a controller count as cast on cooldown (design choice). To put them on the main skills' scale: main / s = Σ cooldown skills (one use / cooldown) + the others sharing the casts by weight at the snapshot's attack / cast speed (read as uses per second: **unverified**) — design choice. **Damage is a function of one side** (0.9.2): D(side) = exp(Σ weight × ln skill damage) × (1 + extra/s ÷ main/s), each term from that side's own sheet, and the shown ratio is D(after) / D(before). So a swap and its reverse are exact reciprocals: |(1 + x)(1 + y) − 1| < 0.5% is a test (`tests/test_symmetry.cpp`, every fixture pair and synthetic ring / off-hand / weapon / two-hander / requirement cases). 0.9.1 took the arithmetic mean of after/before ratios with the before side's weights and the before side's main/s, which made both directions positive (tome ↔ shield: +6.3% / +8.7%). No attack / cast speed in the snapshot → procs and pets not scored, noted.
- **Attack vs cast speed — engine:** `Player::CalculateSkillDps` (0x3E06D0) takes the speed by skill class: `SkillActivatedWeapon` / `WeaponPool` → `CalculateAttackSpeed`, `Skill_AttackRadiusSpin` → `GetAttackSpeed`, `SkillActivatedSpell` / `SkillActivatedBuffSelf` → `GetSpellCastSpeed` (0x693A0). The model does the same (0.9.9): `gdc::classes` holds the engine's own class tree (`GAME::RTTI_ClassInfo` parents read from `x64/Game.dll`, `re/tools/class_tree.py` → `re/GameClassTree.tsv`, 388 classes), and `classes::speed` walks a skill record's class up that tree in `CalculateSkillDps`'s order. A class outside those branches (`SkillSecondary_*`, unknown) falls back to attack speed with weapon damage, else cast speed. This fixes the 31 `SkillActivatedSpell` skills with `weaponDamagePct` > 0 (Aegis of Menhir, Leap, Counter Strike, Voice of Command, ...), which now take cast speed; Forcewave (`Skill_AttackWave`) is a cast class too.
- **Uses per second — engine (traced 2026-11-15; used from 0.9.8 for the speed cap, the on-attack / on-cast proc rates and the cast share of non-cooldown main skills):** the model reads `cast_speed` (`Character::GetSpellCastSpeed` 0x693A0 = char attribute 16 (SpellCastSpeed) × (1 − 0.01 × combat type 18, the cast-speed slow on the character), then vfunc +0x680; base 1.25 = `characterSpellCastSpeed` of `malepc01.dbr`) as casts per second. `Player::CalculateSkillDps` does not: for `SkillActivatedSpell` / `SkillActivatedBuffSelf` it stores `SkillAttackInfo+0x30` = cast speed and `+0x2C` = cast speed × `frameRate` / `frameCount` of the skill's hit animation (0x3E0CD2..0x3E0CFD; both from `SkillActivated::GetHitCallbackPoint` 0x51A310, vtable +0x6E0, which asks the character's animation object (vtable 0x6D4D48, slot +0x80 = 0x20B40) for the animation holding the `RightHandHit` / `LeftHandHit` / `Both` callback and returns `GraphicsAnim::GetLength` (Engine 0x977A0, +0xC8) and `GetFrameRate` (0x977C0, +0xCC)). So casts per second = 1.25 × (1 + cast speed %) only for an animation exactly one second long. The animation per skill and its frames are not known offline (resolved at run time by the skill's animation type, Skill+0x5C4, and the character's animation set). Weapon skills (`SkillActivatedWeapon` / `WeaponPool`) take `Player::CalculateAttackSpeed(true)` × 0.01 ÷ a vfunc +0x50 of the weapon's object (1.0 without one) instead, and `Skill_AttackRadiusSpin` takes `GetAttackSpeed`; neither is traced further. Without a capture (weapon skills, schema < 10) procs on attack and the speed cap still rest on `cast_speed` / `attack_speed` being uses per second (**unverified**, off by frameRate / frameCount). Since 0.9.8 the adapter reads them for every learned `SkillActivatedSpell` / `SkillActivatedBuffSelf` (snapshot schema 10, `docs/SNAPSHOT.md`). **Verified in game (2026-11-15):** winterpoison, cast speed 1.375, every captured skill 19 frames @ 30 → 2.17/s; Devouring Swarm held 10 s from full energy = 22 casts. The character sheet shows no per-skill rate (its Attacks per Second stays 1.42 with Dreeg's Evil Eye or Devouring Swarm selected); where the game displays `SkillAttackInfo+0x2C` is not traced. Every skill, potions included, resolved to the same 19-frame animation on that character: a shared cast animation per weapon set is likely, not traced. The animation follows the weapon type; a swap keeps the before side's frames (weapons out of scope). **Speed unit — engine (traced 2026-11-16), player and pets:** `Character::GetAttackSpeed` 0x692E0 = char attribute 15 (AttackSpeed: `characterAttackSpeed` × (1 + modifier %)) × (1 + attribute 17 `characterBaseAttackSpeed`, × `GameEngine`+0x374B8 when the hand state is 10, dual wield) × (1 − 0.01 × the attack-speed slow, +0x540 vt+0x10 type 0x11), then vt+0x688; `GetSpellCastSpeed` likewise with 16 / type 0x12, then vt+0x680 (consts 0x7A8054 = 1.0, 0x7A7FC8 = 0.01). The cap: `Player::CapAttackSpeed` 0x3E5B70 clamps to [Player+0x5028 %, (+0x502C + +0x10AC) %] of attribute 15, `Monster::CapAttackSpeed` 0x37C570 / `CapSpellCastSpeed` 0x37C4D0 to [+0x4EEC %, +0x4EF0 %] / [+0x4EE4 %, +0x4EE8 %] (Pet and PetPlayerScaling vtables 0x7862D0 / 0x7859A8 hold Monster's), `Character::CapAttackSpeed` 0x687D0 for the rest. Nothing in it is a time: both speeds are multipliers on animation playback, and uses per second = speed × frame rate / frames of the skill's hit animation, as `CalculateSkillDps` does for spells (verified above). For weapon skills `Player::CalculateAttackSpeed(true)` 0x3CD580 = `GetAttackSpeed` / X × 100, X from a 0x2B8-byte block a vt+0x20 of Player+0xF50 fills at difficulty + 1 (its floats +0x6C, +0x154 × 0.01 and +0x23C + 1: not mapped to fields), × the two hands' weapon vfunc +0x50 ratio when dual wielding; so the weapon unit stays **unverified**, and the sheet's Attacks per Second (2.76 against `attack_speed` 2.303, docs/SNAPSHOT.md) confirms `attack_speed` alone is not attacks per second.
- Auto skills: learned `records/skills/playerclass*` skills of class `Skill_Attack*`, `Skill_BuffAttackRadiusToggled` or a `SkillActivatedWeaponPool` subclass (class tree) with damage, top `auto_skills` by points, weighted by points; all on cooldown → default weapon attack fills in (`filler_weight`). Secondary skills (`SkillSecondary_*`) and pets are never main skills.

**Survivability:** EHP = Life / damage taken per incoming attack, through the pipeline of §9 (dodge / deflect, enemy OA vs DA, block, enemy damage reduction, resists, armor per region), × the threshold curves. Resists: R_t = min(uncapped − `resist_reduction`, max) (sheet values, `model::sheet`); `resist_reduction` (a preset design choice: incoming enemy RR) lowers the 9 base resists only. Base absorption: every armor entry carries `GameEngine::GetArmorDefensiveAbsorption` (engine, traced: `DefenseAttribute_BaseProtectionAbsorption::LoadFromTable` 0x204420 copies GameEngine+0x1328, which `GameEngine::LoadFromDatabase` 0x2C8830 loads from `gameengine.dbr armorDefensiveAbsorption` = 70; `AddToAccumulator` 0x204BF0 builds one entry per piece with armor > 0; `GetTotalDefenseType(39)` 0x11A880 averages the positive entries), × (1 + absorption modifier %). Procs add three more factors and buff the totals (§10). Not scored: DoT vs hit split, stage 5 of §9, stage 8 outside procs.

**Other:** 1 + Σ weight × value over the preset's `[other]` keys whose `when` holds + `default` × Σ unlisted stats / 100 (`gdc::utility`, §6). Weights are design choices (`docs/PRESETS.md` "Other"). Granted skills (items, sets) are listed "+? unscored" only when neither the stat model (ungated passives, `docs/MODEL.md`) nor §4 / §10 gives them a value; set fields outside `gdc::fields` always are.

**Components:** base item = item stats − relic stats − augment stats (per accumulator row; absorption left alone), minus the components' flat damage from their DB records (matched by value). Slot fit: the record's `amulet` / `ring` / `medal` / `head` / `chest` / `shoulders` / `hands` / `legs` / `feet` / `waist` flag (DB: present on all 491 ItemRelic / ItemEnchantment records). Completion bonuses are not moved (noted).

## 6. Skill levels, buffs and utility stats (2026-11-14)

**+skills** (`model::swap`, `docs/MODEL.md` item skill grants). A skill's stats at the old and new level come from its record's per-level arrays, so every mapped field moves with the level: damage, RR, % bonuses, resists, flat stats of passives and auras. Main skills and their modifiers are scored at the new level in Damage; RR through `gdc::effects` (§4).
- **+mastery** goes to every skill with that `mastery_id` (`SetMasteryAugment` 0x5373D0). **The mastery bar itself does not rise** — store 20261114-172933-952 records 1 → 2 (Occultist +1 item): every Occultist skill's augmented level rose by 1, `_classtraining_class03.dbr` stayed 0, and the mastery record has no `skillUltimateLevel` (DB). Community agrees: item bonuses to a mastery do not raise its bar ([Steam: GD mechanics, "+skills"](https://steamcommunity.com/sharedfiles/filedetails/?id=1855311027)). Evidence is from one level-50 mastery only.
- **0-point skills:** +levels to a skill with no points do nothing (`GetCurrentLevel` 0x4909C0 returns 0 for an unlearned skill). Score 0; the tooltip lists them grey `X +n (unused, 0 points)`.
- **Timed buffs you cast** (0.9.3; `model::buff_timing`): a class skill (`records/skills/playerclass*`) whose stats record (its `buffSkillName` record or its own) is a `SkillBuff_Passive` / `Skill_BuffSelfDuration` with a `skillActiveDuration` (DB classes and fields). The consumables the skill list also holds (50 records at level 1 on winterpoison: resist potions with `skillActiveDuration` 900 and no recharge) are not: 0.9.2 counted them as permanent buffs, which put +30 on every resist of both sides. **Uptime** = min(1, `skillActiveDuration` / (`skillCooldownTime` × (1 − CDR))) at the skill's current rank, CDR = the character's total (0.9.4, snapshot schema 9 `skill_attributes`: `SkillManager::CollectAvailableSkillAttributes` 0x535BE0 — equipped items, learned skills incl. devotions, running buffs, item skills — as `SkillAttributeAccumulator::GetValue` 0x51F1A0 gives it without the chance parts, value + |value| × modifier / 100); older snapshots: the equipped items' sum. The formula is the user's rule ("recast on cooldown", preset `recast_buffs`); that CDR shortens this recharge is not traced in the engine. Blood of Dreeg rank 3: 60 s / (12 s × 0.88) → 100%. **Normalization** (`model::expected`): before scoring, a learned timed buff that is up (snapshot `buff_active`, schema 8) is taken out of the totals with its modifiers, an active proc self buff (§10) likewise; on a normalized snapshot a swap never adds a timed buff's level change to the totals (`InactiveLevel::timed`), on a raw one with the buff running it takes the buff out as it was and puts it back as it is after the swap. **States** (`score::compare`): each side's buffs that are neither always up nor never (learned ones and procs' timed self buffs) split it into on / off states; every term (Damage, Survivability, Other) is computed on the real totals of each state (the buff at that side's rank and modifiers) and mixed by the states' probabilities, buffs taken as independent — so a resist cap or a threshold applies per state, not to an averaged sheet. Up to 4 buffs per side are enumerated (16 states), those with the largest uptime × (1 − uptime); the rest stay on the totals × uptime. A buff at 100% is simply on. Each term is mixed on its own and geometrically (0.9.4): ln X = Σ p_state × ln X_state for X = Damage, Survivability, Other, so total = D^a × S^b × O^c still holds, the parts add up exactly and the reverse swap is exactly reciprocal (symmetry test: 1708 pairs, worst |(1+x)(1+y) − 1| = 9e-8). Buffs that add nothing to the totals and absorb nothing are not enumerated (they cannot change either side). **Learned passives with their own trigger** (`Skill_PassiveOnLifeBuffSelf`, `Skill_PassiveOnHitBuffSelf`, e.g. Fighting Spirit `fightingspirit1.dbr`: 24% when hit) are self buffs at the uptime their trigger gives, as an item's proc (§10), not cast on cooldown. Toggles and auras have no duration: they are in the engine's totals, always on. `buff_mode = "live"`: learned timed buffs as the snapshot caught them (on = 100%, off = 0%), procs unchanged. Snapshots before schema 8: state not known, nothing removed, nothing enumerated. **Not in the states:** a buff's own `skillCooldownReduction` / `skillManaCostReduction` (29 / 3 buff records in the DB; skill attributes, which the model's totals do not re-derive): the Shift view lists them per buff as "(not modeled)" (0.9.5), and a buff caught running keeps its part in the snapshot's CDR total (CollectAvailableSkillAttributes counts running buffs; `model::expected` does not take it out). The Shift buff block also lists every char / defense stat of the buff that no sheet line shows (speeds, max speeds, avoidance, slow resist, ...: `tooltip::buff_lines`).

**Utility values** (`gdc::utility::values`, one per `[other]` key; dodge / deflect, block and the damage-reduction debuff are Survivability stages since 0.9.0, §9):
| Key | Value | Source |
|---|---|---|
| `run_speed` | sheet run speed (1 = +100%) | `model::sheet` |
| `life_regen`, `energy_regen` | % of Life regenerated per s; Energy per s / 10 | `model::sheet`, `combat_formulas.h` |
| `life_leech` | per main skill, weighted like Damage: (items' offensive 20 × min(1, `weaponDamagePct` / 100) + the skill's and its modifiers' own `offensiveLifeLeechMin`) / 100; no main skills: items' / 100. Spells (no weapon damage) get none from gear | **engine** (0.9.5): `SkillManager::CollectAvailableOffensiveDamageAttributes` (0x535FB0) adds `Character::GetEquipArmorDamageAttributes` only when the skill's weapon % > 0, scale % × 0.01; `DamageAttributeAbs_LifeLeech::AddDamageToAccumulator` (0x19A810) value × min(scale, 1.0) (const 0x7A8054). Modifier scale (`Skill_Modifier` 0x4FB320) not traced |
| `stun_resist`, `sleep_resist`, `trap_resist`, `freeze_resist`, `petrify_resist`, `knockdown_resist`, `disruption_resist`, `slow_resist` | sheet resist (capped) / 100 for defense 42, 43, 44, 45, 46, 48, 13; 16 = Slow Resistance: `defensiveTotalSpeedResistance` 24 (suffix `b_ar056_ar.dbr`) shows as 24 on defense 16, 17, 18 and 19 in the engine's item stats, so 17..19 are copies and not counted again (0.9.4; were 3 extra unlisted stats) | `model::sheet`, re/CombatAttributeType.tsv, fixture `lvl15-wp-after-feet-dreeg-on` |
| `cooldown_reduction`, `skill_cost_reduction` | character's skill attribute 1 / 2 / 100 (schema 9; before: the items' sum). **No [other] weight since 0.9.6:** listed at weight 0 for the Shift view. CDR is read by Damage (§5, cooldown main skills) and timed-buff uptime; energy cost reduction by nothing (no energy model: no preset has an energy threshold; Shift: `(not scored: no energy model)`) | SkillAttributeType; `SkillAttributeAccumulator::GetValue` 0x51F1A0 |
| `experience` | char 55 % / 100 × (1 − level / `maxPlayerLevel`) | DB `records/creatures/pc/playerlevels.dbr` maxPlayerLevel = 100 |
| `retaliation`, `retaliation_modifier` | items' retaliation flat (avg of min / max, / 1000) and the largest retaliation % / 100 | item-only |
| `pet_bonus` | Σ over items of the mean of their `petBonusName` record's mapped defensive % fields (life, resists, DA, ...), as a fraction; its damage, speed and OA are scored in the pets' Damage (§4) | DB, e.g. `b012a_medal` → 1.0 (`defensiveConvert` 100; its OA 5% is in pet damage). **shortcut:** mean only. The `pets` condition holds only for a summon of `Class = Pet` (`PetPlayerScaling` takes no pet bonus, §5) |
| `racial_damage`, `racial_defense` | Σ over item records of `racialBonusPercentDamage` / `racialBonusPercentDefense` / 100 × the share of monsters with any race in `racialBonusRace` | **engine:** `ItemEquipment::ContributeRacialBonusDamage` 0x33FC00 adds an entry's % once when 0x592080 finds any case-insensitive match (0x591FE0, `/` = `\`) between its races and the target's `characterRacialProfile`. Share: `re/EnemyRaces.tsv` (`re/tools/enemy_table.py --races`, 2559 Common / Champion / Hero records grouped by race set, 1.3.1.2; `enemy::race_share`), e.g. Race003 + Race005 = 397 / 2559 = 15.5%. **design choice:** every monster record weighs the same; the table has every record at every level, so the share does not depend on level or difficulty (no spawn data). `Aetherial` and Race017 are on items but on no monster: 0. `racialBonusAbsoluteDamage` (2 records) is not scored |

Item-only stats have no character total in the snapshot, so skills' parts of them are not seen (**shortcut**, noted in code). Any char / defense / damage-% / item-offense type that no score term reads is **unlisted**: it counts at `default` per 100 of its value and is named (`Other: <stat> a -> b (w default)` in Shift, the log); char 8 ManaLimitReserve is left out (more reserved energy is worse). Which of defense / defense_modifier holds types 55 / 56: not traced.

## 7. Requirements — engine (Game.dll 1.3.1.1 disassembly)

- Raw values: the item's virtual getters (`ItemEquipment::GetLevel/Strength/Dexterity/IntelligenceRequirement`, vt+0x528..0x540); each is the max of the item, its relic (+0x1558) and its augment (+0x1560) (0x343AB0, 0x343BD0). The printed value comes from the `itemcostformulas_*` equations at item load; we read the result, not the equations.
- `Item::AreRequirementsMet` (0x326930): `level >= E(L, LevelReqReduction (54))` or L = 0.
- `ItemEquipment::AreRequirementsMet` (0x343C30): that, then per Physique / Cunning / Spirit (char attribute 1 / 2 / 3): `total >= E(R, red)` or R = 0, with `red` = the totals of the item's three reduction types (+0xDF8.. per attribute, the first always, the others when non-zero; e.g. Weapon / Melee / Shield / Armor / Jewelry `*ReqReduction`) + `GlobalReqReduction` (53) when > 0.
- `E(r, x) = trunc(r − r·x·0.01 + 0.5)` (float; 0.01 at 0x7A7FC8, 0.5 at 0x7A8024).
- Tooltip: the candidate's check uses the engine's call (`met`) only when it ran against the snapshot's own level, difficulty and Physique / Cunning / Spirit totals (`requirements.met_at`, `requirements::engine_met`); otherwise the formula on the snapshot decides. The adapter keeps hovered and bag items in a pool and rescoring them after a level-up used the old flag (0.9.8: "can't equip yet" + "the engine disagrees" on an equippable medal, `tests/test_requirements.cpp`). With a fresh flag core's formula only names what is missing and a mismatch is a model warning. Not covered: a requirement reduction (types 53 / 54, the item's `*ReqReduction`) that changes while level and attributes stay. After a swap, the formula runs on the predicted sheet for every other equipped item that met its requirements before; one that fails is shown red and taken off (scored as removed), which can break another (at most 4 rounds).
- Unverified: whether the engine checks the candidate with the old item still worn (assumed: yes, the current sheet).

## 8. Difficulty resistance penalty — DB, folded into the engine totals

`records/game/balancingadjustment_mp+difficulty_players01.dbr` (12-entry arrays, docs/RECON.md:115): Fire / Cold / Lightning / Pierce / Poison −25 from entry 5, −50 from entry 9; Aether / Bleeding / Chaos / Vitality −25 from entry 9 (3 difficulties × 4 player counts: in the enemies' pak `characterLifeMultModifier` is 0 / 90 / 180 / 270 in every group of four; the index is the engine's game balance level, `GameEngine::GetBalanceDifficulty` 0x2D8530, whose mapping to players is not traced). The snapshot's `defense` totals already include it: in the lvl96 Ultimate fixtures (`lvl96-s4-before-ring-c031`, `lvl96-s5-after-ring-c031`) every base resist total = Σ item + skill contributions − 50 (the first five) or − 25 (the other four), exactly; in `lvl16-wp-before-belt` (Normal) the gap is 0. The model swaps item contributions on those totals and never adds a penalty, so nothing is counted twice. Elite (−25 / 0 by the record) is not in any fixture yet.

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
| 8 | Flat reduced damage from monster types, % absorption, flat absorption | after `ExecuteDefense` (`ApplyDamage` 0x11D340, not traced further); order from the [guide](https://www.grimdawn.com/guide/character/combat/) | procs only (§10): `damageAbsorptionPercent` and `damageAbsorption` of proc buffs × uptime; nothing else (no such stat in the snapshot totals) |

**Armor before or after the physical resist?** After. `BasePhysical::ReduceDamage` (0x1175F0) multiplies the stored damage by (1 − resist) the moment the resist entry executes and merely accumulates armor; the armor equation runs later, in `Execute`, on the reduced damage. This matches the official guide's list (Resistances, then Armor) and contradicts the 2016 forum reading (armor first, [Order of defense seems inappropriate](https://forums.crateentertainment.com/t/order-of-defense-seems-inappropriate/34474)). `tests/test_score.cpp` "armor absorbs…".

**Enemy model** (`gdc::enemy`, `re/EnemyByLevel.tsv`, `re/tools/enemy_table.py`): median Common / Champion / Hero `Monster` record (2559 records, 1.3.1.2) at the character's level, one row set per difficulty. OA / DA: `combatformulas.dbr` ability equations on the monster's bio record (`characterAttributeEquations`). Hit: the largest flat direct damage of one of its skills × the attribute factor (physical × (dex / 245 + 1), magical × (int / 215 + 1)). **Not included:** weapon damage, the monster's own % damage bonuses, the `charLevel` offset between area and player (`gameengine.dbr monsterLevelGapFixer` 0 / 5 / 7 is not read), bosses, more than one player. **unverified:** that the ability equations apply to monsters as to players (TakeAttack calls `DesignerCalculateDefensiveAbility` on whoever defends; the attacker's OA arrives precomputed in `ParametersCombat+0x14`).

**Difficulty** (engine: `Monster::ContributeGameBalanceCharAttributes` 0x37B710, `…OffensiveModifierAttributes` 0x37BB80, `…DefenseAttributes` 0x37B8F0 and the four others). Every monster adds, in this order: (1) `gameengine.dbr monsterAttributePak` (`balancingadjustment_mp+difficulty_enemies01.dbr`, GameEngine+0x1C58) at Monster+0x46C4 = the game balance level (copied from GameEngine+0x36D18 in `Monster::InitialUpdate`); (2) while Monster+0x46C8 > 0 (the difficulty ramp, GameEngine+0x36D1C, `GetDifficultyRamp` 0x2D85A0) the challenge adjustment (`GameEngine::GetChallengeAdjustment` 0x2F1C20: with `GameInfo::GetMode` 0 or 2, GameEngine+0x2CD8 on Normal / Elite = `gameengine.dbr challengeAdjustment` (`balancingadjustment_challengemode_enemies01.dbr`, Veteran; loaded at `LoadFromDatabase` +0x6C37) and +0x3810 on Ultimate = `gameascendant.dbr ultimateChallangeAdjustment` (`balancingadjustment_ultramode_enemies01.dbr`, Ascendant; +0x6DE2); mode 1 or no difficulty: +0x3270 = `survivalRecord survivalAdjustment<Difficulty>`, the Crucible); (3) a per-monster pak (Monster+0x2098, not read); (4) the Ascendant altar modifiers by monster class (`GetAscendantCharAttributes` 0x2EA680 …, set in play, not in the database). The table has six row sets: (1) with the single-player entry of each difficulty, and each again with its challenge adjustment on top (Normal / Elite Veteran, Ascendant):

| | Normal | Elite | Ultimate | Veteran adds (Normal, Elite) | Ascendant adds (Ultimate) |
|---|---|---|---|---|---|
| `offensiveTotalDamageModifier` % | −25 | +25 | +40 | +40 | +150 |
| `offensivePhysicalModifier` / Pierce / Aether, Chaos, Life % | −10 / −8 / −20 | 0 | 0 | +10 (Physical) | −96 (Physical) |
| `characterOffensiveAbility` flat, `…Modifier` % | 0, −12 | +40, −8 | +50, −8 | +25, +5 | +80, +3.5 |
| `characterDefensiveAbility` flat, `…Modifier` % | +35, −15 | +60, −8 | +75, −8 | +15, +5 | +80, +5 |
| `characterDexterity` / `Intelligence` / `Strength` `Modifier` % | −5 / −5 / −5 | +5 / +5 / +4 | +10 / +10 / +5 | — / — / +5 | — |
| `offensiveCritDamageModifier` % (Survivability: monster crits, §5 tiers) | — | — | — | — | +40 |
| `defensive<Type>` (added to `[enemy]` resists, `re/EnemyResistByDifficulty.tsv`) | 0 | 0..6 | 2..12 | — | — |

Flat and % ability go into the ability equation as for a player; total and per-type damage % are added together (as the player's "all damage" and type bonuses are). Level 27: hit 397 (148 physical) / OA 686 on Normal, 1758 (399) / 882 on Ascendant; level 100: hit 3384 / 6926 / 8048 / 15146 (Normal / Elite / Ultimate / Ascendant), 5579 / 9373 with Veteran. Elite Veteran hits harder than plain Ultimate (+65% against +40% damage). The snapshot picks the row: `character.difficulty` 0..2 × 2, + 1 while `difficulty_ramp` > 0 (`enemy::difficulty`), the condition the monster itself tests (Monster+0x46C8, copied from the ramp in `Monster::Monster`). **Not applied:** the ultramode record's `characterLifeModifier` 850 / attack and cast speed +12% / `offensiveSlowDamageMultModifier` 30 (DoT damage, the table hit is direct only) / `retaliationTotalDamageModifier` 50, and the altar modifiers. **unverified:** that the ramp is above 0 exactly when the Veteran / Ascendant option is on (`IsUltimateVeteran` 0x2E90E0 tests `GameInfo::GetChallenge` == 3 on Ultimate; the ramp is set by `SetDifficultyRamp` 0x2D8540, its link to the challenge value not traced), and whether the adjustment's scalar fields scale with the ramp value (it is passed as the pak index); no Veteran, Elite or Ascendant snapshot exists yet.

**Enemy resistance reduction** (`[survival] resist_reduction`, default `"auto"`). What a monster takes off a player's resist is `defensive<Type>` < 0 (and `defensiveElementalResistance` for Cold / Fire / Lightning) on its debuff records (classes `SkillBuff_Debuf`, `SkillBuff_DebufTrap`, `SkillBuff_Contageous`, `SkillBuff_DebufFreeze`), category 1 of §1; no skill of these 2559 monsters has an `offensive*ResistanceReduction*` field. Per level: for each of the nine base resists, the monsters whose skills (record or `buffSkillName`, at `skillLevel<n>`) lower it and the median amount among them; `rr_share` = the mean share over the nine resists, `rr` = the mean amount weighted by those shares. Result (1.3.1.2): 2.6% of the monsters lower a given resist by 7.5 (level 1) to 8.2 (50) points, 3.1% by 11.2 at 60–70, 3.6% by 17.6 from 80 on; 103 to 183 records, all but one Champion or Hero. **Bosses** (`boss_rr`, `boss_rr_share`): the same over the 485 Quest (408), Boss (48: 19 + 29 Nemesis) and SuperBoss (29) records: 7.1% lower a given resist by 12.1 up to level 50, 7.3% by 18.2 at 70, 7.3% by 25.3 from 75 on (charLevel equations up to `charLevel × 1.1 + 2`). The same on every difficulty (the paks carry no such field; the records have no per-difficulty skill data). The score mixes three states per base resist: `(1 − b)(1 − rr_share) + b(1 − boss_rr_share)` at f(resist), `(1 − b) rr_share` at f(resist − rr), `b boss_rr_share` at f(resist − boss_rr), with `b` = `[survival] boss_share` (default 0.15; 1.3.1.2 has 485 of 3044 records = 0.159; a design choice: bosses are fought longer than one record suggests, raise it for boss stages), in the damage taken and in the `resist` threshold (geometric there), so losing overcap costs a few hundredths of a percent per point while a point under the cap costs in full. A number in the preset (any `[[stage]]`) replaces the mix by an always-on reduction from `resist_reduction_from` on (game difficulty; below it the mix stays), e.g. the guide's 25 at `plague-garden` 94+ (Shattered Realm and Ascendant altars are not in the table). Records are not weighted by how often they spawn. `tests/test_regress.cpp`, `tests/test_score.cpp`.

**Difficulty lookahead** (`[survival] difficulty_lookahead`, default on). On Normal and Elite (Veteran included) every state above is scored twice: as now for `1 − t`, and for `t` with the next difficulty's extra penalty taken off as well (§8, `re/PlayerResistByDifficulty.tsv`: Normal → Elite 25 on Pierce / Cold / Fire / Poison / Lightning, Elite → Ultimate 25 on all nine), so overcap up to that penalty counts before the switch. `t` ramps linearly from 0 to 1 over levels 65–75 on Normal and 84–94 on Elite and stays 1 past them. **Design choice:** the database has no level range per difficulty (`records/proxies/limit_area*.dbr` clamp the spawns of 10 pools only); 75 and 94 are the switches of `docs/build-ref/plague-garden.md`'s level plan. The cap is 80 on every difficulty (`playerDefenseCap`), so only the penalty moves. Ultimate and Ascendant have nothing ahead (Ascendant uses Ultimate's balance level). `tests/test_score.cpp`.

**Crit for my damage** (§5): the same `expected_hit` against the table's DA. PTH past 135 only adds 1.5× crits; +crit damage adds to every tier above 1.0.

**Contributions** (`score::Result::contributions`, Shift line `top:`): every factor of Survivability has its own ln ratio (Life, DA, dodge / deflect, block, enemy damage reduction, each threshold; the `resist` threshold per resist, joined with that resist's damage part); the damage-taken sum is split over the damage types, Damage over the main skills and procs, Other over its keys, each in proportion to its linear change. × the term's exponent × 100 = points of the total; the parts add up to ln(1 + total).

**Shift sheet lines** take their points from the comparison the lines themselves show: the items as hovered, components included (`Slot::hovered`; when that total differs from the headline, which compares both items without components, the heading states it: `sheet as hovered, components included: +2.4% [points of it]`). 0.9.8 put the headline's points on them: "Life 4093->4002 (-91) [+1.1]" was Life 3957 → 4002 without the worn medal's Corpse Dust. A part that still moves against its line is printed on its own (`Life: other effects [+1.1]`), never on the line. `tests/test_regress.cpp`, `tests/test_tooltip.cpp`.

**Coverage:** `docs/COVERAGE.md` lists every item stat field and what the score does with it.

## 10. Procs beyond damage and RR: crowd control and self buffs (0.9.1) — design, DB fields

**Choice:** a proc's self buff is not scored as a thing of its own. Its stats × uptime are added to the character's totals (`model::buffed`, the same `derive` a swap uses) and the whole score runs on that sheet, so a resist buff meets the resist cap, armor goes through the armor equation, DA through the hit table, OA / damage % through Damage — no second copy of any formula. Uptime is §4's: `min(1, per_sec × skillActiveDuration)`, `per_sec = 1 / (cooldown + 1 / (chance × trigger rate))`, so recharge and duration both cap it; preset `[uptime]` overrides it. Since 0.9.3 a timed proc buff below 100% is a buff state like the learned ones (§6): the score runs with it on and with it off and mixes the two by uptime (low-health buffs by their weight below); its % and flat absorption follow the same state. What the totals cannot hold becomes a Survivability factor:
- **Crowd control** on enemies (`offensive{Stun,Freeze,Knockdown,Fear,Confusion,Petrify,Sleep,Trap,Convert}Min/Max/Chance`, re/CombatAttributeType.tsv 42..53; seconds × chance, the longest kind per proc, procs combined as independent): damage × (1 − `cc_share` × uptime). `cc_share` (preset, default 0.1, design guess) = share of incoming damage from enemies the CC really stops; enemy CC resists and the proc's radius are not read.
- **Absorption** of a timed buff: × (1 − u × `damageAbsorptionPercent`/100) × (1 − u × min(1, `damageAbsorption` / enemy hit)). **Heals and absorb shields** (`skillLifePercent` × Life + `skillLifeBonus`; `damageAbsorption` of a buff without a duration, once per use): life per second against the life lost per second (`incoming_hits_per_sec` × enemy hit × damage taken per attack), at most 90% of it. **unverified:** flat absorption per hit vs pool, where the engine applies either (§9 stage 8), the shields' `…DamageQualifier` types (ignored).
- Self buff = record class `Skill_BuffSelfDuration`, `Skill_BuffSelfShield`, `Skill_GiveBonus`, `Skill_PassiveOnLifeBuffSelf`, `Skill_PassiveOnHitBuffSelf`, `Skill_PassiveOnHitBuffShield`, or a `buffSkillName` record of class `SkillBuff_Passive`, and their subclasses in the engine's class tree (0.9.9: `Skill_BuffSelfImmobilize` Blade Barrier is a timed `Skill_BuffSelfDuration`; `Skill_PassiveOnCritBuffSelf` is an on-hit buff). **Audit (0.9.9, all ten masteries' buff skills against the DB):** `Skill_PassiveOnCritBuffSelf` (Inquisitor and Berserker `passive02`) is triggered by the owner's crits (**engine:** `Character::AttackTarget` → `SkillManager::OnCriticalAttack` 0x535900 → vtable +0x1F8 `IsSkillOnCritActive` 0x5021E0 → +0x1A8 `OnHitActivation`): rate = attacks × crit chance × `onHitActivationChance`. `Skill_BuffRadiusToggled` auras keep their flat damage on the `buffSkillName` record when it is a `SkillBuff_Passive` (Presence of Virtue, Aura of Conviction, Elemental Infusion, Blast Shield, Nature's Blessing, Amatok's Pact; Aura of Censure's is a `SkillBuff_Debuf`, left out). `Skill_ProjectileModifier` (Blackwater's RR) counts through its parent only, not as a skill of its own. Applies to item, set and devotion effects alike. **Low-health buffs** are up when needed: uptime = min(1, threshold / 100) × min(1, per_sec / `low_health_per_sec`) — the threshold (`lifeMonitorPercent` / the controller's `triggerParam`) is the share of a fight's danger they cover (design choice). **Shortcuts:** buff fields outside `gdc::fields` (retaliation, mana cost, racial) add nothing. A buff up at snapshot time is removed first (§6 buff normalization).
- Parts in `top:`: `Proc crowd control`, `Proc absorb / heal`; buff stats show under the part they move. Tests: `tests/test_sanity.cpp` "granted …".

## 11. Engine notes behind the code

Longer explanations moved out of code comments. Code points here as "MECHANICS §11.x".

### 11.1 Elemental damage (`score_conversion.cpp`)

Elemental damage is a third each of Cold, Fire and Lightning: ConvertCombatAttributes 0x1199F0 makes three entries (types 5, 6, 8) at 1/3 (const 0x7A8010) for a conversion into Elemental, and ConvertDamage 0x119ED0 treats Elemental as those three types. So it is split before % bonuses and conversions apply.

### 11.2 Conversion order (`score_conversion.cpp`)

Skill::CollectCombatParameters 0x499E40: the skill's own conversions first (CollectLocalConversionAttributes 0x496F00: its record's pairs, then its modifiers', 0x49A3B7), then the character's (0x49A5D7: SkillManager::GetConversionAttributes, Character::GetEquipConversionAttributes, vt+0x708 — the same calls as SkillManager::CollectAvailableConversionAttributes 0x536400, which fills the snapshot in that Data order). ConversionAttributeAccumulator::Apply 0x17DB20 runs ConvertDamage once per Data, i.e. per source type in the order AddAttribute 0x17D890 first saw it; Weigh 0x17D520 only scales, never reorders. ConvertDamage 0x119ED0, for one source type: an entry matches its own type, BasePhysical matches Physical, Elemental matches Cold / Fire / Lightning; every pair takes its weighed percent of the same entry amount (a source type over 100% has every pair scaled by 100 / total); DoT is not moved into Pierce, Chaos or Aether (0x119FE0: those pairs are skipped, the others keep their weighed percent); the entry is then scaled to 100 - the moved percent, so the next source type that matches it (Fire after Elemental, or Elemental after Fire) works on the rest. New entries are flagged (byte +0xF, 0x119E44) and never converted again, in either level.

### 11.3 Conversion warnings (`score_conversion.cpp`)

The build's types: >= 10% of its main skills' damage (design choice). A gear conversion acts only on damage no conversion made (convert(): new entries are not converted again), so the source side is read with the gear conversions taken out, the target side with them. Unknown (no database, no main skills): every type counts.

### 11.4 Weapon and flat damage (`score_damage.cpp`)

- `offensiveBase<Type>Min / Max` is DamageAttributeAbsBase_*: elemental base damage that shares its type with flat damage in the item's getter output.
- Flat damage applies to weapon attacks only (community). Passives, toggles and auras count (Skill_BuffRadiusToggled keeps its stats on the buffSkillName record: presenceofvirtue1_buff.dbr). Skill_BuffSelfDuration is a toggle subclass; with a duration it is a timed buff. A timed buff's flat damage (bloodofdreeg1_buff.dbr poison) is left out. A SkillBuff_Debuf (auracensure1_buff.dbr) is not a passive buff record.
- Set bonus flat damage: Character::ContributeMiscOffensiveDamageAttributes 0x6AA80.
- Design choice: weapon base damage is not compared (the game's tooltip does that), so both sides use the mean of their R Hand bases. That keeps the base identical in both directions. Dual wield's second weapon base is not modeled.
- DoT duration: only the record value of DamageAttributeDurMod_* — no affix rolls, no skill / devotion parts, so large values overstate the ratio. Revisit when the adapter captures GetTotalDurationModifierType.

### 11.5 Attribute bonus (`score_damage.cpp`)

combatformulas.dbr: physical / pierce + Cunning/245, physical DoT + Cunning/215, magical + Spirit/215, magical DoT + Spirit/200; damage = base x (1 + % / 100 + this). Checked in game (lvl16-wp-lhand-* fixtures): Poison Nova 21/s x (1 + 0.93 + 198.22 / 200) = 61 x 5 s = 305 shown (§5).

### 11.6 Skill pace (`score_damage.cpp`)

- Design choice: cast whenever ready. Recharge x (1 - CDR) as in Skill::GetCooldownTime, capped by attack / cast speed read as uses per second (unverified; 0 = no cap).
- Attack or cast speed by the skill's class as Player::CalculateSkillDps (classes::speed), whatever weapon damage % its modifiers add (Vile Eye on Dreeg's Evil Eye, shieldhammer1's 100%). A class outside those branches (SkillSecondary_*, unknown): attack speed with weapon damage, else cast speed (unverified).
- Uses per second at most. Cast class with a hit animation: cast speed x frame rate / frames, as CalculateSkillDps stores in SkillAttackInfo+0x2C. The adapter captures a hit animation (schema 10) for SkillActivatedSpell / BuffSelf only. Otherwise the sheet's attack / cast speed is read as uses per second (unverified).
- Conversion memo: reused across a side's buff states. Its notes come only from the first conversion, as callers keep one copy of each note; mains convert before hits, as skill_damage() runs before extra_damage().

### 11.7 Pets and leech (`score_side.cpp`)

- Pet bonuses: what GameEngine::ContributePetBonus* hands a Class Pet (effects.h): the petBonusName records of equipped items with their affixes and components (level 1) and of learned skills, devotions and item skills with points (own or buffSkillName record, at their level). Timed buffs' and set bonuses' pet bonuses are left out (sets lists them unscored).
- A Class Pet's hit, experimental (§4): per type x (1 + (pet bonus % + the pet's own passives' %) / 100 + its attributes as combatformulas.dbr for the player), and the expected hit at its OA against the enemy DA. The pet's level is the player's; Cunning / Spirit / OA come from its characterAttributeEquations record (DB equations of charLevel) plus pet bonuses. The formulas are the player's (unverified for pets); no crit damage or DoT duration bonuses, no conversions.
- PetPlayerScaling pets attack with the owner's combat parameters (effects::summoned), like procs.
- Gear ADCtH only reaches skills with weapon damage, x min(weaponDamagePct / 100, 1): SkillManager::CollectAvailableOffensiveDamageAttributes 0x535FB0 adds GetEquipArmorDamageAttributes only from Skill::CollectCombatParameters 0x499E40 when weapon % > 0, and DamageAttributeAbs_LifeLeech::AddDamageToAccumulator 0x19A810 takes min(scale, 1.0). So spells get none. The skill's and its modifiers' own ADCtH count in full (unverified: Skill_Modifier passes its caller's scale).
- A Skill_WPAttack_* replaces a weapon attack, so only its excess over 100% counts; the replaced attack is taken as the default attack. A pet's weapon is its own (effects::Effect::weapon); the pet record stands for its plain attack.

### 11.8 Skill levels

- `GetCurrentLevel` (0x4909C0) is `level ? min(level + augmented, cap) : 0` outside devotions. The cap is
  SkillProfile+0x150 = max(skillUltimateLevel, skillMaxLevel) (`SkillProfile::LoadProfile` 0x545A27).
- Grants: `SetSkillLevelAugment` (0x5375B0) by record; `SetMasteryAugment` (0x5373D0) to the mastery and every
  skill whose `GetMasteryId` (Skill+0x13C) matches; `SetAllSkillAugment` (0x537230) to every skill but one the
  caller names. That exclusion is not traced, so the model warns when +all skills changes.
- The mastery bar takes no item bonus: with +1 Occultist (d016_relic) every Occultist skill rose by 1 while
  `_classtraining_class03.dbr` stayed. Its record has no skillMaxLevel / skillUltimateLevel.
- Skill_BuffRadius skills take their cap from the buffSkillName record, assumed to be what `GetCurrentLevel`'s
  profile (vt+0x578) reads.
- `AddModifierDefenseAttributes` (0x494970) adds only Skill_Modifier objects. Whether Skill_Transmuter is one is
  not traced, so transmuters with stats are not modeled.
- An item skill modifier adds through the skill's own collector: possession1's captured passive has attack speed
  modifier 16, two sword1h_b013_possession.dbr at 8 each (one per dual-wielded b013 sword); possession1's own
  record has none (auto-snapshot 2026-11-14).
- The engine's numbers must equal record + attached modifiers. When they differ a conditional part is in play and
  the record delta is not trusted.

### 11.9 Attributes and defense totals (`stat_model_derive.cpp`)

- playerlevels.dbr: lifeIncrement 20, lifeIncrementDexterity 8, lifeIncrementIntelligence 12, manaIncrement 16 per
  8 attribute points. They apply to attribute totals, bonuses included: the poison swap fixture shows +12.1 Spirit
  -> +18.15 Life, +24.2 Energy.
- Absorption (type 39) is averaged over entries with a positive `GetTotalAbsorption` (`GetTotalDefenseType`
  0x11A90E..0x11A954); other defense types sum.
- Every armor entry carries `GameEngine::GetArmorDefensiveAbsorption`: `BaseProtectionAbsorption::LoadFromTable`
  (0x204420) copies GameEngine+0x1328 (gameengine.dbr armorDefensiveAbsorption, `LoadFromDatabase` 0x2C8830);
  `AddToAccumulator` (0x204BF0) builds it only for armor > 0; `GetTotalDefenseType(39)` (0x11A880) averages them.
  So the total is that constant while any armor exists, else 0.

### 11.10 Item-granted skills (`stat_model_granted.cpp`)

- Removed: its captured contribution (schema 6), and its damage % from its record when on. Added: only a
  Skill_Passive without weapon flags at a constant level. Procs, buffs, pets, weapon-gated passives and level
  equations are reported, not scored.
- `SkillManager::GetCharAttributes` (0x52F920) checks only the level, so a passive that captured nothing adds only
  damage %.

### 11.11 Procs, cooldowns and pets (`effects.cpp`, `effects_pets.cpp`)

- Cooldown: `Skill::GetCooldownTime` 0x4917E0 skips CDR only when Skill+0x2F9 (`IsItemSkill` 0x30AD10, set by
  `SkillManager::AddItemSkills` 0x538BC0 and buff replicas) or Skill+0x2FC (potionModifier) is set. Devotion powers
  cast via `SkillAutoCastController::AppraiseAndExecute` 0x523440 -> `GetCooldownTime`, so they get CDR. Item and
  set skills (`own` false) keep the DB value. Devotion powers keep cooldown and templateAutoCast on the buff record
  (tier2_32d_skill.dbr).
- `Skill::GetSkillModifierData` 0x49AF90 (from `GetCooldownTime` 0x4917E0, `GetPetLimit` 0x493880,
  `GetSpawnObjectTimeToLive` 0x493660): the skill's profile plus its learned (Skill+0x140) and item (Skill+0x158)
  modifiers', added by `SkillProfile_Modifiers` 0x54ACF0. `e.modifiers` holds both.
- Random triggers: expected time between casts = recharge + 1 / (chance x rate).
- skillChanceWeight is read as a percent of weapon attacks ("x% Chance to be Used").
- On crit: `Character::AttackTarget` -> `SkillManager::OnCriticalAttack` 0x535900: every skill whose vt+0x1F8
  (`IsSkillOnCritActive` 0x5021E0) is true gets OnHitActivation, so the owner's own crits trigger it.
- Design choice: cast whenever ready; kept up with no cooldown or when CDR takes it all.
- itemSkillLevelEq: a number or "itemLevel/4+1" (303 records in 1.3.1.1), rounded down, using the base item's
  itemLevel. Checked on one item: a03_torso001b (itemLevel 11) "of Balanced Steel" shows Ring of Steel at level 3.
- Engine subclasses count too: Skill_ProjectileModifier blackwater3 (RR) belongs to blackwater1.
- Pet estimate (experimental, MECHANICS §4), per pet: the plain attack (attackSkillName, else the pet's weapon) at
  characterAttackSpeed, a spell class at characterSpellCastSpeed, x (1 + pet bonus %) for Class Pet, read as uses
  per second like the player's sheet speeds (unverified; `Character::GetAttackSpeed` 0x692E0 is type 15 x
  (1 + type 17) for every Character). specialAttack<n>SkillName: chance / max(specialAttack<n>Delay, cooldown) per
  second (design reading of the fields); its share of the pet's time comes off the plain attack. Other attack
  skills with a cooldown: once per cooldown. A preset pet_attack_rate replaces the plain attack's rate.
- Transmuters: `GetSpawnObjectTimeToLive` 0x493660: the last modifier with a non-zero vt+0x6F0 replaces the sum;
  only `Skill_SpawnPetTransmuter` 0x509F40 has one (modSpawnObjectsTimeToLive[level - 1]), Skill_Modifier's is 0.
  `Skill::AddModifierPetChanges` 0x494CE0: its modSpawnObjects replaces the pet, indexed by the summon's level
  (min(level - 1, size - 1), 0x50A631; summon_blightbeast1b: max level 1, 26 entries).
- Live pets (snapshot schema 12): the engine's own speeds, shifted by the pet bonus change since the capture
  (`GetTotalCharModifier` is the % in them; the cap is not re-applied to the shift), and per skill the hit
  animation, so uses per second = speed x frame rate / frames, as the engine times the player's spells (§5 "Uses
  per second"). The pet's own weapon: its SkillManager's default skill (`GetDefaultSkillId(0)`) holds the animation.

## 12. Adapter reads (`adapter/src/`)

### 12.1 Snapshot layout reads (`snapshot_*.cpp`)

Runs on the game thread in the `GameEngine::Update` detour. Calls only exported getters, exported accumulator
ctors / dtors on our own buffers, item getters through vtable slots checked against the exported vftables, and the
unexported object-id lookup found by `gdc::scan`. Layout reads and their sources:

```text
  Character+0x5D0  mem::vector<EquipManagerContainer>: Character::GetAttachedItems (0x63B90)
  EquipManagerContainer = 12 bytes {u32 id, EquipLocations, flags}: its ctor (0x288670) and
                   EquipManager::GetItemId (0x2847D0, stride 12, location at +4)
  mem::vector = {T* begin, T* end, T* cap}: EquipManager::GetItemId reads begin/end at +0/+8
  Item+0x538       ItemReplicaInfo: Item::GetItemReplicaInfo (0x3244D0)
  ItemReplicaInfo  string/u32 offsets: ItemReplicaInfo::StreamProperties (0x585840) / ReadProperties
  std::string      MSVC x64 layout {buf[16] | ptr, size @0x10, capacity @0x18}
  CharAttributeAccumulator values float[58] @0, modifiers float[58] @0xE8: AddValue / GetModifier
  CombatAttributeAccumulator vectors of CombatAttribute* @0x08/0x20/0x38/0x50: its dtor/Clear
  CombatAttribute vtable +0x10 = GetType: CombatAttributeAccumulator::GetTotalDefenseType
  CombatAttribute vtable +0x20 GetDamageType, +0x78 GetMinMaxValues, +0xF0 GetDuration: the
                   CombatAttributeAbsDamage / DurDamage vftables (0x7347D8 / 0x734568); the
                   CombatAttribute base slots are stubs (ret / return 0), so any subclass is safe
  CombatAttributeAbsDefense +0x2C = cap bonus: its ModifyDefenseCap (vt+0xA0, 0x113AE0) adds to it,
                   its Process (0x115370) caps at Character::GetDefenseAttributeCap() + [+0x2C]
  SkillAttributeAccumulator float[5] at +0x08 value / +0x1C chance value / +0x30 modifier /
                   +0x44 chance modifier: AddValue / AddChanceValue / AddModifier / AddChanceModifier
                   (0x51F160..0x51F190); ctor clears 0x50 bytes from +8
  ConversionAttributeAccumulator = mem::vector<Data*>; Data {u32 from; mem::vector<{u32 to, float pct}>
                   at +8}: ConversionAttributeAccumulator::AddAttribute (0x17D890)
  Skill+0x158      mem::vector of 0x30-byte {u32 modifier id, EquipLocations, std::string set,
                   bool relic @0x28, bool augment @0x29}: Skill::AddToItemSkillModifiers (0x48FD30),
                   Skill::GetItemSkillModifiers (0x48FFD0)
  Object vtable +0 = GetClassInfo, RTTI_ClassInfo+0x10 = parent: Character::GetEquipCharAttributes
  CombatDisplayAccumulator(bool) writes up to +0x8B: its ctor (0x11B980)
  CombatAttributeDamage_BasePhysical: its +8 type is DamageAttribute_BasePhysical::vt+0x28 = 0
                   (AddDamageToAccumulator 0x1A0370), for which GetMinMaxValues (0x117500) writes
                   nothing; GetValues (vt+0x110) returns {rolled, min, max, ?, ?} (+0x20/+0x24/+0x28/+0x48/+0x2C)
  Character+0x7A8  CombatManager PTH table (sources: §13.18)
  CombatDisplayAccumulator vftable: 8 slots (0x6D3CF8, CombatAttributeAccumulator's follows at
                   0x6D3D38); +0x18 GetChanceMultiplier, +0x28 SetGlobalChance writes +0x88 (0x16A40);
                   GetChanceMultiplier (0x11BA10) reads +0x88; the dtor (0x11B9F0) rewrites the vptr
  CombatAttributeDefense_AbsorptionProtection +0x28: read by GetTotalReduction (0x1182F0), written
                   only by Process (0x118370), never by the ctor (0x1182A0): undefined until processed
```

### 12.2 Tooltip block (`tooltip*.cpp`)

- Threads: the game thread reads items and serves cached blocks, never scores. A miss queues the item and shows
  "computing..." until the next hover; no engine call to rebuild an open tooltip is traced. The worker thread
  scores. Any fault or C++ exception turns the block off for the session; the game's own lines are never touched.
- GameTextLine (ctor 0x313030): 0x40 bytes, +0 GameTextClass, +8 std::u16string, +0x28 bool, +0x30
  GraphicsTexture*, +0x38 float. `ItemEquipment::GetUIDisplayText` passes false, null, 1.0 (0x33DF35..0x33DF44).
- Lines are built by the exported GameTextLine ctor and moved in with the engine's push_back
  (`gdc::scan::line_push_back_call`), so the game owns and frees them: the ctor copies our text into a
  game-allocated buffer, push_back moves it, nothing is left to free. GameTextLineToString is unused in
  Game.dll 1.3.1.1.
- `Item::GenerateSearchText` 0x329370 calls `GetUIDisplayText` (vt+0x458) at +0xB8 for stash search text; that
  caller is skipped.

## 13. Notes moved from code comments

Longer comments, grouped by source file. The code keeps a short pointer to each section.

### 13.1 `adapter/src/dllmain.cpp`

Loads under the ASI loader, identifies the build, resolves exports. Only a missing required export disables
(docs/RECON.md §6); an unknown hash just warns.
Safety rules (crash 2026-11-14, docs/BUILD.md "Safe-mode test"): DllMain only starts one thread; the kill switch
(GDCompare.disable or GDCOMPARE_DISABLE=1) is checked first; log lines go straight to WriteFile so a crash
leaves the last step; each step runs under SEH; the vectored handler only logs faults inside this module.

### 13.2 `adapter/src/hooks.cpp`

F10 by default: the game binds F1..F7 (keybindings.txt, DirectInput 59..65), Steam takes F12, KDE takes
Ctrl+F1..F12 and Alt+F-keys.

### 13.3 `adapter/src/snapshot_character.cpp`

As for an incoming hit with no enemy reduction: ProcessDefense 0x11AB40 runs the modifiers, then each
entry's Process applies its % and cap. ReductionInfo (0x18 bytes) and DamageScaleInfo (0 = no scaling) are zero.

### 13.4 `adapter/src/snapshot_internal.h`

Item slots in the Object-subobject vftable, as called by Character::GetEquipCharAttributes (*0x498),
GetEquipOffensiveModifierAttributes (*0x4B0), GetEquipRetaliationModifierAttributes (*0x4C0),
GetAllDefenseAttributes (*0x4C8); skill / damage / retaliation / conversion slots from the exported
ItemEquipment vftable. ItemRelic / ItemEnchantment have stubs at 0x4A8 / 0x4B8. Checked in init.

Scale 1.0, as Player::CalculateRetaliationValues 0x3CDE50 passes (const 0x7A8054);
DamageAttributeAbsBaseElemental::AddDamageToAccumulator 0x18F530 multiplies min / max by it.

Values Skill::CollectWeaponDamage writes inline (0x4991AB..0x499204): +0x84 ModifyProcs 1, +0x85 IncludeProcs 0,
+0x88 GlobalChance 0. Then GetChanceMultiplier 0x11BA10 returns chance x 0.01, so chance stats count at
their expected value instead of a roll.

AddDamageToAccumulator 0x18F530 multiplies by acc vt+0x18 (chance) and never stores the chance, so each item
getter also runs with a patched vtable copy returning 1 (raw), 0 (excluded) and the effective chance
(as 0x11BA45..0x11BA5B). Non-chance stats keep multiplier 1.

Player::CalculateSkillDps calls vt+0x6E0 on an is-a SkillActivated (0x3E09A3) and uses the frames only for an
is-a SkillActivatedSpell / SkillActivatedBuffSelf (0x3E0BEE..0x3E0CFD). Only objects whose slot holds the
exported SkillActivated::GetHitCallbackPoint (0x51A310) are called.

ArmorJewelry_Ring::vftable{Object} 0x6D7658 + 0x550 = GetItemType 0x2DE70; ItemEquipment's (0x76F718) =
Item::GetItemType 0x167E0.

ItemEquipment::AreRequirementsMet 0x343C30 calls vt+0x530 / 0x538 / 0x540 (Str / Dex / Int) and
Item::AreRequirementsMet 0x326930 vt+0x528 (level); it reads the reduction CharAttributeType ids at these offsets
(first always, the other two when non-zero).

Character::GetPetPen 0x7E100 (Character+0x2050); PetPen+8 is an MSVC std::list head (PetPen::GetPetOwner 0x3C9B00),
node {next, prev, u32 pet id @0x10, u32 summoning skill id @0x14, ..., bool released @0x1C}:
Skill_SpawnPet::KillExtraPets 0x509890.

GameEngine::GetPlayerFolder(Player*) 0x2E2FC8 (1.3.1.2): a set byte at Player+0x4BDD picks MangleBackupPlayerName
("__" + name, rva 0x61815c) over ManglePlayerName ("_" + name, rva 0x6180ec) for the save folder.
GetPlayerFolder(Player*) also passes Player+0x4B48: set = main\\ whatever the game mode.

### 13.5 `core/include/gdc/arz.h`

.arz v3 layout: re/tools/arz.py (parsed every record of 1.3.1.1 database.arz and GDX1..3). Records are LZ4
blocks of {u16 type, u16 count, u32 key string index, count x 4-byte values}. Never reads out of bounds.

A record's fields sorted by name (std::map's order), found through a hash index: a scored tooltip looks up
fields ~10^6 times and a tree walk's string compares were a fifth of its time.

### 13.6 `core/include/gdc/attribute_names.h`

From Game.dll 1.3.1.1: CharAttributeType / CombatAttributeType from exported class names (re/*.tsv, checked by
tests/test_attribute_names.cpp; 41, 47, 49 have none). SkillAttributeType: the +8 store in each
SkillAttributeVal_* / Mod_* ctor (0x51EE70..0x51F050). GameDifficulty: GetGameDifficultyAsPath 0x2D85B0.
EquipLocations: EquipManager::LocationToString 0x286480. Skill_Type has no string table.

### 13.7 `core/include/gdc/classes.h`

Player::CalculateSkillDps 0x3E06D0 picks the speed by class, in this order: SkillActivatedWeapon (0x3E0B3D),
SkillActivatedWeaponPool (0x3E0B80) -> CalculateAttackSpeed; Skill_AttackRadiusSpin (0x3E0BB7) -> GetAttackSpeed;
SkillActivatedSpell (0x3E0BEE), SkillActivatedBuffSelf (0x3E0C25) -> GetSpellCastSpeed. kNone: none of them.

### 13.8 `core/include/gdc/code_scan.h`

Character::GetEquipCharAttributes (1.3.1.1 rva 0x695E0, site +0x82):
FF 15 <rel32>   call [rip+rel32]   ; Singleton<ObjectManager>::Get via the IAT
48 8B C8        mov  rcx, rax
8B D7           mov  edx, edi      ; the id
E8 <rel32>      call <lookup>      ; rva 0x19D40

ItemEquipment::GetUIDisplayText (rva 0x33DCE0) appends "tagTransmuted" with it at +kLinePushBackSite:
48 8D 55 A7   lea  rdx, [rbp-0x59]
48 8B CE      mov  rcx, rsi
E8 <rel32>    call push_back         ; rva 0x1C550

Checks the first 0xAC bytes (calls are rel32, so position independent). It MOVES the line in: the u16string at
+8 goes through 0x1C4C0, which takes over the heap buffer. So a line must own a buffer from the game's allocator,
which GameTextLine's exported ctor provides.

### 13.9 `core/include/gdc/combat_formulas.h`

offensiveAbilityEquation:
  (offensiveAbilityDV + (characterLevelDV * 12) + ((dexterityDV + bonusDV) * 0.5))
    * (1 + (offensiveAbilityModifierDV / 100)) + 53
DesignerCalculateOffensiveAbility subtracts the OA reduction (type 22) and clamps at 0. bonusDV 0 reproduces
the sheet (2946 at level 96).

probabilityToHitEquation:
  ((((OA / ((DA / 3.5) + OA)) * 300) * 0.3) + (((((OA * 3.25) + 10000) - (DA * 3.25)) / 100) * 0.7)) - 50

Character::DesignerCalculateCriticalChance (0x686D0), as a fraction:
  max(0, (pth - pthThreshold2) / max(CombatManager+0x27C, pth))
pth against the target's DA; the sheet's "Chance to Crit" uses the last attacked monster.

One attack against a target, as CombatManager::TakeAttack (0x11D950) resolves it through
CombatManager::CalculateDamageModifier (0x120B10), Game.dll 1.3.1.1:
  pth = max(probabilityToHitEquation, pthMinimum)   (DesignerCalculateProbabilityToHit 0x121790)
  x   = uniform roll x max(CombatManager+0x27C, pth);  x > pth: miss (only possible below 100)
  pth <= pthThreshold1: damage x normalPTHEquation (probabilityToHitDV/70, combatformulas.dbr)
  else damage x pthDamageModifier of the highest threshold k in 2..6 with x > pthThreshold_k (1 below
  threshold 2). So pth past the last threshold only adds top-tier crits.
  A modifier above 1 gets the attacker's crit damage bonus added (TakeAttack 0x11ECBD: xmm10 > 1.0 ->
  += ParametersCombat+0xA0), i.e. the bonus is added to every crit tier.
No thresholds (tests): no tiers, multiplier = chance.

records/game/playerresourcebehavior.dbr, per second (elapsedTime = 1):
  lifeRegenEquation (lifeRegen + ((physique-50)/25)) * elapsedTime
  manaRegenEquation (((manaRegen*((intelligence/384)+1))+((intelligence-50)/100))+6.5) * elapsedTime
lifeRegen / manaRegen = types 6 / 7 total (includes %). CharacterBio::GetLife/ManaRegenValue (0x8B020 /
0x8B080) write into CharacterBio, so the adapter does not call them.

### 13.10 `core/include/gdc/components.h`

Item stats include relic and augment; each component's own getter output is subtracted or added per row. A
component's flat damage is not in its own stats but in the parent's offensive_damage, so it is matched by value
from the component's record. Absorption rows are left alone (averaged per item, stat_model.cpp).

### 13.11 `core/include/gdc/db_fields.h`

defensive*: DefenseAttributeAbs_* / AbsMod_* +0x50 load field (re/DefenseAttributeFields.tsv). character*: the
+0x40 string (re/CharAttributeType.tsv); CharAttributeMod_LifeMultiplier is not a plain % and left out.
tests/test_db_fields.cpp checks the table against the TSVs.

The engine spreads 58 / 60 over single types in damage_modifiers (fixture b002e_torso: +29% all damage shows on
every type and as row 60); spread() does the same.

Crit damage is a percent in records and a fraction in row 59 (witchfire2.dbr 22 -> 23 moved the row 0.36 ->
0.37), so `scale` converts.

### 13.12 `core/include/gdc/effects.h`

Every effect has uses per second and an uptime in [0, 1] tagged with its origin (MECHANICS §1, §4).
DB sources (ERRATA §0, re/CombatAttributeType.tsv):
RR type 1: defensive<Type> < 0 on a debuff record (debufSkill > 0 or SkillBuff_Debuf), duration skillActiveDuration
RR type 2 / 3: offensive{Total,Physical,Elemental}ResistanceReduction{Percent,Absolute}Min (32..37), duration
the field's DurationMin
procs: templateAutoCast / itemSkillAutoController -> chanceToRun, triggerType, triggerParam; passives:
lifeMonitorPercent, onHitActivationChance, skillChanceWeight (Skill_WPAttack_*)
pets: spawnObjects, petLimit, spawnObjectsTimeToLive (+ modifiers'; a transmuter's modSpawnObjectsTimeToLive /
modSpawnObjects), the pet's attackSkillName and skillName<n>; live pets' speeds and animations (Snapshot::pets)

Pets: the pet record and its skills that hold its weapon damage (Skill_Passive with flat damage at their level),
which weaponDamagePct scales; the pet record itself stands for a plain attack (100% of it).

Pet: Pet::ContributeMisc* (0x3C7C10..) add GameEngine::ContributePetBonus*, so pet bonuses apply.
PetPlayerScaling overrides all seven ContributeMisc* (0x3C8840..0x3C88B0) to Character's, so no pet bonus, and
Skill::CollectCombatParameters 0x499E40 swaps its caster for the owning Player.

A DB equation of charLevel (pets' skillLevel<n>, bio records: "((charLevel*21.3)^1.29)+35"): numbers,
charLevel, + - * / ^ and parentheses. nullopt for anything else.

A pet's skills with their levels: skillName<n> / skillLevel<n> (numbers or equations of charLevel) and, at the
modifier's level, the petSkillName of the summon's learned SkillSecondary_PetModifier (summon_hellhound2_petmodifier
-> petskill_hellhound_fireclaw, level 0 in the pet record).

Type 1 adds value x uptime; types 2 and 3 are highest-only, each source active with probability
u_k x prod over stronger (1 - u_j), sources independent (MECHANICS §4). Order 1 -> 2 -> 3; type 2 only while the
resist is above 0 (MECHANICS §1).

### 13.13 `core/include/gdc/enemy.h`

Median Common / Champion / Hero monster per level and difficulty (1.3.1.1 + GDX1..3), from
re/EnemyByLevel.tsv, re/EnemyResistByDifficulty.tsv, re/PlayerResistByDifficulty.tsv and re/EnemyRaces.tsv
(re/tools/enemy_table.py, docs/MECHANICS.md §8, §9).

Normal, Normal Veteran, Elite, Elite Veteran, Ultimate, Ascendant: the game difficulty x 2, + 1 while the
difficulty ramp is above 0 (GameEngine::GetChallengeAdjustment 0x2F1C20 applied on top).

Share of the table's monsters a racial bonus listing `races` (racialBonusRace) applies to: those whose
characterRacialProfile shares any tag with it, case-insensitively (ItemEquipment::ContributeRacialBonusDamage
0x33FC00 adds each entry once on a match, 0x592080 / 0x591FE0). The table holds every record at every level and
no difficulty pak sets a race, so the share is the same at every level and difficulty.

What gameengine.dbr playerAttributePak takes off the player's resist (CombatAttributeType) at that difficulty.
Already in the snapshot's totals; used for what the next difficulty takes off on top.

### 13.14 `core/include/gdc/font.h`

StyleManager::LoadStyle 0x1C4400: as is at scale 1, else ceil(points x scale), at least 8
(lowResMinimumFontSize default).

GameTextString's measure (Game.dll 0x317470) on GraphicsFont2::GetStyle(size, 0) (Engine.dll 0xB0830 -> 0xAE660:
nearest size, first on a tie), per glyph (0x317820): x += previous "after" (not for a space after a non-space),
then the bearing (x >= 0), then floor(advance x scale + 0.5). No kerning in these fonts. Outside
U+0020..007E: glyph U+0000.

### 13.15 `core/include/gdc/requirements.h`

Game.dll 1.3.1.1:
Item::AreRequirementsMet 0x326930: level (Character+0x1760) >= E(level req, type 54 LevelReqReduction).
ItemEquipment::AreRequirementsMet 0x343C30: also Physique / Cunning / Spirit >= E(req, sum of the item's three
reduction types (first always, others when non-zero) + type 53 GlobalReqReduction when > 0).
E(r, x) = trunc(r - r * x * 0.01 + 0.5) in float (0.01 at 0x7A7FC8, 0.5 at 0x7A8024). A requirement of 0 always passes.

### 13.16 `core/include/gdc/score.h`

score = Damage^a x Survivability^b x Other^c; change % = (after / before - 1) x 100.
Only ratios are reported, so terms need no absolute unit. Sources: docs/MECHANICS.md §5, defense order §9.

A record as main_skills' auto mode sees it: main = an attack-class or weapon-pool skill with damage at `level`;
weapon = weaponDamagePct > 0 (it deals the weapon's damage); types = the damage its own record (or its
buffSkillName record) deals, before conversions (Elemental as Cold, Fire and Lightning, Physical base as Physical).

The 9 base resists share one threshold term (geometric mean of their factors) raised to this: 1 = together
they count like one other threshold, 9 = each resist counts like one.

recast_buffs: uptime = min(1, duration / (recharge x (1 - CDR))); else [uptime] from the record.
live_buffs: take each buff as the snapshot caught it.

Enemy resistance reduction on the 9 base resists. Empty = the monster table's (gdc/enemy.h): its rr for
its rr_share of the incoming damage, bosses' boss_rr for boss_share x boss_rr_share. A number is always on.

On Normal and Elite: score the base resists also under the next difficulty's extra penalty, for a share
that ramps from 0 to 1 over the last levels before that difficulty (score.cpp lookahead()).

Mean defensive<Type> of the 48 Boss records under records/creatures/enemies/ (database.arz + GDX1..3, unchanged 1.3.1.1 -> 1.3.1.2);
enemy::resist adds the difficulty's share.

Save keys (snap::save_key). A name in character / characters that one of them names matches only that save
when the snapshot knows its save; names no key mentions match by name alone.

Pet summons are Skill_SpawnPet / Skill_TargetedSpawnPet whose spawnObjects has Class Pet; the
PetPlayerScaling ones (mines, totems, seals, Wind Devils) are not pets. Modifiers count for their parent.
Thresholds in archetype() are design choices.

Damage by type after conversions as Skill::CollectCombatParameters: `skill` first, then `global` (fractions, in
snapshot order), each weighed to 100% per source type and run source type by source type in first-seen order;
converted damage is not converted again; Elemental in or out is a third each of Cold, Fire, Lightning
(docs/MECHANICS.md §2).

Smooth threshold, no step: ln f = above x u - (below - above) x width x ln(1 + e^(-u / width)),
u = value / target - 1.

File name (no extension) F10 writes for this character: "frozen-winterofhell-928303920" (Steam account),
"-local", plus "-custom" / "-backup"; the name alone when the save is unknown. ASCII only.

Each term's ln ratio split over its parts and scaled by the term's exponent, so the parts sum to
ln(1 + percent / 100) x 100.

Buffs that are not always up split each side into states. Terms are computed per state and mixed
geometrically (ln X = sum p x ln X_state, buffs independent) so caps see real sheets and the reverse swap
stays exact. Only the kMaxEnumerated buffs with the largest uptime x (1 - uptime) are enumerated.

`memo` keeps the before side for the next call with the same `before` object and weapon base (one tooltip
scores one snapshot against several afters); `before` must outlive it unchanged, as must skills, p and db.

### 13.17 `core/include/gdc/sets.h`

The engine adds set bonuses outside the items: Character::ContributeMisc*Attributes (0x6A6B0 char, 0x6A980
defense, 0x6AA80 damage, 0x6ABB0 modifiers, 0x6AEE0 conversions) walk the set map (Character+0x1FD0) with
n = GetNumberOfItemsInSet (0x6A4C0) and index every field at min(n - 1, size - 1); n = 0 adds nothing
(CharAttributeMod::AddToAccumulator 0xCCA80). So item getters never contain set bonuses.
ItemSet::SkillModifierInstall (0x34B660 -> 0x34FB00) adds modifiers only while
itemSkillModifierControl[min(n - 1, size - 1)] >= 1 (always when empty).

### 13.18 `core/include/gdc/snapshot.h`

2: devotion_current_level_sum (was devotion_points_spent), pth.
3: chance-based item stats carry their chance and 100% values (Damage::chance/raw_*, CombatTotals::proc);
   item AbsorptionProtection reduction no longer written. Schema 2 still reads (no chance data).
4: Skill::passive, character.name. Schemas 2..3 still read (passive absent).
5: Skill::modifier_ids. Schemas 2..4 still read (modifier links unknown; check `schema` before use).
6: Snapshot::item_skills, Skill::item_id. Schemas 2..5 still read (granted skills unknown).
7: Item::requirements. Schemas 2..6 still read (requirements unknown: not checked).
8: Skill::buff_active. 9: Snapshot::skill_attributes (before: empty = unknown, check `schema`).
10: Skill::anim_frames / anim_frame_rate (before: 0 = unknown). 11: difficulty_ramp, requirements.met_at.
12: Snapshot::pets (before: empty = unknown). 13: character.save (before: empty = unknown).
14: character.save with the Steam account and main / user folder (a schema 13 key reads as unknown).

The save a character lives in, as GameEngine::GetPlayerFolder (Game.dll 1.3.1.2 0x2E2D80) builds it:
<save root> + "main\\" (campaign) or "user\\" (custom game) + "_" + name (ManglePlayerName) or "__" + name
(MangleBackupPlayerName). The root is Steam Cloud's userdata/<account>/219990/remote/save when
GameEngine::GetCloudStorage is set, else My Games/Grim Dawn/save. Keys: "cloud/928303920/main/_Name",
"local/user/__Name". steam_account 0 = the local root.

Schema 11. What that call ran against: Character level, GameEngine difficulty and the Physique / Cunning /
Spirit totals. `met` stands only for a snapshot in the same state (requirements::engine_met).

Schema 8. Character::IsSkillBuffActive 0x67DD0 looks up the SkillManager buff map (+0x100), filled and
erased by CreateRemoveSkillBuff 0x53A990 by the name it is given: the record or its buff record.

Read at Character+0x7A8 = CombatManager+0x278 (CombatManager at Character+0x530:
BasePhysical::GetTotalDamage 0x117560; DesignerCalculateCriticalChance 0x686D0 reads +0x7AC / +0x7B8).
Field order: CombatManager::LoadRecord 0x11D10E..0x11D265 (combatformulas.dbr). +0x27C is not in the record;
the ctor 0x11BC3C sets it to 100.0.

Schema 12. A pet in the player's PetPen (Character::GetPetPen 0x7E100: Character+0x2050, an MSVC std::list at +8
as PetPen::GetPetOwner 0x3C9B00 walks it). Node +0x10 pet id, +0x14 the summoning skill's id, +0x1C released:
Skill_SpawnPet::KillExtraPets 0x509890 counts nodes with +0x1C == 0 and +0x14 == its id, releases +0x10 and sets
+0x1C. Released nodes are skipped.

Schema 11. GameEngine::GetDifficultyRamp 0x2D85A0: above 0 every monster also gets the challenge
adjustment (Monster::ContributeGameBalanceCharAttributes 0x37B710); on Ultimate that is Ascendant.

SkillManager::GetNumDevotionPointsSpent 0x534D40 sums GetCurrentLevel over skills with Skill+0x4A0 != 0, so
celestial powers count their level. Points actually spent = sum of `level` over devotion skills.

Schema 9. SkillManager::CollectAvailableSkillAttributes 0x535BE0: equipped items (vt+0x4A0 via
GetEquipSkillAttributes 0x69910), the character's vt+0x6D8 / +0x718 / +0x750, and GetSkillAttributes 0x52FA90
(active learned skills, buffs, item skills). Engine total: SkillAttributeAccumulator::GetValue 0x51F1A0 =
value + |value| x modifier x 0.01.

SkillManager::GetItemSkillList 0x531730 (+0x258). GetCharAttributes 0x52F920 / GetDefenseAttributes 0x530C10 /
GetOffensiveModifierAttributes 0x52FDA0 add each one's collectors while GetCurrentLevel != 0, so `passive` is
exactly what one adds. Schema 6.

### 13.19 `core/include/gdc/stat_model.h`

(sum of item armor x combatRegion*Chance + waist armor) x (1 + Protection modifier / 100).
Inferred: matches the level-96 sheet (1408); the waist term is a fit.

CombatManager::PickRegion 0x120D90 picks one region per attack; an armor entry protects its own region, or all
without one (AbsorptionProtection::Execute 0x118400). Chances sum to 1.

### 13.20 `core/include/gdc/store.h`

One chain per segment per character: a full baseline, then RFC 7386 merge patches between keyed snapshots.
Files: <root>/<character>/<segment>-<seq>-<full|delta>.json.gz; names sort in time order (docs/SNAPSHOT.md).

Arrays keyed by identity so a patch touches only what changed. Keyed entries are in key order, so the engine's
list order is lost. A missing or repeated key leaves the array as is.

### 13.21 `core/include/gdc/tooltip.h`

ASCII only: every English font (Fonts.arc *.fnt; GraphicsFont2::Initialize 0xAFC90) has exactly
U+0020..007E per style, so "->" and " - " instead of arrows and dashes. Layout examples: tests/test_tooltip.cpp.

EquipmentCtrl::ValidateItemType 0x288D30: location -> required type. Type 8 (shield / off-hand) -> L Hand;
type 9 (weapon) -> R Hand and L Hand.

GraphicsFont2::ProcessCharacter (Engine.dll 0xB0C00) reads pairs: "^" + letter is a colour, "^" + anything
else draws neither, "&" + B / I / S sets the style. "^^" and "&&" draw one, so both are doubled. "{" and "}"
are never drawn and become "(" and ")".

Item tooltip (Grim Dawn.exe provider 0x1EE9F0, layout 0x236020, mode 1) is as wide as a sample in the default
style (jura 16, StyleManager::LoadStyle): "The quick brown fox jumped over the lazy dog The quick" at >= 1600 px,
else "The quick brown fox jumped over the lazy". GrayStats1 has no indent (gameengine.dbr).

### 13.22 `core/include/gdc/utility.h`

Other = 1 + sum of weight x value + default x sum of unlisted stats (MECHANICS §5). Unlisted stats count at
`default` per 100 and are always named, never dropped.

### 13.23 `core/src/arz.cpp`

Field names are short ASCII and often differ only in the middle (offensiveSlowColdDurationMin /
offensiveSlowFireDurationMin): length, head, middle and tail words, mixed. Far cheaper than std::hash's
full pass; a collision only costs a probe.

### 13.24 `core/src/enemy.cpp`

re/EnemyByLevel.tsv: level, oa, da, hit, hit_physical, rr, rr_share, crit_damage, boss_rr, boss_rr_share; one
block per difficulty.

### 13.25 `core/src/hook_targets.cpp`

mov eax, [rcx+0x36D1C]; Monster::Monster 0x372B40 copies it to Monster+0x46C8 (the challenge adjustment level).

movzx eax, byte [rcx+0x375EA]; GetUserSaveDataFolder (0x2E2B20 in 1.3.1.2) drops the My Games base when set.

lea rax, [rcx+0x430] / mov eax, [rcx+0x1CC]: GetPlayerFolder (0x2E2D80 in 1.3.1.2) picks main\\ for mode 1, else user\\.

GameTextLineToString (0x316EB0) is never referenced in Game.dll 1.3.1.1, so it is not a draw hook.
GenerateSearchText calls GetUIDisplayText (call at +0xB8) for stash search: the detours skip that caller.

As the item rollover (Grim Dawn.exe 0x1EE9F0) and GameTextString (0x316CE0) read them. GetWidth is
`mov eax, [rcx+0x64]; ret`, folded with GridRegion::GetHeightInCells, so call only.

Slots 0x408 / 0x438, checked against Skill_Passive's; Skill_BuffSelfToggled's overrides sit at the same
slots (0x707B80 / 0x707BB0, vftable 0x707778).

Buff map (SkillManager+0x100) is keyed by record name: SkillServices_Character::CreateUpdateSkillBuff 0x5586F0
passes SkillBuffTransfer+0 after SkillManager::FindSkillId. GetBuffSkillName = Skill+0x40.

Live pets (snapshot schema 12): the player's PetPen, each pet checked is-a Character, its SkillManager's
default skill, GetHitCallbackPoint on its skills that are-a SkillActivated (CalculateSkillDps 0x3E09A3's check).

### 13.26 `core/src/stat_model_effects.cpp`

SkillBuff_Passive (buffSkillName of Skill_BuffRadius / SkillSecondary_BuffSelfDuration / Skill_BuffOther in
1.3.1.1) or Skill_BuffSelfDuration (overguard1) with skillActiveDuration. Toggles, auras, debuffs, attacks and
Skill_PassiveOn*BuffSelf are not timed buffs.

Added through the skill's own collector: possession1's captured passive has AttackSpeed modifier 16 = two
sword1h_b013_possession.dbr (characterAttackSpeedModifier 8, one per dual-wielded b013 sword); its own record
has none (auto-snapshot 2026-11-14).

### 13.27 `core/src/stat_model_internal.h`

SetSkillLevelAugment 0x5375B0 by record; SetMasteryAugment 0x5373D0 to the mastery and every skill whose
GetMasteryId (Skill+0x13C) matches; SetAllSkillAugment 0x537230 to every skill but one the caller names
(not traced, warned in swap()).
Skill and mastery grants that differ between the sides; a mastery's with the ids of the snapshot's skills of
that record.

### 13.28 `core/src/text.cpp`

Header {char[4] "ARC\0", u32 version 3, files, parts, part table size, string table size, table offset};
parts {u32 offset, compressed, decompressed}; 44-byte entries {u32 type, offset, compressed, decompressed, ?;
u64 filetime; u32 parts, first part, name length, name offset}. Type 1 with equal sizes is stored whole, else LZ4.

### 13.29 `core/src/utility.cpp`

Read elsewhere or by a key here. Char 8 ManaLimitReserve is left out (more is worse). Defense 17..19 copy 16:
defensiveTotalSpeedResistance shows as 24 on all four types (fixture lvl15-wp-after-feet-dreeg-on), so slow resist
is the slow_resist key alone.

### 13.30 `installer/src/buildpreset.h`

A build preset as the wizard writes it: GDCompare.presets/<name>.toml with [match] characters = [...], so it applies
to those characters at every level (score::match skips the mastery and damage checks for them; docs/PRESETS.md).
[match] damage stays: conversion_warn uses it to flag gear that converts into another type.

### 13.31 `installer/src/catalog.h`

Class or mastery name from Text_EN ("tagSkillClassName0306" / "tagSkillClassName03"), gender forms joined:
"[ms]Sorcerer[fs]Sorceress" -> "Sorcerer / Sorceress". Falls back to the code.

### 13.32 `installer/src/detect.cpp`

Heroic's GOG install list, installed.json: {"installed": [{"install_path": ...}, ...]}. The layout is not
documented; any object with an install_path is tried and checked on disk.

### 13.33 `installer/src/detect.h`

Where Grim Dawn is installed. Every candidate is checked on disk (Grim Dawn.exe in the root, x64/ or compat/,
plus database/database.arz), so a wrong guess from a registry key or launcher file is dropped, never used.

Steam roots that exist: Linux ~/.local/share/Steam, ~/.steam/steam, Flatpak ~/.var/app/com.valvesoftware.Steam;
Windows HKCU Software\Valve\Steam SteamPath, HKLM WOW6432Node Valve\Steam InstallPath, Program Files (x86)\Steam.

### 13.34 `installer/src/install.cpp`

UTF-16LE text after `key` in a version resource String block: key, NULs to 4-byte alignment, value, NUL.

### 13.35 `installer/src/install.h`

Install / update / repair / uninstall in the folder the game runs from (x64/ or compat/). The plan is made first
and shown; nothing is written while it has a blocker. Rules:
- A file is ours when GDCompare.installed (TSV name \t sha256, the same file scripts/deploy.sh writes) lists it
  with its current hash, or it is byte-identical to the shipped one.
- dinput8.dll and dinput8.ini that are not ours are never overwritten: an Ultimate ASI Loader there is reused,
  anything else is a conflict to resolve by hand.
- GDCompare.* files that are not ours (a manual install, an edited preset) are backed up to
  GDCompare.backup/<time>/ before they are replaced; an edited preset that still parses is kept.
- Saves and game files are never touched; only the names below are written or removed.

### 13.36 `installer/src/platform.h`

Grim Dawn.exe running: Linux /proc (wine names the process after the exe; its cmdline starts with a Windows path),
Windows Toolhelp32. nullopt: could not tell.

### 13.37 `installer/src/saves.h`

Characters from the save folder, read-only: the header of each save/main|user/<folder>/player.gdc.

.gdc header (decoded from three local saves 2026-11-16; names, classes and levels match the characters in game):
key = first u32 ^ 0x55555555; table[i] = k = rotr(k, 1) x 39916801 (mod 2^32) for i = 0..255, k starting at key.
Each later read: value = raw ^ key (low byte for a u8), then key ^= table[b] for every raw byte b.
Fields: magic u32 0x58434447 ("GDCX"), version u32 (2), name (u32 count + count x (u8, u8) UTF-16LE), sex u8,
class tag (u32 count + bytes, "tagSkillClassName0306" or "" before the first mastery), level u32, hardcore u8,
and from version 2 an expansion u8. Nothing past the header is read.

Save roots in the order tried: Steam Cloud (userdata/<id>/219990/remote/save), the Proton prefix's and the
Windows Documents folder's My Games/Grim Dawn/save. One entry per save folder; unreadable saves are skipped.

### 13.38 `installer/src/steam.h`

Steam's text KeyValues files (libraryfolders.vdf, localconfig.vdf, appmanifest_*.acf) and Grim Dawn's launch
options with `dinput8=n,b` merged into WINEDLLOVERRIDES (Wine loads the ASI loader's dinput8.dll only then).
deploy.sh and uninstall.sh print them through `gdcompare-setup launch-options`. Never writes Steam files: Steam rewrites them while it runs.

present: lower-case file names in the game folder; given, DXVK_HDR is dropped unless a renodx*.addon64 is there
(RenoDX HDR). Every other variable and override (dxgi for a DLSS / ReShade mod) is kept as is.

### 13.39 `installer/src/tui.h`

Console UI: arrow-key menus with search and a side panel on terminals that can (Linux termios + ANSI, Windows
console with virtual terminal sequences), numbered menus everywhere else (--plain, pipes, TERM=dumb). Bold and
yellow warnings on any terminal that takes escape codes; NO_COLOR drops the color, "! " still marks warnings.
Every user-facing string goes through tr() so a translation table can be added in one place.

Index into items, or -1 for back (Esc / 0). Enter takes `start`. panel(i): text shown beside (or under) item i.
quits: back leaves setup (the main menu), and the help says so.

### 13.40 `installer/src/wizard_save.cpp`

One setup preset per character: the others drop it (a preset left for no one is removed, after asking).
Dropping a save keeps the name while another listed save of that name still uses the preset.
