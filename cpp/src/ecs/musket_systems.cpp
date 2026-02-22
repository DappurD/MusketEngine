#include "musket_systems.h"
#include "musket_components.h"
#include <cmath>
#include <cstring>
#include <vector>

// NOTE: g_macro_battalions is defined in world_manager.cpp (the golden TU).
// ecs.each<> template statics must share the TU where components are
// registered.

namespace musket {

void register_movement_systems(flecs::world &ecs) {

  // ═════════════════════════════════════════════════════════════
  // SYSTEM 1: Spring-Damper Formation Physics (CORE_MATH.md §1)
  //
  // DeepThink-verified math. Soldiers are physics particles
  // attached to formation slots via critically-damped springs.
  // O(1) per entity at 60Hz.
  //
  // NOTE: CORE_MATH.md shows Flecs v3 .iter() syntax.
  //       Flecs v4.1.4 system_builder only has .each().
  //       Math is identical; only the lambda signature changed.
  // ═════════════════════════════════════════════════════════════
  ecs.system<Position, Velocity, const SoldierFormationTarget,
             const BattalionId>("SpringDamperPhysics")
      .with<IsAlive>()
      .each([](flecs::entity e, Position &p, Velocity &v,
               const SoldierFormationTarget &target, const BattalionId &bat) {
        if (e.has<CavalryState>() && e.get<CavalryState>().state_flags != 0)
          return;

        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;

        float dx = (float)target.target_x - p.x;
        float dz = (float)target.target_z - p.z;

        // M7 Phase A: Apply flag cohesion decay (Trap 21: floor at 0.2)
        uint32_t bat_id = bat.id % MAX_BATTALIONS;
        float stiffness =
            target.base_stiffness * g_macro_battalions[bat_id].flag_cohesion;
        if (e.has<Routing>())
          stiffness = 0.0f; // Routing orthogonal override
        float damping = target.damping_multiplier * std::sqrt(stiffness);

        // Trap 19 Fix: Exponential decay damping (unconditionally stable)
        v.vx += (stiffness * dx) * dt;
        v.vz += (stiffness * dz) * dt;
        float decay = std::exp(-damping * dt);
        v.vx *= decay;
        v.vz *= decay;

        // Speed clamp — prevents supersonic rubber-banding
        constexpr float MAX_SPEED = 4.0f; // m/s (infantry)
        float speed_sq = (v.vx * v.vx) + (v.vz * v.vz);
        if (speed_sq > (MAX_SPEED * MAX_SPEED)) {
          float inv_speed = 1.0f / std::sqrt(speed_sq);
          v.vx = v.vx * inv_speed * MAX_SPEED;
          v.vz = v.vz * inv_speed * MAX_SPEED;
        }

        p.x += v.vx * dt;
        p.z += v.vz * dt;
      });

  // ═════════════════════════════════════════════════════════════
  // SYSTEM 2: Formation March Order
  //
  // When a soldier has a MovementOrder, slide their formation
  // slot target toward the order destination each frame.
  // Once the slot arrives, mark the order as complete.
  // ═════════════════════════════════════════════════════════════
  ecs.system<SoldierFormationTarget, MovementOrder>("FormationOrderMove")
      .with<IsAlive>()
      .each([](flecs::entity e, SoldierFormationTarget &target,
               MovementOrder &order) {
        if (order.arrived)
          return;

        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;

        constexpr float MARCH_SPEED = 3.0f;  // m/s (march pace)
        constexpr float ARRIVAL_DIST = 1.0f; // Close enough

        // M7 Phase B: Drummer speed buff (+10%)
        uint32_t bat_id = 0;
        if (e.has<BattalionId>()) {
          bat_id = e.get<BattalionId>().id % MAX_BATTALIONS;
        }
        float speed_mult =
            g_macro_battalions[bat_id].drummer_alive ? 1.10f : 1.0f;

        // Direction from current slot to order destination
        float dx = order.target_x - target.target_x;
        float dz = order.target_z - target.target_z;
        float dist_sq = dx * dx + dz * dz;

        if (dist_sq < ARRIVAL_DIST * ARRIVAL_DIST) {
          order.arrived = true;
          return;
        }

        float dist = std::sqrt(dist_sq);
        float step = MARCH_SPEED * speed_mult * dt;
        if (step > dist)
          step = dist; // Don't overshoot

        float inv_dist = 1.0f / dist;
        target.target_x += dx * inv_dist * step;
        target.target_z += dz * inv_dist * step;
      });
}

// ═════════════════════════════════════════════════════════════
// M3+M8: COMBAT SYSTEMS (Spatial Hash + Volley Fire)
// ═════════════════════════════════════════════════════════════

void register_combat_systems(flecs::world &ecs) {

  // ── M8 System: Spatial Grid Rebuild (PreUpdate) ──────────────
  // Rebuilds the flat-array spatial hash from scratch every frame.
  // Uses .each() (Flecs API). Frame boundary detected via frame_id.
  // Cost: ~0.8ms at 100K entities (memset + linear insert).
  ecs.system<const Position, const BattalionId, const TeamId>(
         "SpatialGridRebuild")
      .kind(flecs::PreUpdate)
      .with<IsAlive>()
      .without<MacroSimulated>()
      .each([](flecs::entity e, const Position &p, const BattalionId &b,
               const TeamId &t) {
        SpatialHashGrid &grid = e.world().get_mut<SpatialHashGrid>();

        // Detect frame boundary: world tick count changes → new frame
        uint32_t current_frame =
            (uint32_t)(e.world().get_info()->world_time_total * 60.0);
        if (grid.last_frame_id != current_frame) {
          grid.last_frame_id = current_frame;
          grid.active_count = 0;
          memset(grid.cell_head, -1, sizeof(grid.cell_head));
        }

        if (grid.active_count >= SPATIAL_MAX_ENTITIES)
          return;

        int cx, cz;
        SpatialHashGrid::world_to_cell(p.x, p.z, cx, cz);
        int cell_idx = cz * SPATIAL_WIDTH + cx;
        int idx = grid.active_count++;

        grid.entity_id[idx] = e.id();
        grid.pos_x[idx] = p.x;
        grid.pos_z[idx] = p.z;
        grid.bat_id[idx] = b.id % MAX_BATTALIONS;
        grid.team_id[idx] = t.team;

        // Insert at head of flat-array linked list
        grid.entity_next[idx] = grid.cell_head[cell_idx];
        grid.cell_head[cell_idx] = idx;
      });

  // ── System 3: Musket Reload Tick (60Hz) ─────────────────────
  // Counts down reload_timer for all alive soldiers with muskets.
  ecs.system<MusketState>("MusketReloadTick")
      .with<IsAlive>()
      .each([](flecs::entity e, MusketState &ms) {
        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;
        if (ms.reload_timer > 0.0f) {
          ms.reload_timer -= dt;
          if (ms.reload_timer < 0.0f)
            ms.reload_timer = 0.0f;
        }
      });

  // ── System 4: Volley Fire (M8: Spatial Hash queries) ──────────
  // O(N×K) where K = entities within musket range (~7x7 cells).
  // Replaces the catastrophic O(N²) w.each() scan from M3.
  // Fire discipline logic preserved from M7.5.
  ecs.system<const Position, MusketState, const SoldierFormationTarget,
             const BattalionId, const TeamId>("VolleyFireSystem")
      .with<IsAlive>()
      .without<Routing>()        // Trap 27: Routing soldiers DO NOT fire!
      .without<MacroSimulated>() // S-LOD: off-screen agents don't fire
      .each([](flecs::entity e, const Position &pos, MusketState &ms,
               const SoldierFormationTarget &tgt, const BattalionId &bat,
               const TeamId &team) {
        // §12.1: can_shoot enforces Column/Square fire limits
        if (!tgt.can_shoot)
          return;
        if (ms.ammo_count == 0)
          return;
        // Soldiers continue reloading even while holding fire!
        if (ms.reload_timer > 0.0f)
          return;

        constexpr float MAX_MUSKET_RANGE = 100.0f;
        constexpr float BASE_ACCURACY = 0.35f;
        constexpr float RELOAD_TIME = 8.0f;
        constexpr float HUMIDITY_PENALTY = 0.05f;

        uint32_t my_bat_id = bat.id % MAX_BATTALIONS;
        auto &mb = g_macro_battalions[my_bat_id];

        // ─── §12.7: DOCTRINE GATES ─────────────────────────────
        if (mb.fire_discipline == DISCIPLINE_HOLD)
          return;

        if (mb.fire_discipline == DISCIPLINE_BY_RANK) {
          if (tgt.rank_index != mb.active_firing_rank)
            return;
        }

        // ─── §12.7: STATELESS AIM JITTER (KRRR-CRACK!) ────────
        float my_jitter = (float)(e.id() % 100) / 200.0f; // 0.0–0.5s

        if (mb.fire_discipline == DISCIPLINE_MASS_VOLLEY) {
          float elapsed = 0.5f - mb.volley_timer;
          if (elapsed < my_jitter)
            return;
        } else if (mb.fire_discipline == DISCIPLINE_BY_RANK) {
          float elapsed = 3.0f - mb.volley_timer;
          if (elapsed < my_jitter)
            return;
        }

        // ─── Trap 26: O(1) MACRO TARGET LOOKUP ─────────────────
        int best_bat_id = mb.target_bat_id;
        if (best_bat_id == -1)
          return; // All targets blocked or dead

        auto &enemy_bat = g_macro_battalions[best_bat_id];
        float bdx = enemy_bat.cx - pos.x;
        float bdz = enemy_bat.cz - pos.z;
        float bd2 = bdx * bdx + bdz * bdz;
        if (bd2 > (MAX_MUSKET_RANGE * MAX_MUSKET_RANGE * 4.0f))
          return; // Way out of range

        // ─── M8: SPATIAL HASH MICRO TARGET (replaces O(N²) scan) ──
        const SpatialHashGrid &grid = e.world().get<SpatialHashGrid>();
        const int rad_cells =
            static_cast<int>(MAX_MUSKET_RANGE / SPATIAL_CELL_SIZE) + 1;

        int my_cx, my_cz;
        SpatialHashGrid::world_to_cell(pos.x, pos.z, my_cx, my_cz);

        float best_dist_sq = MAX_MUSKET_RANGE * MAX_MUSKET_RANGE;
        uint64_t best_target_id = 0;
        float final_shot_dot = 1.0f;

        // Bounding box iteration (~7x7 cells for 100m range with 32m cells)
        int z_min = (my_cz - rad_cells) < 0 ? 0 : (my_cz - rad_cells);
        int z_max = (my_cz + rad_cells) >= SPATIAL_HEIGHT ? SPATIAL_HEIGHT - 1
                                                          : (my_cz + rad_cells);
        int x_min = (my_cx - rad_cells) < 0 ? 0 : (my_cx - rad_cells);
        int x_max = (my_cx + rad_cells) >= SPATIAL_WIDTH ? SPATIAL_WIDTH - 1
                                                         : (my_cx + rad_cells);

        for (int z = z_min; z <= z_max; ++z) {
          for (int x = x_min; x <= x_max; ++x) {
            int curr_idx = grid.cell_head[z * SPATIAL_WIDTH + x];

            while (curr_idx != -1) {
              // SoA data locality — only touches pos_x/z, bat_id arrays
              if (grid.bat_id[curr_idx] == (uint32_t)best_bat_id) {
                float tdx = grid.pos_x[curr_idx] - pos.x;
                float tdz = grid.pos_z[curr_idx] - pos.z;
                float td2 = tdx * tdx + tdz * tdz;

                if (td2 < best_dist_sq && td2 > 0.01f) {
                  float dist = std::sqrt(td2);
                  float nx = tdx / dist;
                  float nz = tdz / dist;

                  // §12.8: Firing arc — chest facing vs target direction
                  float dot = nx * tgt.face_dir_x + nz * tgt.face_dir_z;
                  if (dot > 0.5f) {
                    best_dist_sq = td2;
                    best_target_id = grid.entity_id[curr_idx];
                    final_shot_dot = dot;
                  }
                }
              }
              curr_idx = grid.entity_next[curr_idx];
            }
          }
        }

        if (best_target_id == 0)
          return;

        // ─── HIT CHANCE + ARC PENALTY ───────────────────────────
        bool officer_alive = mb.officer_alive;
        float current_max_range = officer_alive ? MAX_MUSKET_RANGE : 40.0f;

        float dist = std::sqrt(best_dist_sq);
        if (dist > current_max_range)
          return;

        float hit_chance = BASE_ACCURACY * (1.0f - (dist / current_max_range));
        hit_chance *= (1.0f - HUMIDITY_PENALTY);
        hit_chance *=
            final_shot_dot; // §12.8: Accuracy penalty for angled shots
        if (!officer_alive)
          hit_chance *= 0.3f;

        if (hit_chance < 0.0f)
          hit_chance = 0.0f;
        if (hit_chance > 1.0f)
          hit_chance = 1.0f;

        // Deterministic hash-based random
        flecs::world w = e.world();
        uint64_t seed =
            e.id() ^ (uint64_t)(w.get_info()->world_time_total * 100000.0);
        seed ^= seed >> 33;
        seed *= 0xff51afd7ed558ccdULL;
        seed ^= seed >> 33;
        float roll = (float)(seed & 0xFFFF) / 65535.0f;

        // Fire!
        ms.reload_timer = RELOAD_TIME;
        ms.ammo_count--;

        if (roll <= hit_chance) {
          // Trap 32: Deferred removal for thread safety
          w.entity(best_target_id).remove<IsAlive>();
        }
      });
}

// ═════════════════════════════════════════════════════════════
// M4: PANIC & MORALE SYSTEMS (CORE_MATH.md §4)
// ═════════════════════════════════════════════════════════════

void register_panic_systems(flecs::world &ecs) {

  // ── System 5: Panic CA Diffusion (5Hz) ──────────────────────
  // Double-buffered Von Neumann diffusion with evaporation.
  // Runs every 0.2s (5Hz). Swaps buffers after each pass.
  ecs.system<PanicGrid>("PanicDiffusionSystem")
      .each([](flecs::entity e, PanicGrid &grid) {
        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;

        grid.tick_accum += dt;
        if (grid.tick_accum < 0.2f)
          return;                // 5Hz gate
        grid.tick_accum -= 0.2f; // Trap 16 Fix: preserve fractional remainder

        constexpr float EVAPORATE = 0.95f;
        constexpr float SPREAD = 0.025f; // 2.5% per neighbor
        constexpr int W = PanicGrid::WIDTH;
        constexpr int H = PanicGrid::HEIGHT;

        // Diffuse BOTH team layers independently
        for (int team = 0; team < PanicGrid::TEAMS; team++) {
          for (int z = 0; z < H; z++) {
            for (int x = 0; x < W; x++) {
              int idx = z * W + x;
              float center = grid.read_buf[team][idx];

              if (center < 0.001f) {
                grid.write_buf[team][idx] = 0.0f;
                continue;
              }

              float neighbors = 0.0f;
              if (x > 0)
                neighbors += grid.read_buf[team][idx - 1];
              if (x < W - 1)
                neighbors += grid.read_buf[team][idx + 1];
              if (z > 0)
                neighbors += grid.read_buf[team][idx - W];
              if (z < H - 1)
                neighbors += grid.read_buf[team][idx + W];

              float new_val = (center * EVAPORATE) + (neighbors * SPREAD);
              if (new_val > 1.0f)
                new_val = 1.0f;
              grid.write_buf[team][idx] = new_val;
            }
          }

          // Swap buffers for this team
          for (int i = 0; i < PanicGrid::CELLS; i++) {
            grid.read_buf[team][i] = grid.write_buf[team][i];
          }
        }
      });

  // ── System 6: Panic → Stiffness + Routing Tag (60Hz) ─────────
  // Per GDD §5.1: panic drops stiffness. §5.3: panic > 0.6 → Routing.
  // Formation slot is PRESERVED — if panic clears, soldier reforms.
  // Now reads from the soldier's OWN team layer
  ecs.system<const Position, SoldierFormationTarget, const TeamId>(
         "PanicStiffnessSystem")
      .with<IsAlive>()
      .each([](flecs::entity e, const Position &pos,
               SoldierFormationTarget &target, const TeamId &team) {
        flecs::world w = e.world();
        const PanicGrid &grid = w.get<PanicGrid>();

        int t = team.team % PanicGrid::TEAMS;
        int idx = PanicGrid::world_to_idx(pos.x, pos.z);
        float panic = grid.read_buf[t][idx];

        // M7.5 §12.3: route threshold = 0.65, recovery = 0.25 (retuned for
        // 3-rank density)
        constexpr float ROUTE_THRESHOLD = 0.65f;
        constexpr float RECOVERY_THRESHOLD = 0.25f;
        constexpr float BASE_STIFFNESS = 50.0f;
        constexpr float MIN_FACTOR = 0.2f;

        bool is_routing = e.has<Routing>();

        if (!is_routing && panic > ROUTE_THRESHOLD) {
          e.add<Routing>();
          is_routing = true;
        } else if (is_routing && panic < RECOVERY_THRESHOLD) {
          e.remove<Routing>();
          is_routing = false;
        }

        if (is_routing) {
          // GDD §5.3: stiffness = 0 while routing (springs disconnected)
          target.base_stiffness = 0.0f;
        } else {
          // GDD §5.1: panic drops stiffness (0 = full, 1.0 = 20%)
          float factor = 1.0f - (panic * (1.0f - MIN_FACTOR));
          target.base_stiffness = BASE_STIFFNESS * factor;
        }
      });

  // ── System 7: Routing Behavior (60Hz) ───────────────────────
  // Per GDD §5.3: routing soldiers sprint away from nearest enemy
  // at 5.0 m/s and emit +0.05 panic/tick (contagion).
  ecs.system<const Position, Velocity, const TeamId>("RoutingBehaviorSystem")
      .with<IsAlive>()
      .with<Routing>()
      .each([](flecs::entity e, const Position &pos, Velocity &v,
               const TeamId &team) {
        flecs::world w = e.world();
        float dt = w.delta_time();
        if (dt <= 0.0f)
          return;

        constexpr float ROUTE_SPRINT = 5.0f; // m/s (GDD §5.3)
        constexpr float CONTAGION = 0.05f;   // panic/tick (GDD §5.3)

        // Find nearest enemy to flee FROM
        float nearest_dist_sq = 1e18f;
        float enemy_x = pos.x;
        float enemy_z = pos.z;

        // Trap 8+9 Fix: Use macro battalion centroid instead of O(N) scan
        for (int i = 0; i < MAX_BATTALIONS; i++) {
          if (g_macro_battalions[i].alive_count == 0)
            continue;
          if (g_macro_battalions[i].team_id == team.team)
            continue;
          float bdx = g_macro_battalions[i].cx - pos.x;
          float bdz = g_macro_battalions[i].cz - pos.z;
          float bd2 = bdx * bdx + bdz * bdz;
          if (bd2 < nearest_dist_sq) {
            nearest_dist_sq = bd2;
            enemy_x = g_macro_battalions[i].cx;
            enemy_z = g_macro_battalions[i].cz;
          }
        }

        // Flee direction = AWAY from nearest enemy
        float flee_dx = pos.x - enemy_x;
        float flee_dz = pos.z - enemy_z;
        float flee_dist = std::sqrt(flee_dx * flee_dx + flee_dz * flee_dz);

        if (flee_dist > 0.01f) {
          float inv = 1.0f / flee_dist;
          v.vx = flee_dx * inv * ROUTE_SPRINT;
          v.vz = flee_dz * inv * ROUTE_SPRINT;
        }

        // GDD §5.3: routing soldiers emit +0.05 panic/tick (contagion)
        PanicGrid &grid = w.ensure<PanicGrid>();
        int t = team.team % PanicGrid::TEAMS;
        int idx = PanicGrid::world_to_idx(pos.x, pos.z);
        grid.read_buf[t][idx] +=
            0.10f * dt; // M7.5 §12.3: contagion retuned from 0.25/tick
        if (grid.read_buf[t][idx] > 1.0f)
          grid.read_buf[t][idx] = 1.0f;
      });

  // ── System 7: Death → Panic Injection (observer) ────────────
  // When IsAlive is removed, inject +0.4 fear at the death position.
  // This creates panic hotspots around kill zones.
  // Inject panic into the DEAD SOLDIER'S team layer
  ecs.observer<const Position, const TeamId>("DeathPanicInjector")
      .event(flecs::OnRemove)
      .with<IsAlive>()
      .each([](flecs::entity e, const Position &pos, const TeamId &team) {
        flecs::world w = e.world();
        PanicGrid &grid = w.ensure<PanicGrid>();

        int t = team.team % PanicGrid::TEAMS;
        int idx = PanicGrid::world_to_idx(pos.x, pos.z);
        grid.read_buf[t][idx] +=
            0.20f; // M7.5 §12.3: death fear retuned from 0.4
        if (grid.read_buf[t][idx] > 1.0f)
          grid.read_buf[t][idx] = 1.0f;
      });

  // ── System 7b: Distributed Drummer Aura (M7.5 §12.4) ─────
  // If drummer is alive, EVERY soldier cleanses their own cell.
  // Per-soldier: -0.015/sec. 15 men/cell = -0.225/sec total.
  // Aura morphs with formation shape. Brittle flanks!
  ecs.system<const Position, const BattalionId, const TeamId>(
         "DistributedDrummerAura")
      .with<IsAlive>()
      .each([](flecs::entity e, const Position &pos, const BattalionId &bat,
               const TeamId &team) {
        uint32_t id = bat.id % MAX_BATTALIONS;
        if (!g_macro_battalions[id].drummer_alive)
          return;

        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;

        PanicGrid &grid = e.world().ensure<PanicGrid>();
        int t = team.team % PanicGrid::TEAMS;
        int idx = PanicGrid::world_to_idx(pos.x, pos.z);
        if (idx >= 0 && idx < PanicGrid::CELLS) {
          grid.read_buf[t][idx] -= 0.015f * dt;
          if (grid.read_buf[t][idx] < 0.0f)
            grid.read_buf[t][idx] = 0.0f;
        }
      });
}

// ═════════════════════════════════════════════════════════════
// M5: ARTILLERY SYSTEMS (CORE_MATH.md §3, GDD §5.2)
// ═════════════════════════════════════════════════════════════

void register_artillery_systems(flecs::world &ecs) {

  // ── System 8: Artillery Reload & Unlimber Tick (60Hz) ───────
  // Counts down reload_timer and unlimber_timer per battery.
  ecs.system<ArtilleryBattery>("ArtilleryReloadTick")
      .each([](flecs::entity e, ArtilleryBattery &bat) {
        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;

        // Unlimber countdown (60s to deploy)
        if (bat.unlimber_timer > 0.0f) {
          bat.unlimber_timer -= dt;
          if (bat.unlimber_timer <= 0.0f) {
            bat.unlimber_timer = 0.0f;
            bat.is_limbered = false;
          }
        }

        // Reload countdown (only when deployed)
        if (!bat.is_limbered && bat.reload_timer > 0.0f) {
          bat.reload_timer -= dt;
          if (bat.reload_timer < 0.0f)
            bat.reload_timer = 0.0f;
        }
      });

  // ── System 9: Artillery Fire (spawn shots) ──────────────────
  // When a battery has a FireOrder and is ready: spawn ArtilleryShot
  // entities (one per gun). Uses traverse_angle to aim.
  ecs.system<const Position, ArtilleryBattery, const FireOrder, const TeamId>(
         "ArtilleryFireSystem")
      .each([](flecs::entity e, const Position &pos, ArtilleryBattery &bat,
               const FireOrder &fire, const TeamId &team) {
        // Can't fire while limbered or unlimbering
        if (bat.is_limbered || bat.unlimber_timer > 0.0f)
          return;
        // Still reloading
        if (bat.reload_timer > 0.0f)
          return;

        // Determine ammo type: use canister if target is close (<100m)
        float dx = fire.target_x - pos.x;
        float dz = fire.target_z - pos.z;
        float dist = std::sqrt(dx * dx + dz * dz);
        bool use_canister = (dist < 100.0f) && (bat.ammo_canister > 0);
        ArtilleryAmmoType ammo_type =
            use_canister ? AMMO_CANISTER : AMMO_ROUNDSHOT;

        // Check ammo
        if (ammo_type == AMMO_ROUNDSHOT && bat.ammo_roundshot <= 0)
          return;
        if (ammo_type == AMMO_CANISTER && bat.ammo_canister <= 0)
          return;

        flecs::world w = e.world();

        // Fire direction
        float dir_len = dist;
        if (dir_len < 1.0f)
          dir_len = 1.0f;
        float dir_x = dx / dir_len;
        float dir_z = dz / dir_len;

        // Muzzle velocity calculation
        // For roundshot: ~450 m/s initial, 45° elevation adjusted for range
        // For canister: ~350 m/s, flatter trajectory
        constexpr float ROUNDSHOT_SPEED = 200.0f; // scaled for gameplay
        constexpr float CANISTER_SPEED = 150.0f;
        constexpr float RELOAD_TIME = 15.0f; // seconds between volleys

        float speed =
            (ammo_type == AMMO_ROUNDSHOT) ? ROUNDSHOT_SPEED : CANISTER_SPEED;

        // Calculate elevation angle for ballistic arc to target
        // Using simplified ballistic formula: vy = (g*d) / (2*vx)
        // for approximate targeting
        float flat_speed = speed * 0.9f; // 90% horizontal
        float time_to_target = dist / flat_speed;
        if (time_to_target < 0.1f)
          time_to_target = 0.1f;
        float vy_needed =
            0.5f * 9.81f * time_to_target; // compensate for gravity drop

        // Spawn one shot per gun
        int shots_to_fire = bat.num_guns;
        for (int g = 0; g < shots_to_fire; g++) {
          // Slight spread per gun for visual variety
          uint32_t gun_seed = (uint32_t)(e.id() * 31 + g * 7);
          float spread_x = ((float)(gun_seed % 100) / 100.0f - 0.5f) * 0.05f;
          float spread_z =
              ((float)((gun_seed * 13) % 100) / 100.0f - 0.5f) * 0.05f;

          w.entity()
              .set<ArtilleryShot>({pos.x, // x
                                   1.0f,  // y (cannon height)
                                   pos.z, // z
                                   dir_x * flat_speed + spread_x * speed, // vx
                                   vy_needed,                             // vy
                                   dir_z * flat_speed + spread_z * speed, // vz
                                   10.0f,     // kinetic_energy
                                   ammo_type, // ammo type
                                   true})     // active
              .set<TeamId>({team.team});
        }

        // Consume ammo and start reload
        if (ammo_type == AMMO_ROUNDSHOT)
          bat.ammo_roundshot -= 1;
        else
          bat.ammo_canister -= 1;
        bat.reload_timer = RELOAD_TIME;

        // Remove fire order (single volley per order)
        e.remove<FireOrder>();
      });

  // ── System 10: Artillery Kinematics (60Hz) ──────────────────
  // Gravity integration for in-flight cannonballs (CORE_MATH.md §3).
  ecs.system<ArtilleryShot>("ArtilleryKinematicsSystem")
      .each([](flecs::entity e, ArtilleryShot &shot) {
        if (!shot.active)
          return;

        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;

        // Gravity
        shot.vy -= 9.81f * dt;

        // Position integration
        shot.x += shot.vx * dt;
        shot.y += shot.vy * dt;
        shot.z += shot.vz * dt;

        // Kill shot if KE depleted or way off map
        if (shot.kinetic_energy <= 0.0f || shot.x < -500.0f ||
            shot.x > 500.0f || shot.z < -500.0f || shot.z > 500.0f) {
          shot.active = false;
        }
      });

  // ── System 11: Ground Collision & Ricochet (60Hz) ───────────
  // CORE_MATH.md §3: Hard earth = ricochet, mud = sink.
  // Currently uses flat ground (y=0). Terrain height at M15+.
  ecs.system<ArtilleryShot>("ArtilleryGroundCollisionSystem")
      .each([](flecs::entity e, ArtilleryShot &shot) {
        if (!shot.active)
          return;

        constexpr float GROUND_HEIGHT = 0.0f;
        constexpr float WETNESS = 0.2f; // Dry day (0-1 scale)
        constexpr float MUD_THRESHOLD = 0.8f;

        if (shot.y <= GROUND_HEIGHT) {
          if (WETNESS > MUD_THRESHOLD) {
            // MUD: Ball sinks. Zero ricochet. (The Waterloo Effect)
            shot.active = false;
          } else {
            // HARD EARTH: Ricochet! (CORE_MATH.md §3)
            shot.y = GROUND_HEIGHT + 0.1f;
            shot.vy = std::abs(shot.vy) * 0.4f; // Lose 60% vertical
            shot.vx *= 0.7f;                    // Friction
            shot.vz *= 0.7f;

            // Ball stops if too slow
            if (shot.vy < 1.0f && std::abs(shot.vx) < 1.0f &&
                std::abs(shot.vz) < 1.0f) {
              shot.active = false;
            }
          }
        }
      });

  // ── System 12: Artillery Hit Detection (60Hz) ───────────────
  // Roundshot: plows through formation, -1.0 KE per kill.
  // Canister: cone shotgun at <100m.
  // Queries all alive soldiers, checks proximity to active shots.
  ecs.system<ArtilleryShot, const TeamId>("ArtilleryFormationHitSystem")
      .each([](flecs::entity shot_e, ArtilleryShot &shot,
               const TeamId &shot_team) {
        if (!shot.active)
          return;
        if (shot.kinetic_energy <= 0.0f) {
          shot.active = false;
          return;
        }

        flecs::world w = shot_e.world();

        // Trap 8 Fix: Use inline system query (no thread_local leak)

        constexpr float HIT_RADIUS = 1.5f; // meters
        constexpr float HIT_RADIUS_SQ = HIT_RADIUS * HIT_RADIUS;
        constexpr float KE_PER_KILL = 1.0f;

        // For canister: wider area, multiple hits
        constexpr float CANISTER_RADIUS = 5.0f;
        constexpr float CANISTER_RADIUS_SQ = CANISTER_RADIUS * CANISTER_RADIUS;
        constexpr int CANISTER_MAX_HITS = 12;

        float check_radius_sq =
            (shot.ammo == AMMO_CANISTER) ? CANISTER_RADIUS_SQ : HIT_RADIUS_SQ;

        int hits_this_frame = 0;
        int max_hits = (shot.ammo == AMMO_CANISTER) ? CANISTER_MAX_HITS : 100;

        w.each([&](flecs::entity te, const Position &tp, const TeamId &tt,
                   const BattalionId &tb) {
          if (!te.has<IsAlive>())
            return;
          // Only hit enemies
          if (tt.team == shot_team.team)
            return;
          // Already spent
          if (shot.kinetic_energy <= 0.0f)
            return;
          if (hits_this_frame >= max_hits)
            return;

          float dx = tp.x - shot.x;
          float dz = tp.z - shot.z;
          float d2 = dx * dx + dz * dz;

          if (d2 < check_radius_sq) {
            te.remove<IsAlive>();
            shot.kinetic_energy -= KE_PER_KILL;
            hits_this_frame++;

            if (shot.kinetic_energy <= 0.0f) {
              shot.active = false;
            }
          }
        });
      });
}

// ═════════════════════════════════════════════════════════════
// M6: CAVALRY SYSTEMS (Deep Think #3 + #4)
//
// Deep Think #3: Ballistic kinematics (locked direction vector)
// Deep Think #4: Battalion centroids, parallel vector rule
// ═════════════════════════════════════════════════════════════

void register_cavalry_systems(flecs::world &ecs) {

  // CRITICAL FIX: Deleted explicit component re-registrations and static query
  // to avoid TU Component ID mismatches and DLL lifecycle traps.

  // ── System: Cavalry Ballistic Kinematics (60Hz) ─────────────
  // Handles ALL cavalry movement in states 1 (Charging) and
  // 2 (Disordered). State 0 (Walk) uses the spring-damper.
  ecs.system<Position, Velocity, CavalryState, SoldierFormationTarget,
             const MovementStats>("CavalryBallistics")
      .with<IsAlive>()
      .each([](flecs::entity e, Position &p, Velocity &v, CavalryState &cs,
               SoldierFormationTarget &tgt, const MovementStats &stats) {
        if (cs.state_flags == 0)
          return; // Walk — spring-damper handles this

        float dt = e.world().delta_time();
        if (dt <= 0.0f)
          return;

        cs.state_timer += dt;

        // ─────────────────────────────────────────────────────
        // STATE 1: BALLISTIC CHARGE (Projectile Mode)
        // ─────────────────────────────────────────────────────
        if (cs.state_flags == 1) {
          // Cubic ramp: 1.5s. Heavy start → explosive lurch.
          float t = cs.state_timer / 1.5f;
          if (t > 1.0f)
            t = 1.0f;
          cs.charge_momentum = 1.2f * t * t * t; // Max 1.2 — ~3-4 kills vs Line

          // Speed from locked direction vector
          float current_speed =
              stats.base_speed +
              (stats.charge_speed - stats.base_speed) * cs.charge_momentum;
          v.vx = cs.lock_dir_x * current_speed;
          v.vz = cs.lock_dir_z * current_speed;

          // Exhaustion cutoff — "blown horse" after 5s
          if (cs.state_timer > 5.0f) {
            cs.state_flags = 2;
            cs.state_timer = 0.0f;
          }
        }
        // ─────────────────────────────────────────────────────
        // STATE 2: DISORDERED (Drift and Reform)
        // ─────────────────────────────────────────────────────
        else if (cs.state_flags == 2) {
          cs.charge_momentum = 0.0f;

          // High friction — horses pulling up
          v.vx *= 0.95f;
          v.vz *= 0.95f;

          // Cap drift speed to 2 m/s
          float speed_sq = (v.vx * v.vx) + (v.vz * v.vz);
          if (speed_sq > 4.0f) {
            float ratio = 2.0f / std::sqrt(speed_sq);
            v.vx *= ratio;
            v.vz *= ratio;
          }

          // Recovery after 10s
          if (cs.state_timer >= 10.0f) {
            cs.state_flags = 0;
            cs.state_timer = 0.0f;

            // CRITICAL: Reset formation target to current position.
            // Prevents violent rubber-band back to charge origin.
            tgt.target_x = p.x;
            tgt.target_z = p.z;

            e.remove<ChargeOrder>();
            e.remove<Disordered>();
          }
        }

        // Integrate position for ballistic/disordered states
        p.x += v.vx * dt;
        p.z += v.vz * dt;
      });

  // ── System: Cavalry Impact (60Hz) ───────────────────────────
  // Sequential micro-collisions. Kills enemies, spends momentum.
  // If momentum depleted → disordered.
  ecs.system<const Position, CavalryState, Velocity, const TeamId>(
         "CavalryImpact")
      .with<ChargeOrder>()
      .with<IsAlive>()
      .each([&ecs](flecs::entity cav, const Position &cp, CavalryState &cs,
                   Velocity &cv, const TeamId &ct) {
        if (cs.state_flags != 1 || cs.charge_momentum <= 0.0f)
          return;

        constexpr float CONTACT_RADIUS = 1.8f;
        constexpr float CONTACT_RADIUS_SQ = CONTACT_RADIUS * CONTACT_RADIUS;
        bool hit_anyone = false;

        auto q = ecs.query_builder<const Position, const TeamId,
                                   const FormationDefense>()
                     .with<IsAlive>()
                     .build();

        q.each([&](flecs::entity target, const Position &tp, const TeamId &tt,
                   const FormationDefense &fd) {
          if (cs.charge_momentum <= 0.0f)
            return;
          if (tt.team == ct.team)
            return;

          float dx = tp.x - cp.x;
          float dz = tp.z - cp.z;
          float dist_sq = dx * dx + dz * dz;

          if (dist_sq > CONTACT_RADIUS_SQ)
            return;

          float cost = 0.25f / (1.0f - fd.defense + 0.001f);

          if (cs.charge_momentum < cost) {
            cs.charge_momentum = 0.0f;
            return;
          }

          target.remove<IsAlive>();
          cs.charge_momentum -= cost;
          hit_anyone = true;
        });

        // Momentum spent → disordered (timer resets for 10s drift)
        if (hit_anyone && cs.charge_momentum <= 0.0f) {
          cs.state_flags = 2;
          cs.state_timer = 0.0f;
          cav.remove<ChargeOrder>();
        }
      });
}

// ═════════════════════════════════════════════════════════════
// M9: ECONOMY SYSTEMS (GDD §7.1 — Smart Buildings, Dumb Agents)
// ═════════════════════════════════════════════════════════════

// Global job board (transient — cleared each matchmaker tick)
static std::vector<LogisticsJob> g_global_job_board;
static int g_idle_citizen_count = 0; // Trap 41: early-out for matchmaker

void register_economy_systems(flecs::world &ecs) {

  // ── System M9.1: Citizen Movement (60Hz) ─────────────────────
  // The "Dumb Agent" loop. If IDLE/WORKING/SLEEPING, zero velocity
  // and skip (costs 0 CPU). If moving, spring toward current_target.
  ecs.system<Citizen, Position, Velocity>("CitizenMovementSystem")
      .with<IsAlive>()
      .without<MacroSimulated>()
      .each([](flecs::entity e, Citizen &c, Position &pos, Velocity &vel) {
        // Skip stationary states — costs 0 CPU
        if (c.state == CSTATE_IDLE || c.state == CSTATE_WORKING ||
            c.state == CSTATE_SLEEPING) {
          vel.vx = 0.0f;
          vel.vz = 0.0f;
          return;
        }

        // Trap 40: Validate target is alive before moving toward it
        if (c.current_target == 0 || !e.world().is_alive(c.current_target)) {
          c.state = CSTATE_IDLE;
          c.current_target = 0;
          vel.vx = 0.0f;
          vel.vz = 0.0f;
          return;
        }

        // Simple spring toward target position
        // (Full flow field pathfinding is M11 — for now, direct spring)
        const Position &tp = e.world().entity(c.current_target).get<Position>();
        float dx = tp.x - pos.x;
        float dz = tp.z - pos.z;
        float dist_sq = dx * dx + dz * dz;

        if (dist_sq < 1.0f) {
          // Arrived — velocity zeroed, state machine handles transition
          vel.vx = 0.0f;
          vel.vz = 0.0f;
          return;
        }

        // Normalize and apply citizen walking speed
        constexpr float CITIZEN_SPEED = 2.0f; // m/s
        float inv_dist = 1.0f / std::sqrt(dist_sq);
        vel.vx = dx * inv_dist * CITIZEN_SPEED;
        vel.vz = dz * inv_dist * CITIZEN_SPEED;
      });

  // ── System M9.2: Citizen Routine (5Hz) ───────────────────────
  // The "Brain" — evaluates arrival, advances state machine.
  // Amortized at 5Hz via a tick accumulator.
  ecs.system<Citizen, const Position>("CitizenRoutineSystem")
      .with<IsAlive>()
      .without<MacroSimulated>()
      .each([](flecs::entity e, Citizen &c, const Position &pos) {
        // 5Hz amortization: only tick every ~0.2s using entity hash
        // Distributes load across frames instead of all citizens at once
        uint32_t frame_slot = (uint32_t)(e.id() % 12);
        uint32_t current_slot =
            (uint32_t)(e.world().get_info()->world_time_total * 60.0) % 12;
        if (frame_slot != current_slot)
          return;

        // Count idle citizens for Trap 41 matchmaker guard
        if (c.state == CSTATE_IDLE)
          g_idle_citizen_count++;

        // State machine transitions on arrival
        if (c.state == CSTATE_LOGISTICS_TO_SRC && c.current_target != 0) {
          if (!e.world().is_alive(c.current_target)) {
            c.state = CSTATE_IDLE;
            c.current_target = 0;
            return;
          }
          const Position &tp =
              e.world().entity(c.current_target).get<Position>();
          float dx = tp.x - pos.x;
          float dz = tp.z - pos.z;
          if ((dx * dx + dz * dz) < 4.0f) {
            // Arrived at source — pick up goods, redirect to dest
            // (In full implementation, reads from Workplace inventory)
            c.state = CSTATE_LOGISTICS_TO_DEST;
            // current_target will be set to dest by matchmaker
          }
        } else if (c.state == CSTATE_LOGISTICS_TO_DEST &&
                   c.current_target != 0) {
          if (!e.world().is_alive(c.current_target)) {
            c.state = CSTATE_IDLE;
            c.current_target = 0;
            c.carrying_amount = 0;
            return;
          }
          const Position &tp =
              e.world().entity(c.current_target).get<Position>();
          float dx = tp.x - pos.x;
          float dz = tp.z - pos.z;
          if ((dx * dx + dz * dz) < 4.0f) {
            // Arrived at dest — deliver goods, become idle
            c.carrying_amount = 0;
            c.carrying_item = 0; // ITEM_NONE
            c.state = CSTATE_IDLE;
            c.current_target = 0;
          }
        }

        // Satisfaction update from CivicGrid (sleeping citizens)
        if (c.state == CSTATE_SLEEPING) {
          const CivicGrid &civic = e.world().get<CivicGrid>();
          int idx = CivicGrid::world_to_idx(pos.x, pos.z);
          float market = civic.market_access[idx];
          float pollute = civic.pollution[idx];
          // Satisfaction rises with market access, drops with pollution
          c.satisfaction += (market * 0.01f - pollute * 0.02f);
          if (c.satisfaction < 0.0f)
            c.satisfaction = 0.0f;
          if (c.satisfaction > 1.0f)
            c.satisfaction = 1.0f;
        }
      });

  // ── System M10.1: DiscreteBatchProductionSystem (1Hz) ─────────
  // Replaces M9 WorkplaceLogicSystem with multi-recipe discrete batches.
  // Traps: 50 (Tool Death Spiral), 51 (Byproduct Gridlock)
  ecs.system<Workplace>("DiscreteBatchProductionSystem")
      .with<IsAlive>()
      .each([](flecs::entity e, Workplace &wp) {
        // 1Hz amortization
        uint32_t frame_slot = (uint32_t)(e.id() % 60);
        uint32_t current_slot =
            (uint32_t)(e.world().get_info()->world_time_total * 60.0) % 60;
        if (frame_slot != current_slot)
          return;

        // No workers → no production
        if (wp.active_workers <= 0)
          return;

        // Check if all required inputs are satisfied
        bool inputs_ok = true;
        for (int i = 0; i < 3; i++) {
          if (wp.in_items[i] != 0 && wp.in_stock[i] < wp.in_reqs[i]) {
            inputs_ok = false;
            break;
          }
        }
        if (!inputs_ok)
          return;

        // Efficiency = active_workers / max_workers
        float efficiency = (wp.max_workers > 0) ? (float)wp.active_workers /
                                                      (float)wp.max_workers
                                                : 0.0f;

        // Trap 50: Tool Death Spiral
        // BYPASS_TOOLS flag = Blacksmith works bare-handed at 0.25x
        if (!(wp.flags & WP_FLAG_BYPASS_TOOLS)) {
          if (wp.tool_durability <= 0.0f)
            efficiency *= 0.25f;
        }

        // Advance production timer
        wp.prod_timer += efficiency;
        if (wp.prod_timer < wp.base_time)
          return;

        // === BATCH COMPLETE ===
        wp.prod_timer = 0.0f;

        // Deduct inputs
        for (int i = 0; i < 3; i++) {
          if (wp.in_items[i] != 0)
            wp.in_stock[i] -= wp.in_reqs[i];
        }

        // Add outputs (Trap 51: Byproduct Gridlock)
        constexpr uint16_t MAX_STOCK = 500;
        for (int i = 0; i < 3; i++) {
          if (wp.out_items[i] != 0) {
            uint16_t new_stock = wp.out_stock[i] + wp.out_yields[i];
            // If output exceeds max, clamp (excess thrown in river — Trap 51)
            wp.out_stock[i] = (new_stock > MAX_STOCK) ? MAX_STOCK : new_stock;
          }
        }

        // Degrade tools (if tools exist)
        if (!(wp.flags & WP_FLAG_BYPASS_TOOLS) && wp.tool_durability > 0.0f) {
          wp.tool_durability -= 1.0f;
        }

        // Inject pollution into CivicGrid
        if (wp.pollution_out > 0.0f) {
          const Position &pos = e.get<Position>();
          CivicGrid &civic = e.world().get_mut<CivicGrid>();
          int idx = CivicGrid::world_to_idx(pos.x, pos.z);
          civic.pollution[idx] += wp.pollution_out;
        }

        // Post logistics jobs for outputs exceeding threshold
        constexpr uint16_t WAGON_THRESHOLD = 20;
        for (int i = 0; i < 3; i++) {
          if (wp.out_items[i] != 0 && wp.out_stock[i] >= WAGON_THRESHOLD) {
            LogisticsJob job;
            job.source_building = e.id();
            job.dest_building = 0; // Matchmaker assigns nearest consumer
            job.item_type = wp.out_items[i];
            job.amount =
                (uint8_t)(wp.out_stock[i] > 255 ? 255 : wp.out_stock[i]);
            job.priority = wp.out_stock[i];
            job.flow_field_id = 0;
            g_global_job_board.push_back(job);
          }
        }
      });

  // ── System M10.2: WagonKinematicsSystem (60Hz) ───────────────
  // Road-graph movement via flow fields. O(1) lookup per wagon per frame.
  // Trap 52: Validate dest_building is alive before reading it.
  ecs.system<CargoManifest, Position, Velocity>("WagonKinematicsSystem")
      .with<IsAlive>()
      .each([](flecs::entity e, CargoManifest &cargo, Position &pos,
               Velocity &vel) {
        // Trap 52: Validate destination is still alive
        if (cargo.dest_building == 0 ||
            !e.world().is_alive(cargo.dest_building)) {
          // Destination destroyed — halt wagon, clear velocity
          vel.vx = 0.0f;
          vel.vz = 0.0f;
          // TODO: M9 Matchmaker should reroute to nearest valid depot
          return;
        }

        // Flow field pathfinding stub (full flow fields are M11)
        // For now: direct spring toward destination (same as citizen movement)
        const Position &tp =
            e.world().entity(cargo.dest_building).get<Position>();
        float dx = tp.x - pos.x;
        float dz = tp.z - pos.z;
        float dist_sq = dx * dx + dz * dz;

        constexpr float WAGON_SPEED = 3.0f;     // m/s (slower than cavalry)
        constexpr float ARRIVAL_DIST_SQ = 4.0f; // 2m arrival radius

        if (dist_sq < ARRIVAL_DIST_SQ) {
          // Arrived at destination
          vel.vx = 0.0f;
          vel.vz = 0.0f;

          // Deliver cargo to dest workplace
          if (e.world().entity(cargo.dest_building).has<Workplace>()) {
            Workplace &dest_wp =
                e.world().entity(cargo.dest_building).get_mut<Workplace>();
            // Find matching input slot and deposit
            for (int i = 0; i < 3; i++) {
              if (dest_wp.in_items[i] == cargo.item_type) {
                dest_wp.in_stock[i] += cargo.amount;
                break;
              }
            }
          }

          // Deduct from source output
          if (cargo.source_building != 0 &&
              e.world().is_alive(cargo.source_building) &&
              e.world().entity(cargo.source_building).has<Workplace>()) {
            Workplace &src_wp =
                e.world().entity(cargo.source_building).get_mut<Workplace>();
            for (int i = 0; i < 3; i++) {
              if (src_wp.out_items[i] == cargo.item_type) {
                if (src_wp.out_stock[i] >= cargo.amount)
                  src_wp.out_stock[i] -= cargo.amount;
                break;
              }
            }
          }

          cargo.amount = 0;
          // Wagon returns to idle — Matchmaker reassigns next tick
          return;
        }

        // Move toward destination
        float inv_dist = 1.0f / std::sqrt(dist_sq);
        vel.vx = dx * inv_dist * WAGON_SPEED;
        vel.vz = dz * inv_dist * WAGON_SPEED;
      });

  // ── System M11.1: HazardIgnitionSystem (5Hz) ─────────────────
  // Richmond Ordinance: spark_risk near volatile wagons → explosion.
  // Uses M8 Spatial Hash for O(1) proximity check.
  ecs.system<const Workplace, const Position>("HazardIgnitionSystem")
      .with<IsAlive>()
      .each([](flecs::entity e, const Workplace &wp, const Position &pos) {
        if (wp.spark_risk <= 0.0f)
          return;

        // 5Hz amortization
        uint32_t frame_slot = (uint32_t)(e.id() % 12);
        uint32_t current_slot =
            (uint32_t)(e.world().get_info()->world_time_total * 60.0) % 12;
        if (frame_slot != current_slot)
          return;

        // Query M8 Spatial Hash for nearby volatile entities
        const SpatialHashGrid &grid = e.world().get<SpatialHashGrid>();

        // Check nearby cells (15m radius → ~2 cells at 8m cell size)
        constexpr float IGNITION_RADIUS = 15.0f;
        int cx, cz;
        SpatialHashGrid::world_to_cell(pos.x, pos.z, cx, cz);
        int cell_range = (int)(IGNITION_RADIUS / SPATIAL_CELL_SIZE) + 1;

        // Simple RNG humidity check (10% chance per tick per spark source)
        uint32_t rng =
            (uint32_t)(e.id() * 2654435761u +
                       (uint32_t)(e.world().get_info()->world_time_total *
                                  1000.0));
        if ((rng % 100) > 10)
          return; // 90% humidity saves the day

        // Scan for volatile wagons in range
        for (int dz = -cell_range; dz <= cell_range; dz++) {
          for (int dx = -cell_range; dx <= cell_range; dx++) {
            int nx = cx + dx;
            int nz = cz + dz;
            if (nx < 0 || nx >= SPATIAL_WIDTH || nz < 0 || nz >= SPATIAL_HEIGHT)
              continue;

            int cell_idx = nz * SPATIAL_WIDTH + nx;
            int head = grid.cell_head[cell_idx];
            while (head >= 0 && head < grid.active_count) {
              uint64_t target_id = grid.entity_id[head];
              if (e.world().is_alive(target_id)) {
                flecs::entity target = e.world().entity(target_id);
                if (target.has<CargoManifest>()) {
                  const CargoManifest &cm = target.get<CargoManifest>();
                  if (cm.volatility > 0.0f) {
                    // Check actual distance
                    const Position &tp = target.get<Position>();
                    float ddx = tp.x - pos.x;
                    float ddz = tp.z - pos.z;
                    if ((ddx * ddx + ddz * ddz) <
                        IGNITION_RADIUS * IGNITION_RADIUS) {
                      // KABOOM — defer entity deletion
                      target.remove<IsAlive>();
                      // TODO: Queue VoxelDestructionEvent for M13
                    }
                  }
                }
              }
              head = grid.entity_next[head];
            }
          }
        }
      });

  // ── M12.1: WagonCombatObserver (Event Driven) ────────────────
  // When a wagon dies (cavalry, artillery), cargo is lost.
  // If carrying Black Powder, secondary explosion kills nearby entities.
  ecs.observer<CargoManifest>("OnWagonDestroyed")
      .event(flecs::OnRemove)
      .each([](flecs::entity e, CargoManifest &cargo) {
        // If volatile cargo, trigger secondary explosion
        if (cargo.volatility > 0.0f && cargo.amount > 0) {
          // Detonate: kill everything in 20m radius using spatial hash
          const SpatialHashGrid &grid = e.world().get<SpatialHashGrid>();
          const Position &pos = e.get<Position>();
          constexpr float BLAST_RADIUS = 20.0f;

          int cx, cz;
          SpatialHashGrid::world_to_cell(pos.x, pos.z, cx, cz);
          int cell_range = (int)(BLAST_RADIUS / SPATIAL_CELL_SIZE) + 1;

          for (int dz = -cell_range; dz <= cell_range; dz++) {
            for (int dx = -cell_range; dx <= cell_range; dx++) {
              int nx = cx + dx;
              int nz = cz + dz;
              if (nx < 0 || nx >= SPATIAL_WIDTH || nz < 0 ||
                  nz >= SPATIAL_HEIGHT)
                continue;

              int cell_idx = nz * SPATIAL_WIDTH + nx;
              int head = grid.cell_head[cell_idx];
              while (head >= 0 && head < grid.active_count) {
                uint64_t target_id = grid.entity_id[head];
                if (target_id != e.id() && e.world().is_alive(target_id)) {
                  flecs::entity target = e.world().entity(target_id);
                  const Position &tp = target.get<Position>();
                  float ddx = tp.x - pos.x;
                  float ddz = tp.z - pos.z;
                  if ((ddx * ddx + ddz * ddz) < BLAST_RADIUS * BLAST_RADIUS) {
                    target.remove<IsAlive>();
                  }
                }
                head = grid.entity_next[head];
              }
            }
          }
        }
        // Cargo permanently lost
        cargo.amount = 0;
      });

  // ── System M9.4: Zeitgeist Aggregation (0.2Hz) ──────────────
  // 5-second tick. Sums satisfaction by social_class.
  ecs.system<const Citizen>("ZeitgeistAggregationSystem")
      .with<IsAlive>()
      .each([](flecs::entity e, const Citizen &c) {
        // 0.2Hz: every ~300 frames using a global slot
        uint32_t current_slot =
            (uint32_t)(e.world().get_info()->world_time_total * 0.2) % 2;
        static uint32_t last_slot = 999;
        if (current_slot == last_slot)
          return;

        GlobalZeitgeist &z = e.world().get_mut<GlobalZeitgeist>();

        // Only reset on first entity of new tick
        static uint32_t reset_frame = 999;
        uint32_t this_frame =
            (uint32_t)(e.world().get_info()->world_time_total * 60.0);
        if (reset_frame != this_frame) {
          reset_frame = this_frame;
          z.angry_peasants = 0;
          z.angry_artisans = 0;
          z.angry_merchants = 0;
          z.total_citizens = 0;
          z.avg_satisfaction = 0.0f;
          g_idle_citizen_count = 0; // Also reset idle count
        }

        z.total_citizens++;
        z.avg_satisfaction += c.satisfaction;

        if (c.satisfaction < 0.4f) {
          if (c.social_class == 0)
            z.angry_peasants++;
          else if (c.social_class == 1)
            z.angry_artisans++;
          else if (c.social_class == 2)
            z.angry_merchants++;
        }

        // Last entity updates the slot
        last_slot = current_slot;
      });

  // ── M9.5: Conscription Bridge Observer ───────────────────────
  // Trap: remove<Citizen>() must cleanly untangle all references.
  // Severs workplace link, household link, drops carried goods.
  ecs.observer<Citizen>("OnCitizenDraftedOrKilled")
      .event(flecs::OnRemove)
      .each([](flecs::entity e, Citizen &c) {
        flecs::world w = e.world();

        // 1. Sever Workplace Link
        if (c.workplace_id != 0 && w.is_alive(c.workplace_id)) {
          Workplace &wp = w.entity(c.workplace_id).get_mut<Workplace>();
          if (wp.active_workers > 0)
            wp.active_workers--;
        }

        // 2. Sever Household Link
        if (c.home_id != 0 && w.is_alive(c.home_id)) {
          Household &hh = w.entity(c.home_id).get_mut<Household>();
          if (hh.living_population > 0)
            hh.living_population--;
        }

        // 3. Drop carried goods (item entity spawn would go here)
        // For now, goods are simply lost (proper drop_item is M11)
        c.carrying_amount = 0;
        c.carrying_item = 0;
      });
}

} // namespace musket
