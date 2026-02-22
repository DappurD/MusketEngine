# STATE LEDGER

> This file is the AI agent's external memory. Before starting any new task, **read this file** and `docs/GDD.md`. After completing a task, **update this file**.

## Current Phase
**Phase 1: Combat Prototype** — M0.5 ✅, M1 ✅, M2 ✅, M3 ✅, M4 ✅, M5 ✅, M6 ✅, M7 ✅, M8 ✅, M9 ✅, M10-M12 ✅. All tested 2026-02-21.

## Project Identity
- **Game**: The Musket Engine — Neuro-Symbolic Napoleonic War & Economy Simulator
- **Engine**: Godot 4.6 + GDExtension (`musket_engine.dll`) + Flecs ECS (C++)
- **Architecture**: Data-Oriented Design (DOD). C++ = Brain. Godot = Eyes. Never mix.
- **Renderer**: **Vulkan / Forward+**.

## What Is Built & Compiling
| Component | Status | File(s) |
|---|---|---|
| Project structure | ✅ Ready | `project.godot`, `musket_engine.gdextension` |
| GDExtension boilerplate | ✅ Compiling | `cpp/src/register_types.cpp/.h` |
| ECS components (all POD) | ✅ Written | `cpp/src/ecs/musket_components.h` |
| SCons build | ✅ Unity Build | `cpp/SConstruct`, `cpp/src/ecs/musket_master.cpp` |
| godot-cpp | ✅ Submodule @ `godot-4.5-stable` | `cpp/godot-cpp/` |
| Flecs | ✅ Vendored (header-only) | `cpp/flecs/flecs.h/.c` |
| nlohmann/json | ✅ Vendored (header-only) | `cpp/thirdparty/json.hpp` |
| musket_engine.dll | ✅ Compiled | `bin/musket_engine.dll` |
| **M0.5: JSON Prefab Loader** | ✅ Complete | `prefab_loader.h/.cpp`, `res/data/*.json` |
| **M1: ECS Foundation** | ✅ Complete | See below |
| **M2: Battalion Movement** | ✅ Complete | `musket_systems.h/.cpp` |
| **M3: Volley Combat** | ✅ Complete | `musket_systems.h/.cpp`, `rendering_bridge.cpp` |
| **M4: Panic & Morale** | ✅ Complete | `musket_systems.cpp`, `musket_components.h` |
| **M5: Artillery** | ✅ Complete | `musket_systems.cpp`, `musket_components.h`, `rendering_bridge.cpp` |
| **M6: Battalion Rendering + Cavalry** | ✅ Complete | `rendering_bridge.h/.cpp`, `world_manager.h/.cpp`, `musket_systems.cpp`, `test_bed.gd` |
| **M8: Spatial Hash Grid** | ✅ Complete | `musket_components.h` (SpatialHashGrid singleton, MacroSimulated tag), `musket_systems.cpp` (SpatialGridRebuild, VolleyFire rewrite), `world_manager.cpp` |
| **M9: Per-Citizen Economy** | ✅ Complete | `musket_components.h` (Citizen 32B, Workplace 32B, Household, CivicGrid, Zeitgeist), `musket_systems.cpp` (5 economy systems + conscription observer), `world_manager.cpp`, `prefab_loader.cpp` |
| **M10-M12: Supply Chains** | ✅ Complete | `musket_components.h` (Workplace 64B multi-recipe, CargoManifest 32B), `musket_systems.cpp` (DiscreteBatchProduction, WagonKinematics, HazardIgnition, WagonCombatObserver), `prefab_loader.cpp` |
| **Napoleonic Asset Pack** | ✅ Imported | `res/models/{soldiers,props,buildings}/`, `res/textures/` |

### M1 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/world_manager.h/.cpp` | `MusketServer` Node — Flecs world, spawn, sync, orders |
| `cpp/src/ecs/rendering_bridge.h/.cpp` | Packs ECS transforms + team_id into PackedFloat32Array |
| `res/scripts/fly_camera.gd` | WASD + mouse free-look camera |
| `res/scripts/test_bed.gd` | MultiMesh setup, fire/march controls, per-instance team colors |
| `res/scenes/test_bed.tscn` | Ground plane, MusketServer, FlyCamera, DirectionalLight |

### M2 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/musket_systems.h/.cpp` | SpringDamperPhysics + FormationOrderMove systems |

