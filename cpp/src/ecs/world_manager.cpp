#include "world_manager.h"
#include "musket_components.h"
#include "musket_systems.h"
#include "prefab_loader.h"
#include "rendering_bridge.h"
#include <cmath>
#include <cstdlib>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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

  // Register M2 movement systems
  musket::register_movement_systems(ecs);

  // Register M3 combat systems
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

  int cols = 20;
  float spacing = 1.5f;

  for (int i = 0; i < count; i++) {
    int row = i / cols;
    int col = i % cols;

    float x = center_x + (col - cols / 2) * spacing;
    float z = center_z + row * spacing;

    // Small random jitter so they don't look robotic
    float jx = ((float)(std::rand() % 100) / 100.0f - 0.5f) * 0.3f;
    float jz = ((float)(std::rand() % 100) / 100.0f - 0.5f) * 0.3f;

    // Allocate a stable rendering slot
    uint32_t mm_slot = bat.alloc_slot();

    ecs.entity()
        .set<Position>({x + jx, z + jz})
        .set<Velocity>({0.0f, 0.0f})
        .set<SoldierFormationTarget>({x, z, 50.0f, 2.0f})
        .set<MovementStats>({4.0f, 8.0f})
        .set<TeamId>({(uint8_t)team_id})
        .set<MusketState>({0.0f, 30, 13})
        .set<FormationDefense>({0.2f}) // Line formation by default
        .set<RenderSlot>({bat_id, mm_slot})
        .add<IsAlive>();
  }

  UtilityFunctions::print("[MusketEngine] Battalion #", bat_id,
                          " spawned: ", count,
                          " entities with stable MM slots.");
}

void MusketServer::order_march(float target_x, float target_z) {
  UtilityFunctions::print("[MusketEngine] March order → (", target_x, ", ",
                          target_z, ")");

  auto q = ecs.query_builder<const Position>().with<IsAlive>().build();

  q.each([&](flecs::entity e, const Position &p) {
    if (e.has<SoldierFormationTarget>()) {
      const SoldierFormationTarget &t = e.get<SoldierFormationTarget>();
      e.set<MovementOrder>(
          {target_x + t.target_x, target_z + t.target_z, false});
    }
  });
}

void MusketServer::order_fire(int team_id, float target_x, float target_z) {
  UtilityFunctions::print("[MusketEngine] Fire order (team ", team_id, ") → (",
                          target_x, ", ", target_z, ")");

  auto q = ecs.query_builder<const TeamId>()
               .with<IsAlive>()
               .with<MusketState>()
               .build();

  q.each([&](flecs::entity e, const TeamId &t) {
    if (t.team == (uint8_t)team_id) {
      e.set<FireOrder>({target_x, target_z});
    }
  });
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
        .set<SoldierFormationTarget>({cx, cz, 30.0f, 1.5f})
        .set<MovementStats>({4.0f, 12.0f}) // Walk 4, Charge 12
        .set<TeamId>({(uint8_t)team_id})
        .set<CavalryState>({0.0f, 0.0f, 0.0f, 0.0f, 0, 0})
        .set<RenderSlot>({bat_id, mm_slot})
        .add<IsAlive>();
  }

  UtilityFunctions::print("[MusketEngine] Cavalry battalion #", bat_id,
                          " spawned: ", count, " riders.");
}

void MusketServer::order_charge(int team_id, float target_x, float target_z) {
  UtilityFunctions::print("[MusketEngine] Charge order (team ", team_id,
                          ") → (", target_x, ", ", target_z, ")");

  auto q = ecs.query_builder<const Position, CavalryState, const TeamId>()
               .with<IsAlive>()
               .build();

  q.each([&](flecs::entity e, const Position &p, CavalryState &cs,
             const TeamId &t) {
    if (t.team != (uint8_t)team_id)
      return;

    // Compute normalized direction from current position to target
    float dx = target_x - p.x;
    float dz = target_z - p.z;
    float dist = std::sqrt(dx * dx + dz * dz);

    if (dist > 0.01f) {
      cs.lock_dir_x = dx / dist;
      cs.lock_dir_z = dz / dist;
    } else {
      cs.lock_dir_x = 0.0f;
      cs.lock_dir_z = 1.0f; // Default forward
    }

    cs.state_flags = 1; // CHARGING
    cs.state_timer = 0.0f;
    cs.charge_momentum = 0.0f;
    e.add<ChargeOrder>();
  });
}

} // namespace godot
