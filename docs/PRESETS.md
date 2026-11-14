# PRESETS — design choices

## Stage thresholds (decision 2026-11-14)
No source gives thresholds for stages below endgame, so none are derived or scaled.
- Every `[[stage]]` block lists its own threshold values explicitly.
- Resist target is 80 % at every stage (`gameengine.dbr` `playerDefenseCap` [80,80,80]).
- Other thresholds for lower stages are chosen per preset and commented as design choices, not mechanics.

## Uptime fallback (decision 2026-11-14)
Last resort only: `0`, so a guess never inflates a score. Never silent: the tooltip shows `+? unscored: <effect>` next to the total, the Shift view marks it `default ⚠`. The preset sets the real value (`[uptime]`, per stage if needed). Pets use a preset value until Phase 3. Mechanism: `docs/MECHANICS.md` §4.

## Threat profile: `incoming_hits_per_sec` (decision 2026-11-14)
Drives "on being hit" uptimes. Design guesses, no source; tune from play, override per preset/stage.

| Stage (plague-garden) | Default |
|---|---|
| 1–10 | 1.0 |
| 10–35 | 1.5 |
| 35–93 | 2.0 |
| 94+ | 3.0 |
