# Game database (`gdc::arz`)

Reader: `core/include/gdc/arz.h`. Format from `re/tools/arz.py`, which parsed every record of the four
1.3.1.1 files during recon.

## .arz v3 layout

| Part | Layout |
|---|---|
| header (24 B) | `u16 ?`, `u16 version = 3`, `u32 record_table_off`, `u32 record_table_size`, `u32 record_count`, `u32 string_table_off`, `u32 string_table_size` |
| string table | blocks of `u32 n`, then `n × {u32 len, bytes}`, until `string_table_size` is consumed |
| record table | `record_count × {u32 name string index, u32 class len, class bytes, u32 data_off, u32 compressed, u32 decompressed, u64 file time}` |
| record data | at `24 + data_off`: a raw LZ4 block that decompresses to `{u16 type, u16 count, u32 key string index, count × 4 B}` repeated. Type 0 int32, 1 float, 2 string index, 3 bool (int32) |

## Files and layering

`Database::open_game(dir)` loads `database/database.arz`, then `gdx1/database/GDX1.arz`,
`gdx2/database/GDX2.arz` and `gdx3/database/GDX3.arz` if present. When a record name exists in several
files, the **last one wins whole** (no field merge). 1.3.1.1: 3341 base records are redefined by GDX1,
1598 by GDX2, 1765 by GDX3, and most redefinitions differ.

Evidence for that order comes from the F9 snapshots of 2026-11-14 (`tests/fixtures/snapshots/`). Each of these skill bonuses exists only in the newest copy of the item's record, and each shows up in or drops out of the character's `augmented_level`:
- `b102e_ring` (GDX1, GDX2): GDX2 adds `augmentSkillName3 = playerclass09/aegis1` +3. After equipping it, `aegis1` goes from — to 3.
- `c031_ring` (base, GDX2): GDX2 adds `playerclass09/summon_celestialguardian2_petmodifier` +2. After removing it, that skill goes from 2 to —.
- `b002e_torso` (GDX1, GDX3): GDX3 adds `playerclass10/werewolf2` +3. After removing it, `werewolf2` goes from 3 to —.

So GDX2 replaces GDX1 and base, and GDX3 replaces GDX1.

## Not handled

- Item stat jitter: equipped `c031_ring` reads Life 161 / OA 35 against record 200 / 32
  (`attributeScalePercent` 30). The roll comes from the item seed; the formula is not traced, so item stats
  come from the snapshot (engine getters), not from records.
- Mods (`mods/*/database/*.arz`): not loaded.
- `.arc` text (tags → display names): `re/tools/arc.py` only; the C++ port follows when the tooltip needs names.
