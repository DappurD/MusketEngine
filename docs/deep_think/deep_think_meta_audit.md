# Deep Think Prompt #0: Meta-Audit (Architecture Laws)

> **YOU MUST READ THIS PROMPT BEFORE ALL OTHERS.** It establishes the laws that govern every design decision. Any response that violates these laws is REJECTED.

---

## Ground Truth: What Already Exists

Before designing ANYTHING, you must understand what's already built and compiling:

### Built & Tested (M0.5 — M7.5)

| Milestone | What It Does | Key Files |
|-----------|-------------|-----------|
| M0.5 | JSON prefab loader | `prefab_loader.h/.cpp`, `res/data/*.json` |
| M1 | Flecs world, spawn, sync | `world_manager.h/.cpp`, `rendering_bridge.h/.cpp` |
| M2 | Spring-damper formation physics | `musket_systems.cpp` (`SpringDamperPhysics`) |
| M3 | Musket fire, reload, smoke FX | `musket_systems.cpp` (`VolleyFireSystem`) |
| M4 | Panic CA grid, routing, morale | `musket_systems.cpp` (`PanicDiffusionSystem`, `RoutingBehaviorSystem`) |
| M5 | Artillery ballistics, canister, ricochet | `musket_systems.cpp` (`ArtilleryKinematicsSystem`) |
| M6 | Cavalry charge, momentum, melee | `musket_systems.cpp` (`CavalryBallistics`), `world_manager.cpp` |
| M7 | Command network (officer/drummer/flag) | `world_manager.cpp` (command staff, order delay pipeline) |
| M7.5 | Fire discipline, dynamic formations | `musket_systems.cpp` (`DistributedDrummerAura`), `world_manager.cpp` (OBB targeting) |

### Current Component Inventory (`musket_components.h`, 300 lines)

```
Spatial:      Position(8B), Velocity(8B), Height(4B)
Formation:    SoldierFormationTarget(64B, alignas(64)), FormationShape enum, FireDiscipline enum
Stats:        MovementStats(8B)
Combat State: TeamId(1B), BattalionId(4B), IsAlive(tag), Routing(tag), MusketState(6B)
Orders:       MovementOrder(12B), HaltOrder(tag), FireOrder(8B)
Artillery:    ArtilleryShot(28B), ArtilleryBattery(28B), ArtilleryAmmoType enum
Cavalry:      CavalryState(24B), FormationDefense(4B), ChargeOrder(tag), Disordered(tag)
Command:      MacroBattalion(128B), Drummer(tag), FormationAnchor(tag), PendingOrder struct
Panic:        PanicGrid singleton (64×64 CA, 2 team layers, double-buffered)
Render:       RenderSlot(8B)
```

### Architecture Documents

| Document | What It Contains | Must-Read Sections |
|----------|-----------------|-------------------|
| `docs/GDD.md` | Full technical spec (639 lines) | §2 Directives, §4.1 Neuro-Symbolic Stack, §6 LLM General, §7 City Builder, §12 Modding |
| `docs/CORE_MATH.md` | Exact C++ algorithms (773 lines) | §1-4 (combat), §5-6 (urbanism), §7-8 (economy, rendering) |
| `STATE.md` | What's built, issues, next steps | "Implementation Law" section, "Known Issues", milestone map |
| `docs/LEGACY_MAP.md` | Legacy code → milestone mapping | Voxel porting strategy, per-milestone reference files |

---

## Law 1: The Implementation Law (from STATE.md)

Every system implementation MUST follow this process. No exceptions.

| Step | Action |
|------|--------|
| 1. **Read** | Pull the exact spec from `GDD.md` + `CORE_MATH.md` |
| 2. **Verify** | Validate the algorithm works (convergence, reasonable constants, edge cases) |
| 3. **Flag** | If the spec is wrong or underspecified → **stop and ask** before writing code. Update the GDD first |
| 4. **Implement** | Translate the verified spec directly into code |
| 5. **Never invent** | If behavior isn't in the GDD, it doesn't exist. Don't improvise — extend the GDD first |

---

## Law 2: The 4 Unbreakable Rules (from GDD §2.1)

1. **ARCHITECTURAL BOUNDARY**: ALL game logic in C++ via Flecs ECS. Godot = visual/audio client only
2. **DOD ONLY**: No OOP for entities. Components MUST be POD structs (≤32B ideal). Systems iterate via `ecs.query().each()`
3. **ANTI-HALLUCINATION**: Use Godot 4.x GDExtension (`ClassDB::bind_method`) and Flecs C++11/17 API. Search local headers, don't guess
4. **ERA-AGNOSTIC ENGINE**: C++ structs must NOT be named after 18th-century flavor. Bad: `MusketSystem`. Good: `RangedVolleySystem`

---

