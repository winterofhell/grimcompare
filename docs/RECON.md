# RECON — Phase 0

Date: 2026-11-14. Everything below was checked on this machine unless marked **[ref]** (a third-party source, not yet checked against our binaries) or **[unverified]**.

## 1. Install facts

| Item | Value | Source |
|---|---|---|
| Install | `~/.local/share/Steam/steamapps/common/Grim Dawn` (library 0) | `libraryfolders.vdf`, `appmanifest_219990.acf` |
| Steam build | buildid `25813250`; depots 219991 (base), 483840, 642280/642281 (AoM), 897670/897671 (FG), 2699230/2699231 (FoA) | appmanifest |
| Game version | **v1.3.1.1 (x64)** | string `"v1.3.1.1 (x64)"` in Engine.dll (both builds) |
| Prefix | `compatdata/219990/pfx`; user dir `.../steamuser/Documents/My Games/Grim Dawn` | |
| Renderer | direct3d11 | `log.html` (from Aug 17, v1.3.0.7, so a run before the update) |

**The game runs from `compat/`, not `x64/`.** `compat/ReShade.log` shows ReShade injected into `S:\common\Grim Dawn\compat\Grim Dawn.exe`. This is Steam's "Compatibility Mode (SteamOS/Linux)" launch entry ([DPYes thread] says the same).

| Binary | x64/ sha256 | compat/ sha256 | PDB path |
|---|---|---|---|
| Game.dll | `cd7ecd79…cecad2` | **identical** | `…\x64\SteamShip\Game.pdb` |
| Engine.dll | `4c2ea14c…3153` | `6dbc7458…c792c9` (different) | compat: `…\x64\SteamShipProton\Engine.pdb` |
| Grim Dawn.exe | `d3915e25…c4c4` | `e976909f…ec9` (different) | — |

Consequences:
- Game.dll RVAs are the same for both launch options. Engine.dll differs; Phase 1 hashes each module from the path it was actually loaded from (logged, informational).
- `compat/` also ships `Game.lib/.exp` and `Engine.lib/.exp` (full import libraries). They are not needed now; we resolve by name at runtime (`GetProcAddress` on mangled names).
- The exe's `FileVersion` resource reads `0.3.0.0`, which is useless. Use `Engine::GetVersion()` (exported) plus module hashes.

### Loader plan
- `compat/DirectInput.dll` imports `DINPUT8.dll` (winedump), so **Ultimate ASI Loader x64 as `compat/dinput8.dll`** gets loaded. Launch options became `WINEDLLOVERRIDES="dxgi,dinput8=n,b" …` (`gdcompare-setup launch-options add` never touches `dxgi`; it drops only `DXVK_HDR` when RenoDX is gone) (one variable; a second `WINEDLLOVERRIDES=` would override the first).
- Timing [ref gd-uniquetab HANDOFF]: `dinput8.dll` loads *after* the engine reads its database, while `winmm` loads at process start. That is fine for us: we hook UI and read functions only.
- **Late load needs `dontloadfromdllmain=0`** (crash 2026-11-14, minidump): with the UAL default the proxy's `DirectInput8Create` (`jmp [ptr]`) ran before the loader had loaded the system `dinput8.dll` → execute AV at 0x0, return address `DirectInput.dll+0x2E73` (`DirectInputDevice::Initialize`). `GDCompare.asi` was not loaded yet. Fix: `adapter/ual/dinput8.ini`. That crash ran with OptiScaler/ReShade loaded; they were removed afterwards, so later runs are not comparable 1:1.
- Fallback: OptiScaler's own ASI loading (`LoadAsiPlugins=true`, default folder `.\OptiScaler\plugins\` per the ini comment) **[unverified: path resolution with `OptiDllPath=auto`]**. It avoids a new proxy DLL but ties us to OptiScaler. Not chosen.
- `winmm.dll` stays free for DPYes/Riftgate if you ever install them.

## 2. Reference projects

