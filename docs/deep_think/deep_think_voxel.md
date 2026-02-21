# Deep Think Prompt #4: Voxel Integration — M13-M14

> **PREREQUISITE**: Read Prompt #0 (Meta-Audit) first. M8 spatial hash should exist.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §5.5 | Siege: Star Forts, The Breach (artillery shatters voxels → debris ramp → flow field recalc), Medical |
| GDD | §7.4 | Procedural Urbanism: Voronoi plots, Vauban solver, Haussmannization |
| GDD | §2.5 Rules 3+5 | Airgap (no Godot includes in Flecs) + Async (grid recalc on background thread) |
| CORE_MATH | §3 | DDA Artillery (already implemented in musket_systems.cpp). Voxel integration extends this |
| CORE_MATH | §5 | Vauban Magistral Line vertex math |
| STATE.md | M13 | "Voxel integration — port legacy voxel engine, LOS raycasting" |
| STATE.md | M14 | "Fortifications — voxel placement, structural integrity, siege" |
| LEGACY_MAP.md | Full section | Phased porting strategy (Beachhead → Full Engine → Rendering) |

## § What's Already Built

**Existing voxel touchpoints**:
- `ArtilleryShot.kinetic_energy` — currently decrements per man penetrated. Voxel integration: KE also spent destroying voxel blocks
- `ArtilleryKinematicsSystem` — gravity, position updates. Already works. Voxel collision = terrain height check (placeholder)
- DDA math from CORE_MATH §3 — the raycast algorithm IS the voxel traversal algorithm

**PanicGrid architecture** (reusable pattern):
- 64×64 flat array, double-buffered, chunk-based skip
- Voxel grid can use the SAME pattern: flat array, chunk indexing, async rebuild

## § Legacy Code Reference (from LEGACY_MAP.md)

| File | Lines | What to Mine |
|------|-------|-------------|
| `cpp_src/voxel_world.cpp` | 1,494 | Full terrain: get/set, coord conversion, DDA raycast (L481-597), `destroy_sphere()` (L174-401), `destroy_box()` (L410-475) |
| `cpp_src/voxel_world.h` | — | Chunk/world dimensions, material enums |
| `cpp_src/structural_integrity.cpp` | 576 | Collapse: connectivity check, flood-fill islands, ground-distance BFS |
| `cpp_src/voxel_mesher_blocky.cpp` | — | Greedy meshing algorithm |
| `cpp_src/voxel_generator.cpp` | — | Procedural terrain generation |

### Porting Rules (LEGACY_MAP.md)
1. **Steal the math, rewrite the plumbing**
2. **Voxel grid = Flecs singleton resource**, not a Godot Node
3. **Destruction → async**: `destroy_sphere()` queues rebuild on background thread
4. **Meshing on main thread**: C++ marks dirty chunks, Godot reads + builds `ArrayMesh`

## § Design Questions

### Voxel Grid Architecture
1. Voxel grid as Flecs singleton: `ecs.set<VoxelGrid>({...})`. What's the struct? Flat array per chunk? Chunk size?
2. Material types: stone, earth, wood, iron. How does material affect: artillery penetration, structural integrity, fire spread?
3. The legacy `voxel_world.cpp` uses a `VoxelChunk` class hierarchy. Rewrite as flat POD struct?

### Destruction Pipeline
4. `destroy_sphere(center, radius, KE)` — how does ArtilleryShot interact? When shot hits terrain, spend KE to destroy voxels in sphere?
5. Structural integrity after destruction: flood-fill checks which blocks are still connected to ground. Disconnected blocks → cascade collapse → more panic injection
6. Async rebuild: destruction happens on main thread (deterministic). Mesh rebuild dispatched to background. What's the sync protocol?

### Fortifications (M14)
7. Player-built fortifications: Vauban solver (CORE_MATH §5) generates vertices. How do these become voxel blocks?
8. The Breach: "Artillery shatters wall → debris cascades into 45° rubble ramp → C++ recalculates flow field → enemy pours through." Design the breach pipeline
9. Siege trenching: `destroy_box()` along a sapping path. How does the player draw the sap line?

### Research: Fortification Progression
10. Palisade (wood, cheap, weak) → Redoubt (earth, field fortification) → Star Fort (stone, bastion geometry) → Polygonal Fort (late era). How does the voxel system handle different construction types?
11. Field fortifications (breastworks, abatis, trenches): temporary structures built DURING battle. Different from permanent city walls

## Deliverables
1. `VoxelGrid` singleton struct (Flecs resource, flat arrays, chunk layout)
2. Destruction pipeline (`destroy_sphere`, `destroy_box`, structural integrity)
3. Async mesh rebuild protocol
4. Fortification construction system (player placement → voxel blocks)
5. Breach pipeline (artillery → wall destruction → rubble ramp → flow field update)
6. ⚠️ Traps section
