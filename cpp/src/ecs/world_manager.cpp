#include "world_manager.h"
#include "musket_components.h"
#include "musket_systems.h"
#include "prefab_loader.h"
#include "rendering_bridge.h"
#include <cmath>
#include <cstdlib>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// ═══════════════════════════════════════════════════════════════
// GOLDEN TU: Array and centroid function MUST live here.
// ecs.each<> resolves Flecs component IDs from TU-local static
// template variables. These must share the TU where components
// are registered to get the correct IDs (MSVC TU mismatch rule).
// ═══════════════════════════════════════════════════════════════
MacroBattalion g_macro_battalions[MAX_BATTALIONS];
PendingOrder g_pending_orders[MAX_BATTALIONS];

static void compute_battalion_centroids(flecs::world &ecs) {
  float dt = ecs.get_info()->delta_time;

  // 1. Zero TRANSIENT data only (Trap 23: preserve flag_cohesion)
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    g_macro_battalions[i].cx = 0.0f;
    g_macro_battalions[i].cz = 0.0f;
    g_macro_battalions[i].alive_count = 0;
    g_macro_battalions[i].team_id = 999;
    g_macro_battalions[i].flag_alive = false;
    g_macro_battalions[i].drummer_alive = false;
    g_macro_battalions[i].officer_alive = false;
  }

  // 2. Accumulate + detect M7 command network tags
  ecs.each([](flecs::entity e, const Position &p, const BattalionId &b,
              const TeamId &t) {
    if (!e.has<IsAlive>())
      return;
    uint32_t id = b.id % MAX_BATTALIONS;
    g_macro_battalions[id].cx += p.x;
    g_macro_battalions[id].cz += p.z;
    g_macro_battalions[id].alive_count++;
    g_macro_battalions[id].team_id = t.team;

    // M7: Tag detection
    if (e.has<FormationAnchor>())
      g_macro_battalions[id].flag_alive = true;
    if (e.has<Drummer>())
      g_macro_battalions[id].drummer_alive = true;
    if (e.has<ElevatedLOS>())
      g_macro_battalions[id].officer_alive = true;
  });

  // 3. Finalize centroids + M7 pipelines + M7.5 targeting + fire discipline
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    auto &mb = g_macro_battalions[i];

    // Trap 24: Shatter command if almost wiped out
    if (mb.alive_count > 0 && mb.alive_count < 10) {
      mb.flag_alive = mb.drummer_alive = mb.officer_alive = false;
    }

    if (mb.alive_count > 0) {
      mb.cx /= (float)mb.alive_count;
      mb.cz /= (float)mb.alive_count;

      // Phase A: Flag cohesion decay (16s to 0.2 floor)
      if (mb.flag_alive) {
        mb.flag_cohesion = std::min(1.0f, mb.flag_cohesion + dt * 0.1f);
      } else {
        mb.flag_cohesion = std::max(0.2f, mb.flag_cohesion - dt * 0.05f);
      }

      // M7.5 §12.7: Dead officer = loss of fire discipline!
      if (!mb.officer_alive && mb.fire_discipline != DISCIPLINE_AT_WILL) {
        mb.fire_discipline = DISCIPLINE_AT_WILL;
      }

      // M7.5 §12.7: Officer's Metronome — tick the fire discipline timer
      if (mb.fire_discipline == DISCIPLINE_BY_RANK) {
        mb.volley_timer -= dt;
        if (mb.volley_timer <= 0.0f) {
          mb.active_firing_rank = (mb.active_firing_rank + 1) % 3;
          mb.volley_timer = 3.0f; // 3s between rank volleys
        }
      } else if (mb.fire_discipline == DISCIPLINE_MASS_VOLLEY) {
        mb.volley_timer -= dt;
        if (mb.volley_timer <= 0.0f) {
          mb.fire_discipline = DISCIPLINE_HOLD; // Window closed
        }
      }

      // ── M7.5 §12.8 Trap 26: Hoisted Macro Targeting ──
      // O(B²) total: find nearest unblocked enemy battalion for each battalion
      mb.target_bat_id = -1;
      float best_dist = 1e18f;

      for (int j = 0; j < MAX_BATTALIONS; j++) {
        auto &enemy = g_macro_battalions[j];
        if (enemy.alive_count == 0 || enemy.team_id == mb.team_id)
          continue;

        float edx = enemy.cx - mb.cx;
        float edz = enemy.cz - mb.cz;
        float ed2 = edx * edx + edz * edz;
        if (ed2 >= best_dist)
          continue;

        // Check if a FRIENDLY battalion's OBB blocks this shot path
        bool blocked = false;
        for (int fb = 0; fb < MAX_BATTALIONS; fb++) {
          if (fb == i || fb == j)
            continue;
          auto &f = g_macro_battalions[fb];
          if (f.alive_count == 0 || f.team_id != mb.team_id)
            continue;

          // OBB diagonals
          float rx = -f.dir_z * f.ext_w, rz = f.dir_x * f.ext_w;
          float fx = f.dir_x * f.ext_d, fz = f.dir_z * f.ext_d;
          // Diagonal 1: (cx-rx-fx) to (cx+rx+fx)
          float d1ax = f.cx - rx - fx, d1az = f.cz - rz - fz;
          float d1bx = f.cx + rx + fx, d1bz = f.cz + rz + fz;
          // Diagonal 2: (cx+rx-fx) to (cx-rx+fx)
          float d2ax = f.cx + rx - fx, d2az = f.cz + rz - fz;
          float d2bx = f.cx - rx + fx, d2bz = f.cz - rz + fz;

          // CCW segment intersection test (zero sqrt)
          auto ccw = [](float ax, float az, float bx, float bz, float cx,
                        float cz) {
            return (cz - az) * (bx - ax) > (bz - az) * (cx - ax);
          };
          auto seg_hit = [&](float ax, float az, float bx, float bz, float cx,
                             float cz, float dx, float dz) {
            return ccw(ax, az, cx, cz, dx, dz) != ccw(bx, bz, cx, cz, dx, dz) &&
                   ccw(ax, az, bx, bz, cx, cz) != ccw(ax, az, bx, bz, dx, dz);
          };

          if (seg_hit(mb.cx, mb.cz, enemy.cx, enemy.cz, d1ax, d1az, d1bx,
                      d1bz) ||
              seg_hit(mb.cx, mb.cz, enemy.cx, enemy.cz, d2ax, d2az, d2bx,
                      d2bz)) {
            blocked = true;
            break;
          }
        }

        if (!blocked && ed2 < best_dist) {
          best_dist = ed2;
          mb.target_bat_id = j;
        }
      }
    }

    // Phase D: Order Delay Pipeline
    auto &order = g_pending_orders[i];
    if (order.type != ORDER_NONE) {
      order.delay -= dt;
      if (order.delay <= 0.0f) {
        OrderType otype = order.type;
        float tx = order.target_x;
        float tz = order.target_z;

        if (otype == ORDER_DISCIPLINE) {
          // M7.5 §12.7: Fire discipline change
          mb.fire_discipline = (FireDiscipline)order.requested_discipline;
          if (mb.fire_discipline == DISCIPLINE_BY_RANK) {
            mb.active_firing_rank = 0;
            mb.volley_timer = 3.0f;
          } else if (mb.fire_discipline == DISCIPLINE_MASS_VOLLEY) {
            mb.volley_timer = 0.5f; // 0.5s execution window
          }
        } else {
          // Dispatch to ECS entities in this battalion
          ecs.each([&](flecs::entity e, const BattalionId &b) {
            if ((int)(b.id % MAX_BATTALIONS) != i || !e.has<IsAlive>())
              return;
            if (e.has<CavalryState>()) {
              const auto &cs = e.get<CavalryState>();
              if (cs.state_flags != 0)
                return;
            }

            if (otype == ORDER_MARCH && e.has<SoldierFormationTarget>()) {
              const auto &st = e.get<SoldierFormationTarget>();
              e.set<MovementOrder>({(float)(tx + st.target_x),
                                    (float)(tz + st.target_z), false});
            } else if (otype == ORDER_FIRE) {
              e.set<FireOrder>({tx, tz});
            }
          });
        }
        order.type = ORDER_NONE;
      }
    }
  }

  // Diagnostic (every 2s at 60Hz)
  static int tick = 0;
  if (tick++ % 120 == 0) {
    godot::UtilityFunctions::print(
        "[CENTROIDS] Bat0: alive=", g_macro_battalions[0].alive_count,
        " flag=", g_macro_battalions[0].flag_alive,
        " drum=", g_macro_battalions[0].drummer_alive,
        " cohesion=", g_macro_battalions[0].flag_cohesion,
        " | Bat1: alive=", g_macro_battalions[1].alive_count);
  }
}

