# Legacy Assets (Reference Only — DO NOT COMPILE)

These files were copied from the previous `ai-test-project` prototype. They contain working C++ and GDScript systems that must be **refactored** into Dengine's `flecs::module` architecture before use.

## What's Here

### `cpp_src/` — The C++ Math (The Gold)
| File | What It Does | Dengine Target Module |
|---|---|---|
| `simulation_server.cpp` (290KB!) | Monolithic game server | Break into `CoreModule` + era modules |
| `pheromone_map_cpp.cpp` | Ant-colony pheromone diffusion | `CoreModule` (flow/pathfinding) |
| `influence_map.cpp` | Tactical influence grid | `CoreModule` (spatial awareness) |
| `gpu_tactical_map.cpp` | GPU compute tactical overlay | `CoreModule` (GPU acceleration) |
| `radiance_cascades.cpp` | Radiance cascade GI | `CoreModule` (lighting) |
| `gpu_chunk_culler.cpp` | GPU frustum culling | `CoreModule` (rendering optimization) |
| `voxel_world.cpp` | Voxel terrain engine | `CoreModule` (terrain) |
| `voxel_mesher_blocky.cpp` | Greedy meshing | `CoreModule` (terrain) |
| `colony_ai_cpp.cpp` | Colony AI decision making | `EconomyModule` |
| `combat_los.cpp` | Line-of-sight raycasting | `CoreModule` (spatial queries) |
| `tactical_query.cpp` | Tactical spatial queries | `CoreModule` (spatial queries) |
| `theater_commander.cpp` | High-level AI commander | `AICommandModule` |
| `structural_integrity.cpp` | Building physics | `CoreModule` (voxel destruction) |
| `ecs/musket_systems.cpp` | Early Flecs ECS systems | Refactor into era modules |
| `ecs/musket_rendering.cpp` | ECS → MultiMesh bridge | `CoreModule` (rendering bridge) |

### `world/` — Godot Renderers
| File | What It Does |
|---|---|
| `voxel_world_renderer.gd` | Reads C++ voxel data → renders meshes |
| `effects.gd` | Visual effects system |
| `time_of_day.gd` | Day/night cycle |

### `ai/` — GDScript AI Layer
| Folder | What It Does |
|---|---|
| `colony/` | Economy AI, task allocators, build planners |
| `llm/` | LLM integration, prompt builders, sector commanders |

### `autoload/` — Godot Singletons
| File | What It Does |
|---|---|
| `doctrine_registry.gd` | Unit doctrine definitions |
| `scenario_state.gd` | Game state management |
| `sound_manager.gd` | Audio system |

## Rules
1. **NEVER** add these to the SConstruct build. They are reference only.
2. When porting math, follow §2.4 (Era-Agnostic Naming) — rename era-specific names.
3. When porting GDScript logic to C++, follow §2.1 (Architectural Boundary).
