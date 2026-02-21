# Deep Think Prompt #8: AI Systems — Civilian, Military & Strategic

> **PREREQUISITE**: Read Prompt #0 first. Requires M8 (Spatial Hash + LLM General), M9 (Citizens).

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §2.2 | Two-layer arch: citizens visually act out economic life via state machines |
| GDD | §7.1 | Smart Buildings, Dumb Agents: 1Hz matchmaker, O(1) scaling |
| GDD | §7.6 | Zeitgeist: O(1) SIMD reduction → feeds LLM Mayor prompts |
| GDD | §6 | LLM General controls the battlefield (see Prompt #7) |
| GDD | §5.1 | Scale Switch: 20+ = formation, <20 = skirmish |
| CORE_MATH | §7 | Zeitgeist query pattern |

## § What's Already Built

- `CitizenRoutine` state machine (GDD §2.2, designed but not implemented)
- `SpringDamperPhysics` — citizens reuse the same movement system
- `MacroBattalion` centroid + OBB targeting (M7.5) — military AI reads this
- `PanicGrid` — military AI reads panic levels for decision-making
- `Drummer`/`FormationAnchor` tags — command network affects AI decisions

## § Legacy Code Reference

| File | What to Mine |
|------|-------------|
| `cpp_src/colony_ai_cpp.h` | 8 goal types (CAPTURE, DEFEND, ASSAULT, FLANK, HOLD, RECON, FIRE_MISSION, DEFEND_BASE), SquadRole enum, CoordTag bitmask |
| `cpp_src/colony_ai_cpp.cpp` | Full auction scoring: `_score_capture_poi()`, `_score_assault_enemy()`, etc. Coordination bonus, hysteresis anti-thrashing |
| `ai/colony/ARCHITECTURE.md` | Economy AI: EconomyState, TaskAllocator, BuildPlanner |
| `cpp_src/theater_commander.h` | 9-axis strategy: aggression, concentration, tempo, risk_tolerance, exploitation, terrain_control, medical_priority, suppression_dominance, intel_coverage |

## § Three AI Tiers

### Tier 1: Civilian AI
- Reuses `SpringDamperPhysics` for movement (spring toward waypoints)
- `CitizenRoutine` state machine drives phase transitions
- Questions: crowd behavior at market? Rain response? Emergency sheltering?

### Tier 2: Military AI (integrates with LLM General)
- LLM provides strategic direction (30-60s async)
- Battle Commander provides 60Hz instant reactions
- Legacy `ColonyAICPP` auction provides the FALLBACK when LLM is offline
- Questions: which legacy goal types map to Crown & Caliber? New goals needed?

### Tier 3: Strategic AI (AI Rival Lords)
- GDD §8.4: AI lords exist on the Cartographer's Table
- Questions: full citizen sim or abstracted? How granular is AI economy?

## Deliverables
1. Civilian emergent behavior specification
2. Military AI: map legacy ColonyAICPP goals → Crown & Caliber orders
3. Strategic AI lord architecture (full sim vs abstracted)
4. Integration with LLM General (Prompt #7) — how do the tiers compose?
5. ⚠️ Traps section