namespace godot {

MusketServer::MusketServer() {}

MusketServer::~MusketServer() {}

void MusketServer::_bind_methods() {
  // M1: Core
  ClassDB::bind_method(D_METHOD("spawn_test_battalion", "count", "center_x",
                                "center_z", "team_id"),
                       &MusketServer::spawn_test_battalion);
  ClassDB::bind_method(D_METHOD("get_transform_buffer"),
                       &MusketServer::get_transform_buffer);
  ClassDB::bind_method(D_METHOD("get_visible_count"),
                       &MusketServer::get_visible_count);
  ClassDB::bind_method(D_METHOD("order_march", "target_x", "target_z"),
                       &MusketServer::order_march);
  ClassDB::bind_method(
      D_METHOD("order_fire", "team_id", "target_x", "target_z"),
      &MusketServer::order_fire);
  ClassDB::bind_method(D_METHOD("get_alive_count", "team_id"),
                       &MusketServer::get_alive_count);

  // M5: Artillery
  ClassDB::bind_method(
      D_METHOD("spawn_test_battery", "num_guns", "x", "z", "team_id"),
      &MusketServer::spawn_test_battery);
  ClassDB::bind_method(
      D_METHOD("order_artillery_fire", "team_id", "target_x", "target_z"),
      &MusketServer::order_artillery_fire);
  ClassDB::bind_method(D_METHOD("order_limber", "team_id"),
                       &MusketServer::order_limber);
  ClassDB::bind_method(D_METHOD("order_unlimber", "team_id"),
                       &MusketServer::order_unlimber);
  ClassDB::bind_method(D_METHOD("get_projectile_buffer"),
                       &MusketServer::get_projectile_buffer);
  ClassDB::bind_method(D_METHOD("get_projectile_count"),
                       &MusketServer::get_projectile_count);

  // M6: Battalion Rendering
  ClassDB::bind_method(D_METHOD("get_active_battalions"),
                       &MusketServer::get_active_battalions);
  ClassDB::bind_method(D_METHOD("get_battalion_buffer", "battalion_id"),
                       &MusketServer::get_battalion_buffer);
  ClassDB::bind_method(D_METHOD("get_battalion_instance_count", "battalion_id"),
                       &MusketServer::get_battalion_instance_count);

  // M6: Cavalry
  ClassDB::bind_method(
      D_METHOD("spawn_test_cavalry", "count", "x", "z", "team_id"),
      &MusketServer::spawn_test_cavalry);
  ClassDB::bind_method(
      D_METHOD("order_charge", "team_id", "target_x", "target_z"),
      &MusketServer::order_charge);

  // M7.5: Fire Discipline + Formation API
  ClassDB::bind_method(
      D_METHOD("order_fire_discipline", "battalion_id", "discipline_enum"),
      &MusketServer::order_fire_discipline);
  ClassDB::bind_method(
      D_METHOD("order_formation", "battalion_id", "shape_enum"),
      &MusketServer::order_formation);
}

void MusketServer::_ready() {
  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }
  init_ecs();
}

