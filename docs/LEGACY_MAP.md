# Legacy Asset → Milestone Cross-Reference

> **AI DIRECTIVE**: Before implementing any milestone, check this file for reference code from the prototype.
> The math is solid; the previous implementation had integration bugs (units fighting each other). **Treat as reference, not gospel.**
> All files are in `legacy_assets/` — **NEVER compile them directly**.

---

## ⚠️ Voxel Engine Porting Strategy

> **Decision (2026-02-20)**: The legacy voxel engine (`voxel_world.cpp`, 1494 lines) is **reference-only**. Do NOT port it wholesale. The math is solid; the architecture must be rewritten to comply with DOD/ECS/Airgap rules.

### Phased Approach

| Phase | Milestone | What to Port | What to Rewrite |
|---|---|---|---|
| **Beachhead** | M5 (Artillery) | DDA raycast math (`raycast()` L481–597, `raycast_multi()` L599–709, `check_los()` L731–739) | Wrap in clean Flecs system, no Godot includes |
| **Full Engine** | M15–M18 (Siege) | Destruction math (`destroy_sphere()` L174–401, `destroy_box()` L410–475), structural integrity (flood-fill, island detection) | Flat voxel arrays as ECS resources, async mesh rebuild via background thread |
| **Rendering** | M19–M22 (Polish) | Greedy mesher algorithm from `voxel_mesher_blocky.cpp` | Godot-side only: reads C++ arrays, builds `ArrayMesh` on main thread |

### Key Legacy Voxel Files (Reference Only)

| File | Lines | Contains |
|---|---|---|
| `cpp_src/voxel_world.cpp` | 1,494 | Full terrain engine: get/set, coordinate conversion, DDA raycast, destruction |
| `cpp_src/voxel_world.h` | — | Header with chunk/world dimensions, material enums |
| `cpp_src/voxel_chunk.h` | — | Chunk data structure |
| `cpp_src/voxel_mesher_blocky.cpp/.h` | — | Greedy meshing algorithm |
| `cpp_src/voxel_generator.cpp/.h` | — | Procedural terrain generation |
| `cpp_src/voxel_lod.cpp/.h` | — | LOD system for distant chunks |
| `cpp_src/voxel_materials.h` | — | Material/block type definitions |
| `cpp_src/voxel_post_effects.cpp/.h` | — | Post-processing effects |
| `cpp_src/structural_integrity.cpp` | 576 | Building collapse: connectivity, flood-fill islands, ground-distance BFS |
| `cpp_src/register_voxel_core.cpp` | — | Old GDExtension registration (DO NOT reuse directly) |
| `world/voxel_world_renderer.gd` | — | GDScript: reads C++ data → Godot meshes |
| `scenes/voxel_test.tscn` | — | Working test scene (camera + debug overlays) |
| `ui/ui/voxel_debug_hud.gd` | — | Debug HUD for voxel inspection |
| `ui/ui/voxel_debug_overlay.gd` | — | Debug overlay visualization |

### Rules When Porting

1. **Steal the math, rewrite the plumbing.** Copy DDA stepping logic verbatim. Rewrite everything around it.
2. **Voxel grid = Flecs singleton resource**, not a Godot Node. `ecs.set<VoxelGrid>({...})`.
3. **Destruction → async.** `destroy_sphere()` queues a rebuild job on a background thread. Agents use stale flow fields for 2–3 frames (per GDD §2.5 Rule 5).
4. **Meshing stays on main thread** (Godot rendering is single-threaded). C++ marks dirty chunks; Godot-side reads and rebuilds `ArrayMesh`.

---

## M1: ECS Foundation (World Manager, Capsule Spawning)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `cpp_src/ecs/musket_rendering.cpp` | **MultiMesh bridge** — packs ECS transforms into `PackedFloat32Array` for `multimesh_set_buffer()`. 16 floats per instance (12 transform + 4 custom_data). Velocity→facing math. | Full file (91 lines) |
| `cpp_src/ecs/musket_rendering.h` | Header for `sync_muskets_to_godot()` | Full file (16 lines) |
| `cpp_src/ecs/musket_systems.cpp` | `register_musket_systems()` — registers spring-damper, artillery traversal, and panic observer with `flecs::world` | Full file (92 lines) |
| `cpp_src/ecs/components.h` | Legacy component definitions (compare with current `musket_components.h`) | Check for diffs |

