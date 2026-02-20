# STATE LEDGER

> This file is the AI agent's external memory. Before starting any new task, **read this file** and `docs/GDD.md`. After completing a task, **update this file**.

## Current Phase
**Phase 1: Combat Prototype** — M0.5 ✅, M1 ✅, M2 ✅, M3 ✅, M4 ✅, M5 ✅, M6 ✅. All tested 2026-02-20.

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
| SCons build | ✅ Working | `cpp/SConstruct` |
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

## What Is NOT Built Yet
- M7: Command Network
- M8-M14: LLM General, Economy
- **M15-M18: Urbanism & Siege** — full voxel engine
- M19-M22: Weather, Night, Audio, Cartographer
- **VAT Ragdoll Shader** — death observer infrastructure ready, shader not written
- **Asset pipeline** — FBX soldier models need Godot editor mesh export (.tres)

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

## Known Issues
- `flecs_STATIC` macro redefinition warning (harmless)
- godot-cpp using 4.5-stable (backwards compatible)
- C++ exception handler warning from nlohmann/json (no `/EHsc`)
- Static query in `rendering_bridge.cpp` is initialized on first call — safe for single-threaded Godot main thread
- **Cavalry slightly OP** — momentum 2.0 may need tuning down. Each horse kills ~6 against Line. Adjust the `2.0f` multiplier in `CavalryBallistics` system or cost formula in `CavalryImpact`.
- **No team colors in battalion mode** — `use_colors` disabled for buffer stride match. Team coloring needs a shader reading `INSTANCE_CUSTOM`.

## C++ ↔ Godot Bridge
- GDExtension: `musket_engine.gdextension`
- Entry symbol: `musket_engine_init`
- Registered class: `MusketServer` (Node), 3 methods bound via ClassDB
- Build: `python -m SCons platform=windows target=template_debug` in `cpp/`
- Output: `bin/musket_engine.dll`

## Immediate Next Step
1. ~~M1-M6~~ ✅ DONE
2. **Tune cavalry** — momentum multiplier and charge ramp constants
3. **Team color shader** — reads `INSTANCE_CUSTOM.g` for team-based albedo
4. **VAT ragdoll shader** — reads death data from `custom_data[12-15]`
5. Begin **M7: Command Network**