void MusketServer::init_ecs() {
  UtilityFunctions::print("[MusketEngine] Initializing ECS...");

  // Register core components
  ecs.component<Position>("Position");
  ecs.component<Velocity>("Velocity");
  ecs.component<Height>("Height");
  ecs.component<IsAlive>("IsAlive");
  ecs.component<Routing>("Routing");
  ecs.component<TeamId>("TeamId");
  ecs.component<BattalionId>("BattalionId");
  ecs.component<SoldierFormationTarget>("SoldierFormationTarget");
  ecs.component<MovementStats>("MovementStats");
  ecs.component<MovementOrder>("MovementOrder");
  ecs.component<MusketState>("MusketState");
  ecs.component<FireOrder>("FireOrder");
  ecs.component<CavalryState>("CavalryState");
  ecs.component<Workplace>("Workplace");

  // M5: Artillery components
  ecs.component<ArtilleryShot>("ArtilleryShot");
  ecs.component<ArtilleryBattery>("ArtilleryBattery");

  // M6: Rendering + Cavalry components
  ecs.component<RenderSlot>("RenderSlot");
  ecs.component<FormationDefense>("FormationDefense");
  ecs.component<ChargeOrder>("ChargeOrder");
  ecs.component<Disordered>("Disordered");

  // M7: Command Network components
  ecs.component<FormationAnchor>("FormationAnchor");
  ecs.component<Drummer>("Drummer");
  ecs.component<ElevatedLOS>("ElevatedLOS");

  // Register M2 movement systems
  musket::register_movement_systems(ecs);

  // Initialize M8 spatial hash grid singleton (heap-allocated: 4.2MB)
  // Must come before register_combat_systems which registers the rebuild
  // system.
  {
    auto *shg = new SpatialHashGrid();
    memset(shg, 0, sizeof(SpatialHashGrid));
    memset(shg->cell_head, -1, sizeof(shg->cell_head));
    ecs.set<SpatialHashGrid>(*shg);
    delete shg;
  }

  // Register M3+M8 combat systems
  musket::register_combat_systems(ecs);

  // Initialize M4 panic grid singleton (zero-initialized)
  ecs.set<PanicGrid>({});

  // Register M4 panic systems (must come after PanicGrid singleton)
  musket::register_panic_systems(ecs);

  // Register M5 artillery systems
  musket::register_artillery_systems(ecs);

  // Register M6 cavalry systems
  musket::register_cavalry_systems(ecs);
  musket::register_death_clear_observer(ecs);

  // Initialize M9 economy singletons
  ecs.set<CivicGrid>({});
  ecs.set<GlobalZeitgeist>({});

  // Register M9 economy systems
  musket::register_economy_systems(ecs);

  // Load JSON prefabs
  musket::load_all_prefabs(ecs);

  UtilityFunctions::print("[MusketEngine] ECS ready — systems registered.");
}

