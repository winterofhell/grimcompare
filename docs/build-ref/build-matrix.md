# Grim Dawn Build Stat-Priority & Conversion Matrix (Patch 1.3.1.x / FoA)

> Complete stat-priority, damage-type, and conversion config reference for all non-meme archetypes across Melee, Ranged, Casters, Pets, and Hybrids.

---

## Global Config Rules

- **Conversion Order:** Skill-specific conversions (transmuters and item skill modifiers) apply *before* global conversions. A damage type can only be converted once. Always match **Resistance Reduction (RR)**, **Flat Damage**, and **% Damage** to the *post-conversion* damage type.
- **Speed Scaling Split:**
  - **Attack Speed:** DARs (`Cadence`, `Savagery`, `Fire Strike`, `Righteous Fervor`, `Onslaught`), WPS procs, `Eye of Reckoning`, `Blade Arc`, and Werewolf `Feral Claws`.
  - **Cast Speed:** `Forcewave` (with *Tremor*), `Stun Jacks`, `Phantasmal Blades`, `Bone Harvest` spam, channeled rays/streams (`AAR`, `Drain Essence`, `FoI`, `Winds of Asterkarn`), and spell rotations.
- **WPS Cap:** Non-transformed DAR builds aim for **100% combined WPS chance** (using *Ascended Affixes* in 1.3.1+ where needed).
- **Berserker Forms (`Werewolf` / `Wereraven`):** Standard DARs and WPS are disabled while transformed. Werewolf scales off Weapon Damage + Attack Speed (`Feral Claws`); Wereraven supports spell casting (`Ice Talons`, `Bone Harvest`, `PRM`) and grants `+10% TDM` / cast speed via *Form of the Beast*.
- **Player-Scaled vs. True Pets:** True pets ignore all player `% Damage`, `OA`, and `Crit`—they only scale with **Pet Bonuses** and enemy **RR**. Player-scaled pets (`Storm Totem`, `Wind Devil`, `Mortar Trap`, `Blade Spirit`, `Guardians of Empyrion`) ignore Pet Bonuses and scale strictly with **Player stats**.

---

## A. Melee

### 1. Blitz / Vire Skater (*The Bonkfather*)
- **Classes:** Warlord, Battlemage, Thane
- **Damage:** Physical + Internal Trauma (Thane also has a Bleed *Leap* variant via *Jarldin* set)
- **Conversion:** None (pure Physical/IT; avoid Elemental/Chaos flat bloat)
- **Stat Priorities:**
  1. **Offense:** `% Physical`, `% Internal Trauma`, `Flat Physical`, `Physical RR`, `Crit Damage`, `OA`
  2. **Cooldown / Speed:** `CDR` (for `Blitz` / `Vire's Might` / `Leap` loops), moderate `Attack Speed`
  3. **Attributes:** High `Physique` (armor/HP/shield reqs) + `Cunning` (OA & Physical %)
  4. **Defense:** `Block Chance / Recovery`, `Armor` + `100% Armor Absorption`, `Physical Resist`, `HP Regen`, `ADCtH`

### 2. Forcewave 2H
- **Classes:** Warlord, Death Knight, Battlemage, Commando
- **Damage:** Physical + Internal Trauma (Warlord/DK/Battlemage) | Fire + Burn (Commando)
- **Conversion:** Physical route: None. Fire route: `100% Physical -> Fire` via *Sunherald's Claymore* (*Menhir's Bulwark* mod) or *Shar'Zul's Worldeater* + global conversion.
- **Stat Priorities:**
  1. **Speed:** `Cast Speed` (mandatory — *Tremor* Forcewave scales with Cast Speed, NOT Attack Speed)
  2. **Offense:** `% Physical` or `% Fire`, `Flat Physical/Fire`, `OA`, `Crit Damage`, `Forcewave / Rending Force ranks`
  3. **Attributes:** `Physique` (heavy armor), remainder `Cunning` (Physical) or `Spirit/Physique` (Fire)
  4. **Defense:** `ADCtH`, `Physical Resist`, `Armor`, `Energy Regen`