### M3 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/musket_systems.cpp` | MusketReloadTick + VolleyFireSystem (nearest-enemy targeting) |
| `cpp/src/ecs/musket_components.h` | Added `TeamId`, `BattalionId` components |

### GDScript API (MusketServer)
| Method | Signature |
|---|---|
| `spawn_test_battalion` | `(count: int, center_x: float, center_z: float, team_id: int)` |
| `order_march` | `(target_x: float, target_z: float)` |
| `order_fire` | `(team_id: int, target_x: float, target_z: float)` |
| `get_alive_count` | `(team_id: int) → int` |
| `get_transform_buffer` | `→ PackedFloat32Array` |
| `get_visible_count` | `→ int` |
| `spawn_test_battery` | `(num_guns: int, x: float, z: float, team_id: int)` |
| `order_artillery_fire` | `(team_id: int, target_x: float, target_z: float)` |
| `order_limber` | `(team_id: int)` |
| `order_unlimber` | `(team_id: int)` |
| `get_projectile_buffer` | `→ PackedFloat32Array` |
| `get_projectile_count` | `→ int` |
| `get_active_battalions` | `→ PackedInt32Array` |
| `get_battalion_buffer` | `(battalion_id: int) → PackedFloat32Array` |
| `get_battalion_instance_count` | `(battalion_id: int) → int` |
| `spawn_test_cavalry` | `(count: int, center_x: float, center_z: float, team_id: int)` |
| `order_charge` | `(team_id: int, target_x: float, target_z: float)` |

### M5 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/musket_systems.cpp` | ArtilleryReloadTick, ArtilleryFireSystem (spawn shots), ArtilleryKinematicsSystem (gravity), ArtilleryGroundCollisionSystem (ricochet/mud), ArtilleryFormationHitSystem (KE penetration + canister cone) |
| `cpp/src/ecs/musket_components.h` | `ArtilleryAmmoType` enum (ROUNDSHOT/CANISTER), `ammo` field in `ArtilleryShot`, `unlimber_timer` in `ArtilleryBattery` |
| `cpp/src/ecs/rendering_bridge.cpp` | `sync_projectiles()` — packs active shot positions for MultiMesh |

### M4 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/musket_systems.cpp` | PanicDiffusionSystem (5Hz CA, per-team layers), PanicStiffnessSystem (routing tag), RoutingBehaviorSystem (5 m/s sprint) |
| `cpp/src/ecs/musket_components.h` | `PanicGrid` singleton (64×64 CA, 2 team layers), `Routing` tag |

### M6 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/musket_components.h` | `RenderSlot` (8B), `CavalryState` (24B, lock_dir_x/z), `FormationDefense`, `ChargeOrder`, `Disordered` |
| `cpp/src/ecs/rendering_bridge.h/.cpp` | `BattalionShadowBuffer` (lazy init), `sync_battalion_transforms()`, `register_death_clear_observer()` |
| `cpp/src/ecs/musket_systems.cpp` | `CavalryBallistics` (cubic ramp, locked vector), `CavalryImpact`, spring-damper airgap |
| `cpp/src/ecs/world_manager.cpp` | Battalion API, `spawn_test_cavalry()`, `order_charge()` (direction lock) |
| `res/scripts/test_bed.gd` | Battalion rendering via `multimesh_set_buffer()`, V toggle, C charge |

### M7 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/musket_components.h` | `Drummer` tag, `PendingOrder` struct, `OrderType` enum, expanded `MacroBattalion` (flag/drummer/officer/cohesion) |
| `cpp/src/ecs/world_manager.cpp` | `g_pending_orders[256]`, centroid pass detects M7 tags, flag cohesion decay, order delay pipeline, command staff spawning |
| `cpp/src/ecs/musket_systems.cpp` | SpringDamperPhysics flag_cohesion, drummer speed buff, officer blind fire, DrummerPanicCleanseSystem |
### M7.5 Files
| File | Purpose |
|---|---|
| `cpp/src/ecs/musket_components.h` | `FormationShape`, `FireDiscipline` enums, `alignas(64) SoldierFormationTarget` (64B, double coords, face vectors, can_shoot, rank_index), `MacroBattalion` +OBB/discipline/target_bat_id, `ORDER_DISCIPLINE` |
| `cpp/src/ecs/world_manager.cpp` | 3-rank spawner (0.8m×1.2m), embedded command staff, hoisted O(B²) targeting (OBB seg-int), Officer's Metronome, ORDER_DISCIPLINE pipeline, `order_fire_discipline()`, `order_formation()` geometry engine |
| `cpp/src/ecs/musket_systems.cpp` | VolleyFireSystem rewrite (`.without<Routing>()`, can_shoot, doctrine gates, stateless jitter, firing arc dot, hit_chance×dot), panic retuning (0.20/0.10/0.65/0.25), DistributedDrummerAura |
| `res/scripts/test_bed.gd` | M7.5 keybinds: 4-7 fire discipline, 8-0 formation shape |