void MusketServer::spawn_test_battalion(int count, float center_x,
                                        float center_z, int team_id) {
  uint32_t bat_id = next_battalion_id++;

  UtilityFunctions::print("[MusketEngine] Spawning battalion #", bat_id, " (",
                          count, " soldiers, team ", team_id, ") at (",
                          center_x, ", ", center_z, ")");

  // Activate the battalion shadow buffer
  auto &bat = musket::get_battalion(bat_id);
  bat.active = true;

  // M7.5: True Napoleonic Line — 3 ranks deep (§12.1)
  constexpr int RANKS = 3;
  constexpr float SP_X = 0.8f; // 0.8m shoulder-to-shoulder
  constexpr float SP_Z = 1.2f; // 1.2m depth between ranks
  int cols = (int)std::ceil((float)count / RANKS);

  // M7.5: Set initial OBB geometry for this battalion
  auto &mb = g_macro_battalions[bat_id % MAX_BATTALIONS];
  mb.dir_x = 0.0f;
  mb.dir_z = -1.0f;                        // Facing -Z (Godot forward)
  mb.ext_w = (cols * SP_X) / 2.0f + 2.0f;  // Half-width + 2m buffer
  mb.ext_d = (RANKS * SP_Z) / 2.0f + 2.0f; // Half-depth + 2m buffer

  // Center offsets for perfectly centering the formation
  float start_x = center_x - ((cols - 1) * SP_X) / 2.0f;
  float start_z = center_z - ((RANKS - 1) * SP_Z) / 2.0f;

  int center_col = cols / 2;

  for (int i = 0; i < count; i++) {
    int row = i % RANKS; // 0=Front, 1=Middle, 2=Rear
    int col = i / RANKS; // 0..166

    float x = start_x + col * SP_X;
    float z = start_z + row * SP_Z;

    // Micro-jitter to avoid robotic grid
    float jx = ((float)(std::rand() % 100) / 100.0f - 0.5f) * 0.15f;
    float jz = ((float)(std::rand() % 100) / 100.0f - 0.5f) * 0.15f;

    // Allocate a stable rendering slot
    uint32_t mm_slot = bat.alloc_slot();

    auto e = ecs.entity()
                 .set<Position>({x + jx, z + jz})
                 .set<Velocity>({0.0f, 0.0f})
                 .set<SoldierFormationTarget>({(double)x,
                                               (double)z,
                                               50.0f,
                                               2.0f,
                                               0.0f,
                                               -1.0f, // Face forward (-Z)
                                               true,
                                               (uint8_t)row,
                                               {}})
                 .set<MovementStats>({4.0f, 8.0f})
                 .set<TeamId>({(uint8_t)team_id})
                 .set<BattalionId>({bat_id})
                 .set<MusketState>(
                     {0.0f, 30, 13}) // Trap 28: no stagger, all start loaded
                 .set<FormationDefense>({0.2f}) // Line formation by default
                 .set<RenderSlot>({bat_id, mm_slot})
                 .add<IsAlive>();

    // M7.5: Embed command staff in center file (§12.2)
    if (col == center_col) {
      if (row == 0)
        e.add<ElevatedLOS>(); // Officer: Front rank
      if (row == 1)
        e.add<FormationAnchor>(); // Flag: Middle rank (protected)
      if (row == 2)
        e.add<Drummer>(); // Drummer: Rear rank
    }
  }

  UtilityFunctions::print("[MusketEngine] Battalion #", bat_id,
                          " spawned: ", count, " soldiers (3-rank line, ", cols,
                          " files wide).");
}

