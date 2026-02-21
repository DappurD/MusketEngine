// ═════════════════════════════════════════════════════════════
// MUSKET ENGINE: TEST UNITY BUILD
// ═════════════════════════════════════════════════════════════
// Headless test binary. No Godot dependency.
// Build: scons test=yes
// Run:   bin\musket_tests.exe
// ═════════════════════════════════════════════════════════════

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// ── Flecs ───────────────────────────────────────────────────
#include "../flecs/flecs.h"

// ── Pure C++ engine code (Godot-free) ───────────────────────
#include "../src/ecs/musket_components.h"
#include "../src/ecs/musket_systems.h"

// Define the globals that normally live in world_manager.cpp
MacroBattalion g_macro_battalions[MAX_BATTALIONS];
PendingOrder g_pending_orders[MAX_BATTALIONS];

// Include the systems implementation (Godot-free)
#include "../src/ecs/musket_systems.cpp"

// ── Test Infrastructure ─────────────────────────────────────
#include "test_harness.h"

// ── Test Suites (domain-based) ──────────────────────────────
#include "test_combat.cpp"
#include "test_invariants.cpp"
#include "test_perf.cpp"

