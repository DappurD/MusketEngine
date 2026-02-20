# STATE LEDGER

> This file is the AI agent's external memory. Before starting any new task, **read this file** and `docs/GDD.md`. After completing a task, **update this file**.

## Current Phase
**Phase 1: Combat Prototype (Marketing Demo)** — Not started. GDD under review.

## Project Identity
- **Game**: The Musket Engine — Neuro-Symbolic Napoleonic War & Economy Simulator
- **Engine**: Godot 4.6 + GDExtension (`musket_engine.dll`) + Flecs ECS (C++)
- **Architecture**: Data-Oriented Design (DOD). C++ = Brain. Godot = Eyes. Never mix.
- **Renderer**: Vulkan / Forward+. If Vulkan crashes, **we broke something — debug, don't switch.**

## What Is Built & Compiling
| Component | Status | File(s) |
|---|---|---|
| Project structure | ✅ Ready | `project.godot`, `musket_engine.gdextension` |
| GDExtension boilerplate | ✅ Written | `cpp/src/register_types.cpp/.h` |
| ECS components | ✅ Written | `cpp/src/ecs/musket_components.h` |
| SCons build | ✅ Written | `cpp/SConstruct` |
| godot-cpp | ❌ Not cloned | Needs `git submodule add` |
| Flecs | ❌ Not cloned | Needs single-header in `cpp/flecs/` |
| First compilation | ❌ Not attempted | |

## What Is NOT Built Yet
- Everything in Phases 1-6 of the GDD
- No scenes, no shaders, no GDScript yet

## C++ ↔ Godot Bridge
- GDExtension: `musket_engine.gdextension`
- Entry symbol: `musket_engine_init`
- Build: `scons` in `cpp/` directory
- Output: `bin/musket_engine.windows.template_debug.x86_64.dll`

## Immediate Next Step
1. Clone `godot-cpp` as git submodule in `cpp/godot-cpp/`
2. Download Flecs single-header into `cpp/flecs/`
3. Run `scons` — verify clean compilation
4. Create minimal test scene to verify Godot loads the extension