---

## M2: Battalion Movement (Spring-Damper, Formations)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `cpp_src/ecs/musket_systems.cpp` | `MusketSpringDamperPhysics` system — exact Flecs `.iter()` pattern with stiffness/damping/speed clamp | L18–L53 |
| `cpp_src/pheromone_map_cpp.cpp` | `gradient()` and `gradient_raw()` — flow field direction sampling for pathfinding | L311–L340 |

---

## M3: Volley Combat (Inverse Sieve)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `cpp_src/combat_los.cpp` | `check_visibility()` — vision cone math, DDA voxel LOS, physics raycast fallback | L15–L125 |
| `cpp_src/combat_los.cpp` | `check_friendly_fire()` — aim corridor projection, perpendicular distance, spread factor | L127–L161 |
| `cpp_src/combat_los.cpp` | `batch_check_visibility()` — batched LOS for performance | L163–L191 |

---

## M4: Panic & Morale (Cellular Automata)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `cpp_src/pheromone_map_cpp.cpp` | **Full CA grid implementation**: init, double-buffer tick, Von Neumann diffusion, evaporation, chunk-based skip. Almost 1:1 with our Panic Grid design. | L76–L409 |
| `cpp_src/pheromone_map_cpp.cpp` | `deposit_radius()` — inject panic at kill site in a radius | L165–L192 |
| `cpp_src/pheromone_map_cpp.cpp` | `deposit_cone()` — cone-shaped deposition (smoke from muzzle) | L194–L241 |
| `cpp_src/pheromone_map_cpp.cpp` | GPU acceleration via compute shaders (`setup_gpu`, `tick_gpu`) | L481+ |
| `cpp_src/ecs/musket_systems.cpp` | `OnSlaughter_InjectTerror` observer — `flecs::OnRemove` + `with<IsAlive>()` pattern | L78–L88 |

---

## M5: Artillery (DDA Ballistics)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `cpp_src/ecs/musket_systems.cpp` | `MusketArtilleryTraversal` — gravity integration, kinematics update | L57–L72 |
| `cpp_src/voxel_world.cpp` | `raycast()` — full 3D DDA (Digital Differential Analyzer) through voxel grid | L481–L597 |
| `cpp_src/voxel_world.cpp` | `raycast_multi()` — multi-hit DDA for cannonball penetration | L599–L709 |
| `cpp_src/voxel_world.cpp` | `check_los()` — fast boolean LOS check | L731–L739 |

---

## M7: Command Network

| Legacy File | What to Mine | Lines |
|---|---|---|
| `cpp_src/influence_map.cpp` | Sector grid with threat/opportunity layers — maps to Aide-de-Camp sector overlay | Full file (268 lines) |

---

## M8–M10: LLM General (State Compressor, Integration, Couriers)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `ai/ai/llm/llm_prompt_builder.gd` | **Battlefield → YAML SitRep** formatter. Maps directly to our State Compressor design. | Full file |
| `ai/ai/llm/llm_config.gd` | Multi-provider config (Anthropic, OpenAI, Ollama, LM Studio). Env var detection. | Full file |
| `ai/ai/llm/llm_theater_advisor.gd` | HTTP client, JSON order parsing, response → weight modifiers | Full file |
| `ai/ai/llm/llm_sector_commander.gd` | Per-sector LLM command decisions | Full file |
| `ai/ai/llm/battle_memory.gd` | Persistent battle context for LLM feedback loop | Full file |
| `ai/ai/llm/sector_grid.gd` | Sector coordinate system (A1–F6 with terrain tags) | Full file |
| `ai/ai/llm/README.md` | Full LLM integration guide: providers, dual-mode, env vars, troubleshooting | Full file |
| `cpp_src/theater_commander.cpp` | **Utility AI** battle commander: response curves (logistic, gaussian, quadratic), sensor computation, battlefield snapshot, axis configs | Full file (855 lines) |
| `cpp_src/influence_map.cpp` | Threat/opportunity grid feeding into LLM SitRep | Full file (268 lines) |

