// ═════════════════════════════════════════════════════════════
// MUSKET ENGINE: HEADLESS TEST HARNESS
// ═════════════════════════════════════════════════════════════
// RAII fixture for doctest. Each TEST_CASE_FIXTURE gets a
// pristine flecs::world with zeroed globals and registered
// systems. No Godot dependency.
// ═════════════════════════════════════════════════════════════
#pragma once
#include "doctest.h"
#include <cmath>
#include <cstring>

// Forward-declare the extern globals (defined in test_master.cpp)
extern MacroBattalion g_macro_battalions[MAX_BATTALIONS];
extern PendingOrder g_pending_orders[MAX_BATTALIONS];

// ── Minimal centroid pass (pure ECS, no Godot print) ────────
// This is the test-only version of compute_battalion_centroids.
// It replicates the production logic from world_manager.cpp
// without any UtilityFunctions::print calls.
static void test_compute_centroids(flecs::world &ecs) {
  float dt = ecs.get_info()->delta_time;

  // 1. Zero transients (Trap 23: preserve persistent fields)
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    g_macro_battalions[i].cx = 0.0f;
    g_macro_battalions[i].cz = 0.0f;
    g_macro_battalions[i].alive_count = 0;
    g_macro_battalions[i].team_id = 999;
    g_macro_battalions[i].flag_alive = false;
    g_macro_battalions[i].drummer_alive = false;
    g_macro_battalions[i].officer_alive = false;
  }

  // 2. Accumulate
  ecs.each([](flecs::entity e, const Position &p, const BattalionId &b,
              const TeamId &t) {
    if (!e.has<IsAlive>())
      return;
    uint32_t id = b.id % MAX_BATTALIONS;
    g_macro_battalions[id].cx += p.x;
    g_macro_battalions[id].cz += p.z;
    g_macro_battalions[id].alive_count++;
    g_macro_battalions[id].team_id = t.team;
    if (e.has<FormationAnchor>())
      g_macro_battalions[id].flag_alive = true;
    if (e.has<Drummer>())
      g_macro_battalions[id].drummer_alive = true;
    if (e.has<ElevatedLOS>())
      g_macro_battalions[id].officer_alive = true;
  });

  // 3. Finalize
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    auto &mb = g_macro_battalions[i];
    if (mb.alive_count > 0 && mb.alive_count < 10) {
      mb.flag_alive = mb.drummer_alive = mb.officer_alive = false;
    }
    if (mb.alive_count > 0) {
      mb.cx /= (float)mb.alive_count;
      mb.cz /= (float)mb.alive_count;

      if (mb.flag_alive) {
        mb.flag_cohesion = std::min(1.0f, mb.flag_cohesion + dt * 0.1f);
      } else {
        mb.flag_cohesion = std::max(0.2f, mb.flag_cohesion - dt * 0.05f);
      }

      if (!mb.officer_alive && mb.fire_discipline != DISCIPLINE_AT_WILL) {
        mb.fire_discipline = DISCIPLINE_AT_WILL;
      }

      if (mb.fire_discipline == DISCIPLINE_BY_RANK) {
        mb.volley_timer -= dt;
        if (mb.volley_timer <= 0.0f) {
          mb.active_firing_rank = (mb.active_firing_rank + 1) % 3;
          mb.volley_timer = 3.0f;
        }
      } else if (mb.fire_discipline == DISCIPLINE_MASS_VOLLEY) {
        mb.volley_timer -= dt;
        if (mb.volley_timer <= 0.0f) {
          mb.fire_discipline = DISCIPLINE_HOLD;
        }
      }

      // Hoisted targeting
      mb.target_bat_id = -1;
      float best_dist = 1e18f;
      for (int j = 0; j < MAX_BATTALIONS; j++) {
        auto &enemy = g_macro_battalions[j];
        if (enemy.alive_count == 0 || enemy.team_id == mb.team_id)
          continue;
        float edx = enemy.cx - mb.cx;
        float edz = enemy.cz - mb.cz;
        float ed2 = edx * edx + edz * edz;
        if (!false && ed2 < best_dist) { // No OBB check in test (simplified)
          best_dist = ed2;
          mb.target_bat_id = j;
        }
      }
    }

    // Order pipeline
    auto &order = g_pending_orders[i];
    if (order.type != ORDER_NONE) {
      order.delay -= dt;
      if (order.delay <= 0.0f) {
        if (order.type == ORDER_DISCIPLINE) {
          mb.fire_discipline = (FireDiscipline)order.requested_discipline;
          if (mb.fire_discipline == DISCIPLINE_BY_RANK) {
            mb.active_firing_rank = 0;
            mb.volley_timer = 3.0f;
          } else if (mb.fire_discipline == DISCIPLINE_MASS_VOLLEY) {
            mb.volley_timer = 0.5f;
          }
        }
        order.type = ORDER_NONE;
      }
    }
  }
}

// ── RAII Test Fixture ───────────────────────────────────────
struct EngineTestHarness {
  flecs::world ecs;

  EngineTestHarness() {
    // 1. NUKE all global state
    std::memset(g_macro_battalions, 0, sizeof(g_macro_battalions));
    std::memset(g_pending_orders, 0, sizeof(g_pending_orders));

    // 2. Restore persistent invariants
    for (int i = 0; i < MAX_BATTALIONS; ++i) {
      g_macro_battalions[i].flag_cohesion = 1.0f;
      g_macro_battalions[i].fire_discipline = DISCIPLINE_AT_WILL;
      g_macro_battalions[i].target_bat_id = -1;
      g_macro_battalions[i].dir_z = -1.0f;
    }

    // 3. Register all ECS systems
    musket::register_movement_systems(ecs);
    musket::register_combat_systems(ecs);
    musket::register_panic_systems(ecs);

    // 4. Initialize singletons that systems depend on
    PanicGrid pg = {};
    std::memset(&pg, 0, sizeof(pg));
    ecs.set<PanicGrid>(pg);
  }

  // Deterministic frame stepping
  void step(int frames = 1, float dt = 1.0f / 60.0f) {
    for (int i = 0; i < frames; i++) {
      test_compute_centroids(ecs);
      ecs.progress(dt);
    }
  }

  // Quick soldier spawner
  flecs::entity spawn_soldier(uint32_t bat_id, float x, float z,
                              uint8_t team = 255) {
    if (team == 255)
      team = (uint8_t)(bat_id % 2);
    return ecs.entity()
        .set<Position>({x, z})
        .set<Velocity>({0.0f, 0.0f})
        .set<BattalionId>({bat_id})
        .set<TeamId>({team})
        .set<MovementStats>({4.0f, 8.0f})
        .set<FormationDefense>({0.2f})
        .add<IsAlive>();
  }

  // Soldier with full formation target (for combat tests)
  flecs::entity spawn_armed_soldier(uint32_t bat_id, float x, float z,
                                    uint8_t rank = 0) {
    uint8_t team = (uint8_t)(bat_id % 2);
    return ecs.entity()
        .set<Position>({x, z})
        .set<Velocity>({0.0f, 0.0f})
        .set<SoldierFormationTarget>(
            {(double)x, (double)z, 50.0f, 2.0f, 0.0f, -1.0f, true, rank, {}})
        .set<MusketState>({0.0f, 60, 0}) // Loaded, 60 ammo
        .set<BattalionId>({bat_id})
        .set<TeamId>({team})
        .set<MovementStats>({4.0f, 8.0f})
        .set<FormationDefense>({0.2f})
        .add<IsAlive>();
  }
};