| Project | Status | Reusable for us |
|---|---|---|
| **gd-uniquetab** (`github.com/ffuuu18-lab/gd-uniquetab`) | Exists, MIT, C++17/MSVC, MinHook, Ultimate ASI Loader. Supports **1.3.0.8 + 1.3.1.1** | **Tooltip technique** (below), SEH-guarded reads, all-or-nothing hook install, COMDAT-alias check, threading rules, `ItemReplicaInfo` offsets (+0x28 prefix, +0x48 suffix, +0x68 seed; replica at item+0x538 on 1.3.0.8). Their DB/ARC reader serves another purpose; we write our own small one. |
| **Grim Gleaner** (`github.com/kultcher/grim_gleaner`, Crate forum t/158323) | Exists. Python tool, v0.9.2-beta (catalog for 1.3.0.7) | Affix/stat category lists and build-priority weights as a **sanity reference** for presets. Its mechanism (rewriting localization tags in `Text_EN`) and its roll-blind grading are not what we want. |
| **gddb**, **gd-cli** | **Not found** under those names. Closest: `xaviershay/gd-explorer` (Haskell: arz/arc/gdc parsing, stats, DPS estimate, covers GDX3), `bealex/GrimDawner` (Swift: arz/arc/LZ4/tex + `Documentation/GameData.md`), `atom0s/grimarz`/`grimarc` (format docs) | Format cross-checks; gd-explorer's stat code as a second opinion for MECHANICS.md. |
| **GD Stash** (mamba, Crate t/29036) | Exists, closed source (Java). GitHub repos under that name with random owners look like **malware bait**, do not use | Nothing directly. We read item values from the engine, not from `.gdc`. |
| **GrimTools Build Calculator** | Closed web tool | Formula cross-check only (cite per formula in MECHANICS.md). |
| **Grim Internals** | Old, closed; no `Engine.def` repo found. A Crate thread shows the old `.def` approach (MinGW→MSVC name aliases) | Nothing: we have the real export tables. |
| **Perfect Rolls – Runtime Patch** (Nexus mods/247) | Exists; signature scanning of Game.dll; **writes** process memory | Name `GrimDawnPerfectRolls` not found on GitHub; repo link only via Nexus page. Not needed: we use exports. |
| **DPYes** (Crate t/133378) | Closed. Loader = `winmm.dll` in `x64/` **or `compat/`** for the Linux launch option | Confirms the compat-folder finding and the `n,b` override pattern. |

### What vanilla already does (do not duplicate)
- Side-by-side tooltip of the hovered item vs the equipped item (`tagTutorialTip49`, `tagQuickTip49`). Weapons also show a **DPS comparison**: exports `ItemEquipment::CompareDPS`, `Player::CompareItems(uint, float&×4, bool&)`.
- **Ctrl** = "show details" / compare *without* components/augments/skills (patch 1.2.1.2 HF2), and "hold Ctrl to hide tooltip". So Ctrl is taken. **Shift** is only used for click actions (shift-click transfer, chat link), so it is free as a hover modifier **[verify in-game: no Shift hover behaviour]**.
- 1.3.0.0: detailed tooltips show roll ranges `[min-max]`.
- Vanilla does **not** show character-sheet deltas (resists to cap, HP, OA/DA…), set-bonus loss, requirement breakage, or a build-aware score. That is our niche.

## 3. Exports (Game.dll 25,113 / Engine.dll 6,283)