void MusketServer::order_march(float target_x, float target_z) {
  UtilityFunctions::print("[MusketEngine] March order → (", target_x, ", ",
                          target_z, ")");

  // M7: Route through pending order pipeline for ALL battalions
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    if (g_macro_battalions[i].alive_count == 0)
      continue;
    g_pending_orders[i].type = ORDER_MARCH;
    g_pending_orders[i].target_x = target_x;
    g_pending_orders[i].target_z = target_z;
    // Last-write-wins with penalty reset (Deep Think ruling #3)
    g_pending_orders[i].delay =
        g_macro_battalions[i].drummer_alive ? 2.0f : 8.0f;
  }
}

void MusketServer::order_fire(int team_id, float target_x, float target_z) {
  UtilityFunctions::print("[MusketEngine] Fire order (team ", team_id, ") → (",
                          target_x, ", ", target_z, ")");

  // M7: Route through pending order pipeline for matching team
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    if (g_macro_battalions[i].alive_count == 0)
      continue;
    if (g_macro_battalions[i].team_id != (uint32_t)team_id)
      continue;
    g_pending_orders[i].type = ORDER_FIRE;
    g_pending_orders[i].target_x = target_x;
    g_pending_orders[i].target_z = target_z;
    g_pending_orders[i].delay =
        g_macro_battalions[i].drummer_alive ? 2.0f : 8.0f;
  }
}

int MusketServer::get_alive_count(int team_id) const {
  int count = 0;
  flecs::world &w = const_cast<flecs::world &>(ecs);
  auto q = w.query_builder<const TeamId>().with<IsAlive>().build();

  q.each([&](flecs::entity e, const TeamId &t) {
    if (t.team == (uint8_t)team_id) {
      count++;
    }
  });
  return count;
}

PackedFloat32Array MusketServer::get_transform_buffer() const {
  return transform_buffer;
}

int MusketServer::get_visible_count() const { return visible_count; }

void MusketServer::_process(double delta) {
  if (Engine::get_singleton()->is_editor_hint()) {
    return;
  }

  // Pre-pass: battalion centroids (GOLDEN TU — same TU as component
  // registration)
  compute_battalion_centroids(ecs);

  // Tick the ECS world
  ecs.progress(delta);

  // ── DUAL WRITE (Strangler Fig Migration) ──
  // Legacy path: sequential repack for old GDScript code
  musket::sync_transforms(ecs, transform_buffer, visible_count);

  // New path: stable slot writes to battalion shadow buffers
  musket::sync_battalion_transforms(ecs);

  // M5: Projectile sync
  musket::sync_projectiles(ecs, projectile_buffer, projectile_count);
}

// ═══════════════════════════════════════════════════════════════
// M5: Artillery API
// ═══════════════════════════════════════════════════════════════

void MusketServer::spawn_test_battery(int num_guns, float x, float z,
                                      int team_id) {
  UtilityFunctions::print("[MusketEngine] Spawning battery (", num_guns,
                          " guns, team ", team_id, ") at (", x, ", ", z, ")");

  ecs.entity()
      .set<Position>({x, z})
      .set<Velocity>({0.0f, 0.0f})
      .set<TeamId>({(uint8_t)team_id})
      .set<ArtilleryBattery>({num_guns, 0.0f, 0.0f, 50, 20, false, 0.0f});
}

