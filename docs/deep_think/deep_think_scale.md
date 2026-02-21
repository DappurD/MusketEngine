# Deep Think Prompt #1: Spatial Scale & Threading — M8

> **PREREQUISITE**: Read Prompt #0 (Meta-Audit) first.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §1 Vision | "100,000 agents fully simulated at 60 FPS" |
| GDD | §2.5 Rule 5 | Async pathfinding — background thread, agents use stale data 2-3 frames |
| STATE.md | Performance | VolleyFireSystem O(N²) = **4.4s/tick at 10K entities**. Spatial hash is prerequisite for everything |
| STATE.md | M8 | "Spatial hash grid — fix O(N²) targeting, prerequisite for everything" |
| CORE_MATH | §1 | Spring-damper is O(N) — scales fine. Problem is TARGETING, not physics |

## § What's Already Built

**The bottleneck** (`musket_systems.cpp`, `VolleyFireSystem`):
- Each firing soldier does `w.each()` over ALL enemies to find nearest in target battalion
- At 1K entities: 46ms. At 10K: 4.4 seconds. **This is the #1 scaling blocker.**

**MacroBattalion centroid cache** (`world_manager.cpp`):
- `g_macro_battalions[256]` populated every frame via `ecs.each<>()` in golden TU
- Battalion-level targeting is O(B²) where B=active battalions (max 256). This is fine
- The bottleneck is the PER-SOLDIER nearest-enemy lookup within the target battalion

**PanicGrid** (`musket_components.h`):
- 64×64 CA grid already exists as a spatial structure
- Cell size = 4.0m. Total coverage = 256m × 256m
- This is NOT the spatial hash — it's a dedicated panic diffusion grid

## § Legacy Code Reference

| File | What to Mine |
|------|-------------|
| `legacy_assets/cpp_src/pheromone_map_cpp.cpp` | Full spatial grid: init, chunk-based skip, `gradient()` sampling, deposit functions |
| `legacy_assets/cpp_src/influence_map.h` | Grid-based spatial queries with channel system |
| `legacy_assets/cpp_src/gpu_tactical_map.h` | GPU-accelerated grid (compute shader path) |

## § Design Questions

### Spatial Hash Grid
1. Grid resolution: same 4m cells as PanicGrid? Or separate grid with different cell size?
2. Entity insertion: full rebuild every frame? Or incremental move-on-change?
3. Query API: `get_entities_in_radius(x, z, r)` → returns entity IDs? Or `get_nearest_enemy(x, z, team)`?
4. Memory layout: SoA flat arrays (like PanicGrid) or hash map? At 10K entities, which is faster?

### Threading (GDD §2.5 Rule 5)
5. Which systems can run multi-threaded? Spring-damper is embarrassingly parallel. VolleyFire has race conditions (kill flags). PanicCA is sequential
6. Flecs pipeline stages: `PreUpdate` (spatial hash rebuild) → `OnUpdate` (physics) → `PostUpdate` (targeting/combat). What ordering?
7. Flow field recalculation: background thread with stale-field tolerance (2-3 frames). What triggers a recalc? Building placement? Wall destruction?

### LOD / SLOD (GDD §8.4)
8. Off-screen regions: GDD says 0.1Hz abstract tick. When does a region become "off-screen"? Camera frustum? Distance threshold?
9. SLOD math: `inventory_iron += abstract_throughput` replaces pathfinding 50 miners. How does this abstract tick maintain determinism with the full sim?

## Deliverables
1. Spatial hash grid specification (cell size, memory layout, query API, rebuild strategy)
2. Threading model (which systems parallelize, pipeline ordering, sync points)
3. Benchmark targets: VolleyFireSystem with spatial hash at 10K, 50K, 100K entities
4. LOD/SLOD transition specification
5. ⚠️ Traps section