## What Is NOT Built Yet
- ~~**M7.5: Dynamic Tactical Formations**~~ ✅ BUILT (feature/m7.5-formations branch)
- M8: LLM General (Battle Commander + State Compressor)
- M8-M14: LLM General, Economy
- **M15-M18: Urbanism & Siege** — full voxel engine
- M19-M22: Weather, Night, Audio, Cartographer
- **VAT Ragdoll Shader** — death observer infrastructure ready, shader not written
- **Asset pipeline** — FBX soldier models need Godot editor mesh export (.tres)

### 🔴 Open Design Questions (Think Before Building)
- **Melee Combat**: The engine has no melee system. Bayonet charges, cavalry saber engagements, and hand-to-hand routing need design. Key questions: Does melee use the same spring-damper slots? How do locked formations (Square) interact with charging infantry? What happens to `can_shoot` during melee? Does the panic grid need a "melee shock" injection separate from death fear?
- **Small Skirmish Scaling**: The current architecture is optimized for 200-500 man battalions with macro-level targeting (O(B²) on battalion centroids). Does this architecture degrade gracefully for 10-20 man skirmish parties, hunting parties, or scouting detachments? The `MacroBattalion` struct assumes all units operate as formations — solo or small-group entities may need a different targeting path.

### 🟡 Performance Findings (from Test Suite, 2026-02-21)
- **O(N²) Micro-Targeting Bottleneck**: `VolleyFireSystem` does `w.each()` per soldier to find the closest enemy in the target battalion. At 10K entities this takes **4.4 seconds per tick** (100M iterations). At 1K (realistic battle) it's 46ms. **Spatial partitioning** (grid hash or BVH) is needed before scaling beyond ~2K active shooters. This should be addressed before or during M8.
- **Traps 30-32 Verified Safe**: NaN bomb (Trap 30) — spring-damper uses raw `dx*stiffness`, no normalization at zero. Grid OOB (Trap 31) — `world_to_idx` already clamps to `[0, CELLS-1]`. Stale targeting (Trap 32) — `target_bat_id` resets to `-1` every frame in centroid pass.

## Implementation Law

Every system implementation MUST follow this process. No exceptions.

| Step | Action |
|---|---|
| 1. **Read** | Pull the exact spec from `GDD.md` + `CORE_MATH.md`. |
| 2. **Verify** | Use `/prove-math` to validate the algorithm works (convergence, reasonable constants, edge cases). If the GDD spec is wrong, flag it. |
| 3. **Flag** | If the spec is wrong or underspecified → **stop and ask the user** before writing code. Update the GDD first, then implement the updated spec. |
| 4. **Implement** | Translate the verified spec directly into code. No shortcuts, no "good enough for now." |
| 5. **Never invent** | If behavior isn't in the GDD, it doesn't exist. Don't improvise — extend the GDD and get sign-off first. |

The GDD is a **living document**. If a system needs a constant or behavior not yet specified, the correct action is to add it to the GDD and get user approval — never to make up values inline.