void MusketServer::order_artillery_fire(int team_id, float target_x,
                                        float target_z) {
  UtilityFunctions::print("[MusketEngine] Artillery fire (team ", team_id,
                          ") → (", target_x, ", ", target_z, ")");

  auto q = ecs.query_builder<const TeamId, const ArtilleryBattery>().build();

  q.each([&](flecs::entity e, const TeamId &t, const ArtilleryBattery &bat) {
    if (t.team == (uint8_t)team_id) {
      e.set<FireOrder>({target_x, target_z});
    }
  });
}

void MusketServer::order_limber(int team_id) {
  UtilityFunctions::print("[MusketEngine] Limber order (team ", team_id, ")");

  auto q = ecs.query_builder<ArtilleryBattery, const TeamId>().build();

  q.each([&](flecs::entity e, ArtilleryBattery &bat, const TeamId &t) {
    if (t.team == (uint8_t)team_id) {
      bat.is_limbered = true;
      bat.unlimber_timer = 0.0f;
    }
  });
}

void MusketServer::order_unlimber(int team_id) {
  UtilityFunctions::print("[MusketEngine] Unlimber order (team ", team_id, ")");

  auto q = ecs.query_builder<ArtilleryBattery, const TeamId>().build();

  q.each([&](flecs::entity e, ArtilleryBattery &bat, const TeamId &t) {
    if (t.team == (uint8_t)team_id && bat.is_limbered) {
      bat.unlimber_timer = 60.0f;
    }
  });
}

PackedFloat32Array MusketServer::get_projectile_buffer() const {
  return projectile_buffer;
}

int MusketServer::get_projectile_count() const { return projectile_count; }

// ═══════════════════════════════════════════════════════════════
// M6: Battalion Rendering API
// ═══════════════════════════════════════════════════════════════

PackedInt32Array MusketServer::get_active_battalions() const {
  return musket::get_active_battalion_ids();
}

PackedFloat32Array MusketServer::get_battalion_buffer(int battalion_id) const {
  const auto &bat = musket::get_battalion((uint32_t)battalion_id);
  return bat.buffer;
}

int MusketServer::get_battalion_instance_count(int battalion_id) const {
  const auto &bat = musket::get_battalion((uint32_t)battalion_id);
  return bat.max_allocated;
}

// ═══════════════════════════════════════════════════════════════
// M6: Cavalry API
// ═══════════════════════════════════════════════════════════════

void MusketServer::spawn_test_cavalry(int count, float x, float z,
                                      int team_id) {
  uint32_t bat_id = next_battalion_id++;

  UtilityFunctions::print("[MusketEngine] Spawning cavalry battalion #", bat_id,
                          " (", count, " riders, team ", team_id, ") at (", x,
                          ", ", z, ")");

  auto &bat = musket::get_battalion(bat_id);
  bat.active = true;

  int cols = 10;
  float spacing = 2.0f; // Wider spacing for cavalry

  for (int i = 0; i < count; i++) {
    int row = i / cols;
    int col = i % cols;

    float cx = x + (col - cols / 2) * spacing;
    float cz = z + row * spacing;

    float jx = ((float)(std::rand() % 100) / 100.0f - 0.5f) * 0.5f;
    float jz = ((float)(std::rand() % 100) / 100.0f - 0.5f) * 0.5f;

    uint32_t mm_slot = bat.alloc_slot();

    ecs.entity()
        .set<Position>({cx + jx, cz + jz})
        .set<Velocity>({0.0f, 0.0f})
        .set<SoldierFormationTarget>(
            {(double)cx, (double)cz, 30.0f, 1.5f, 0.0f, -1.0f, false, 0, {}})
        .set<MovementStats>({4.0f, 12.0f}) // Walk 4, Charge 12
        .set<TeamId>({(uint8_t)team_id})
        .set<BattalionId>({bat_id})
        .set<CavalryState>({0.0f, 0.0f, 0.0f, 0.0f, 0, 0})
        .set<RenderSlot>({bat_id, mm_slot})
        .add<IsAlive>();
  }

  UtilityFunctions::print("[MusketEngine] Cavalry battalion #", bat_id,
                          " spawned: ", count, " riders.");
}

