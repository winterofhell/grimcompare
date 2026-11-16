# Snapshot (Phase 2)

Press the snapshot key in game (default **F9**). The adapter writes
`<game dir>/snapshots/snapshot-YYYYMMDD-HHMMSS-mmm.json` (UTC) and logs one line to `GDCompare.log`.
Nothing is written in the main menu or on a loading screen.

Key: `GDCompare.ini` next to `GDCompare.asi`:

```ini
[snapshot]
key=120   ; Windows virtual-key code, decimal (F9 = 120). Out of 1..254 → F9.
[freeze]
key=121   ; freeze the preset in use (docs/PRESETS.md); F10 = 121, 0 = off; the snapshot key → off
```

Keys are ignored unless the game window has focus. Neither F9 nor F10 is bound in the game on this install
(checked 2026-11-14: `Settings/keybindings.txt` and `alternate_keybindings.txt` bind DIK 59..65 = F1..F7 only;
DIK 67 / 68 unbound). F10 was picked for freeze because KDE's default global shortcuts take Ctrl+F1..F12
(Ctrl+F9 = Present Windows, the old freeze chord) and Alt+F-keys, and Steam takes F12.

## Rules

- Read-only: engine getters, exported accumulator ctors/dtors on our own buffers, and item getters
  through the item's own vtable. No game memory is written; no value is recomputed.
- Runs on the game thread, after the original `GameEngine::Update` (detour).
- Each section runs under SEH. A fault adds a line to `errors`, sets `complete: false`, and turns
  snapshots off until the game restarts. The file is still written.
- Zero values and empty lists are omitted from the JSON. `difficulty` is always written.
- Floats are written as the shortest decimal that round-trips the engine's `float`.

## Fields → source

All RVAs: Game.dll 1.3.1.1 (`re/Game.exports.tsv`). Labels are the entries in
`core/src/hook_targets.cpp`.

