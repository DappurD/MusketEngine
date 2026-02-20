# STATE LEDGER

> This file is the AI agent's external memory. Before starting any new task, **read this file** and `docs/GDD.md`. After completing a task, **update this file**.

## Current Phase
**Phase 1: Combat Prototype (Marketing Demo)** — Dependencies set up, first compile successful.

## Project Identity
- **Engine**: Dengine — Dennis's Era-Agnostic Crowd & Logistics Engine (C++ Flecs ECS)
- **Game**: The Musket Engine — Neuro-Symbolic Napoleonic War & Economy Simulator (first game on Dengine)
- **Runtime**: Godot 4.6 + GDExtension (`musket_engine.dll`) + Flecs ECS
- **Architecture**: Data-Oriented Design (DOD). C++ = Brain. Godot = Eyes. Never mix.
- **Renderer**: **Vulkan / Forward+**. If Vulkan crashes, we broke something — debug, don't switch.

## What Is Built & Compiling
| Component | Status | File(s) |
|---|---|---|
| Project structure | ✅ Ready | `project.godot`, `musket_engine.gdextension` |
| GDExtension boilerplate | ✅ Compiling | `cpp/src/register_types.cpp/.h` |
| ECS components (all POD) | ✅ Written | `cpp/src/ecs/musket_components.h` |
| SCons build | ✅ Working | `cpp/SConstruct` |
| godot-cpp | ✅ Submodule @ `godot-4.5-stable` | `cpp/godot-cpp/` |
| Flecs | ✅ Vendored (header-only) | `cpp/flecs/flecs.h`, `cpp/flecs/flecs.c` |
| musket_engine.dll | ✅ Compiled (620KB) | `bin/musket_engine.dll` |

## What Is NOT Built Yet
- No scenes, shaders, or GDScript yet
- No Flecs world instantiation (world_manager.cpp)
- Everything in Phases 1-6 of the GDD

## Known Issues
- `flecs_STATIC` macro redefinition warning (harmless, defined in both SConstruct and flecs.h)
- godot-cpp has no 4.6 tag yet — using 4.5-stable (GDExtension API is backwards compatible)

## C++ ↔ Godot Bridge
- GDExtension: `musket_engine.gdextension`
- Entry symbol: `musket_engine_init`
- Build: `python -m SCons platform=windows target=template_debug` in `cpp/`
- Output: `bin/musket_engine.dll`

## Immediate Next Step
1. Open project in Godot editor — verify Vulkan loads and extension registers
2. Create minimal test scene (`res://scenes/test_bed.tscn`)
3. Write `world_manager.cpp` — instantiate `flecs::world`, register components
4. Begin M1: Battalion Movement Orders
