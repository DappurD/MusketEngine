#include "musket_systems.h"
#include "musket_components.h"
#include <cmath>

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
// M3: VOLLEY COMBAT SYSTEMS (CORE_MATH.md §2)
// ═════════════════════════════════════════════════════════════

void register_combat_systems(flecs::world &ecs) {

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

  // ── System 4: Volley Fire (60Hz, fires when ready) ──────────
  // Simplified Inverse Sieve: each ready firer targets the
  // nearest living enemy within MAX_MUSKET_RANGE.
  // Full grid-walk sieve deferred to M3.5.
  ecs.system<const Position, MusketState, const FireOrder, const TeamId>(
         "VolleyFireSystem")
      .with<IsAlive>()
      .each([](flecs::entity e, const Position &pos, MusketState &ms,
               const FireOrder &fire, const TeamId &team) {
        // Not ready to fire yet
        if (ms.reload_timer > 0.0f)
          return;
        // Out of ammo
        if (ms.ammo_count == 0)
          return;

        constexpr float MAX_MUSKET_RANGE = 100.0f; // meters
        constexpr float BASE_ACCURACY = 0.35f;     // ~35% at point blank
        constexpr float RELOAD_TIME = 8.0f;        // seconds per shot
        constexpr float HUMIDITY_PENALTY = 0.05f;  // 5% misfire (clear day)

        // Trap 8+9 Fix: Use macro battalion centroid for O(B) targeting
        // instead of O(N) full-entity scan with leaked thread_local query.
        float best_dist_sq = MAX_MUSKET_RANGE * MAX_MUSKET_RANGE;
        flecs::entity best_target = flecs::entity::null();

        // Step 1: Find nearest enemy BATTALION centroid (O(256))
        int best_bat_id = -1;
        float best_macro_dist = 1e18f;
        for (int i = 0; i < MAX_BATTALIONS; i++) {
          if (g_macro_battalions[i].alive_count == 0)
            continue;
          if (g_macro_battalions[i].team_id == team.team)
            continue;
          float bdx = g_macro_battalions[i].cx - pos.x;
          float bdz = g_macro_battalions[i].cz - pos.z;
          float bd2 = bdx * bdx + bdz * bdz;
          if (bd2 < best_macro_dist) {
            best_macro_dist = bd2;
            best_bat_id = i;
          }
        }

        // Step 2: If nearest battalion is way out of range, skip
        if (best_bat_id == -1 ||
            best_macro_dist > (MAX_MUSKET_RANGE * MAX_MUSKET_RANGE * 4.0f))
          return;

        // Step 3: Find nearest individual in that battalion via system query
        // (ecs.system queries are safe cross-TU, no leak)
        flecs::world w = e.world();
        w.each([&](flecs::entity te, const Position &tp, const TeamId &tt,
                   const BattalionId &tb) {
          if (!te.has<IsAlive>())
            return;
          if (tt.team == team.team)
            return;
          if ((int)(tb.id % MAX_BATTALIONS) != best_bat_id)
            return;
          float tdx = tp.x - pos.x;
          float tdz = tp.z - pos.z;
          float td2 = tdx * tdx + tdz * tdz;
          if (td2 < best_dist_sq) {
            best_dist_sq = td2;
            best_target = te;
          }
        });

        if (!best_target.is_valid() || !best_target.is_alive())
          return;

        // M7 Phase C: Officer blind fire — dead officer = 40m range, 0.3x
        // accuracy
        uint32_t my_bat_id = 0;
        if (e.has<BattalionId>()) {
          my_bat_id = e.get<BattalionId>().id % MAX_BATTALIONS;
        }
        bool officer_alive = g_macro_battalions[my_bat_id].officer_alive;
        float current_max_range = officer_alive ? MAX_MUSKET_RANGE : 40.0f;

        float dist = std::sqrt(best_dist_sq);
        if (dist > current_max_range)
          return;

        float hit_chance = BASE_ACCURACY * (1.0f - (dist / current_max_range));
        hit_chance *= (1.0f - HUMIDITY_PENALTY);
        if (!officer_alive)
          hit_chance *= 0.3f; // Blind fire into smoke

        // Clamp to [0, 1]
        if (hit_chance < 0.0f)
          hit_chance = 0.0f;
        if (hit_chance > 1.0f)
          hit_chance = 1.0f;

        // Simple hash-based random from entity ID + world time
        // (deterministic, no <random> header needed)
        uint64_t seed =
            e.id() ^ (uint64_t)(w.get_info()->world_time_total * 100000.0);
        seed ^= seed >> 33;
        seed *= 0xff51afd7ed558ccdULL;
        seed ^= seed >> 33;
        float roll = (float)(seed & 0xFFFF) / 65535.0f;

        // Fire! Reset reload regardless of hit/miss
        ms.reload_timer = RELOAD_TIME;
        ms.ammo_count--;

        if (roll <= hit_chance) {
          // Kill the target — remove IsAlive tag
          best_target.remove<IsAlive>();
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

} // namespace musket
