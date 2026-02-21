# Deep Think Prompt #2: Per-Citizen Economy — M9

> **PREREQUISITE**: Read Prompt #0 (Meta-Audit) first. Requires M8 (Spatial Hash) to be designed.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §2 | Full per-citizen simulation: `CitizenRoutine` state machine, family units, needs, approval |
| GDD | §2.2 | Two-layer architecture: Layer 1 = invisible 5Hz economy, Layer 2 = visible 60Hz behavior |
| GDD | §7.1 | "Smart Buildings, Dumb Agents" — buildings post to `LogisticsJob` array, 1Hz matchmaker assigns closest IDLE citizen |
| GDD | §7.5 | Conscription bridge: `e.remove<Citizen>(); e.add<SoldierFormationTarget>();` |
| GDD | §7.6 | Zeitgeist: O(1) SIMD reduction over 100K citizens for political mood |
| CORE_MATH | §6 | Exact structs: `Citizen` (16B), `Workplace` (10B), `LogisticsJob`, 60Hz movement loop |
| CORE_MATH | §7 | Zeitgeist query pattern |
| STATE.md | M9 | "Per-citizen economy — family entities, needs, marketplace aura, approval" |
| STATE.md | M10 | "Draft system — citizen ↔ soldier mutation, economic impact" |

## § What's Already Built

**Reusable systems**:
- `Position`(8B), `Velocity`(8B) — citizens use these same components
- `SpringDamperPhysics` — citizens can reuse the SAME spring-damper for movement (spring toward waypoints)
- `PanicGrid` 64×64 CA — same grid architecture could host marketplace aura, approval, pollution
- `MacroBattalion` centroid cache — pattern can be extended to `MacroDistrict` aggregation

**The draft bridge** (GDD §7.5):
```cpp
e.remove<Citizen>();              // Drops from economy loop
e.add<SoldierFormationTarget>();  // Adds to combat loop
```
This is the MOST IMPORTANT integration point. The citizen struct must be designed so this mutation works cleanly.

## § Legacy Code Reference

| File | What to Mine |
|------|-------------|
| `legacy_assets/ai/colony/ARCHITECTURE.md` | EconomyState, TaskAllocator, BuildPlanner design |
| `legacy_assets/ai/colony/core/` | GDScript economy implementation (reference for logic, rewrite in C++) |
| `legacy_assets/cpp_src/pheromone_map_cpp.cpp` | Grid-based proximity (marketplace aura could use same CA diffusion) |

## § GDD Structs (Already Specified in CORE_MATH §6)

```cpp
struct Citizen {             // 16 bytes
    enum State { IDLE, WALKING_TO_SOURCE, WALKING_TO_DEST };
    State current_state;
    uint8_t carrying_item;   // ITEM_WHEAT, ITEM_MUSKET, ITEM_IRON_ORE
    uint8_t carrying_amount;
    flecs::entity current_target;
};

struct Workplace {           // 10 bytes
    uint8_t consumes_item;
    uint8_t produces_item;
    int inventory_in;
    int inventory_out;
};

struct LogisticsJob {
    flecs::entity source_building;
    flecs::entity dest_building;
    uint8_t item_type;
};
```

**These structs exist in CORE_MATH but are NOT yet in `musket_components.h`.** Deep Think must evaluate whether they're complete enough for the full vision or need expansion.

## § Design Questions

### Citizen Lifecycle
1. The `CitizenRoutine` in GDD §2.2 has 7 phases (SLEEP→COMMUTE→WORK→MARKET→HOME). Does `Citizen.State` (3 states in CORE_MATH §6) need expansion to match?
2. Family system: `FamilyId` component linking 2-4 entities. How does this work without pointer chasing in flat ECS? Parent-child via shared `family_id` integer?
3. The conscription bridge: when a citizen becomes a soldier (`remove<Citizen>` + `add<SoldierFormationTarget>`), what happens to their `Workplace` reference? Must the workplace detect the missing worker and post a new `LogisticsJob`?

### Economy Architecture
4. Marketplace aura (GDD §2.2 Layer 1): "Is there bread in the market? Is this citizen within range?" — use the spatial hash from M8? Or a dedicated CA grid like PanicGrid?
5. The 1Hz matchmaker (GDD §7.1): scans `LogisticsJob` array, finds closest IDLE citizen. Is this O(N×J) or can the spatial hash make it O(J)?
6. Flow field pathfinding: citizens follow pre-calculated flow fields (GDD §7.1). How many flow fields active at once? One per building? Per road network?

### Approval & Migration
7. GDD §2.3: approval >75% = 2 families/month, <25% = unrest. Is approval per-citizen aggregation (Zeitgeist query) or per-district?
8. Migration: new families arrive as entities. Where do they spawn? Nearest road entrance? Edge of map?

### The Struct Design Question
9. Is `Citizen` (16B) big enough? It needs: state, carrying item, family_id, workplace_id, satisfaction, occupation. That's more than 16B. Design the FULL struct now.
10. Does `Workplace` (10B) need: worker_count, max_workers, production_rate, hazard_level? Full vision struct?

## Deliverables
1. Complete `Citizen` POD struct (full vision, byte-counted, aligned)
2. Complete `Workplace` POD struct (full vision)
3. `FamilyId` component design
4. Marketplace aura system specification
5. 1Hz Matchmaker algorithm
6. Conscription bridge integration (citizen→soldier→citizen round-trip)
7. Zeitgeist aggregation system
8. ⚠️ Traps section