void MusketServer::order_charge(int team_id, float target_x, float target_z) {
  // Find cavalry centroid for this team
  float cav_cx = 0, cav_cz = 0;
  int cav_count = 0;

  // EPHEMERAL EACH: Searches instantly, leaves zero memory footprint!
  ecs.each([&](flecs::entity e, const Position &p, CavalryState &cs,
               const TeamId &t) {
    if (!e.has<IsAlive>())
      return;
    if (t.team == (uint8_t)team_id) {
      cav_cx += p.x;
      cav_cz += p.z;
      cav_count++;
    }
  });

  if (cav_count == 0) {
    UtilityFunctions::print("[MusketEngine] No cavalry alive on team ",
                            team_id);
    return;
  }
  cav_cx /= cav_count;
  cav_cz /= cav_count;

  // Find Nearest Enemy Battalion safely using the cached Team ID
  float best_dist = 999999.0f;
  int best_target = -1;

  for (int i = 0; i < MAX_BATTALIONS; i++) {
    auto &mb = g_macro_battalions[i];
    if (mb.alive_count == 0)
      continue;
    if (mb.team_id == (uint32_t)team_id)
      continue; // Safely skip allies!

    float dx = mb.cx - cav_cx;
    float dz = mb.cz - cav_cz;
    float d2 = (dx * dx) + (dz * dz);

    if (d2 < best_dist) {
      best_dist = d2;
      best_target = i;
    }
  }

  if (best_target != -1) {
    UtilityFunctions::print("[MusketEngine] Charge → bat ", best_target);

    float target_cx = g_macro_battalions[best_target].cx;
    float target_cz = g_macro_battalions[best_target].cz;

    int committed = 0;
    ecs.each([&](flecs::entity e, const Position &p, CavalryState &cs,
                 const TeamId &t) {
      if (!e.has<IsAlive>())
        return;
      if (t.team != (uint8_t)team_id)
        return;
      if (cs.state_flags != 0)
        return; // Skip already charging/disordered

      // Compute parallel charge vector toward enemy centroid
      float dx = target_cx - p.x;
      float dz = target_cz - p.z;
      float dist = std::sqrt(dx * dx + dz * dz);
      if (dist < 0.01f)
        return;

      cs.lock_dir_x = dx / dist;
      cs.lock_dir_z = dz / dist;
      cs.state_flags = 1; // → Charging
      cs.state_timer = 0.0f;
      cs.charge_momentum = 0.0f;

      ChargeOrder order;
      order.target_battalion_id = best_target;
      order.is_committed = true;
      e.set<ChargeOrder>(order);
      committed++;
    });
    UtilityFunctions::print("[MusketEngine] ", committed,
                            " cavalry committed to charge");
  } else {
    UtilityFunctions::print(
        "[MusketEngine] Charge -> fallback coords (No enemies found)");
  }
}

// ═══════════════════════════════════════════════════════════
// M7.5: FIRE DISCIPLINE + FORMATION API
// ═══════════════════════════════════════════════════════════

void MusketServer::order_fire_discipline(int battalion_id,
                                         int discipline_enum) {
  if (battalion_id < 0 || battalion_id >= MAX_BATTALIONS)
    return;
  if (discipline_enum < 0 || discipline_enum > 3)
    return;

  // Feed into M7 Drummer Latency Pipeline
  g_pending_orders[battalion_id].type = ORDER_DISCIPLINE;
  g_pending_orders[battalion_id].requested_discipline =
      (uint8_t)discipline_enum;
  g_pending_orders[battalion_id].delay =
      g_macro_battalions[battalion_id].drummer_alive ? 2.0f : 8.0f;

  UtilityFunctions::print("[MusketEngine] Fire discipline → bat ", battalion_id,
                          " discipline=", discipline_enum);
}

