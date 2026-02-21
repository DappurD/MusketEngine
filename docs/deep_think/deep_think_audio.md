# Deep Think Prompt #10: Audio & Military Music — M19-M22

> **PREREQUISITE**: Read Prompt #0 first. Builds on M7 command network.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §5.4 | Command Network: Drummer = -0.2 panic/tick, 2s→8s order delay when dead |
| GDD | §8.3 | Acoustic Physics: speed of sound (343 m/s), audio delay, diegetic music |
| CORE_MATH | §8 | Acoustic delay GDScript: `dist / 343.0 = delay_seconds` |

## § What's Already Built
- `Drummer` tag, `DrummerPanicCleanseSystem` (M7)
- `FormationAnchor` tag for flag bearer
- Command network: drummer death = 8s order delay, silence, cohesion decay
- Speed of sound delay code exists in CORE_MATH §8

## § Design Questions
1. Drum signals as state-change triggers: specific drum patterns → ECS state changes (Reveille, Assembly, Charge, Retreat). How does audio trigger gameplay?
2. Signal range: drums audible within X meters — uses spatial hash for range query?
3. Faction audio: French batteries d'Ordonnance vs British regimental bands. Gameplay effects?
4. Sonic fog of war: in heavy smoke, players locate units by listening for drum patterns. Implementation?
5. Diegetic vs orchestral: GDD says orchestral only for statistical extremes (40%+ routing). What triggers it?

## Deliverables
1. Drum signal → ECS state mapping
2. Spatial audio range system
3. Faction audio specification
4. Diegetic music trigger system
5. ⚠️ Traps section