---

## M11–M14: Economy (Civilians, Production, Conscription)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `ai/ai/colony/ARCHITECTURE.md` | **Economy AI architecture doc**: Resource flow model, task lifecycle, decision layers, extraction guide, performance targets, interfaces | Full file (382 lines) |
| `ai/ai/colony/core/economy_state.gd` | Per-team stockpile tracking, production rates | Full file |
| `ai/ai/colony/core/task_allocator.gd` | Greedy distance-weighted worker→task assignment | Full file |
| `ai/ai/colony/core/build_planner.gd` | Multi-axis site scoring for construction | Full file |
| `ai/ai/colony/colony_ai.gd` | High-level economy AI orchestrator | Full file |
| `ai/ai/colony/economy_constants.gd` | Resource type enums, production chain constants | Full file |
| `ai/ai/colony/integration/sim_bridge.gd` | Adapter: Economy AI → SimulationServer unit queries | Full file |
| `ai/ai/colony/integration/voxel_resource_scanner.gd` | Scan voxel world for ore/resource nodes | Full file |
| `ai/ai/colony/integration/world_adapter.gd` | Spatial/terrain queries (distance, pathability, height) | Full file |
| `cpp_src/colony_ai_cpp.cpp` | C++ colony AI decision-making backend | Full file |
| `cpp_src/economy_state.cpp` | C++ resource tracking (thread-safe, atomic ops) | Full file |

---

## M15–M18: Urbanism & Siege (Vauban, Destruction)

| Legacy File | What to Mine | Lines |
|---|---|---|
| `cpp_src/voxel_world.cpp` | **Full voxel terrain engine**: setup, get/set voxels, coordinate conversion, DDA raycast | Full file (1494 lines) |
| `cpp_src/voxel_world.cpp` | `destroy_sphere()` / `destroy_sphere_ex()` — artillery crater destruction with debris generation | L174–L401 |
| `cpp_src/voxel_world.cpp` | `destroy_box()` — rectangular breach destruction | L410–L475 |
| `cpp_src/structural_integrity.cpp` | **Building collapse physics**: chunk-level connectivity, flood-fill island detection, island meshing, ground-distance BFS for weakened voxels | Full file (576 lines) |
| `cpp_src/voxel_mesher_blocky.cpp` | Greedy meshing algorithm for voxel terrain | Full file |

---

## M19–M22: Weather, Night, Audio, Cartographer

| Legacy File | What to Mine | Lines |
|---|---|---|
| `world/time_of_day.gd` | Day/night cycle GDScript (LOS scaling, lighting) | Full file |
| `world/effects.gd` | Visual effects system (smoke, explosions) | Full file |
| `world/voxel_world_renderer.gd` | Reads C++ voxel data → Godot mesh rendering | Full file |

---

## General Reference (All Milestones)

| Legacy File | What to Mine |
|---|---|
| `scenes/voxel_test.tscn` | Working test scene with camera, simulation, debug overlays |
| `scenes/main_menu.gd/.tscn` | Menu system |
| `autoload/doctrine_registry.gd` | Unit doctrine definitions (formation types, behavior presets) |
| `autoload/scenario_state.gd` | Game state management singleton |
| `autoload/sound_manager.gd` | Audio system |
| `autoload/asset_manager.gd` | Asset loading |
| `autoload/content_pack_manager.gd` | Mod/content pack loading system |
| `ui/ui/ai_debug_overlay.gd` | AI debug visualization |
| `ui/ui/pheromone_debug_hud.gd` | CA grid debug HUD |
| `ui/ui/voxel_debug_hud.gd` | Voxel debug HUD |
| `cpp_src/gpu_tactical_map.cpp` | GPU compute tactical overlay |
| `cpp_src/gpu_chunk_culler.cpp` | GPU frustum culling |
| `cpp_src/radiance_cascades.cpp` | Radiance cascade GI |
| `cpp_src/tactical_query.cpp` | Tactical spatial queries |
