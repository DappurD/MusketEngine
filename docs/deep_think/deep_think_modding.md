# Deep Think Prompt #14: Modding & Multiplayer — Core Architecture

> **PREREQUISITE**: Read Prompt #0 first. This is a cross-cutting audit, not a milestone.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §2.3 | Moddability Mandate: no hardcoded stats, JSON prefabs, components initialized from `res://data/` |
| GDD | §2.4 | Era-Agnostic Engine: C++ doesn't know about "muskets" — it knows `RangedWeapon` |
| GDD | §12.1 | Pillar 1: JSON Prefabs (`res://data/units.json`) |
| GDD | §12.2 | Pillar 2: LLM Personality API (`.txt` prompts in `res://data/ai_prompts/`) |
| GDD | §12.3 | Pillar 3: Visual overhauls (`.pck` loading) |
| GDD | §12.4 | Pillar 4: GDScript Event Bus (C++ signals, modders write 1Hz listeners) |
| GDD | §13 | Multiplayer: Coalition (asymmetric co-op), Tactical Duel (1v1), Grand Campaign |
| GDD | §14 | Dengine Modules: CoreModule, MeleeCombat, LineCombat, ModernCombat. Mix-and-match via `mod_config.json` |
| GDD | §4.3 | Networking: Server-authoritative, macro-sync at 10Hz, spring-damper visual interpolation |

## § What's Already Built
- JSON prefab loader (M0.5): `prefab_loader.h/.cpp` reads `res/data/*.json`
- `napoleon.txt`: LLM personality prompt exists in `res/data/ai_prompts/`
- All stats are in components (not hardcoded) — partial compliance with §2.3

## § Design Questions

### Modding Audit
1. Which current component fields are hardcoded constants instead of data-driven? (`MAX_SPEED = 4.0f` in CORE_MATH §1, `AMMO_ROUNDSHOT` enum) — enumerate ALL hardcoded values
2. JSON prefab format: is the current format sufficient for modders? What fields are missing?
3. Event Bus: which C++ systems currently emit Godot signals for modders to hook?

### Multiplayer Feasibility
4. GDD §4.3 specifies: server broadcasts macro-state at 10Hz (battalion anchors, grid updates, death events). Is this viable with current MacroBattalion architecture?
5. Coalition mode: Player 1 (Mayor) + Player 2 (Marshal). Different input → same ECS world. How?
6. What must NOT be done now that would block multiplayer later? (Deterministic RNG, tick ordering)

### Dengine Modules
7. Current code IS the `LineCombat` module. Can it be isolated into a Flecs module with clean boundaries?
8. What's in `CoreModule` (era-agnostic)? Spatial hash, pathfinding, spring-damper, panic CA?

## Deliverables
1. Hardcoded values audit (all magic numbers in current code)
2. JSON prefab format evaluation
3. Event Bus signal catalog
4. Multiplayer enabler recommendations (what to adopt now for free)
5. Dengine module boundary specification
6. ⚠️ Traps section
