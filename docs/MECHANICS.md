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

## Not modelled
- Devotion refund / "self-support" (e.g. Crane counting its own affinity): irrelevant, the adapter reads the actual devotions.