## Architectural Decisions
| Date | Decision | Rationale |
|---|---|---|
| 2026-02-20 | **Voxel engine: reference legacy, don't port directly** | Legacy `voxel_world.cpp` (1494 lines) has correct math but wrong architecture. Port DDA math at M5, full destruction at M15+. See `LEGACY_MAP.md` for detailed strategy. |
| 2026-02-20 | **Ballistic cavalry bypasses spring-damper** | Deep Think #3: "You cannot use an Attractor to simulate a Projectile." Charging cavalry use locked direction vector, cubic ramp, airgap from formation physics. |
| 2026-02-20 | **Per-team panic grid** | `PanicGrid.read_buf[2][CELLS]` — deaths on team X only panic team X. Prevents attackers from catching defender's panic. |
| 2026-02-20 | **Lazy battalion init** | Static `PackedFloat32Array` arrays crash DLL before Godot runtime. Use `new[]` on first access. |
| 2026-02-20 | **Golden TU rule** | All `ecs.each<>()` calls and `g_macro_battalions` MUST live in `world_manager.cpp`. MSVC generates different Flecs component IDs per Translation Unit. Only `ecs.system<>()` is safe cross-TU (does deep world lookup). |
| 2026-02-21 | **Flecs v4.1.4 API cheatsheet** | `e.get<T>()` returns `const T` (value, NOT pointer). Use `e.ensure<T>()` to get mutable `T&`. Use `e.set<T>({...})` for assignment. Use `e.has<T>()` for tag checks. Use `e.add<T>()` / `e.remove<T>()` for tags. Never use `->` on `get<T>()`. |
| 2026-02-20 | **No static ECS memory** | Never store `static flecs::query<>`. DLL survives Godot Play/Stop — statics point at dead worlds and return 0 entities. |
| 2026-02-20 | **Battalion-level targeting via MacroBattalion centroid cache** | `g_macro_battalions[256]` is populated every frame in `_process()` using `ecs.each<>()` in the golden TU. Cavalry reads centroids to find nearest enemy battalion. |
| 2026-02-20 | **No thread_local queries** | Trap 8: `thread_local new flecs::query` leaks memory and segfaults on Play/Stop. Use `w.each()` or macro battalion centroids instead. |
| 2026-02-20 | **O(B) targeting via centroids** | Trap 9: Volley fire and routing use macro battalion centroid lookup O(256) instead of O(N) full-entity scan. |
| 2026-02-20 | **Exponential decay damping** | Trap 19: `v *= exp(-damping * dt)` is unconditionally stable. Replaces semi-implicit Euler `v += (k*x - d*v) * dt` which explodes when `damping*dt > 1.0`. |
| 2026-02-20 | **Chrono-drift fix** | Trap 16: Panic grid `tick_accum -= 0.2f` preserves fractional remainder instead of resetting to 0. |
| 2026-02-20 | **Unity Build** | `musket_master.cpp` `#include`s all ECS `.cpp` files. Single TU permanently eliminates MSVC template static ID mismatch. `w.each<>()` is now safe everywhere. SCons compiles only `register_types.cpp` + `musket_master.cpp`. |

## Known Issues
- `flecs_STATIC` macro redefinition warning (harmless)
- godot-cpp using 4.5-stable (backwards compatible)
- C++ exception handler warning from nlohmann/json (no `/EHsc`)
- Static query in `rendering_bridge.cpp` is initialized on first call — safe for single-threaded Godot main thread
- **Rendering bridge builds queries per-call** — needs caching or conversion to registered systems (Trap 11)
- **M10: Projectile tunneling** — ROUNDSHOT_SPEED=200 at 60Hz = 3.3m/frame > 2m line depth. Need CCD segment check (Trap 12)
- **M10: Panic grid edge singularity** — `world_to_idx` clamps to edges, routing soldiers stack in corner cells (Trap 14)
- **M10: Unaligned POD structs** — `MusketState` 6B, `Workplace` 10B. Add `alignas(8)` + padding (Trap 18)
- **M12: PanicGrid data race** — `std::atomic<float>` needed for multi-threaded ECS (Trap 13)

## C++ ↔ Godot Bridge
- GDExtension: `musket_engine.gdextension`
- Entry symbol: `musket_engine_init`
- Registered class: `MusketServer` (Node), 3 methods bound via ClassDB
- Build: `python -m SCons platform=windows target=template_debug` in `cpp/`
- **Architecture**: Unity Build — only `register_types.cpp` + `musket_master.cpp` are compiled
- Output: `bin/musket_engine.dll`

## Immediate Next Step
1. ~~M1-M6~~ ✅ DONE
2. ~~Unity Build~~ ✅ DONE
3. ~~Trap fixes 8, 9, 16, 19~~ ✅ DONE
4. **Team color shader** — reads `INSTANCE_CUSTOM.g` for team-based albedo
5. Begin **M7: Command Network**