| JSON | Source |
|---|---|
| `schema` | 12 (1 → 2: `devotion_points_spent` renamed, `character.pth` added; 2 → 3: chance fields below, item type-38 `reduction` dropped; 3 → 4: `skills[].passive`; 4 → 5: `skills[].modifier_ids`; 5 → 6: `item_skills[]`; 6 → 7: `requirements`; 7 → 8: `skills[].buff_active`; 8 → 9: `skill_attributes[]`; 9 → 10: `skills[].anim_frames` / `anim_frame_rate`; 10 → 11: `difficulty_ramp`, `requirements.met_at`; 11 → 12: `pets[]`). Readers accept 2..12; 2 has no chance data, 2..3 no `passive`, 2..4 no `modifier_ids`, 2..5 no `item_skills`, 2..7 no `buff_active` (unknown), 2..8 no `skill_attributes` (cooldown reduction then from the items alone), 2..9 no hit animation (cast speed read as uses per second), 2..10 no `difficulty_ramp` (read as 0) and no `requirements.met_at` (the engine's `met` is then not used), 2..11 no `pets` (pets then from their records alone) |
| `mod_version`, `game_build` | adapter constant; known-build table (`core/include/gdc/known_builds.h`) |
| `complete`, `errors` | see Rules |
| `character.level` | `Character::GetCharLevel` |
| `character.difficulty`, `difficulty_name` | `GameEngine::GetGameDifficulty`; names 0 Normal, 1 Elite, 2 Ultimate (`GameEngine::GetGameDifficultyAsPath` 0x2D85B0 / `GetDifficultyFromName` 0x2D8650) |
| `character.save` | schema 14 (1.0.0), `gdc::snap::save_key`: `cloud/<Steam account>/` when `GameEngine::GetCloudStorage` is set, else `local/`; `main/`, or `user/` for a custom game; then `__` + name when Player+0x4BDD is set (the engine's backup folder) or `_` + name (`GameEngine::GetPlayerFolder`, docs/RECON.md §10). Empty when an optional target or the Steam account is missing; a schema 13 key reads as empty. Matched by `[match] saves` (docs/PRESETS.md) |
| `character.difficulty_ramp` | schema 11 (adapter 0.9.9), `GameEngine::GetDifficultyRamp` 0x2D85A0 (GameEngine+0x36D1C; optional target, 0 when unresolved). Above 0 monsters also get the challenge adjustment: Veteran on Normal / Elite, Ascendant on Ultimate (`docs/MECHANICS.md` §9, **unverified** in game) |
| `character.skill_points` | `Character::GetSkillPoints` (unspent) |
| `character.attribute_points` | `Character::GetModifierPoints` (`Character+0x1768`): unspent attribute points — `records/creatures/pc/playerlevels.dbr` grants `characterModifierPoints` per level (vs `skillModifierPoints`); level-3 fixture reads 2 |
| `character.devotion_points` / `_total` | `Character::GetDevotionPoints` (unspent) / `GetTotalDevotionPoints` |
| `character.devotion_current_level_sum` | `SkillManager::GetNumDevotionPointsSpent` (0x534D40). Despite the name: Σ `Skill::GetCurrentLevel` (vt+0x1C8) over skills with `Skill+0x4A0 ≠ 0`, i.e. devotion nodes and celestial powers at their current level (level-96 snapshot: 163 with 55 points spent). Points actually spent = Σ `skills[].level` over devotion records (55) |
| `character.pth` | CombatManager PTH table read in place at `Character+0x7A8` (CombatManager embedded at `Character+0x530`, see `snap::PthTable`): `minimum` (`pthMinimum`), `crit_divisor_min` (+0x27C, 100.0 from the CombatManager ctor), `thresholds[6]` / `damage_modifiers[6]` (`pthThreshold1..6` / `pthDamageModifier1..6`, `CombatManager::LoadRecord`) |
| `character.life_max`, `mana_max` | `Character::GetLifeLimit`, `GetManaLimit` |
| `character.attack_speed`, `cast_speed` | `Character::GetAttackSpeed`, `GetSpellCastSpeed` |
| `character.run_speed`, `run_speed_uncapped` | `Character::GetRunSpeed(false / true)` (0x69040): `CharAttributeAccumulator(Character+0xF78).GetValue(14) × (1 − 0.01·slow)`; `true` returns before the cap, `false` applies `CapAbsoluteRunSpeed` / vt+0x678 (`Player::CapRunSpeed` 0x3E5A10) |
| `character.defense_cap` | `Player::GetDefenseAttributeCap` (0x3E59F0, `Player+0x5010`); `gameengine.dbr playerDefenseCap` = 80 |
| `character.char_attributes[]` | per `CharAttributeType` 1..57 (`re/CharAttributeType.tsv`): `total` = `GetTotalCharAttribute`, `modifier` = `GetTotalCharModifier`, `base` = `GetBaseCharAttribute`, `points_spent` = `GetPointsSpent` (types 1–3 only) |
| `defense[]` | `Character::GetAllDefenseAttributes` into a `CombatDisplayAccumulator(true)` (all combat accumulators, see below), then per `CombatAttributeType` present (`re/CombatAttributeType.tsv`): `GetTotalDefenseType`, `GetTotalDefenseModifierType`, `GetTotalReduction`, `GetTotalDamageModifierType` (direct = 1, duration = 2) |
| `defense[].processed`, `cap_bonus` | a second accumulator filled the same way, then `CombatAttributeAccumulator::ProcessDefense` (0x11AB40) with a zeroed 0x18-byte `ReductionInfo` and zeroed `DefenseSpecialInfo` (no attacker) → `GetTotalDefenseType`. `ProcessDefense` executes the defense modifiers into the defenses, deletes the modifiers (our accumulator only) and calls each defense's `Process` (vt+0x28). `CombatAttributeAbsDefense::Process` (0x115370): `def += |def|·mod·0.01`, then `min(def, Character vt+0x668 cap + [+0x2C])`. `cap_bonus` = max `[+0x2C]` (written by `ModifyDefenseCap`, vt+0xA0, 0x113AE0) over entries whose vt+0x28 is exactly `CombatAttributeAbsDefense::Process`; `capped` = such an entry exists (schema 3; schema 2 fixtures lack it). Level 96: Disruption and Stun cap at 80 too |
| `damage_modifiers[]` | what `Player::CalculateDamageModifiers` (0x3CD7A0) does without storing it: own `CombatAttributeAccumulator` + `SkillManager::CollectAvailableOffensiveModifierAttributes(acc, 0)` (0x536260, SkillManager at `Player+0x850`), then the aggregate getters as for `defense`. Type 59 = crit damage modifier |
| `conversions[]` | `SkillManager::CollectAvailableConversionAttributes` into a `ConversionAttributeAccumulator`; layout below |
| `skill_attributes[]` | schema 9 (adapter 0.9.4): `SkillManager::CollectAvailableSkillAttributes` (0x535BE0) into a `SkillAttributeAccumulator`, read like an item's `stats.skill_attributes[]`. It calls `Character::GetEquipSkillAttributes` (0x69910: vt+0x4A0 of every equipped item), the character's vt+0x6D8 / +0x718 / +0x750, then `SkillManager::GetSkillAttributes` (0x52FA90: vt+0x410 of every learned skill that is on, devotions included, of the buffs in the buff map and of the item skills). Type 1 = cooldown reduction, 2 = energy cost. The engine's total is `SkillAttributeAccumulator::GetValue(type, with_chance)` (0x51F1A0) = `value + |value| × modifier × 0.01`, the chance parts added when the flag is set (which flag the cooldown uses: **not traced**) |
| `skills[]` | `SkillManager::GetSkillList`; per skill: `Object::GetObjectId`/`GetObjectName`, `Skill::GetSkillType` (`Skill+0xA8`, unnamed), `GetSkillLevel` (points), `GetAugmentedLevel` (+skills from gear), `GetCurrentLevel` (effective), `GetDevotionLevel`, `GetDevotionMaxLevel`, `GetMasteryId`. Masteries and devotion nodes are skills in this list. |
| `skills[].anim_frames`, `anim_frame_rate` | schema 10 (adapter 0.9.8), skills with current level > 0 that are-a `SkillActivatedSpell` / `SkillActivatedBuffSelf` and whose vt+0x6E0 is the exported `SkillActivated::GetHitCallbackPoint` (0x51A310), called as `Player::CalculateSkillDps` does (0x3E0981..0x3E09A3: player, Name = 0, frames, rate; return value ignored). It takes the animation named by the skill's record (Skill vt+0x578 result +0x538) or else the character's animation for the skill's animation type (Skill+0x5C4, ≤ 0x33), and the animation object's vt+0x80 (0x20B40) writes `GraphicsAnim::GetLength` (Engine 0x977A0) to the first and `GetFrameRate` (0x977C0) to the second output. Omitted when 0 (other classes, no animation, or a fault: logged, `complete` false). Depends on the animation set, so on the equipped weapon type |
| `skills[].modifier_ids` | schema 5: Skill+0x140 `mem::vector<u32>` — `Skill::GetModifiers` (0x3C5970) returns its address, `Skill::AddModifier` (0x48FD00) appends to it, `Skill::AddModifierDefenseAttributes` (0x494970) walks it (then the item skill modifiers at +0x158) and adds each object that is-a `Skill_Modifier` through vt+0x6A8. Ids of learned modifiers / transmuters attached to this skill; omitted when empty |
| `skills[].passive` | schema 4, skills with current level > 0: the skill's own vt+0x408 `CollectPassiveCharAttributes` (CharAttributeAccumulator) and vt+0x438 `CollectPassiveDefenseAttributes` (CombatDisplayAccumulator(true), like `defense`). Slots checked against `Skill_Passive`'s vftable (0x711BA0: 0x5011D0 / 0x4FB690); `Skill_BuffSelfToggled`'s overrides sit at the same slots (0x707B80 / 0x707BB0), and its defense collector adds only while toggled on (`+0xDC` set, `+0x618` clear, 0x4EC59A..0x4EC5B0) at `GetCurrentLevel`. Omitted = contributes nothing now. A fault in one skill drops only that skill's `passive` (logged, `complete` false) |
| `item_skill_modifiers[]` | per skill, the 0x30-byte entries of the vector at `Skill+0x158` (`Skill::AddToItemSkillModifiers` 0x48FD30, `GetItemSkillModifiers` 0x48FFD0): `{u32 modifier skill id, EquipLocations +4, std::string set +8, bool relic +0x28, bool augment +0x29}`. The modifier id → object lookup; `stats` only if its class is exactly `Skill_Modifier` (`Skill_Modifier::classInfo`): `CollectModifierOffensiveDamageAttributes(acc, 1.0)`, `…OffensiveModifierAttributes`, `…SkillAttributes`, `…ConversionAttributes` |
| `item_skills[]` | schema 6: `SkillManager::GetItemSkillList` (0x531730, `SkillManager+0x258`): the skills equipped items grant (`itemSkillName`), each read like `skills[]` (incl. `passive`) plus `item_id` = `Skill::GetItemSkillItemId` (0x31D500, `Skill+0x350`). `SkillManager::GetCharAttributes` (0x52F920), `GetDefenseAttributes` (0x530C10) and `GetOffensiveModifierAttributes` (0x52FDA0) walk this list after the learned skills and call vt+0x408 / +0x438 / +0x420 on each skill whose vt+0x1C8 (`Skill::GetCurrentLevel`) is non-zero, so `passive` is what it adds to the totals. Keyed by `id` in the store (two items may grant one record) |
| `skills[].buff_active`, `item_skills[].buff_active` | schema 8 (adapter 0.9.2): `Character::IsSkillBuffActive` (0x67DD0) = find in the buff map at Character+0x950 (`GetSkillManager` 0x6DA90 = +0x850, map at SkillManager+0x100 like `SkillManager::IsSkillBuffActive` 0x53B510), asked with the skill's record and, if not found, `Skill::GetBuffSkillName` (0x17EC0, `Skill+0x40`). Key evidence: `SkillServices_Character::CreateUpdateSkillBuff` (0x5586F0) passes `SkillBuffTransfer+0` (a record name, checked with `SkillManager::FindSkillId`) to `CreateRemoveSkillBuffConfigCmd`, whose `SkillManager::CreateRemoveSkillBuff` (0x53A990) finds / erases it in that map. Written only when true; omitted = not up. A fault leaves it out (logged, `complete` false). Which of the two names a given buff uses: not traced |
| `equipped[]` | ids from `Character::GetAttachedItems` (`Character+0x5D0`, 12-byte `EquipManagerContainer {id, location, flags}`) → object lookup (below). `location`, `location_name`: `EquipLocations`, names from `LocationToString` (0x286480): 0 R Hand, 1 L Hand, 2 Neck, 3 Medal, 4/5 Finger, 6 Waist, 7 Head, 8 Chest, 9 Shoulders, 10 Hands, 11 Legs, 12 Feet, 13 Artifact, 14 Clothing |
| `tooltip_items[]`, `tooltip_age_ms` | ids latched by the `ItemEquipment/ItemArtifact::GetUIDisplayText` detours in the latest game tick that built a tooltip; age since then |
| `inventory[]` | `Character::GetInventoryItems` ids → lookup; identity + replica only, no stats |
| `pets[]` | schema 12 (adapter 0.9.12), optional targets (all four or none: `Character::GetPetPen`, `Character::classInfo`, `SkillActivated::classInfo`, `SkillManager::GetDefaultSkillId`). The player's PetPen (`GetPetPen` 0x7E100 = Character+0x2050) holds an MSVC `std::list` at +8 (`PetPen::GetPetOwner` 0x3C9B00 walks it); node +0x10 pet id, +0x14 the summoning skill's id (`skill_id`), +0x1C released (`Skill_SpawnPet::KillExtraPets` 0x509890 counts +0x1C == 0 and +0x14 == its id, releases +0x10 and sets +0x1C). Released nodes are skipped, at most 64 nodes are walked. Per pet that the object lookup finds and that is-a `Character`: `id`, `record` (`GetObjectName`), `attack_speed` / `cast_speed` (`Character::GetAttackSpeed` / `GetSpellCastSpeed` on the pet: its own accumulator, pet bonuses, slows and `Monster::CapAttackSpeed` in), `attack_speed_modifier` / `cast_speed_modifier` (`GetTotalCharModifier` 15 / 16), `default_skill_id` (`SkillManager::GetDefaultSkillId(0)` 0x52EC10 of the pet's SkillManager), `skills[]` from its `SkillManager::GetSkillList`: `id`, `record`, `current_level`, and `anim_frames` / `anim_frame_rate` from `SkillActivated::GetHitCallbackPoint` called with the pet for skills that are-a `SkillActivated` with that slot (as `CalculateSkillDps` checks before its call, 0x3E09A3). Each pet runs under its own SEH guard: a fault drops that pet (logged, `complete` false) |

Per item (`equipped`, `tooltip_items`, `inventory`):

| JSON | Source |
|---|---|
| `id`, `record` | `Object::GetObjectId`, `Object::GetObjectName` |
| `kind` | RTTI walk: object vtable slot 0 = `GetClassInfo`, parent at `ClassInfo+0x10`, compared with the exported `*::classInfo` |
| `set` | `ItemEquipment::GetItemSetName` |
| `replica` | `ItemReplicaInfo` at `Item+0x538` (`Item::GetItemReplicaInfo` 0x3244D0). Offsets: base +0x08, prefix +0x28, suffix +0x48, seed +0x68, modifier +0x70, relic +0x90, relic_bonus +0xB0, relic_seed +0xD0 (`ItemReplicaInfo::StreamProperties` 0x585840); augment +0xD8, augment_level +0xF8, augment_seed +0xFC, transmute +0x100, ascendant +0x120, ascendant2h +0x140, var1 +0x160, stack_count +0x178, affix_rerolls +0x17C, rerolls +0x180 (stream order, cross-checked with gd-uniquetab `ut_gds.h`) |
| `stats.char_attributes[]` | vtable slot 0x498 (`GetCharAttributes`) into a `CharAttributeAccumulator`; values `float[58]` at +0, modifiers at +0xE8 |
| `stats.defense[]` | slot 0x4C8 (`GetDefenseAttributes`) |
| `stats.offensive_modifiers[]` | slot 0x4B0 (`GetOffensiveModifierAttributes`) |
| `stats.retaliation_modifiers[]` | slot 0x4C0 (`GetRetaliationModifierAttributes`) |
| `stats.offensive_damage[]`, `stats.retaliation[]` | slots 0x4A8 / 0x4B8 (`GetOffensiveDamageAttributes(acc, float)`, `GetRetaliationAttributes(acc, float)`), called with 1.0. The float is a scale factor: `Player::CalculateRetaliationValues` passes 1.0 (constant at 0x7A8054). Per entry of the accumulator's vector at +0x08: vt+0x10 type, vt+0x20 `GetDamageType` (2 = duration → `dot`), vt+0x78 `GetMinMaxValues`, vt+0xF0 `GetDuration`. Weapon base damage is a `CombatAttributeDamage_BasePhysical` (type 1) whose `GetMinMaxValues` writes nothing (its +8 type is 0); for that vftable (0x7358E8) min/max come from `GetValues` (vt+0x110, outputs 2/3 = +0x24/+0x28). ItemEquipment/ItemArtifact only: on ItemRelic/ItemEnchantment these slots are stubs (0x15880); their real getters take an extra parent object id and are called by the parent |
| `stats.skill_attributes[]` | slot 0x4A0 (`GetSkillAttributes`) into a `SkillAttributeAccumulator`: `float[5]` at +0x08 value, +0x1C chance value, +0x30 modifier, +0x44 chance modifier. Types 1 CooldownReduction, 2 ManaCostReduction, 3 ProjectileSpeedModifier, 4 ComboChargeSpendReduction (typed ctors 0x51EE70–0x51F050) |
| `stats.conversions[]` | slot 0x4D0 (`GetConversionAttributes`) into a `ConversionAttributeAccumulator`: `vector<Data*>` at +0, `Data {u32 from; vector<{u32 to, float pct}> +8; float total +0x20}` (`AddAttribute` 0x17D890) |
| `stats.offensive_damage[].chance`, `raw_min`, `raw_max` | chance-based entries only ("X% chance of …"): chance in percent and min/max at 100%. `min`/`max` stay the expected values the engine computes. From the chance passes below; entries matched by index |
| `stats.defense/offensive_modifiers/retaliation_modifiers[].proc` | types with a chance-based part: `{chance, defense, defense_modifier, reduction, damage_modifier, dot_modifier}` = that part at 100% (raw − excluded pass); `chance` = (probe − excluded) / (raw − excluded) for the first field that has one. Exact for one chance stat per type, a raw-weighted mean for several. Same for `item_skill_modifiers[].stats.offensive_modifiers` / `offensive_damage` |
| `relic`, `augment` | `ItemEquipment::GetRelic` / `GetEnchantment` → same stats on the component object (no damage slots) |
| `requirements` (schema 7, ItemEquipment) | `level`, `physique` / `cunning` / `spirit`: the item's own virtual getters vt+0x528 / 0x530 / 0x538 / 0x540 (verified against `ItemEquipment::Get*Requirement` at init; each the max of item, relic, augment). `reductions`: per attribute the three CharAttributeType ids at +0xDF8/+0xDEC/+0xE04, +0xDFC/+0xDF0/+0xE08, +0xE00/+0xDF4/+0xE0C, as `ItemEquipment::AreRequirementsMet` (0x343C30) reads them. `met`: that function called with the player. `met_at` (schema 11): `level` (`Character::GetCharLevel`), `difficulty`, `attributes` (`Character::GetTotalCharAttribute` 1..3) at that call; `met` is used only for a snapshot in the same state. Formula: `gdc/requirements.h` |

Component stats are **already included** in the parent's `stats`: `ItemEquipment`'s `GetCharAttributes`,
`GetSkillAttributes`, `GetConversionAttributes`, `GetRetaliationAttributes` and
`GetOffensiveDamageAttributes` each also call the relic (`+0x1558`), enchantment (`+0x1560`) and
`+0x1568` object. Do not add `relic.stats` / `augment.stats` to `stats`.

All eight slots 0x498–0x4D0 are compared at startup with the exported `ItemEquipment`/`ItemArtifact`/
`ItemRelic`/`ItemEnchantment` vftables (`{for GAME::Object}`; damage slots skipped for relic/enchantment).
A mismatch disables snapshots.

Every `CombatAttributeAccumulator` the adapter fills is a `CombatDisplayAccumulator(true)` (ctor 0x11B980):
`EvaluateChance` always true and `GetChanceMultiplier` = chance × 0.01 for 0 < chance < 100, so
"X% chance of …" stats count at their expected value. A plain accumulator rolls `EvaluateChance` randomly
(0x11B180), which put chance damage randomly in or out of a snapshot. The flags (+0x84 = 1, +0x85 = 0,
+0x88 = 0) equal those `Skill::CollectWeaponDamage` (0x4991AB) writes inline for the sheet.

Chance passes (item combat getters only). `DamageAttributeAbsBaseElemental::AddDamageToAccumulator`
(0x18F530) calls `acc vt+0x18 GetChanceMultiplier(chance)` with the stat's chance (DamageAttribute vt+8)
and multiplies rolled/min/max by it; the entry is always created and the chance is not stored. Items set a
group chance through the virtual `SetGlobalChance` (vt+0x28 → +0x88), which `GetChanceMultiplier` (0x11BA10)
prefers, so `SetIncludeProcs` alone would not give 100% values. Each getter therefore runs four times: on
the plain display accumulator (expected), and on three with a copy of its 8-slot vftable (0x6D3CF8;
CombatAttributeAccumulator's starts at 0x6D3D38) whose slot 0x18 returns, when the original multiplier
≠ 1: 1 (raw), 0 (excluded), the effective chance in percent (probe: `+0x88` if > 0, else the stat's
chance). Multiplier-1 stats are unchanged in all passes. The dtor (0x11B9F0) restores the vptr. Slot 0x18
is checked against the export at startup; a mismatch disables snapshots.

`reduction` for type 38 (`CombatAttributeDefense_AbsorptionProtection`) is not written: `GetTotalReduction`
(0x1182F0) reads +0x28, which only `Process` (0x118370) writes and the ctor (0x1182A0) leaves
uninitialized. Fixtures had those garbage values removed.

Object lookup: unexported `ObjectManager` id→object (RVA 0x19D40). Found at startup by decoding the
call in `Character::GetEquipCharAttributes` (`call [IAT Singleton<ObjectManager>::Get]; mov rcx,rax;
mov edx,edi; call rel32`) and accepted only if the IAT slot holds Engine.dll's
`Singleton<ObjectManager>::Get`. Exactly one match is required (`core/src/code_scan.cpp`).

## Auto snapshots

The adapter also captures by itself (`adapter/src/hooks.cpp`); F9 stays manual and also writes the
pretty `snapshots/snapshot-*.json` as before.

- **Triggers:** on `GameEngine::Update` (the hook F9 already uses), at most every 100 ms,
  the adapter probes the main player: pointer (`load`), difficulty, character level (`level`; the
  points that come with it are not tagged separately), unspent skill / attribute / devotion points
  (`skill`, `attribute`, `devotion`), and an FNV-1a hash over the attached items' (id, location,
  component object id, augment object id; 0 = none) (`equip`). Socketing a component or applying an
  augment keeps the item id and slot, so the component ids are what catch it (`ItemEquipment::GetRelic` /
  `GetEnchantment` read `+0x1558` / `+0x1560`, `Object::GetObjectId` reads `+0x2C`; Game.dll / Engine.dll 1.3.1.1). A change opens a debounce window (`auto_debounce_ms`, default 2000); one capture follows
  when the probe has been quiet that long. A burst that ends in the state of the last capture writes
  nothing.
- **Cost:** the probe calls six getters plus, per attached item, the object lookup and the two
  component getters under SEH, and allocates nothing. A capture copies the same sections as F9 minus
  tooltips and inventory; the log shows its game-thread time and each section's:
  `auto snapshot (equip): game thread N us; delta <file>, B bytes, writer M ms`, then
  `sections us: character …, skills …`. JSON, gzip and disk I/O run on the writer thread (queue of at
  most 8). Since 0.3.1 jobs are recycled (`snap::recycle`, up to 8 kept): strings and plain vectors keep
  their buffers and the reader's per-pass temporaries are game-thread scratch, so a warm capture
  allocates only for optional per-item / per-skill stats.
- **Faults:** a probe fault, a section fault or a writer exception turns auto snapshots off for the
  session (`auto snapshots off` in the log); the game continues.
- **Storage** (`gdc::store`, `core/include/gdc/store.h`): `snapshots/<character>/`, the folder name
  from `Player::GetPlayerName` (ASCII letters, digits, `-`, `_`; other bytes `_` plus a hash).
  The first record per character and session is `full`; later ones are `delta` RFC 7386 merge patches
  against the previous record in keyed form (arrays such as `equipped`, `skills`, `defense` become
  objects keyed by location, record or type). One gzip'd compact JSON file per record:
  `<segment>-<seq>-<full|delta>.json.gz`, with `format`, `kind`, `segment`, `seq`, `trigger`, `taken_at`
  and `snapshot` or `patch`. A segment (chain) is closed and a new full record starts once it exceeds
  a quarter of the cap. When the folder exceeds `auto_cap_mb` (default 50), whole closed segments are
  deleted, oldest first.
- **Reading:** `tools/snapsum [DIR]` prints one short block per record (trigger, changed slots,
  skill levels, sheet values); `tools/snapsum --full SEGMENT-SEQ` rebuilds one record as a normal
  snapshot JSON (`snap::from_json` reads it).
- **INI** (`GDCompare.ini`, `[snapshot]`): `auto` (1), `auto_debounce_ms` (2000, 200–60000),
  `auto_cap_mb` (50, 1–4096).

## Not captured / unverified

- Not exported as getters, so core computes them (`core/include/gdc/combat_formulas.h`, equations from
  `records/game/combatformulas.dbr`): OA, DA, probability to hit. The engine's own
  `CombatManager::DesignerCalculate*` write CombatManager scratch variables and are not called.
- Crit chance: `Character::DesignerCalculateCriticalChance(pth)` (0x686D0) =
  `max(0, (pth − pthThreshold2) / max(CM+0x27C, pth))`, CM+0x27C = 100 (`formulas::critical_chance`).
  The sheet's "Chance to Crit" is against the **last attacked** monster; that target and its DA are not
  read (the UI code is in the SteamStub-encrypted exe). Sheet 24% at OA 2946 ⇔ target DA ≈ 1925–1965.
- Sheet "Attacks per Second" (2.76 = 2.303 × 1.2 at level 96): the 1.2 is not traced. `attack_speed` (2.303) = 1.25 × (1 + 0.88) × (1 + Σ weapon `characterBaseAttackSpeed` × `dwWeaponSpeedFactor` 0.5) matches the sheet's Attack Speed 184%.
- Health / Energy regeneration: computed by core from `records/game/playerresourcebehavior.dbr`
  (`formulas::life_regen` / `mana_regen`); matches the sheet (86.04 / 24.08). The engine's
  `CharacterBio::GetLife/ManaRegenValue` write the equation variables into CharacterBio → not called.
- Armor as shown on the sheet: `defense[]` type 38 is the unmerged sum of all items. The sheet (1408)
  matches `(Σ region% × item armor + belt) × (1 + mod)` with `combatformulas.dbr` region chances
  (inferred, not implemented).
- `cap_bonus`, max resist bonus from gear: `defense_cap` + `cap_bonus` = sheet max resist (80 + 3 = 83)
  is inferred, not verified in game.
- Skill `type` names: no string table found.
- Sheet DPS, current life/energy, money: not read.
- Stash / transfer stash: Phase 5.
- `tooltip_items` may contain the hovered item **and** the equipped items it is compared with (the
  game builds comparison tooltips too). The hovered one is usually the one not in `equipped`.

## Test fixtures

`tests/fixtures/snapshots/`: level 3 with/without a tooltip, level 96 before/after a ring swap, level 96 phase 2.1 (sheet open, junk axe hovered),
level 96 with two max-poison items in ring (5) and chest (8) vs the phase 2.1 one (Poison 25 → 105 raw, 91 processed, cap 80 + 11)
(2026-11-14). All schema 2 (no chance data). Parsed by `snap::from_json`; `test_combat_formulas` checks OA/DA against the sheet.
`lvl96-s4-{before,after}-{ring-c031,chest-b002e}.json`: schema 4, rebuilt with `snapsum --full` from store segment
20261114-123319-910 records 1/2 and 3/4 (dual-wielded b013e swords: Possession item skill modifiers).
`lvl96-s5-{before,after}-ring-c031.json`: schema 5 (0.3.2), store segment 20261114-125844-357 records 5/6 (b201e → c031,
Blood of Dreeg's modifier through its captured parent link).
