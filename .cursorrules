# THE MUSKET ENGINE — AI AGENT RULES

> **READ BEFORE WRITING ANY CODE.** These rules apply to ALL AI assistants working in this repository.

## Mandatory Reading Order
1. `docs/GDD.md` — Full game design (especially §2: The 4 Unbreakable Rules)
2. `docs/CORE_MATH.md` — All C++ algorithms & shader code (DO NOT reinvent)
3. `docs/DEVELOPMENT_PLAN.md` — Phase plan, branch strategy, naming conventions
4. `STATE.md` — Current progress, known bugs, immediate next step

## The 4 Unbreakable Rules

1. **ARCHITECTURAL BOUNDARY**: ALL game logic lives in C++ via Flecs ECS. Godot GDScript is ONLY a renderer/UI client. NEVER write game state, health, movement math, or timers in `.gd` files.

2. **DATA-ORIENTED DESIGN ONLY**: No OOP class hierarchies for entities. No `CharacterBody3D` for agents. Components are POD structs (< 32 bytes). Systems iterate via `ecs.query().each()`.

3. **ANTI-HALLUCINATION**: We use Godot 4.x GDExtension (`ClassDB::bind_method`). We use Flecs C++17 API. If unsure about an API, search `cpp/godot-cpp/` headers. Do NOT guess.

4. **NO SCOPE CREEP**: Output complete, compilable functions. Do not refactor outside the immediate task. Do not implement future GDD features unprompted.

5. **DATA-DRIVEN MODDABILITY**: NEVER hardcode gameplay stats (speed, damage, reload time, production cost, stiffness) as magic numbers in C++ systems. ALL dynamic values must come from Flecs Components initialized via JSON files in `res://data/`. The engine must be blind to what a "French Infantry" is — it only reads data and applies physics.

6. **NETWORK SAFETY**: NEVER sync individual soldier/citizen Position/Velocity over the network. The Server owns the authoritative ECS. Server broadcasts macro-state (battalion anchors, death events, inventories) at 10Hz. Clients run local "Visual ECS" — spring-damper physics pull soldiers to server anchors. Player inputs route through Godot RPCs to the Server's C++ Input Queue.

7. **ERA-AGNOSTIC NAMING**: Do NOT name base C++ structs after 18th-century flavor. Use generic math names: `RangedVolleySystem` not `MusketSystem`, `KineticProjectileSystem` not `CannonballRicochet`. Era flavor comes from JSON data files and Godot shaders. Separate code using `flecs::module` — core logic in `CoreModule`, era-specific combat in isolated modules.

8. **RENDERING BRIDGE**: NEVER call `set_instance_transform()` in a loop. Pack all transforms into one `PackedFloat32Array` → one `RenderingServer::multimesh_set_buffer()` call per frame.

9. **ECS MUTATION RULE**: Do NOT `add<T>()`/`remove<T>()` for high-frequency states (Reloading, Suppressed). Use `state_flags` bitfield inside existing components. Only add/remove for rare permanent shifts (Death, Conscription).

10. **AIRGAP RULE**: Flecs systems NEVER `#include` Godot headers or call Godot API. Flecs threads + Godot SceneTree = Segfault. Godot reads ECS arrays once per frame on main thread.

11. **DOUBLE PRECISION**: Position/Velocity use `double` (64-bit). 32-bit float jitters at 4km+ from origin.

12. **ASYNC PATHFINDING**: Flow Field recalculations go to background thread. Agents use stale data for 2-3 frames. Main thread never drops frames.

13. **ZERO TECHNICAL DEBT**: Every line of code must be state-of-the-art and production-grade. No `// TODO: fix later`, no quick hacks, no "good enough for now" shortcuts. If a proper solution requires more time or external verification, take it.

## After Every Task
Update `STATE.md` with what was built, what changed, and any new bugs.

## Naming Conventions
- C++ components: `PascalCase` — C++ files: `snake_case.cpp`
- Godot scenes: `snake_case.tscn` — Godot nodes: `PascalCase`
- Git branches: `m{N}/{kebab-case}` — Commits: `type: lowercase imperative`