## Law 3: The 5 Safety Laws (from GDD §2.5)

1. **RENDERING BRIDGE**: Pack ALL transforms into single `PackedFloat32Array` → one `multimesh_set_buffer()` call per frame
2. **ECS MUTATION RULE**: Use `state_flags` bitfield for high-frequency combat states. Only `add/remove` for rare permanent shifts (Conscription, Death)
3. **THE AIRGAP**: Flecs systems NEVER `#include` Godot headers. Flecs modifies C++ memory; Godot reads ECS arrays once per frame on main thread
4. **DOUBLE PRECISION**: Global `Position`/`Velocity` use `double` for world coordinates (float loses precision at 4km+)
5. **ASYNC PATHFINDING**: Grid recalculations on background thread. Agents use stale data for 2-3 frames. Main thread never drops a frame

---

## Law 4: Design Full Structs, Implement Systems Incrementally

When designing a NEW component struct:
- Design it for the **full vision** from day one (all fields it will EVER need)
- But implement SYSTEMS that read those fields one at a time, milestone by milestone
- **Why**: Resizing a struct after 10K entities exist = archetype migration = cache destruction = performance cliff

Example: `SoldierFormationTarget` was designed at 64B with `can_shoot`, `rank_index`, `face_dir` fields from the start, even though M2 only used `target_x/z` and `base_stiffness`. M7.5 later activated the remaining fields without any struct change.

---

## Law 5: Explicit System Boundaries

Every Flecs system declaration must specify:
- **Reads**: which components it reads (use `const T*`)
- **Writes**: which components it mutates (use `T*`)
- **Does NOT touch**: explicitly exclude unrelated data

Systems must be composable. No system may reach into another system's private data. Communication happens ONLY through shared component writes.

---

## Law 6: Frame Budget Allocation

Target: **100,000 agents at 60 FPS** (16.6ms total frame budget)

| Category | Budget | Current Systems |
|----------|--------|----------------|
| Physics (spring-damper, movement) | 3.0ms | SpringDamperPhysics, RoutingBehavior |
| Combat (volley, artillery, cavalry) | 3.0ms | VolleyFireSystem, ArtilleryKinematics, CavalryBallistics |
| AI (targeting, decisions) | 2.0ms | Battalion centroid targeting (O(B²)), LLM directive application |
| Panic CA (5Hz, amortized) | 0.5ms | PanicDiffusionSystem |
| Economy (1Hz matchmaker) | 1.0ms | NOT BUILT — M9 |
| Rendering bridge | 2.0ms | sync_battalion_transforms, sync_projectiles |
| Headroom | 5.1ms | Reserved for spikes |

**Known bottleneck** (from STATE.md): `VolleyFireSystem` is O(N²) — 4.4s/tick at 10K entities. Spatial hash (M8) is prerequisite for everything.

---

## Law 7: Modding Must Be Possible (from GDD §2.3 + §12)

4 modding pillars — every design must comply:

1. **JSON Prefabs**: All stats in `res://data/*.json`. No hardcoded gameplay values in C++
2. **LLM Personality API**: AI general = external `.txt` prompt in `res://data/ai_prompts/`
3. **Visual Overhauls**: Godot `.pck` loading for meshes, shaders, audio
4. **GDScript Event Bus**: C++ emits signals. Modders write 1Hz GDScript listeners. Modders own narrative; C++ owns physics

---

## Law 8: Legacy Code Protocol (from LEGACY_MAP.md)

Before implementing any milestone:
1. Check `LEGACY_MAP.md` for reference code from the legacy prototype
2. **Steal the math, rewrite the plumbing** — algorithms are solid, architecture must be DOD/ECS
3. **NEVER compile legacy files directly** — they're in `legacy_assets/` for reference only
4. Legacy LLM system (`llm_sector_commander.gd`, `llm_theater_advisor.gd`, `colony_ai_cpp.h/.cpp`) is the most complete reference — port architecture, not code

---

## Contract For All Subsequent Prompts

Every Deep Think response MUST include:

1. **§ Architecture Anchor**: What GDD section, CORE_MATH section, and existing code this builds on
2. **§ New Components**: Full POD struct designs with byte sizes and `alignas`. These structs must handle the FULL VISION, not just the immediate milestone
3. **§ New Systems**: Flecs system signatures with explicit Read/Write/Exclude declarations
4. **§ Legacy Port Notes**: What legacy code to reference, what math to steal, what to rewrite
5. **§ Integration Points**: How this connects to existing systems (which components does it read/write that other systems also read/write?)
6. **§ Performance**: Estimated time budget, scaling characteristics, known bottlenecks
7. **§ Modding Surface**: What's exposed to JSON prefabs, what's hardcoded
8. **⚠️ Traps**: Potential pitfalls (performance cliffs, coupling risks, scope creep, precision errors)