### 3. Belgothian DW Pierce
- **Classes:** Blademaster, Reaver
- **Damage:** Pierce (pure)
- **Conversion:** Mandatory `100% Physical -> Pierce` (`Blades of Nadaan` devotion + weapon Armor Piercing + *Belgothian* / *Servitor's Slicer*)
- **Stat Priorities:**
  1. **Attributes:** Heavy `Cunning` dump (scales Pierce % + OA; minimum `Physique` for gear)
  2. **Offense:** `Attack Speed` (cap), `Flat Pierce`, `% Pierce`, `OA`, `Crit Damage`, `100% WPS Pool`
  3. **Defense:** `ADCtH`, `Physical Resist`, `Health`, `CC Resists`

### 4. DW Cold / Acid Assassin
- **Classes:** Infiltrator (Cold), Dervish (Acid), Witch Hunter (Acid/Poison)
- **Damage:** Cold + Frostburn (Infiltrator) | Acid + Poison (Dervish, WH)
- **Conversion:**
  - Cold Infiltrator: `Pierce / Physical -> Cold` (*Deathmarked*, *Chillheart*, *Hagarradian*)
  - Acid Dervish: `Dreeg's Reproach` (`100% Phys/Fire -> Acid` on RF) + global `Cold -> Acid` (for *Lethal Assault* flat Cold) + `Pierce -> Acid`
  - Acid WH: Global `Cold / Vitality / Physical -> Acid`
- **Stat Priorities:**
  1. **Offense:** `Attack Speed` (cap), `Flat Cold/Acid`, `% Cold/Frostburn` or `% Acid/Poison`, `100% WPS` (RF) or `CDR` (*Shadow Strike*), `OA`, `Crit Damage`
  2. **Attributes:** `Physique` (armor), split `Cunning` / `Spirit`
  3. **Defense:** `ADCtH`, `Dodge/Deflect`, `Inquisitor Seal` (Infiltrator) or `Ascension` (Dervish), `CC Resists`

### 5. DW Cold / Acid Avalanche (100% WPS)
- **Classes:** Evoker (Cold — *2x Chillflame Evoker*), Thane (Acid — *Gloom Knight*)
- **Damage:** Cold (Evoker) | Acid (Thane)
- **Conversion:**
  - Evoker: `100% Fire & Chaos -> Cold` (*2x Chillflame Evoker* + *2x Seal of the Void*)
  - Thane: `Physical / Cold / Pierce -> Acid` (*Gloom Knight* + *Agony*)
- **Stat Priorities:**
  1. **Special:** `+Avalanche Activation Chance` (via *Ascended Affixes*) to hit `100% WPS`
  2. **Offense:** `Attack Speed`, `Flat Cold` or `Flat Acid`, `% Cold` or `% Acid`, `OA`, `Crit Damage`, `RR`
  3. **Attributes:** `Physique` (gear reqs), split `Cunning` / `Spirit`
  4. **Defense:** `Armor` + `100% Armor Absorption`, `Physical Resist`, `ADCtH`, `Circuit Breakers`

### 6. Cold / Pierce Ring of Steel + Onslaught
- **Classes:** Reaver (*Chillheart* / *Korba* for Cold; *Steel Piercer* for Pierce)
- **Damage:** Cold + Frostburn | Pierce
- **Conversion:**
  - Cold: `Pierce / Physical -> Cold` (*Korba*, *Chillheart*, *Ugdenbog Chillstrife*)
  - Pierce: `Cold / Physical -> Pierce` (*Steel Piercer* + *Blades of Nadaan*)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed` (stack `Onslaught` fast), `+Max Onslaught Charges`, `% Cold/Frostburn` or `% Pierce`, `Crit Damage`, `OA`
  2. **Cooldown:** Balanced `CDR` (do not lower RoS CD below time needed to stack `Onslaught` if using *Chillheart*)
  3. **Attributes:** `Cunning` (Pierce) or balanced `Physique/Cunning/Spirit` (Cold)
  4. **Defense:** `ADCtH`, `Dodge/Deflect`, `Physical Resist`, `Health`

### 7. Cold DW Cadence (100% Zolhan)
- **Class:** Tactician (*2x Arctis*)
- **Damage:** Cold (+ minor Frostburn)
- **Conversion:** `100% Physical -> Cold` (*2x Arctis* global + *Field Command* mod)
- **Stat Priorities:**
  1. **Special:** `+Zolhan's Technique Activation Chance` (*Ascended Affixes* to hit 100%)
  2. **Offense:** `Attack Speed`, `Flat Cold`, `% Cold`, `OA` (*Rune of Hagarrad - Biting Cold* for -DA), `Crit Damage`
  3. **Defense:** `Inquisitor Seal`, `Physical Resist`, `ADCtH`, `Health`

### 8. Shadow Strike
- **Classes:** Reaper (*Deathguard* / *Morgoneth*), Spellbreaker (*Zakull* / *Deathmarked*), Infiltrator
- **Damage:** Acid + Poison (*Deathguard*) | Cold + Frostburn (Spellbreaker / *Morgoneth*) | Pierce (Infiltrator)
- **Conversion:** *Deathguard*: `Cold/Vitality/Physical -> Acid`. *Zakull* / *Morgoneth*: `Physical/Pierce/Aether -> Cold`.
- **Stat Priorities:**
  1. **Cooldown / Speed:** `CDR` (Shadow Strike uptime + defensive mirrors) + `Attack Speed` (for AA hybrids)
  2. **Offense:** `% Acid/Poison` or `% Cold/Frostburn` or `% Pierce`, `Flat Damage` (*Soul Harvest*, *Lethal Assault*), `OA`, `Crit Damage`
  3. **Defense:** `Max Health`, `ADCtH`, `Mirror / Mark of Torment` uptime, `CC Resists`

### 9. Primal Strike 2H / DW
- **Classes:** Warder, Archon, Vindicator, Trickster (DW *Stormrend*)
- **Damage:** Lightning + Electrocute (Vitality variants on Ritualist/Conjurer)
- **Conversion:** `Physical -> Lightning` (*Korvan Storm Halberd* / *Ultos* / *Stormrend* + global Phys->Lightning)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed` (cap), `Flat Lightning`, `% Lightning / Electrocute`, `Lightning RR`, `OA`, `Crit Damage`
  2. **Attributes:** `Physique` (armor), `Cunning/Spirit` for OA/Energy
  3. **Defense:** `ADCtH`, `Health Regen` (*Wendigo Totem* / *Mogdrogen's Pact*), `Physical Resist`, `Energy Regen / Leech`

### 10. Eye of Reckoning (Spin-to-Win)
- **Classes:** Archon (Lightning), Shieldbreaker (Fire), Zealot (Physical/Bleed/Chaos), Warlord (Physical), Dervish (Acid)
- **Damage:** Single target element per variant (Lightning / Fire / Physical / Acid / Chaos)
- **Conversion:** Match `Physical / Fire / Lightning` of EoR + *Soulfire* to chosen element (*Cyclone* for Lightning; *Warborn* + *Beronath/Shard of Menhir* for Physical; *Dunefiend* for Acid)
- **Stat Priorities:**
  1. **Speed:** `Attack Speed` (scales EoR tick rate directly; aim 190–200%+)
  2. **Offense:** `Flat Damage` (matching element), `% Element`, `EoR / Soulfire ranks`, `RR`, `OA`, `Crit Damage`
  3. **Defense:** `ADCtH`, `Max Health` (20k+), `Physical Resist`, `Energy Regen / Leech`, `CC Resists`

### 11. Sword & Board Brawler
- **Classes:** Warlord, Conjurer (*Brute Windcaller*), Sentinel, Oppressor
- **Damage:** Physical + Internal Trauma (+ Bleed on Conjurer)
- **Conversion:** None (or `Elemental/Chaos -> Physical` via *Beronath, Reforged* / *Shard of Menhir*)
- **Stat Priorities:**
  1. **Defense:** `Block Chance`, `Shield Damage Blocked`, `Block Recovery`, `Armor` + `100% Armor Absorption`, `Physical Resist`, `HP Regen`
  2. **Offense:** `Attack Speed`, `Flat Physical`, `% Physical / IT`, `Physical RR`, `OA`
  3. **Attributes:** Heavy `Physique` + `Cunning` for OA

### 12. Krieg / Blood Knight Death Knight
- **Class:** Death Knight
- **Damage:** Aether (*Krieg*) | Vitality (*Blood Knight*)
- **Conversion:** *Krieg*: `100% Physical / Vitality -> Aether` (*Mindwarp* / *Krieg*). *Blood Knight*: `Physical / Cold -> Vitality` (*Edge of Death* / *Nightbringer*).
- **Stat Priorities:**
  1. **Offense:** `Attack Speed`, `Flat Aether` or `Flat Vitality`, `% Aether` or `% Vitality`, `RR`, `OA`, `Crit Damage`
  2. **Defense:** `Max Health` (especially *Blood Knight*, set gives 0 HP), `ADCtH`, `Armor`, `Physical Resist`

### 13. Sunherald Fire Blade Arc
- **Classes:** Commando, Warder
- **Damage:** Fire + Burn
- **Conversion:** `100% Physical -> Fire` via *Sunherald's Claymore* (*Menhir's Bulwark* mod) + global Phys->Fire
- **Stat Priorities:**
  1. **Speed:** `Attack Speed` (spam *Blade Arc* scales with Attack Speed, NOT Cast Speed)
  2. **Offense:** `Flat Fire`, `% Fire / Burn`, `Fire RR` (max *Thermite Mine*), `OA`, `Crit Damage`
  3. **Defense:** `ADCtH`, `Health Regen`, `Physical Resist`, `Circuit Breakers`

### 14. Werewolf Shapeshifter
- **Classes & Damage / Conversion:**
  - **Mystic (Chaos):** `4/5 Fenris` + *Shadowheart* (`100% Elemental & Vitality -> Chaos`).
  - **Primalist (*Primal Sinner*):** **Pierce** (+ Bleed); `2x Sincarvers` (`100% Physical, Elemental, Vitality -> Pierce`).
  - **Veilwalker:** **Cold** (`Fenris` + *Frost Wight's Edge* / *Spectral Longsword* for `Pierce -> Cold`) or **Aether**.
  - **Zealot (*Flaming Wolf Pack*):** **Fire** (*Apocalypse* set, `Physical/Cold -> Fire`).
- **Stat Priorities:**
  1. **Speed:** `Attack Speed` (scales `Feral Claws`; DARs & WPS do NOT work in form)
  2. **Offense:** `OA` + `-DA Shred`, `Crit Damage` (refreshes *Rip & Tear* CD), `Flat & %` of converted type, `RR`
  3. **Defense:** `Voracity` / `ADCtH`, `100% Armor Absorption`, `Physical Resist`, `Health`

### 15. Retaliation Melee Tank
- **Classes:** Warder (*Dawnbreaker* — Lightning), Warlord (Physical), Shieldbreaker (*Cinderborn* — Fire | *Perdition* — Acid), Paladin (Physical / Acid)
- **Damage:** Element-matched Retaliation + `% Retaliation Added to Attack (RtA)`
- **Conversion:** Keep Retaliation type aligned with RR. **Normal `% Physical / % Fire` does NOT scale Retaliation.**
- **Stat Priorities:**
  1. **Core Retal:** `% Retaliation Damage`, `Flat Retaliation` (matching element), `% RtA` modifiers on active skills (`Savagery`, `RF`, `Aegis`, `Grenado`, `Shattering Smash`)
  2. **Offense:** `RR` (matching element), `OA`, `Crit Damage` (RtA skills can crit & leech)
  3. **Attributes:** Pure `Physique`
  4. **Defense:** `Health Regen`, `Block Chance / Recovery`, `Armor`, `Physical Resist`

---

## B. Ranged

### 16. Dual Pistol Fire Strike
- **Classes:** Purifier (*Dagallon*), Pyromancer (*Darkblaze*)
- **Damage:** Fire (Purifier) | Chaos (Pyromancer)
- **Conversion:**
  - Purifier: `Physical / Lightning / Chaos -> Fire` (*Dagallon* + *Seal of the Void* / *Combustion Band*)
  - Pyromancer: `Fire / Acid / Physical -> Chaos` (*Darkblaze* + *Exterminus* + *Voidrend*)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed`, `100% WPS Pool`, `Flat Fire/Chaos`, `% Fire/Chaos`, `OA`, `Crit Damage`, `RR`
  2. **Defense:** `ADCtH`, `Physical Resist` (weakest layer), `Inquisitor Seal` (Purifier) or `Blast Shield` (Pyromancer)

### 17. Boltthrower
- **Class:** Runekeeper (2H Crossbow)
- **Damage:** Pierce (pure)
- **Conversion:** `Cold / Physical -> Pierce` (*Boltthrower* + *Blades of Nadaan* / Pierce gear)
- **Stat Priorities:**
  1. **Special:** `+Storm Spread Activation Chance` (hit 100% WPS), `Attack Speed`
  2. **Attributes:** Heavy `Cunning` (Pierce % + OA)
  3. **Offense:** `Flat Pierce`, `% Pierce`, `Crit Damage`, `OA`, `Pierce RR`
  4. **Defense:** `ADCtH`, `Physical Resist`, `CC Resists`

### 18. Marksman of Amatok
- **Class:** Reaver (Dual Pistols)
- **Damage:** Cold (residual Pierce acceptable)
- **Conversion:** `Physical / Fire -> Cold` (`2x Hagarradian Enforcers` + *Awakened Bloodmane*)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed`, `100% WPS` (`Avalanche` + `Bloodfangs`), `Flat Cold`, `% Cold`, `OA`, `Crit Damage`
  2. **Defense:** `DA`, `Dodge/Deflect`, `ADCtH`, `Physical Resist`

### 19. Aether Hagarrad Gunslinger
- **Class:** Runekeeper (Dual Pistols)
- **Damage:** Aether
- **Conversion:** `Cold / Pierce / Vitality -> Aether` (*Unwound Fate* set + *Rutnick's Blaster* or *Ragefire*)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed`, `100% WPS`, `Flat Aether`, `% Aether`, `OA`, `Crit Damage`, `Aether RR`
  2. **Attributes:** `Physique` (gear), `Spirit` (Aether % / Energy) + `Cunning` (OA)
  3. **Defense:** `Inquisitor Seal`, `ADCtH`, `Physical Resist`, `Health`

### 20. Pierce / Elemental 2H Rifle & Crossbow
- **Classes:** Infiltrator (*Luminari* — Pierce), Tactician (*Valdun* — Pierce | *Discord* — Tri-Elemental), Vindicator (Lightning `Primal Strike`)
- **Conversion:**
  - Pierce (*Valdun* / *Luminari*): `Elemental / Physical -> Pierce`
  - Elemental Tactician: `100% Physical -> Elemental` (`Discord` transmuter)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed`, `Flat & %` matching type, `100% WPS`, `OA`, `Crit Damage`, `RR`
  2. **Attributes:** `Cunning` heavy (Pierce) or `Physique/Spirit` (Elemental/Lightning)
  3. **Defense:** `Inquisitor Seal`, `ADCtH`, `Reflected Damage Reduction` (Valdun high single-hit crits), `Physical Resist`

### 21. The Arcane Archer
- **Class:** Battlemage (2H Ranged)
- **Damage:** Tri-Elemental (Fire / Cold / Lightning)
- **Conversion:** `100% Physical -> Elemental` (*Discord*) + `100% Aether -> Elemental` (*Nar's Arcane Destroyer* + *Iskandra* / *Fatebreakers*)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed`, `Flat Aether & Elemental` (Aether converts 100%), `% Elemental`, `OA`, `Crit Damage`, `Elemental RR` (*Rhowan's Crown*)
  2. **Defense:** `Maiven's Sphere`, `Mirror`, `ADCtH`, `Physical Resist`

### 22. Shotgun Stun Jacks
- **Class:** Elementalist
- **Damage:** Lightning + heavy Electrocute DoT
- **Conversion:** `Physical -> Lightning` (*Awakened Storm Serpent* + *Light's Defender*)
- **Stat Priorities:**
  1. **Speed & Cooldown:** `Cast Speed` (point-blank `Stun Jacks`) + `Attack Speed` (`Savagery` CD-reset hits) + `CDR`
  2. **Offense:** `% Lightning`, `% Electrocute Damage & Duration`, `Flat Lightning`, `OA`, `Crit Damage`
  3. **Defense:** `Health Regen` (5k+ target), `Blast Shield`, `Reduced Enemy Damage`

### 23. Retaliation / Chaos Gunner
- **Classes & Damage:** Shieldbreaker (*Stronghold* — Fire 1H+Shield or Fire/Acid Retaliation) | Pyromancer (*Harbinger* — Chaos 2H Crossbow)
- **Conversion:** *Harbinger*: `Acid / Physical / Fire -> Chaos`. *Stronghold Retal*: align Physical/Fire to chosen Retaliation element.
- **Stat Priorities:**
  - **Stronghold (Fire / Retal):** `Attack Speed`, `Block Chance / Recovery`, `CDR` (`Grenado`/`Aegis`); scale `Flat & % Fire` for normal build OR `% Retaliation` + `% RtA` for Retal variant.
  - **Harbinger (Chaos):** `Attack Speed`, `Flat & % Chaos`, `Doom Bolt ranks & CDR`, `OA`, `Crit Damage`, `Chaos RR`, `Health` & `CC Resists`

---

## C. Casters

### C.1 Channelers

### 24. Albrecht's Aether Ray (AAR)
- **Classes:** Spellbinder (*Clairvoyant* — Aether), Mage Hunter (Aether / Elemental), Templar (Aether / Fire), Warlock (Chaos)
- **Damage:** Aether (standard) | Chaos (*Tainted Power* transmuter + *Black Flame*) | Physical (*Conduit* + Phys gear)
- **Conversion:** Aether route: `Elemental / Vitality -> Aether` (*Clairvoyant* + *Chosen Robe* + *Chains of Ygraad*); *Albrecht's Commendation* medal turns AAR into a twin V-beam.
- **Stat Priorities:**
  1. **Speed:** `Cast Speed` (cap 200%+)
  2. **Offense:** `% Aether` (or Chaos/Physical), `Flat Aether` (if using WD Conduit), `OA`, `Crit Damage`, `Aether RR` (*Spectral Wrath*, *Widow*, *Band of Eternal Haunt*)
  3. **Attributes:** `Physique` (gear), heavy `Spirit` (magic % + Energy Regen)
  4. **Defense:** `AAR ADCtH` (*Pulsing Shard* or *Essence of Grim Dawn*), `CDR` (*Mirror* + *Mark of Torment* / *Eternity*), `Physical Resist`, `Energy Regen / Cost Reduction`

### 25. Drain Essence
- **Classes:** Reaper (*Chillwhisper* — Cold), Spellbinder (*Uroboruuk* — Aether), Apostate (Cold / Vitality), Veilwalker (Cold / Vitality)
- **Damage:** Cold (*Chillwhisper*) | Aether (*Uroboruuk*) | Vitality
- **Conversion:**
  - *Chillwhisper*: `Vitality / Physical -> Cold` (`Chillwhisper` + Cold transmuter; `Blade Spirits` add leech)
  - *Uroboruuk*: `Elemental / Vitality -> Aether`
- **Stat Priorities:**
  1. **Speed:** `Cast Speed` (cap)
  2. **Offense:** `% Cold` (or Aether/Vitality), `Skill Ranks to Drain Essence & Hungering Reach`, `OA`, `Crit Damage`, `RR`
  3. **Defense:** `Armor` + `100% Armor Absorption` (close-range channeler), `Physical Resist`, `DA`, `Energy Regen / Leech` (DE sustain is naturally high via built-in ADCtH)

### 26. Flames of Ignaffar (FoI)
- **Classes:** Purifier (Fire or Chaos Retaliation), Deceiver (Chaos *Tainted Flame* / *Voidsoul*), Paladin (Chaos *Voidsoul* or Fire), Infiltrator (Cold *Mageslayer* + *Farath's Cube*)
- **Damage:** Fire | Chaos (`Tainted Flame` = `100% Lightning -> Chaos`) | Cold (*Farath's Cube*) | Chaos Retaliation (*Pagar's Betrayal* + *Consumption of Agrivix*)
- **Conversion:** Chaos route uses `Tainted Flame` + `Phys/Fire/Vit -> Chaos`. Retal Purifier stacks Fire + Lightning Retaliation converted to Chaos via `Tainted Flame`.
- **Stat Priorities:**
  1. **Speed:** `Cast Speed` (cap)
  2. **Offense:** `% Fire / Chaos / Cold` (OR `% Retaliation` + `% RtA` for Retal Purifier), `OA`, `Crit Damage`, `RR`
  3. **Defense:** `ADCtH` (needs item ADCtH on FoI like *Bloodsworn Sigil*), `Inquisitor Seal`, `Physical Resist`, `Energy Regen / Cost Reduction`

### 27. Winds of Asterkarn + Wereraven
- **Classes:** Evoker (*Awakened Eastern* — Cold), Zealot (*Stormfang* — Cold/Lightning)
- **Damage:** Cold + Frostburn (or Cold/Lightning hybrid)
- **Conversion:** `Physical / Lightning -> Cold` (Evoker uses WoA channeling to reset `Trozan's Sky Shard` CD; Zealot pairs WoA with `Judgment` + `Onslaught` stacks)
- **Stat Priorities:**
  1. **Speed & Cooldown:** `Cast Speed` (cap; *Form of the Beast* helps), `CDR` (for TSS / Judgment loops)
  2. **Offense:** `% Cold`, `% Frostburn`, `OA`, `Crit Damage`, `Cold RR` (*Elemental Storm*, *Murmur*, *Bonechilling Cry*)
  3. **Defense:** `Armor` + `100% Armor Absorption`, `Physical Resist`, `ADCtH` / `Heal Procs`, `Energy Cost Reduction`

---

### C.2 Spam & CDR Nukers

### 28. Panetti's Replicating Missile (PRM)
- **Classes:** Mage Hunter (Elemental Spam), Spellbinder (*Awakened Dreadwalker* — Vitality CD + Onslaught)
- **Damage:** Tri-Elemental / Fire-Lightning (*Invoker* Mage Hunter) | Vitality (*Dreadwalker* Spellbinder)
- **Conversion:**
  - Mage Hunter: Pure Elemental or `Aether -> Elemental`
  - *Dreadwalker* Spellbinder: `Elemental / Aether -> Vitality`; uses `Drain Essence` to build `Onslaught` stacks -> dumps into CD-converted `PRM` (set + *Conduit of Arcane Whispers*)
- **Stat Priorities:**
  1. **Speed:** `Cast Speed` (cap on Spam PRM & DE-stacking)
  2. **Offense:** `% Elemental` or `% Vitality`, `+Max Onslaught Charges` (Dreadwalker), `OA`, `Crit Damage`, `RR`
  3. **Defense:** `Inquisitor Seal` + `Mirror` (Mage Hunter), `DA` + `Physical Resist` + `Drain Essence ADCtH` (Spellbinder), `Energy Regen`

### 29. Phantasmal Blades
- **Classes:** Reaper (Vitality / Aether), Witch Hunter (Acid / Pierce), Reaver (Pierce), Spellbreaker (Cold)
- **Damage:** Vitality (*Spectral Arbalest*) | Aether (*Conduit* + *Blaze Herald*) | Acid (*Venomlance*) | Pierce (*Demonbone* / *Jarinthor's Head*) | Cold (*Spelldrinker*)
- **Conversion:** Always convert all base Pierce/Cold/Vitality/Chaos components of PB into your single target type.
- **Stat Priorities:**
  1. **Speed:** `Cast Speed` (scales PB even though it has `% Weapon Damage` — Attack Speed is useless for spam PB!)
  2. **Offense:** `% Target Element`, `Flat Damage` (scales `%WD` across 9 shotgunning blades), `OA`, `Crit Damage`, `RR`
  3. **Defense:** `Energy Regen` (extreme mana drain on spam PB), `ADCtH` (built into *Heart Seeker* — huge sustain), `Max Health`, `Physical Resist`

### 30. Doombolt Onslaught / CDR
- **Classes:** Mystic (Chaos Crossbow/DW), Witchblade (*Woolly Voidguard* Shield), Warlock (Chaos CDR), Pyromancer (Chaos/Lightning)
- **Damage:** Chaos (primary) or Lightning
- **Conversion:** `Vitality / Elemental / Physical -> Chaos` (*Harbinger* / *Voidheart* / *Blood Orb of Ch'thon*)
- **Stat Priorities:**
  1. **Speed & Cooldown:** `Attack Speed` (for fast `Onslaught` stacking on Mystic) + `CDR` (`Doom Bolt` refresh/uptime)
  2. **Offense:** `% Chaos`, `Flat Chaos`, `+Max Onslaught Charges`, `OA`, `Crit Damage`, `Chaos RR`
  3. **Defense:** `Max Health`, `Armor`, `Physical Resist`, `ADCtH` (crossbow Mystic is very glassy without armor/HP investment)

### 31. Meteor Caster (Trozan / Devastation)
- **Classes:** Druid (Cold/Lightning TSS), Evoker (Cold TSS), Sorcerer (*Pyran* Fire `Devastation` + `Mortar`), Battlemage (*Spellscourge* Physical Shield `Devastation`)
- **Damage:** Cold + Lightning (Druid/Evoker) | Fire + Burn (*Pyran*) | Physical + IT (*Spellscourge*)
- **Conversion:**
  - *Spellscourge*: `Aether / Elemental -> Physical` (*Spellscourge* set + Phys conversion)
  - *Pyran*: `Aether / Physical -> Fire`
- **Stat Priorities:**
  1. **Cooldown & Speed:** `CDR` (mandatory — scales meteor rain uptime), `Cast Speed`
  2. **Offense:** `% Damage` (matching element), `OA`, `Crit Damage`, `RR` (Physical RR via *Assassin's Blade* + *Break Morale* is crucial for Battlemage)
  3. **Defense:** `Maiven's Sphere`, `Mirror`, `Block` (*Spellscourge*), `Health Regen` / `Kiting Speed`

### 32. Demolitionist Bomber
- **Classes:** Sorcerer (*Barrelsmith* — Lightning `Grenado` + `Canister Bomb`), Pyromancer (Fire/Chaos), Saboteur (Fire/Chaos/Cold)
- **Damage:** Lightning + Electrocute (*Barrelsmith*) | Fire + Burn / Chaos
- **Conversion:** *Barrelsmith*: `Physical / Fire -> Lightning`. *Pyromancer*: `Physical / Lightning -> Fire` or `Chaos`.
- **Stat Priorities:**
  1. **Cooldown & Speed:** `CDR` (core — cycles `Grenado` + `Canister` + `BWC` + `Ulzuin's Chosen` resets), `Cast Speed`
  2. **Offense:** `% Element` + `% DoT (Electrocute/Burn)`, `OA`, `Crit Damage`, `RR` (*Thermite Mines*)
  3. **Defense:** `Energy Regen` (severe mana drain), `Blast Shield`, `Mirror`, `Health Regen`, `Physical Resist`

### 33. Morgoneth Tentacles / Nightblade Spell
- **Classes:** Reaper, Spellbreaker
- **Damage:** Cold + Frostburn (or Vitality-Cold split leaning Cold)
- **Conversion:** `Physical / Pierce / Vitality -> Cold` (*Morgoneth's Eternal Night* set)
- **Stat Priorities:**
  1. **Cooldown:** `CDR` (`Shadow Strike`, `Ring of Steel`, `Ill Omen`, `Mark of Torment`)
  2. **Offense:** `% Cold`, `% Frostburn`, `OA`, `Crit Damage`, `Cold RR`
  3. **Defense:** `Max Health`, `ADCtH`, `Physical Resist`, `CC Resists`

### 34. Wereraven Bone Harvest Sniper (*Lokhor* Veilwalker)
- **Class:** Veilwalker (*Lokhor* — Chaos) or Vitality Veilwalker
- **Damage:** Chaos (primary) | Vitality
- **Conversion:** `Cold / Pierce / Physical -> Chaos` (*Lokhor* / *Talons of Ch'thon* / Chaos gear; converts `Bone Harvest`, `Ice Talons`, and `Cold Snap`)
- **Stat Priorities:**
  1. **Speed:** `Cast Speed` (spam `Bone Harvest` + `Ice Talons` scales with Cast Speed)
  2. **Offense:** `% Chaos`, `Flat Chaos / Weapon Damage`, `OA`, `Crit Damage` (Berserker gives ~120% Crit), `Chaos RR`
  3. **Defense:** `Armor` (*Stoneplate Greaves* / *Aurora*), `Physical Resist`, `ADCtH`, `Mark of Torment`

---

### C.3 Totems, Zones, Traps, DoT

### 35. Lightning Totem Caster
- **Classes:** Druid, Vindicator (*Ludrigan*), Warder, Elementalist
- **Damage:** Lightning + Electrocute
- **Conversion:** `Physical -> Lightning` (optional for `Wind Devil` / `Savagery` hybrid)
- **Stat Priorities:**
  1. **Core Player-Scaled:** `% Lightning`, `% Electrocute`, `+Summon Limit` (`Storm Totem` / `Wind Devil`), `CDR`, `Cast Speed`
  2. **Offense:** `OA`, `Crit Damage` (totems inherit player OA & Crit), `Lightning RR`
  3. **Defense:** `Health Regen` (*Wendigo Totem*, *Mogdrogen's Pact*), `Movement Speed` (kiting), `Inquisitor Seal` (Vindicator), `Physical Resist`

### 36. Mortar Trap
- **Classes:** Shieldbreaker, Sorcerer (*Pyran*)
- **Damage:** Fire + Burn (standard) | Lightning (*Rune of Elgoloth* `100% Phys -> Lightning`)
- **Conversion:** *Pyran* / Fire: `Physical -> Fire`. Lightning: `100% Physical -> Lightning` (*Rune of Elgoloth*).
- **Stat Priorities:**
  1. **Core Player-Scaled:** `+Mortar Trap Summon Limit`, `CDR`, `% Fire/Burn` or `% Lightning`
  2. **Offense:** `OA`, `Crit Damage`, `RR` (*Thermite Mines*), `Cast Speed`
  3. **Defense:** `Health Regen`, `Blast Shield`, `Physical Resist`, `Kiting Speed`

### 37. Vitality Zone Caster (*Dark One*)
- **Classes:** Conjurer, Ritualist, Cabalist, Oppressor
- **Damage:** Vitality + Vitality Decay
- **Conversion:** None (or `Elemental/Chaos -> Vitality` for procs; core skills `Ravenous Earth`, `Sigil of Consumption`, `Devouring Swarm` are native Vitality)
- **Stat Priorities:**
  1. **Speed & Cooldown:** `Cast Speed` + `CDR` (faster zone deployment & ramp-up)
  2. **Offense:** `% Vitality`, `% Vitality Decay`, `Vitality RR` (stack -150%+), `OA`, `Crit Damage`
  3. **Defense:** `Max Health`, `Armor`, `Physical Resist` (sustain is godlike once zones are active; weak during initial 1–2s ramp-up)

### 38. Acid / Burn Dreeg's Evil Eye (DEE)
- **Classes:** Sentinel, Conjurer, Witch Hunter, Pyromancer
- **Damage:** Acid + Poison (*Deception of Dreeg* / *Corruptian*) | Fire + Burn (*Ember's Calling*)
- **Conversion:**
  - Acid: `100% Physical -> Acid` (*Corruptian* / *Dreeg* dagger)
  - Burn: `Acid / Poison -> Fire / Burn` (*Ember's Calling* set + global Acid->Fire)
- **Stat Priorities:**
  1. **Speed / Cooldown:** `Cast Speed` (untransmuted spam DEE) OR `CDR` (*Focused Gaze* DoT nuke)
  2. **Offense:** `% Acid + % Poison + Poison Duration` OR `% Fire + % Burn + Burn Duration`, `OA`, `Crit Damage`, `RR`
  3. **Defense:** `Health Regen`, `ADCtH` / `Blood of Dreeg`, `Physical Resist`, `Energy Regen` (spam DEE)

### 39. Bloody Pox / Devouring Swarm Kite
- **Classes:** Conjurer (*Swarmageddon*), Mystic (Bleed), Witch Hunter
- **Damage:** Bleed + Vitality (Conjurer) | Bleed (Mystic: `Bloody Pox: Wasting` + `Huntress`)
- **Conversion:** None (Bleed cannot be converted!)
- **Stat Priorities:**
  1. **Offense:** `% Bleed Damage & Duration`, `% Vitality` (Conjurer), `RR` (*Devouring Swarm*, *Curse of Frailty*, *Huntress*), `OA`, `Crit Damage` (DoT crits snapshot high damage)
  2. **Defense:** `Health Regen`, `Movement Speed`, `Max Health`, `Physical Resist` (note: *Bloody Pox* does NOT consume `Onslaught` stacks)

### 40. Word of Pain + Storm Box
- **Classes:** Mage Hunter, Infiltrator
- **Damage:** Tri-Elemental / Lightning | Aether (converted)
- **Conversion:** `Elemental -> Aether` (if Aether Mage Hunter) or keep Elemental/Lightning
- **Stat Priorities:**
  1. **Offense:** `Cast Speed`, `% Elemental` (or Aether), `OA`, `Crit Damage`, `RR` (*Aura of Censure*, *Death Sentence*)
  2. **Defense:** `Inquisitor Seal`, `Mirror`, `Health Regen` / `Word of Renewal`, `Physical Resist`

### 41. Vanquisher Skate
- **Classes:** Templar, Shieldbreaker
- **Damage:** Fire + Burn
- **Conversion:** `Physical -> Fire` (*Vanquisher* set + Fire gear; removes CD on `Vire's Might`)
- **Stat Priorities:**
  1. **Cooldown:** `CDR` (maximize `Vire's Might` / `Judgment` / `Mirror` uptime)
  2. **Offense:** `% Fire`, `% Burn Damage & Duration`, `OA`, `Crit Damage`, `Fire RR`
  3. **Defense:** `Stun / Slow / Freeze Resist` (mandatory — you skate directly through packs), `Max Health`, `Physical Resist`, `Armor`

---

## D. Pets / Summoners

> **Global Pet Rule (1.3.1+):** Pet survivability against enemy RR was tightened in 1.3.1. Prioritize **Pet Resistances** (especially Acid/Poison, Chaos, Aether, Elemental, Stun/Freeze) and **Pet Slow Resist** (for Callagadra), plus **Player CDR** for fast resummoning.

### 42. Fire Beastcaller Conjurer
- **Class:** Conjurer (*Nature's Call* + *Dracarris*)
- **Army:** 2x `Hellhound`, `Briarthorn`, `Familiar`
- **Conversion:** `Pet Physical / Elemental / Chaos -> Fire`
- **Stat Priorities:**
  1. **Pet Offense:** `% Pet Fire / All Damage`, `Pet Attack / Total Speed`, `Pet Crit Damage`, `Pet OA`
  2. **Pet Defense:** `Pet Resistances` (cap 80%+), `Pet Slow Resist`, `Pet Health`
  3. **Player:** `Fire RR` (*Curse of Frailty*, *Solael's Witchblade*, *Rhowan's Crown*), `Player DA / Health / Resists`

### 43. Lightning Pets (*Bysmiel*) Conjurer
- **Class:** Conjurer (*Bysmiel's Trinkets*)
- **Army:** 2x `Familiar`, `Briarthorn`, `Hellhound`
- **Conversion:** `Pet Physical / Fire -> Lightning`
- **Stat Priorities:**
  1. **Pet Offense:** `% Pet Lightning / All Damage`, `Pet Attack / Cast Speed`, `Pet Crit Damage`, `Pet OA`
  2. **Pet Defense:** `Pet Resistances`, `Pet Health`, `Mend Flesh` heal uptime
  3. **Player:** `Lightning RR` (*Wind Devil*, *Vulnerability*, *Widow*), `Player Survival`

### 44. Skeleton Army / Chaos Mages Cabalist
- **Class:** Cabalist (*Lost Souls* or *Awakened Desecrator Covenant*)
- **Army:** `Raise Skeletons` + `Blight Fiend` (+ `Familiar` / `Hellhound`)
- **Conversion:** *Lost Souls*: `Pet Physical -> Vitality/Chaos`. *Desecrator Covenant*: `Pet Elemental/Physical -> Chaos` (gives 0.8s skeleton resummon CD).
- **Stat Priorities:**
  1. **Pet Offense:** `% Pet Chaos / Vitality / All Damage`, `Pet Attack / Cast Speed`, `Pet Crit Damage`, `Pet OA`
  2. **Pet Defense (CRITICAL):** `Pet Health` & `Pet Resistances` (skeletons die fast in 1.3.1 without overcaps)
  3. **Player:** `CDR` (rapid skeleton resummon), `Chaos/Vitality RR`, `Player DA / Health`

### 45. Vitality / Death's Gate
- **Classes:** Ritualist, Cabalist
- **Army:** `Skeletons` + `Briarthorn` + `Reap Spirit` (*Death's Gate* + *Diviner* / *Lost Souls*)
- **Conversion:** `Pet Physical / Aether -> Vitality`
- **Stat Priorities:**
  1. **Pet Offense:** `% Pet Vitality / All Damage`, `Pet Crit Damage`, `Pet Speed`, `Pet OA`
  2. **Player / Pet Utility:** `CDR` (more `Reap Spirit` wraiths), `Vitality RR`, `Pet Resistances`

### 46. Acid Blight Fiend
- **Classes:** Ritualist (*Ghol's Malice*), Cabalist (*Ghol's Malice* + *Awakened Sphere of Blades*)
- **Army:** Exploding `Blight Fiends` (*Unstable Anomaly*) + `Briarthorn`
- **Conversion:** `Pet Physical -> Acid` (*Ghol's Malice*)
- **Stat Priorities:**
  1. **Pet Offense:** `% Pet Acid / Poison / All Damage`, `Pet Crit Damage`, `Pet Speed`, `Pet OA`
  2. **Cooldown & Sustain:** `Player CDR` (spam resummon exploding Blight Fiends), `Pet Acid/Poison Resists & Health`, `% Health Recovery` on *Blood of Dreeg* (Cabalist)
  3. **Player:** `Acid RR` (*Devouring Swarm* / *Curse of Frailty*, *Manticore*), `Player Survival` (Ritualist lacks *Blood of Dreeg* heal)

### 47. Cold Pets / *Ice King*
- **Classes:** Mystic (*Ice King's Adornments*), Cabalist (Cold Skeletons)
- **Army:** 4x Cold `Hellhounds` (Mystic, summoned 2 at a time) | Cold Skeletons (Cabalist)
- **Conversion:** `Pet Physical / Fire / Chaos -> Cold` (*Ice King* set)
- **Stat Priorities:**
  1. **Pet Offense:** `% Pet Cold / All Damage`, `Pet Attack Speed`, `Pet Crit Damage`, `Pet OA`
  2. **Pet Defense:** `Pet Resistances`, `Pet Health` (note: *Ice King* also gives 10s Hellhound CD reset on player attack for `Blazing Death` explosion loops)
  3. **Player:** `Cold RR` (*Bonechilling Cry*, *Vulnerability*, *Murmur*, *Elemental Storm*), `Player Survival`

### 48. Chaos / Elemental Duo Warlock
- **Class:** Warlock
- **Army:** `Familiar` + `Hellhound`
- **Conversion:** `Pet Elemental -> Chaos` (*Mythical Blood Orb of Ch'thon*)
- **Stat Priorities:**
  1. **Pet Offense:** `% Pet Chaos / All Damage`, `Pet Speed`, `Pet Crit Damage`, `Pet OA`
  2. **Player:** `CDR` (*Mirror* + *Maiven's* + resummons), `Chaos RR`, `Pet Resistances`

### 49. Player-Scaled Pets
- **Classes:** Shieldbreaker (`Guardians`), Paladin, Reaper (`Blade Spirits`), Druid (`Wind Devils`)
- **Damage:** Fire / Acid / Cold / Pierce / Lightning (depends on class & conversion)
- **Conversion:** Standard player conversion applies to player-scaled pets.
- **Stat Priorities:**
  1. **Player Offense (ZERO Pet Bonuses):** `% Damage` of target element, `Player OA`, `Player Crit Damage`, `RR`
  2. **Scaling:** `+Summon Limit` items, `CDR` / `Cast Speed` (for `Wind Devil` / `Mortar`), `Player Defense`

---

## E. Non-Meme Hybrids

### 50. Hybrid Ice King Mystic
- **Class:** Mystic (*Ice King's Adornments*)
- **Damage:** Cold (Player Werewolf `Feral Claws` + 4x Cold `Hellhounds`)
- **Conversion:** `Physical / Fire / Chaos -> Cold` (both player and Hellhound portions via set)
- **Stat Priorities (True Split Hybrid):**
  1. **Player Stats:** `Attack Speed`, `% Cold`, `Flat Cold`, `OA`, `ADCtH` / `Voracity`, `Armor`
  2. **Pet Stats:** `% Pet Cold / All Damage`, `Pet Attack Speed`, `Pet Resistances`, `Pet Health`
  3. **Shared:** `Cold RR` (*Bonechilling Cry*, *Vulnerability*, *Murmur*)

### 51. Woolly Voidguard Witchblade
- **Class:** Witchblade (1H + Shield)
- **Damage:** Chaos
- **Conversion:** `Physical -> Chaos` (*Woolly Voidguard* + Chaos conversion; `Cadence` refreshes `Doom Bolt` CD by 1s at 100% chance)
- **Stat Priorities:**
  1. **Offense:** `Attack Speed`, `Flat & % Chaos`, `Doom Bolt skill ranks`, `OA`, `Crit Damage`, `Chaos RR`
  2. **Defense:** `Block Chance / Recovery`, `Armor` + `100% Armor Absorption`, `Physical Resist`, `ADCtH`

### 52. 100% Storm Spread + Dual Ravens Luminari Deceiver
- **Class:** Deceiver (Dual Pistols)
- **Damage:** Elemental / Lightning
- **Conversion:** None (keeps Elemental/Lightning across player shots and 2x 8-projectile Ravens)
- **Stat Priorities:**
  1. **Player-Heavy Split:** `Attack Speed`, `100% Storm Spread WPS`, `% Elemental / Lightning`, `OA`, `Crit Damage`, `Elemental RR` (*Aura of Censure*, *Vulnerability*)
  2. **Pet Secondary:** `% Pet Elemental / Lightning`, `Pet Speed`, `Pet Resistances`
  3. **Defense:** `Inquisitor Seal`, `ADCtH`, `Blood of Dreeg`, `Physical Resist`

### 53. Walking-Sim Cold Aura Infiltrator
- **Class:** Infiltrator
- **Damage:** Cold + Frostburn
- **Conversion:** `Elemental / Pierce -> Cold` (passive auras: `Aura of Censure` + `Veil of Shadow / Night's Chill` + cold item aura procs)
- **Stat Priorities:**
  1. **Offense:** `% Cold`, `% Frostburn`, `Cold RR`, `OA`, `Crit Damage` (Attack/Cast Speed not needed)
  2. **Defense (Facetank Core):** `Inquisitor Seal`, `Health Regen`, `Armor` + `100% Armor Absorption`, `Physical Resist`, `Max Health`, `CC Resists`

### 54. Physical / Chaos AAR & Devastation
- **Classes:** Battlemage / Templar (*Spellscourge* — Physical `Devastation`) | Warlock / Spellbinder (*Tainted Power* / *Black Flame* — Chaos `AAR`)
- **Damage:** Physical + IT (*Spellscourge*) | Chaos (`Tainted Power`)
- **Conversion:** *Spellscourge*: `Aether / Elemental -> Physical`. Chaos AAR: `Tainted Power` converts `Aether / Lightning -> Chaos` (+ global Fire/Vit->Chaos).
- **Stat Priorities:**
  - **Physical (*Spellscourge*):** `CDR`, `% Physical / IT`, `Shield Block / Armor`, `OA`, `Physical RR`
  - **Chaos AAR:** `Cast Speed`, `% Chaos`, `OA`, `Crit Damage`, `Chaos RR`, `ADCtH` (*Essence of Grim Dawn* / *Pulsing Shard*), `Energy Regen`