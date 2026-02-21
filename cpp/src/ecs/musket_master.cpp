// ═════════════════════════════════════════════════════════════════════════════
// THE MUSKET ENGINE: UNITY BUILD
// ═════════════════════════════════════════════════════════════════════════════
// AI DIRECTIVE: Do not compile individual ECS .cpp files in SCons.
// Compile ONLY this file. This merges all Translation Units (TUs) into one,
// mathematically eradicating the MSVC static template component ID mismatch.
// w.each<T>() and ecs.each<T>() are now 100% safe in any included file.
//
// THE SCIENCE: Flecs caches component IDs in hidden static inline template
// variables (flecs::type_id<T>::id). MSVC instantiates these PER Translation
// Unit in a DLL. A Unity Build has exactly ONE TU, so IDs are unified.
//
// ORDER MATTERS: Core managers first, then systems that depend on them.
// ═════════════════════════════════════════════════════════════════════════════

// 1. Core (component registration, spawn, process loop)
#include "world_manager.cpp"

// 2. Rendering Bridge (shadow buffers, transform packing)
#include "rendering_bridge.cpp"

// 3. Data (JSON prefab loader)
#include "prefab_loader.cpp"

// 4. Systems (all gameplay systems)
#include "musket_systems.cpp"

// ─────────────────────────────────────────────────────────────────────────────
// Future Milestones — include here as you build them:
// #include "combat/siege_systems.cpp"
// #include "economy/logistics.cpp"
// #include "ai/battle_commander.cpp"
// ─────────────────────────────────────────────────────────────────────────────