void MusketServer::order_formation(int battalion_id, int shape_enum) {
  if (battalion_id < 0 || battalion_id >= MAX_BATTALIONS)
    return;

  auto &mb = g_macro_battalions[battalion_id];
  if (mb.alive_count == 0)
    return;

  FormationShape shape = (FormationShape)shape_enum;

  // M7.5 §12.1: Formation geometry constants
  constexpr float SP_X = 0.8f;
  constexpr float SP_Z = 1.2f;
  constexpr int RANKS = 3;

  int N = mb.alive_count; // Includes command staff
  float cx = mb.cx;
  float cz = mb.cz;
  float dir_x = mb.dir_x;
  float dir_z = mb.dir_z;

  // Calculate formation dimensions
  int cols, ranks;
  float defense, speed;
  bool front_only_shoot = false;

  if (shape == SHAPE_LINE) {
    ranks = RANKS;
    cols = (int)std::ceil((float)N / ranks);
    defense = 0.2f;
    speed = 4.0f;
  } else if (shape == SHAPE_COLUMN) {
    cols = 16; // 16-wide column
    ranks = (int)std::ceil((float)N / cols);
    defense = 0.5f;
    speed = 6.0f;
    front_only_shoot = true;
  } else { // SHAPE_SQUARE
    int per_side = (int)std::ceil((float)N / 4.0f);
    cols = per_side;
    ranks = per_side;
    defense = 0.9f;
    speed = 2.0f;
    front_only_shoot = true;
  }

  // Update OBB extents (persistent)
  mb.ext_w = (cols * SP_X) / 2.0f + 2.0f;
  mb.ext_d = (ranks * SP_Z) / 2.0f + 2.0f;

  // Universal rotation helper: local offset → global coordinates
  auto rotate = [&](float lx, float lz, float &gx, float &gz) {
    gx = -lx * dir_z - lz * dir_x;
    gz = lx * dir_x - lz * dir_z;
  };

  // Trap 29: Running index inside ecs.each() — zero heap allocation
  int slot = 0;
  ecs.each([&](flecs::entity e, const BattalionId &b,
               SoldierFormationTarget &tgt, FormationDefense &fd) {
    if ((int)(b.id % MAX_BATTALIONS) != battalion_id || !e.has<IsAlive>())
      return;

    // Determine local offset based on shape
    float ox = 0.0f, oz = 0.0f;
    float local_aim_x = 0.0f, local_aim_z = -1.0f;
    int r = 0;
    bool can_shoot = true;

    if (shape == SHAPE_LINE) {
      r = slot % RANKS;
      int c = slot / RANKS;
      ox = (c - cols / 2) * SP_X;
      oz = r * SP_Z;
    } else if (shape == SHAPE_COLUMN) {
      r = slot / cols;
      int c = slot % cols;
      ox = (c - cols / 2) * SP_X;
      oz = r * SP_Z;
      can_shoot = (r == 0); // Only front rank fires
    } else {                // SHAPE_SQUARE
      int side = slot % 4;
      int pos_on_side = slot / 4;
      int per_side = (int)std::ceil((float)N / 4.0f);
      r = pos_on_side % RANKS;

      float half = (per_side * SP_X) / 2.0f;
      float depth = r * SP_Z;
      if (side == 0) {
        ox = (pos_on_side - per_side / 2) * SP_X;
        oz = -half - depth;
        local_aim_x = 0.0f;
        local_aim_z = -1.0f;
      } else if (side == 1) {
        ox = half + depth;
        oz = (pos_on_side - per_side / 2) * SP_X;
        local_aim_x = 1.0f;
        local_aim_z = 0.0f;
      } else if (side == 2) {
        ox = (pos_on_side - per_side / 2) * SP_X;
        oz = half + depth;
        local_aim_x = 0.0f;
        local_aim_z = 1.0f;
      } else {
        ox = -half - depth;
        oz = (pos_on_side - per_side / 2) * SP_X;
        local_aim_x = -1.0f;
        local_aim_z = 0.0f;
      }
      can_shoot = (r == 0); // Only outermost rank fires per face
    }

    // Rotate to global
    float gx, gz, gax, gaz;
    rotate(ox, oz, gx, gz);
    rotate(local_aim_x, local_aim_z, gax, gaz);

    tgt.target_x = (double)(cx + gx);
    tgt.target_z = (double)(cz + gz);
    tgt.face_dir_x = gax;
    tgt.face_dir_z = gaz;
    tgt.can_shoot = can_shoot;
    tgt.rank_index = (uint8_t)r;
    fd.defense = defense;

    slot++;
  });

  UtilityFunctions::print("[MusketEngine] Formation → bat ", battalion_id,
                          " shape=", shape_enum, " (", slot, " soldiers)");
}

} // namespace godot