Full dumps: `re/Game.exports.tsv`, `re/Engine.exports.tsv` (RVA, mangled, demangled; regenerate with `re/tools/demangle_exports.py`). The RVAs below are from **compat/** binaries. ICF warning: 1,574 exports share RVA `0x15880` (empty stubs), so every hook target must have exactly one export alias (all targets below checked: 1 alias).

### Globals / entry points
- `?gGameEngine@GAME@@3PEAVGameEngine@1@EA` (Game, data) → `GameEngine::GetMainPlayer() const` (0x2EBB70) → `Player*`
- `?gEngine@GAME@@3PEAVEngine@1@EA` (Engine, data); `Engine::GetVersion()` (0x79CD0, Engine) → version string
- `GameEngine::GetGameDifficulty() const` (0x2D8490) → `GameDifficulty`; `GameEngine::GetPlayerAttributePak() const` (likely the difficulty pak, **[unverified]**)

### Player / character sheet
Sheet values come from the engine's own getters. No exported "character sheet UI" class exists (UI classes are not exported), so Phase 2 reads these and calibration verifies them against the screen:
- Attributes: `Character::GetTotalCharAttribute(CharAttributeType)`, `GetTotalCharModifier`, `GetBaseCharAttribute`, `GetPointsSpent`
- HP/Energy: `GetLifeLimit`, `GetManaLimit`, `GetLifeRegenValue`, `GetManaRegenValue`, `GetLifePerStrength/Dexterity/Intelligence`, `GetManaPerIntel`
- Speeds: `GetAttackSpeed`, `GetSpellCastSpeed`, `GetRunSpeed(bool)`
- Defense: `GetAllDefenseAttributes(CombatAttributeAccumulator&)`, `Player::GetDefenseAttributeCap`, `GetPhysicalDamageReductionPercent`, `GetElementalDamageReductionPercent`, `GetTotalDamageReductionPercent/Absolute`
- Equipment accumulators: `GetEquipCharAttributes`, `GetEquipSkillAttributes`, `GetEquipOffensiveModifierAttributes`, `GetEquipConversionAttributes`, `GetEquipRetaliation[Modifier]Attributes`, `GetEquipArmorDamageAttributes`; also `GetSkillCharAttributes`, `GetBaseCharAttributes`
- Level/points: `GetCharLevel`, `GetSkillPoints`, `GetModifierPoints`, `GetDevotionPoints`, `GetTotalDevotionPoints`, `GetSkillMasteries`, `GetSkillList`, `GetSkillManager`, `GetItemSkillList`, `Player::GetPlayerCharacterClass`
- Sets: `Character::GetNumberOfItemsInSet(string)`, `GetItemsInSet(string)`
- OA/DA, crit: no direct getter found yet. Likely inside `CombatManager` (82 exports) **[open, Ghidra]**

### Equipment
- `Character::GetAttachedItems()` → `mem::vector<EquipManagerContainer>`; container ctor `(Item*, EquipLocations, bool)`, so it holds `Item*` + slot + alt-weapon flag **[layout to confirm]**
- `CombatManager::GetEquipManager()`; `EquipManager::GetItemId(EquipLocations)`, `GetItemLocation(uint)`, `GetItemAlternate(uint)`, `GetHandState()`, `GetWeaponIdLeft/Right`, `LocationToString(EquipLocations)` (gives the enum names at runtime)
- Item ids → `Item*`: `ObjectManager` has **no exported GetObject** (template, inlined) **[open: Ghidra or via GetAttachedItems]**

### Items: actual rolled values (engine-computed, no recomputation)
`Item` virtuals, overridden by `ItemEquipment` / `ItemRelic` / `ItemEnchantment` / `ItemArtifact`:
`GetCharAttributes(CharAttributeAccumulator&)`, `GetDefenseAttributes(CombatAttributeAccumulator&)`, `GetOffensiveDamageAttributes(CombatAttributeAccumulator&, float)`, `GetOffensiveModifierAttributes`, `GetRetaliation[Modifier]Attributes`, `GetConversionAttributes(ConversionAttributeAccumulator&)`, `GetSkillAttributes(SkillAttributeAccumulator&)`, `ContributeRacialBonus*`.
Plus `GetLevelRequirement`, `GetStrength/Dexterity/IntelligenceRequirement`, `AreRequirementsMet(Character const*)`, `ItemEquipment::GetItemSetName`, `GetRelic`, `GetEnchantment`, `GetTransmute`, `HasPetBonus`, `IsTwoHandedMeleeWeapon`, `GetLeft/RightHandType`.
Accumulator classes are exported too (`CharAttributeAccumulator`, `CombatAttributeAccumulator` 57 exports, `CombatAttribute` 65, `SkillAttribute`, `DamageAttribute*`), so we can construct one, let the item fill it, and read it back. **This is the core of Phase 2.**

### Tooltip building
- `ItemEquipment::GetUIDisplayText(Character const*, mem::vector<GameTextLine>&, bool)` 0x33DCE0; also `Item::` 0x325CC0, `ItemRelic::` 0x348800, `ItemEnchantment::` 0x339DD0, `ItemArtifact::` 0x32A570, `ItemArtifactFormula::` 0x32DDD0; `ItemSet::GetUIDisplayText`
- `GameTextLine::GameTextLine(GameTextClass, u16string const&, bool, GraphicsTexture const*, float)` 0x313030 (no exported dtor)
- `GameTextLineToString(mem::vector<GameTextLine> const&, mem::list<GameTextString>&)` 0x316EB0, which turns lines into render strings
- [ref gd-uniquetab]: `GameTextLine` is 0x40 bytes (`+0` class, `+8` MSVC u16string, `+0x28` bool, `+0x30` texture, `+0x38` float). `mem::vector` = `{begin,end,cap}`. Technique: detour `GetUIDisplayText` → latch the vector fingerprint; detour `GameTextLineToString` → pass a **borrowed copy** of the line array (own `malloc`) with extra lines appended, then free it. The engine's vector is never grown. Class values `0x2C` (ItemEnchantmentStats, green), `0x4A` (EffectHeading, orange), `0x12` (ItemBaseStats, white); `0x50–0x53` (GrayStats) are desaturated; `0x14`/`0x23` are dropped by a filter. **All [ref], verify on our build in Phase 4.**
- **Corrected 2026-11-14 (adapter 0.4.1):** `GameTextLineToString` is **never called or referenced** in Game.dll 1.3.1.1 (no `call`/`jmp` rel32, no RIP-relative or absolute pointer to 0x316EB0), so 0.4.0's draw hook could never fire. The block is now appended inside the `GetUIDisplayText` detours with the engine's own `mem::vector<GameTextLine>::push_back(GameTextLine&&)` (unexported, rva 0x1C550): found through its call at `ItemEquipment::GetUIDisplayText`+0x264 (`lea rdx,[rbp-0x59]; mov rcx,rsi; call`) and byte-verified (0xAC bytes, rel32 calls only), `gdc::scan::line_push_back_call`. It **moves** the line: the u16string goes through 0x1C4C0, which takes over a heap buffer and resets the source to empty SSO. So each line is first built by the exported ctor 0x313030 (`assign(src, 0, npos)` via 0x15C40: a game-allocator copy of our text), then moved in; the game owns and frees it. `Item::GenerateSearchText` (0x329370) calls GetUIDisplayText (vt+0x458, `call [rax+0x458]` at +0xB8) for stash search text: that return address is skipped. Inline lines use bool false, texture null, float 1.0 (0x33DF35..0x33DF44). F9 snapshots (0.4.0) showed the last build 1.6-4.5 s before the key press while hovering: the UI caches the text per hover. **[unverified in game: build thread, the UI caller and its cache (logged as a backtrace), class 0x50 rendering]**
- The text class names exist as strings in Game.dll (`ItemBaseStats`, `ItemEnchantmentStats`, `EffectHeading`, `GrayStats1..4`, …, each with an `…Indent` variant at 0x69a…–0x69b…), so they map to UI style records.

## 4. Text and colour tags (from the game's own `Text_EN.arc`, base + gdx1-3)
- Inline tags take the form `{^X}`. Counts in Text_EN: `{^n}` 3101, `{^E}` 746, `{^H}` 129, `{^g}`, `{^R}`, `{^o}`, `{^b}`, `{^h}`, `{^y}`, `{^r}`; also `{^w}`, `{^s}`, `{^k}`; `{^-}` appears to reset (`{^o}[Ctrl]{^-} key`). A bare `^O` without braces also occurs (`tagShowDetails=^O[Hold Ctrl…]`).
- `{^n}` = line break (used as `{^n}{^n}` paragraph breaks).
- Usage implies colours: `{^g}` green (good news), `{^r}` red (`tagSkillNotReady`), `{^y}` yellow, `{^w}` white. **The exact letter→RGB table is not in text files [open: Engine disassembly or in-game test].**
- **Font markup** (`GraphicsFont2::ProcessCharacter`, Engine.dll 0xB0C00, reads each character with the next): `^` + a letter = colour (letter table at rva 0x319c10), `^-` special, `^` + anything else draws neither character, `^^` draws `^`; `&` + B/b/I/i/S/s sets the style (bold / italic / normal), `&&` draws `&`, `&` + anything else draws `&`; `{` / `}` are not drawn. So "Poison & Acid" draws as written: the stray "&" of the 0.9.2 test came from the tooltip's own line wrap, not markup. The adapter escapes every line (`tooltip::escape`: `^` and `&` doubled, braces as parentheses); resist names keep "/" so a name never wraps.
- **Glyphs:** the English fonts (`Fonts.arc` root, FNTX: per style a 0x1C-byte header with the glyph count at +0x10, then 0x1C-byte entries starting with the UTF-16 code; `GraphicsFont2::Initialize` 0xAFC90) hold exactly U+0020..007E per style, plus U+0000 (the fallback character, style header +0x18): no "→", "–" or "·". Tooltip text is ASCII ("->", " - ").
- **Tooltip wrap (2026-11-15, adapter 0.9.5; exe RVAs from Grim Dawn.exe).** The item rollover provider (exe 0x1EE9F0) sets layout mode 1 and a sample text by `GraphicsEngine::GetWidth` (Engine 0x91F60, `[rcx+0x64]`): >= 1600 (0x640) "The quick brown fox jumped over the lazy dog The quick" (rva 0x2fd0c0), else "The quick brown fox jumped over the lazy" (rva 0x2fd130); the sample's style name stays "" so `StyleManager::LoadStyle` (Engine 0x1C4400) gives its defaults, `fonts/jura.fnt` size 16. Item vt+0x480 (mode 3) is `ret 0` (Game 0x15880) for ItemEquipment / Armor / Weapon / WeaponArmor / Item. Layout (exe 0x236020): mode 1 width W = the sample's GameTextString width; each wrapping line wider than W is wrapped (0x240930). GameTextString width (Game 0x316CE0) = text width + trunc((indent+6) x s) + trunc(6 x s), s = `GraphicsEngine::GetUIScaleFactor`, indent = gameengine.dbr `<class>Indent` (GrayStats1: none), so a GrayStats1 line wraps iff its text is wider than the sample's.
  - **UI scale** (Engine 0x92060): o = the options value (custom scaling on, `+0x1BA`) else 0.5; x = 2(o - 0.5); x < 0: 1 + 0.3x; else p = clamp(W x H / 4608000, 0, 1) and s = 1 + x when p > 0.75, else (0.7p + 0.1)x + (0.5p + 0.7, or 1 when that is within 0.1 of 1). The adapter calls it rather than repeating this.
  - **Font size** (LoadStyle): fontSize, and at s != 1 ceil(fontSize x s), at least `lowResMinimumFontSize` (default 8). Flags: bold 1, italic 2. `GraphicsFont2::GetStyle` (0xB0830) takes that size exactly, else 0xAE660: the nearest size in file order (first on a tie), flag-equal styles first; scale = size / style size (Game 0x3176C2).
  - **Text width** (Game 0x317470 per run, 0x317820 per glyph; `FNTMetric` +2 s16 bearing, +4 s16 after, +6 u16 advance): from the 2nd glyph on, add the previous glyph's "after" (not for a space after a non-space), the kerning (`GetKerningAmount`, none in jura / linbiolinum_sansserif), the bearing (x clamped at >= 0); every glyph adds floor(advance x scale + 0.5) (0.5 at Game rva 0x7a8024); the line ends with the last "after". Missing glyph: the style's fallback character.
  - Implemented in `gdc::font` (tables from `re/tools/font_tables.py`) and `tooltip::fits` / `pack`. At 2560 x 1440 and options uiScale 0.562814 (s = 1.125628): sample in jura 19 (not in the file: 18 scaled 19/18) = 479 px, about 62 characters of Detail text in linbiolinum_sansserif 20; at scale 1 below 1600 px: 288 px, about 44.
- Tooltip lines are coloured **per line via `GameTextClass`** [ref gd-uniquetab uses no inline tags]. Whether inline `{^X}` works inside a `GameTextLine` is **[unverified]**, so Phase 4 tests both. The UX mock's green/red/grey total maps to a class per line in the worst case.

## 5. Database (`database.arz` + `gdx1/2/3/*.arz`, ARZ v3, LZ4; `.arc` v3, LZ4)
Override order base → gdx1 → gdx2 → gdx3 (e.g. `playerlevels.dbr` exists in base/gdx1/gdx2; gdx2 raises `maxPlayerLevel` 85→100, `maxDevotionPoints` 50→55). Verified constants:
- `records/game/gameengine.dbr`: `playerDefenseCap=[80,80,80]`, `playerPetDefenseCap=[100…]`, `armorDefensiveAbsorption=70`, `playerAttackSpeedCapMin/Max=20/200`, `playerSpellCastSpeedCapMin/Max=20/200`, `playerRunSpeedCapMin/Max=20/135`, `playerReflectCap=30`, `dwWeaponSpeedFactor=0.5`, `skillMasteryTierLevel=[1,5,10,15,20,25,32,40,50]`
- `records/game/combatformulas.dbr`: OA/DA equations (`(… + level*12 + (dex|str + bonus)*0.5) * (1+mod/100) + 53`), PTH equation, PTH thresholds/crit multipliers (`pthThreshold1..6`, `pthDamageModifier1..6`), `magicalDamageEquation` (int/215), `physicalDamageEquation` (dex/245), duration variants (/200, /215), armor equations (`physicalDamageDefenseEquationDGP`), shield block, hit-region chances
- `records/creatures/pc/playerlevels.dbr` (gdx2): `strength/dexterity/intelligenceIncrement=8`, `lifeIncrement=20`, `lifeIncrementDexterity=8`, `lifeIncrementIntelligence=12`, `manaIncrement=16`
- `records/creatures/pc/malepc01.dbr` (gdx3): base `characterLife/Mana=250`, attrs 50, OA/DA 65, `characterAttackSpeed/SpellCastSpeed=1.25`, `characterRunSpeed=0.93`, `skillTree1..10`
- **Difficulty resist penalty**: `records/game/balancingadjustment_mp+difficulty_players01.dbr` (AttributePak, 12-entry arrays). Entries 0–3: 0; 4–7: −25 Fire/Cold/Lightning/Pierce/Poison; 8–11: −50 for those plus −25 Aether/Chaos/Bleed/Vitality/LifeLeech. Reading = 3 difficulties × 4 player counts **[index mapping unverified — do not assume; resolve by Phase 3 calibration against the in-game sheet]**.
- FoA adds `records/game/gameascendant.dbr`, `balancingadjustment_ultramode_enemies01.dbr` (enemy side).
- Attribute names in DB are the legacy ones: Strength=Physique, Dexterity=Cunning, Intelligence=Spirit (consistent with `tagCharStats*`) **[mapping to confirm against UI tags in MECHANICS.md]**.

## 6. Hook plan (minimal, read-only)

| # | Target | Kind | Purpose | Phase |
|---|---|---|---|---|
| 1 | `Engine::GetVersion`, module hashes, all targets below resolved by export name | call | gate: disabled only if a **required** export is missing/forwarded or a detour target is ICF-folded; unknown hash = warning only (decision 2026-11-14) | 1 |
| 2 | `gGameEngine` → `GetMainPlayer`, `GetGameDifficulty`, Character getters, item accumulators | call (game thread only) | snapshot | 2 |
| 3 | `Item*/ItemEquipment/ItemRelic/…::GetUIDisplayText` | detour (call original first) | identify hovered item + line vector | 4 |
| 4 | `vector<GameTextLine>::push_back` (0x1C550, unexported) | call from hook 3 | append our lines after the original; any failure → block off | 4 |
| 5 | Hotkeys | `GetAsyncKeyState` polled from the hook/tick, no WndProc hook | snapshot dump, preset cycle | 2/5 |

Game-thread tick for snapshot work: `GameEngine::Update(int)` (0x2D54B0, no ICF alias) is detoured in Phase 2 — the hotkey dump needs a tick outside tooltip building. Phase 2 also detours `ItemEquipment/ItemArtifact::GetUIDisplayText`, only to record the item id (hook 3's identification part).

Build: clang-cl + lld-link + xwin SDK/CRT, **`/MD`** (see risk 1), MinHook 1.3.4 vendored in `third_party/minhook`. All engine reads go through SEH-guarded helpers; no locks inside SEH frames.

## 7. Risks
1. **Allocator boundary**: the game uses dynamic CRT (Game.dll imports `malloc`/`free` from api-ms-win-crt-heap → ucrtbase, plus `MSVCP140`/`VCRUNTIME140`), so the adapter is built **`/MD`** and shares that heap: `std::string`/`std::allocator` memory the game returns can be freed by us. **Open:** `mem::vector`/`mem::list` carry no allocator in their mangled names; whether they use `operator new`/`malloc` or a private pool is unverified (Ghidra) — until then never free or grow `mem::` containers. The borrowed-array technique keeps ownership on our side. Lines built with the game's `GameTextLine` ctor contain **game-allocated** strings and no dtor is exported. Options: (a) build `GameTextLine` ourselves with our own string storage (layout validated against one ctor-built line), or (b) build lines once and keep them alive. Decide in Phase 4.
2. **Engine.dll differs between launch options**, so the known-build list (`core/include/gdc/known_builds.h`) has both. Game.dll is shared.
3. **dxgi coexistence**: OptiScaler removed 2026-11-14; the user now runs a DLSS mod as ReShade `compat/dxgi.dll` (dlssnr-amd). `deploy.sh`/`gdcompare-setup` never remove the `dxgi` override and never touch `dxgi.dll`.
4. **Sheet values without an exported sheet**: some displayed values (OA/DA, crit, resist with overcap) may only exist inside unexported UI code. Mitigation: Ghidra on Game.dll (not the exe, which has the SteamStub DRM; .text is encrypted on disk [ref gd-uniquetab]).
5. **Accumulator layouts** (`CombatAttributeAccumulator` etc.) are opaque. We construct them via exported ctors and read them via exported getters where available, otherwise SEH-guarded offsets validated at startup.
6. **ICF-folded exports**: guard with an alias-count check before hooking.
7. **Patch churn**: every game update can change Game.dll; unknown hash → warning; a missing/renamed required export → mod inert + log line. `ctest` (`test_real_binaries`) catches this on Linux before launching.
8. Multiplayer: hooks are client-side UI/read only; no packets, no writes.

## 8. Open questions (resolve in Phase 1–3)
- ~~`ObjectManager` id→object lookup~~ resolved in Phase 2: unexported `ObjectFromId(ObjectManager*, u32)` at RVA 0x19D40, located at runtime from its call in `Character::GetEquipCharAttributes` (+0x82) and checked against the `Singleton<ObjectManager>::Get` IAT slot (0x609098). Takes a critical section at `om+0x18`. See `docs/SNAPSHOT.md`.
- ~~ItemReplicaInfo field names~~ resolved: `Item+0x538`, offsets from `ItemReplicaInfo::StreamProperties` (0x585840), table in `docs/SNAPSHOT.md`.
- ~~Item stat getter slots~~ resolved: Object-subobject vftable slots 0x498/0x4A0/0x4A8/0x4B0/0x4B8/0x4C0/0x4C8/0x4D0/0x4D8, checked against the exported `{for GAME::Object}` vftables. The float parameter of 0x4A8/0x4B8 is unknown.
- `mem::map` node layout (needed for stash via `InventorySack::GetInventory`).
- OA/DA, crit chance/damage, and "resist incl. overcap" getters.
- GameTextClass numeric values and inline `{^X}` support in tooltip lines.
- Difficulty-penalty array indexing.
- `EquipLocations` enum values (use `EquipManager::LocationToString` at runtime to log them).

## 9. Tools used (NixOS)
`winedump` (system), `llvm-undname`/`llvm-readobj` via `nix-shell -p llvm`, Python recon readers `re/tools/arc.py`, `re/tools/arz.py` (pure-Python LZ4 block decoder; layout checked against the archive byte counts). Crate's `ArchiveTool.exe` under wine asserts on extract (`archivewriter.cpp:848`), so don't use it. Ghidra is not installed (`nix-shell -p ghidra` when needed).

## 10. Patch 1.3.1.2 (2026-11-16)

| Item | Value | Source |
|---|---|---|
| Steam build | buildid `26655545` (was `25813250`) | appmanifest |
| Game version | **v1.3.1.2 (x64)** | `Engine::GetVersion` string in Engine.dll (both builds) |
| Game.dll | `4e259d4f…6aa6f3`, identical in x64/ and compat/ | sha256 |
| Engine.dll | x64 `b0881e4e…cf20b63c`, compat `ad7fdc95…87120ffe` | sha256 |

Exports (`re/*.exports.tsv` regenerated): Game.dll 25,116, Engine.dll 6,286, exe 26. Added in Game.dll: `Destructible::PlayerOnly`, `Skill::GetSpecialAnimationName`, `PetPen::GetPetList`, `Character::SkillWarmUp` with two bools, `CombatAttribute_DamageCritBonus` / `CombatAttribute_TotalDamageMod::GetMinMaxValues`, and new overloads `SkillManager::GetCharAttributes(acc, bool)` / `GetOffensiveModifierAttributes(acc, uint, bool)`. Removed: the old `SkillWarmUp` and `SkillManager` overloads, `Player::CalculatePetCharAttributeBonus`, `Player::CalculatePetDamageBonus`. Engine.dll adds `Effect::CorrectParticles`, `Emitter::CorrectParticles`, `Engine::RecheckExpansion3Status`.

The new bool on `SkillManager::GetCharAttributes` (0x52F260) skips the learned-skill list (+0x228) when true. Both player-sheet callers pass false (`Character::GetSkillCharAttributes` +0x697E4, `CollectAvailableOffensiveModifierAttributes` 0x535C3A with `r9d` = 0), so the sheet is built as before. The item-skill walk at +0x258 is unchanged (`vt+0x1C8` != 0, then `vt+0x408`).

All 168 targets resolve by name. Every offset, vtable slot and call site the adapter depends on is checked by `re/tools/verify_build.py` (43 checks against the disassembly, all pass on 1.3.1.2). Moved RVAs: `GetUIDisplayText` 0x33D700, the `Singleton<ObjectManager>::Get` IAT slot 0x608098, `GenerateSearchText` 0x328D90 (its `call [rax+0x458]` still at +0xB8), `GameTextLine` ctor 0x312A50. Unchanged: line push_back 0x1C550, move 0x1C4C0, object lookup 0x19D40. `tests/test_code_scan.cpp` pins both builds.

DB: the monster pool grew (2559 Common / Champion / Hero, was 2529; 485 Quest / Boss / SuperBoss, was 448), `re/EnemyByLevel.tsv` / `EnemyRaces.tsv` and `core/src/enemy.cpp` regenerated (`re/tools/enemy_cpp.py`). Unchanged: `combatformulas.dbr`, PTH table, `playerlevels.dbr`, regen equations, boss resist means, `EnemyResistByDifficulty.tsv`, `PlayerResistByDifficulty.tsv`. No new stat field on items (`docs/COVERAGE.md`).
