#include "simulation_server.h"
#include "ecs/musket_components.h"
#include "gpu_tactical_map.h"
#include "influence_map.h"
#include "pheromone_map_cpp.h"
#include "tactical_cover_map.h"
#include "voxel_materials.h"
#include "voxel_world.h"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <iostream>

#ifdef _MSC_VER
#include <intrin.h>
#endif

using namespace godot;

SimulationServer *SimulationServer::_singleton = nullptr;

// ═══════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════

SimulationServer::SimulationServer() { _singleton = this; }

SimulationServer::~SimulationServer() {
  if (_singleton == this) {
    _singleton = nullptr;
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::setup(float map_w, float map_h) {
  // Register Musket Engine logic
  godot::ecs::register_musket_systems(ecs);

  _map_w = map_w;
  _map_h = map_h;
  _map_half_w = map_w * 0.5f;
  _map_half_h = map_h * 0.5f;

  // Spatial hash grid dimensions
  _spatial_w = std::max(1, (int)std::ceil(map_w / (float)SPATIAL_CELL_M));
  _spatial_h = std::max(1, (int)std::ceil(map_h / (float)SPATIAL_CELL_M));
  _spatial_cells.resize(_spatial_w * _spatial_h, -1);
  _spatial_next.resize(MAX_UNITS, -1);

  // Pre-allocate all SoA arrays to MAX_UNITS
  _flecs_id.resize(MAX_UNITS);
  _pos_x.resize(MAX_UNITS, 0.0f);
  _pos_y.resize(MAX_UNITS, 0.0f);
  _pos_z.resize(MAX_UNITS, 0.0f);
  _vel_x.resize(MAX_UNITS, 0.0f);
  _vel_y.resize(MAX_UNITS, 0.0f);
  _vel_z.resize(MAX_UNITS, 0.0f);
  _face_x.resize(MAX_UNITS, 0.0f);
  _face_z.resize(MAX_UNITS, 1.0f);
  _actual_vx.resize(MAX_UNITS, 0.0f);
  _actual_vz.resize(MAX_UNITS, 0.0f);
  // Context steering SoA
  _steer_interest.resize(MAX_UNITS * STEER_SLOTS, 0.0f);
  _steer_danger.resize(MAX_UNITS * STEER_SLOTS, 0.0f);
  _move_mode.resize(MAX_UNITS, MMODE_COMBAT); // default: combat movement
  _noise_level.resize(MAX_UNITS, NOISE_TABLE[MMODE_COMBAT]);
  _climb_target_y.resize(MAX_UNITS, 0.0f);
  _climb_dest_x.resize(MAX_UNITS, 0.0f);
  _climb_dest_z.resize(MAX_UNITS, 0.0f);
  _fall_start_y.resize(MAX_UNITS, 0.0f);
  _climb_cooldown.resize(MAX_UNITS, 0.0f);

  _health.resize(MAX_UNITS, 1.0f);
  _morale.resize(MAX_UNITS, 1.0f);
  _suppression.resize(MAX_UNITS, 0.0f);
  _attack_range.resize(MAX_UNITS, 30.0f);
  _attack_timer.resize(MAX_UNITS, 0.0f);
  _attack_cooldown.resize(MAX_UNITS, 0.5f);
  _accuracy.resize(MAX_UNITS, 0.5f);
  _ammo.resize(MAX_UNITS, 30);
  _mag_size.resize(MAX_UNITS, 30);

  _team.resize(MAX_UNITS, 0);
  _role.resize(MAX_UNITS, ROLE_RIFLEMAN);
  _squad_id.resize(MAX_UNITS, 0);
  _state.resize(MAX_UNITS, ST_IDLE);
  _alive.resize(MAX_UNITS, false);

  _personality.resize(MAX_UNITS, PERS_STEADY);
  _frozen_timer.resize(MAX_UNITS, 0.0f);
  _anim_phase.resize(MAX_UNITS, 0.0f);
  _squad_member_idx.resize(MAX_UNITS, 0);

  _target_id.resize(MAX_UNITS, -1);

  _order.resize(MAX_UNITS, ORDER_NONE);
  _order_x.resize(MAX_UNITS, 0.0f);
  _order_y.resize(MAX_UNITS, 0.0f);
  _order_z.resize(MAX_UNITS, 0.0f);
  _order_target_id.resize(MAX_UNITS, -1);

  _decision_timer.resize(MAX_UNITS, 0.0f);
  _reload_timer.resize(MAX_UNITS, 0.0f);
  _settle_timer.resize(MAX_UNITS, 0.0f);
  _deploy_timer.resize(MAX_UNITS, 0.0f);
  _mode_transition_timer.resize(MAX_UNITS, 0.0f);
  _aim_quality.resize(MAX_UNITS, 1.0f);

  // Tactical AI SoA
  _target_score.resize(MAX_UNITS, 0.0f);
  _target_suppressive.resize(MAX_UNITS, false);
  _attackers_count.resize(MAX_UNITS, 0);
  _cover_value.resize(MAX_UNITS, 0.0f);
  _nearby_squad_count.resize(MAX_UNITS, 0);
  _has_visible_enemy.resize(MAX_UNITS, false);

  // Peek behavior SoA
  _peek_timer.resize(MAX_UNITS, 0.0f);
  _peek_offset_x.resize(MAX_UNITS, 0.0f);
  _peek_offset_z.resize(MAX_UNITS, 0.0f);
  _is_peeking.resize(MAX_UNITS, false);
  _peek_side.resize(MAX_UNITS, 1);

  // Posture SoA
  _posture.resize(MAX_UNITS, POST_STAND);
  _posture_target.resize(MAX_UNITS, POST_STAND);
  _posture_timer.resize(MAX_UNITS, 0.0f);

  // Visibility / Fog of War SoA
  _last_seen_time.resize(MAX_UNITS, -100.0f); // never seen
  _last_known_x.resize(MAX_UNITS, 0.0f);
  _last_known_z.resize(MAX_UNITS, 0.0f);
  _detect_range.resize(MAX_UNITS, 60.0f);
  std::memset(_team_vis, 0, sizeof(_team_vis));
  _vis_cursor = 0;
  _game_time = 0.0f;

  // Pheromone tracking SoA
  _sustained_fire_timer.resize(MAX_UNITS, 0.0f);
  _survived_supp_timer.resize(MAX_UNITS, 0.0f);
  _prev_pos_x.resize(MAX_UNITS, 0.0f);
  _prev_pos_z.resize(MAX_UNITS, 0.0f);

  _count = 0;
  _alive_count = 0;

  // Initialize impact/muzzle event pools (formerly in _init_proj_pool)
  _impact_events.resize(MAX_IMPACT_EVENTS);
  _impact_count = 0;
  _muzzle_events.resize(MAX_MUZZLE_EVENTS);
  _muzzle_event_count = 0;

  // Initialize influence maps (one per team perspective)
  _influence_map[0].instantiate();
  _influence_map[0]->setup(1, map_w, map_h, 4.0f);
  _influence_map[1].instantiate();
  _influence_map[1]->setup(2, map_w, map_h, 4.0f);
  _influence_timer = 0.0f;

  // Initialize unified pheromone maps (one per team, 15 channels each)
  // Grid: pressure-grid resolution (4m/cell), origin at world min corner
  Vector3 phero_origin(-_map_half_w, 0.0f, -_map_half_h);
  int phero_w = std::max(1, (int)(map_w / 4.0f));
  int phero_h = std::max(1, (int)(map_h / 4.0f));
  for (int t = 0; t < 2; t++) {
    _pheromones[t].instantiate();
    _pheromones[t]->initialize(phero_w, phero_h, CH_CHANNEL_COUNT, 4.0f,
                               phero_origin);
    // Combat channel params (SimulationServer owns deposits)
    _pheromones[t]->set_channel_params(CH_DANGER, 0.97f, 0.15f);
    _pheromones[t]->set_channel_params(CH_SUPPRESSION, 0.85f, 0.20f);
    _pheromones[t]->set_channel_params(CH_CONTACT, 0.92f, 0.10f);
    _pheromones[t]->set_channel_params(CH_RALLY, 0.95f, 0.25f);
    _pheromones[t]->set_channel_params(CH_FEAR, 0.90f, 0.30f);
    _pheromones[t]->set_channel_params(CH_COURAGE, 0.93f, 0.25f);
    _pheromones[t]->set_channel_params(CH_SAFE_ROUTE, 0.98f, 0.05f);
    _pheromones[t]->set_channel_params(CH_FLANK_OPP, 0.85f, 0.10f);
    // Economy channel params (ColonyAI owns deposits via GDScript)
    _pheromones[t]->set_channel_params(CH_METAL, 0.98f, 0.05f);
    _pheromones[t]->set_channel_params(CH_CRYSTAL, 0.98f, 0.05f);
    _pheromones[t]->set_channel_params(CH_ENERGY, 0.98f, 0.05f);
    _pheromones[t]->set_channel_params(CH_CONGESTION, 0.85f, 0.20f);
    _pheromones[t]->set_channel_params(CH_BUILD_URGENCY, 0.92f, 0.30f);
    _pheromones[t]->set_channel_params(CH_EXPLORED, 0.99f, 0.02f);
    _pheromones[t]->set_channel_params(CH_STRATEGIC, 0.98f, 0.05f);
    // Attempt GPU acceleration (falls back to CPU silently)
    _pheromones[t]->setup_gpu();
  }
  _pheromone_tick_timer = 0.0f;

  // ── Phase 3: Multithreaded Pipeline Formalization ──
  int thread_count = std::thread::hardware_concurrency() - 1;
  if (thread_count < 1)
    thread_count = 1;
  ecs.set_threads(thread_count);

  // Fast-Path State Systems
  ecs.system<ecs::Suppression, const ecs::Posture>("SuppressionDecay")
      .iter([this](flecs::iter &it, ecs::Suppression *supp_comp,
                   const ecs::Posture *posture_comp) {
        _sys_suppression_decay(it, supp_comp, posture_comp);
      });

  ecs.system<const ecs::LegacyIndex, ecs::Morale, const ecs::Suppression>(
         "Morale")
      .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   ecs::Morale *morale_comp,
                   const ecs::Suppression *supp_comp) {
        _sys_morale(it, idx_comp, morale_comp, supp_comp);
      });

  ecs.system<ecs::State, ecs::CombatBridging, ecs::AmmoInfo>("Reload").iter(
      [this](flecs::iter &it, ecs::State *state_comp,
             ecs::CombatBridging *cb_comp, ecs::AmmoInfo *ammo_comp) {
        _sys_reload(it, state_comp, cb_comp, ammo_comp);
      });

  ecs.system<ecs::Posture>("PostureTransition")
      .iter([this](flecs::iter &it, ecs::Posture *posture_comp) {
        _sys_posture(it, posture_comp);
      });

  flecs::system decisions =
      ecs.system<const ecs::LegacyIndex, ecs::State, ecs::Posture>("Decisions")
          .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                       ecs::State *state_comp, ecs::Posture *posture_comp) {
            _sys_decisions(it, idx_comp, state_comp, posture_comp);
          });

  // Movement Subsystems
  ecs.system<const ecs::LegacyIndex, const ecs::MovementBridging>(
         "MovementClimbFall")
      .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   const ecs::MovementBridging *mb_comp) {
        _sys_movement_climb_fall(it, idx_comp, mb_comp);
      });

  ecs.system<const ecs::LegacyIndex, ecs::DesiredVelocity>("MovementSteering")
      .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   ecs::DesiredVelocity *dv_comp) {
        _sys_movement_steering(it, idx_comp, dv_comp);
      });

  ecs.system<const ecs::LegacyIndex, ecs::DesiredVelocity>("MovementOrca")
      .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   ecs::DesiredVelocity *dv_comp) {
        _sys_movement_orca(it, idx_comp, dv_comp);
      });

  ecs.system<const ecs::LegacyIndex, const ecs::DesiredVelocity>(
         "MovementApply")
      .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   const ecs::DesiredVelocity *dv_comp) {
        _sys_movement_apply(it, idx_comp, dv_comp);
      });

  ecs.system<const ecs::LegacyIndex, ecs::State, ecs::CombatBridging,
             const ecs::Transform3DData, const ecs::Role, ecs::AmmoInfo,
             const ecs::Cooldowns, const ecs::Morale>("Combat")
      .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   ecs::State *state_comp, ecs::CombatBridging *cb_comp,
                   const ecs::Transform3DData *xform_comp,
                   const ecs::Role *role_comp, ecs::AmmoInfo *ammo_comp,
                   const ecs::Cooldowns *cd_comp,
                   const ecs::Morale *morale_comp) {
        _sys_combat(it, idx_comp, state_comp, cb_comp, xform_comp, role_comp,
                    ammo_comp, cd_comp, morale_comp);
      });

  ecs.system<const ecs::ProjectileData, ecs::ProjectileFlight>("Projectiles")
      .iter([this](flecs::iter &it, const ecs::ProjectileData *p_data,
                   ecs::ProjectileFlight *p_flight) {
        _sys_projectiles(it, p_data, p_flight);
      });

  ecs.system<const ecs::LegacyIndex, const ecs::Transform3DData,
             const ecs::Role>("Visibility")
      // Amortized round-robin batch processing will be applied inside the
      // system itself or we can just let it run on all units. For now, we will
      // run on all units as a unified Flecs pass.
      .iter([this](flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   const ecs::Transform3DData *xform_comp,
                   const ecs::Role *role_comp) {
        _sys_visibility(it, idx_comp, xform_comp, role_comp);
      });

  UtilityFunctions::print("[SimulationServer] Setup: map ", map_w, "x", map_h,
                          "m, spatial grid ", _spatial_w, "x", _spatial_h,
                          ", pheromone grid ", phero_w, "x", phero_h,
                          ", flecs threads ", thread_count, " (15 channels)");
}

void SimulationServer::set_gpu_tactical_map(Ref<GpuTacticalMap> map) {
  _gpu_map = map;
}

// ═══════════════════════════════════════════════════════════════════════
//  Spawn / Despawn
// ═══════════════════════════════════════════════════════════════════════

int32_t SimulationServer::spawn_unit(const Vector3 &pos, int team, int role,
                                     int squad_id) {
  if (_count >= MAX_UNITS) {
    UtilityFunctions::push_warning("[SimulationServer] MAX_UNITS reached");
    return -1;
  }

  int32_t id = _count++;

  _pos_x[id] = pos.x;
  _pos_y[id] = pos.y;
  _pos_z[id] = pos.z;
  _vel_x[id] = 0.0f;
  _vel_y[id] = 0.0f;
  _vel_z[id] = 0.0f;
  _face_x[id] = 0.0f;
  _face_z[id] = 1.0f;
  _actual_vx[id] = 0.0f;
  _actual_vz[id] = 0.0f;

  _health[id] = 1.0f;
  _morale[id] = 1.0f;
  _suppression[id] = 0.0f;

  uint8_t r = (uint8_t)std::clamp(role, 0, (int)ROLE_COUNT - 1);
  _attack_range[id] = _role_range(r);
  _attack_cooldown[id] = _role_cooldown(r);
  _accuracy[id] = _role_accuracy(r);
  _ammo[id] = _role_mag_size(r);
  _mag_size[id] = _role_mag_size(r);
  _attack_timer[id] = _randf() * _attack_cooldown[id]; // stagger initial fire

  _team[id] = (uint8_t)team;
  _role[id] = r;
  _squad_id[id] = (uint16_t)squad_id;
  _state[id] = ST_IDLE;
  _alive[id] = true;

  // Stable member index: assigned at spawn, compacted on death (not reshuffled
  // per frame)
  if (squad_id >= 0 && squad_id < MAX_SQUADS) {
    _squad_member_idx[id] = (int16_t)_squad_spawn_counter[squad_id]++;
    _squads[squad_id].team = (uint8_t)team;
  }

  _personality[id] = PERS_STEADY;
  _frozen_timer[id] = 0.0f;
  _anim_phase[id] = _randf(); // stagger initial phase

  _target_id[id] = -1;
  _order[id] = ORDER_NONE;
  _order_x[id] = pos.x;
  _order_y[id] = pos.y;
  _order_z[id] = pos.z;
  _order_target_id[id] = -1;

  // Stagger decision timers so not all units decide same frame
  _decision_timer[id] = _randf() * _tune_decision_interval;
  _reload_timer[id] = 0.0f;
  _settle_timer[id] = 0.0f;
  _deploy_timer[id] = 0.0f;
  _mode_transition_timer[id] = 0.0f;
  _aim_quality[id] = 1.0f;

  // Peek behavior init
  _peek_timer[id] = 0.0f;
  _peek_offset_x[id] = 0.0f;
  _peek_offset_z[id] = 0.0f;
  _is_peeking[id] = false;
  _peek_side[id] = 1;

  // Posture init
  _posture[id] = POST_STAND;
  _posture_target[id] = POST_STAND;
  _posture_timer[id] = 0.0f;

  // Visibility init
  _last_seen_time[id] = -100.0f; // never seen
  _last_known_x[id] = pos.x;
  _last_known_z[id] = pos.z;
  _detect_range[id] = _role_detect_range(r);

  // --- FLECS DUAL-WRITE ---
  auto entity = ecs.entity()
                    .set<ecs::Position>({pos.x, pos.z})
                    .set<ecs::Velocity>({0.0f, 0.0f})
                    .set<ecs::Transform3DData>({0.0f, 1.0f, 0.0f, 0.0f})
                    .set<ecs::Team>({(uint8_t)team})
                    .set<ecs::Role>({r})
                    .set<ecs::State>({ecs::ST_IDLE})
                    .set<ecs::Health>({1.0f, 1.0f})
                    .set<ecs::Morale>({1.0f, 1.0f})
                    .set<ecs::Suppression>({0.0f})
                    .set<ecs::DesiredVelocity>({0.0f, 0.0f})
                    .add<ecs::IsAlive>();

  _flecs_id[id] = entity;
  // ------------------------

  _alive_count++;
  return id;
}

void SimulationServer::kill_unit(int32_t unit_id) {
  if (!_valid(unit_id) || !_alive[unit_id])
    return;

  // Compact squad member indices: decrement indices above the dead unit's
  int dead_sq = _squad_id[unit_id];
  int dead_idx = _squad_member_idx[unit_id];
  if (dead_sq >= 0 && dead_sq < MAX_SQUADS) {
    for (int i = 0; i < _count; i++) {
      if (i != unit_id && _alive[i] && _squad_id[i] == dead_sq &&
          _squad_member_idx[i] > dead_idx) {
        _squad_member_idx[i]--;
      }
    }
  }

  // Casualty morale shock: nearby same-team allies take a morale hit
  // Squadmates hit harder than non-squad allies. Distance-attenuated.
  // Leader death = 2.5x morale shock + wider radius (devastating for squad
  // cohesion)
  {
    bool is_leader = (_role[unit_id] == ROLE_LEADER);
    float shock_radius = is_leader ? 25.0f : 15.0f;
    float shock_squad =
        is_leader ? 0.20f : 0.08f; // leader death = massive squad shock
    float shock_team = is_leader ? 0.06f : 0.03f;
    float r2 = shock_radius * shock_radius;
    int dead_team = _team[unit_id];
    for (int i = 0; i < _count; i++) {
      if (i == unit_id || !_alive[i] || _team[i] != dead_team)
        continue;
      float dx = _pos_x[i] - _pos_x[unit_id];
      float dz = _pos_z[i] - _pos_z[unit_id];
      float d2 = dx * dx + dz * dz;
      if (d2 > r2)
        continue;
      float proximity = 1.0f - std::sqrt(d2) / shock_radius;
      float penalty = (_squad_id[i] == dead_sq) ? shock_squad : shock_team;
      _morale[i] -= penalty * proximity;
      _morale[i] = std::max(_morale[i], 0.0f);
    }
  }

  _alive[unit_id] = false;
  _state[unit_id] = ST_DEAD;
  _vel_x[unit_id] = 0.0f;
  _vel_y[unit_id] = 0.0f;
  _vel_z[unit_id] = 0.0f;
  _actual_vx[unit_id] = 0.0f;
  _actual_vz[unit_id] = 0.0f;
  _alive_count--;

  // --- FLECS DUAL-WRITE ---
  if (_flecs_id[unit_id].is_alive()) {
    _flecs_id[unit_id].remove<ecs::IsAlive>();
    _flecs_id[unit_id].set<ecs::State>({ecs::ST_DEAD});
  }
  // ------------------------
}

void SimulationServer::despawn_unit(int32_t unit_id) {
  // For now, despawn = kill. Dead units stay in arrays (avoids ID
  // invalidation).
  kill_unit(unit_id);
}

// ═══════════════════════════════════════════════════════════════════════
//  Role Defaults
// ═══════════════════════════════════════════════════════════════════════

float SimulationServer::_role_range(uint8_t role) {
  switch (role) {
  case ROLE_RIFLEMAN:
    return 30.0f;
  case ROLE_LEADER:
    return 25.0f;
  case ROLE_MEDIC:
    return 20.0f;
  case ROLE_MG:
    return 40.0f;
  case ROLE_MARKSMAN:
    return 60.0f;
  case ROLE_GRENADIER:
    return 35.0f;
  case ROLE_MORTAR:
    return MORTAR_MAX_RANGE;
  default:
    return 30.0f;
  }
}

float SimulationServer::_role_cooldown(uint8_t role) {
  switch (role) {
  case ROLE_RIFLEMAN:
    return 0.5f;
  case ROLE_LEADER:
    return 0.6f;
  case ROLE_MEDIC:
    return 0.8f;
  case ROLE_MG:
    return 0.15f;
  case ROLE_MARKSMAN:
    return 1.5f;
  case ROLE_GRENADIER:
    return 2.0f;
  case ROLE_MORTAR:
    return 6.0f;
  default:
    return 0.5f;
  }
}

float SimulationServer::_role_accuracy(uint8_t role) {
  switch (role) {
  case ROLE_RIFLEMAN:
    return 0.5f;
  case ROLE_LEADER:
    return 0.45f;
  case ROLE_MEDIC:
    return 0.35f;
  case ROLE_MG:
    return 0.3f;
  case ROLE_MARKSMAN:
    return 0.75f;
  case ROLE_GRENADIER:
    return 0.4f;
  case ROLE_MORTAR:
    return 0.2f;
  default:
    return 0.5f;
  }
}

int16_t SimulationServer::_role_mag_size(uint8_t role) {
  switch (role) {
  case ROLE_RIFLEMAN:
    return 30;
  case ROLE_LEADER:
    return 30;
  case ROLE_MEDIC:
    return 20;
  case ROLE_MG:
    return 100;
  case ROLE_MARKSMAN:
    return 10;
  case ROLE_GRENADIER:
    return 6;
  case ROLE_MORTAR:
    return 4;
  default:
    return 30;
  }
}

SimulationServer::RoleBallistics
SimulationServer::_role_ballistics(uint8_t role) {
  //                          muzzle_vel  spread  energy  damage
  switch (role) {
  case ROLE_RIFLEMAN:
    return {200.0f, 0.018f, 1.0f, 0.50f}; // 2-shot kill
  case ROLE_LEADER:
    return {180.0f, 0.022f, 0.9f, 0.45f}; // 2-3 shots
  case ROLE_MEDIC:
    return {160.0f, 0.032f, 0.7f, 0.30f}; // 3-4 shots
  case ROLE_MG:
    return {220.0f, 0.040f, 1.2f, 0.35f}; // 3-shot kill
  case ROLE_MARKSMAN:
    return {350.0f, 0.005f, 1.8f, 0.90f}; // 1-2 shots
  case ROLE_GRENADIER:
    return {50.0f, 0.020f, 2.0f, 1.00f}; // direct hit = kill
  case ROLE_MORTAR:
    return {42.0f, 0.010f, 2.8f, 1.00f}; // indirect HE shell
  default:
    return {200.0f, 0.018f, 1.0f, 0.50f};
  }
}

float SimulationServer::_role_settle_time(uint8_t role) {
  switch (role) {
  case ROLE_MARKSMAN:
    return 1.2f;
  case ROLE_MG:
    return 1.0f;
  case ROLE_GRENADIER:
    return 0.8f;
  case ROLE_MORTAR:
    return 1.3f;
  case ROLE_RIFLEMAN:
    return 0.4f;
  case ROLE_LEADER:
    return 0.35f;
  case ROLE_MEDIC:
    return 0.25f;
  default:
    return 0.4f;
  }
}

float SimulationServer::_role_deploy_time(uint8_t role) {
  switch (role) {
  case ROLE_MG:
    return 0.8f; // bipod setup
  case ROLE_MARKSMAN:
    return 0.5f; // scope settle + brace
  case ROLE_GRENADIER:
    return 0.3f; // load tube
  case ROLE_MORTAR:
    return 1.1f; // tube setup + correction
  case ROLE_RIFLEMAN:
    return 0.25f; // shoulder rifle, acquire sight picture
  case ROLE_LEADER:
    return 0.20f; // SMG draw
  case ROLE_MEDIC:
    return 0.15f; // quick draw
  default:
    return 0.20f;
  }
}

float SimulationServer::_compute_aim_quality(int32_t unit) const {
  // Compute current spread based on all modifiers (same as _spawn_projectile)
  RoleBallistics bal = _role_ballistics(_role[unit]);
  float spread = bal.base_spread * (1.0f + _suppression[unit] * 1.5f);

  // Settle penalty
  if (_settle_timer[unit] > 0.0f) {
    float max_settle = _role_settle_time(_role[unit]);
    if (max_settle > 0.0f)
      spread *=
          (1.0f + (_settle_timer[unit] / max_settle) * _tune_settle_spread);
  }

  // Posture accuracy
  spread *= _accuracy_mult(unit);

  // Movement penalty
  {
    float spd2 = _actual_vx[unit] * _actual_vx[unit] +
                 _actual_vz[unit] * _actual_vz[unit];
    if (spd2 > 1.0f) {
      float spd = std::sqrt(spd2);
      spread *= (1.0f + std::min(spd * 0.12f, 0.8f));
    }
  }

  // Height advantage (reduces spread)
  if (_target_id[unit] >= 0 && _alive[_target_id[unit]]) {
    float h_diff = _pos_y[unit] - _pos_y[_target_id[unit]];
    if (h_diff > 0.0f) {
      float h_bonus = std::clamp(h_diff / 10.0f, 0.0f, 0.2f);
      spread *= (1.0f - h_bonus);
    }
  }

  // Berserker (much worse)
  if (_state[unit] == ST_BERSERK) {
    spread /= BERSERK_ACCURACY_MULT;
  }

  // Convert spread to quality: 0.0 (bad) to 1.0 (perfect)
  // Max plausible spread ~0.6 rad (MG standing suppressed just moved)
  // Min spread ~0.002 rad (marksman prone settled height advantage)
  constexpr float MAX_SPREAD = 0.6f;
  return 1.0f - std::clamp(spread / MAX_SPREAD, 0.0f, 1.0f);
}

float SimulationServer::_role_detect_range(uint8_t role) {
  switch (role) {
  case ROLE_RIFLEMAN:
    return 60.0f;
  case ROLE_LEADER:
    return 65.0f;
  case ROLE_MEDIC:
    return 50.0f;
  case ROLE_MG:
    return 55.0f;
  case ROLE_MARKSMAN:
    return 80.0f;
  case ROLE_GRENADIER:
    return 55.0f;
  case ROLE_MORTAR:
    return 85.0f;
  default:
    return 60.0f;
  }
}

SimulationServer::TacticalPositionWeights
SimulationServer::_role_tpos_weights(uint8_t role) {
  //                            cover  shoot  fof   height  dist   radius
  switch (role) {
  case ROLE_RIFLEMAN:
    return {1.0f, 1.0f, 0.3f, 0.3f, 1.0f, 10.0f};
  case ROLE_LEADER:
    return {1.0f, 1.0f, 0.5f, 0.3f, 1.0f, 10.0f};
  case ROLE_MEDIC:
    return {1.3f, 0.3f, 0.1f, 0.1f, 1.5f, 10.0f};
  case ROLE_MG:
    return {0.8f, 1.0f, 1.5f, 1.0f, 1.2f, 20.0f};
  case ROLE_MARKSMAN:
    return {1.2f, 1.0f, 1.2f, 1.5f, 0.8f, 20.0f};
  case ROLE_GRENADIER:
    return {0.8f, 0.5f, 0.5f, 0.2f, 1.0f, 10.0f};
  case ROLE_MORTAR:
    return {1.4f, 0.2f, 0.1f, 0.1f, 1.4f, 20.0f};
  default:
    return {1.0f, 1.0f, 0.3f, 0.3f, 1.0f, 10.0f};
  }
}

SimulationServer::PersonalityMoraleModifiers
SimulationServer::_personality_morale(uint8_t pers) {
  //                                  supp_decay  iso_decay  recovery  break
  //                                  recover
  switch (pers) {
  case PERS_STEADY:
    return {1.0f, 1.0f, 1.0f, 0.20f, 0.35f};
  case PERS_BERSERKER:
    return {0.5f, 1.5f, 0.7f, 0.25f, 0.40f};
  case PERS_CATATONIC:
    return {1.5f, 2.0f, 1.2f, 0.30f, 0.50f};
  case PERS_PARANOID:
    return {1.2f, 0.5f, 0.5f, 0.20f, 0.40f};
  default:
    return {1.0f, 1.0f, 1.0f, 0.20f, 0.35f};
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Fast RNG (xorshift64)
// ═══════════════════════════════════════════════════════════════════════

float SimulationServer::_randf() {
  _rng_state ^= _rng_state << 13;
  _rng_state ^= _rng_state >> 7;
  _rng_state ^= _rng_state << 17;
  return (float)(_rng_state & 0xFFFFFF) / 16777216.0f; // 24-bit mantissa
}

void SimulationServer::set_seed(int64_t seed) {
  _rng_state = (uint64_t)seed;
  if (_rng_state == 0)
    _rng_state = 1; // xorshift64 cannot be 0
  _original_seed = seed;
}

int64_t SimulationServer::get_seed() const { return _original_seed; }

// ═══════════════════════════════════════════════════════════════════════
//  Spatial Hash
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_rebuild_spatial_hash() {
  // Clear all cells to -1 (empty)
  std::fill(_spatial_cells.begin(), _spatial_cells.end(), -1);

  for (int i = 0; i < _count; i++) {
    if (!_alive[i]) {
      _spatial_next[i] = -1;
      continue;
    }

    // World pos to grid cell (world is centered at origin)
    int cx = (int)((_pos_x[i] + _map_half_w) / (float)SPATIAL_CELL_M);
    int cz = (int)((_pos_z[i] + _map_half_h) / (float)SPATIAL_CELL_M);
    cx = std::clamp(cx, 0, _spatial_w - 1);
    cz = std::clamp(cz, 0, _spatial_h - 1);

    int cell = cz * _spatial_w + cx;
    _spatial_next[i] = _spatial_cells[cell];
    _spatial_cells[cell] = i;
  }
}

void SimulationServer::_get_units_in_radius(float cx, float cz, float radius,
                                            std::vector<int32_t> &out) const {
  out.clear();
  float r2 = radius * radius;

  // Grid cells to check
  int min_gx = (int)((cx - radius + _map_half_w) / (float)SPATIAL_CELL_M);
  int max_gx = (int)((cx + radius + _map_half_w) / (float)SPATIAL_CELL_M);
  int min_gz = (int)((cz - radius + _map_half_h) / (float)SPATIAL_CELL_M);
  int max_gz = (int)((cz + radius + _map_half_h) / (float)SPATIAL_CELL_M);

  min_gx = std::clamp(min_gx, 0, _spatial_w - 1);
  max_gx = std::clamp(max_gx, 0, _spatial_w - 1);
  min_gz = std::clamp(min_gz, 0, _spatial_h - 1);
  max_gz = std::clamp(max_gz, 0, _spatial_h - 1);

  for (int gz = min_gz; gz <= max_gz; gz++) {
    for (int gx = min_gx; gx <= max_gx; gx++) {
      int32_t idx = _spatial_cells[gz * _spatial_w + gx];
      while (idx >= 0) {
        float dx = _pos_x[idx] - cx;
        float dz = _pos_z[idx] - cz;
        if (dx * dx + dz * dz <= r2) {
          out.push_back(idx);
        }
        idx = _spatial_next[idx];
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════

float SimulationServer::_distance_sq(int32_t a, int32_t b) const {
  float dx = _pos_x[a] - _pos_x[b];
  float dy = _pos_y[a] - _pos_y[b];
  float dz = _pos_z[a] - _pos_z[b];
  return dx * dx + dy * dy + dz * dz;
}

bool SimulationServer::_check_los(int32_t from, int32_t to) const {
  VoxelWorld *vw = VoxelWorld::get_singleton();
  if (!vw)
    return true; // Fallback: assume clear if no world
  Vector3 a(_pos_x[from], _pos_y[from] + _eye_height(from), _pos_z[from]);
  Vector3 b(_pos_x[to], _pos_y[to] + _center_mass(to), _pos_z[to]);
  return vw->check_los(a, b);
}

float SimulationServer::_check_wall_energy_cost(int32_t from,
                                                int32_t to) const {
  VoxelWorld *vw = VoxelWorld::get_singleton();
  if (!vw)
    return 0.0f; // No world = assume clear

  Vector3 a(_pos_x[from], _pos_y[from] + _eye_height(from), _pos_z[from]);
  Vector3 b(_pos_x[to], _pos_y[to] + _center_mass(to), _pos_z[to]);
  Vector3 diff = b - a;
  float dist = diff.length();
  if (dist < 1e-4f)
    return 0.0f;

  VoxelHit hits[MAX_PEN_VOXELS];
  int num_hits = vw->raycast_multi(a, diff / dist, dist, hits, MAX_PEN_VOXELS);
  if (num_hits == 0)
    return 0.0f; // Clear LOS

  // STRICT PENETRATION BLOCK:
  // If we hit any wall, return effectively infinite cost.
  // This disables shooting through walls completely.
  return 1.0e9f;

  float voxel_scale = vw->get_voxel_scale();
  float total_cost = 0.0f;
  for (int h = 0; h < num_hits; h++) {
    total_cost += get_material_density(hits[h].material) * PENETRATION_FACTOR *
                  voxel_scale;
  }
  return total_cost;
}

void SimulationServer::_clamp_to_terrain(int32_t i) {
  _pos_x[i] = std::clamp(_pos_x[i], -_map_half_w, _map_half_w);
  _pos_z[i] = std::clamp(_pos_z[i], -_map_half_h, _map_half_h);

  // Find ground height via VoxelWorld
  VoxelWorld *vw = VoxelWorld::get_singleton();
  if (!vw)
    return;

  float scale = vw->get_voxel_scale();
  // Convert world XZ to voxel coordinates
  int vx = (int)((_pos_x[i] + _map_half_w) / scale);
  int vz = (int)((_pos_z[i] + _map_half_h) / scale);

  // Search downward from current Y for solid ground
  int vy_start = (int)(_pos_y[i] / scale) + 2; // a bit above current pos
  vy_start = std::clamp(vy_start, 0, vw->get_world_size_y() - 1);

  for (int vy = vy_start; vy >= 0; vy--) {
    if (vw->get_voxel(vx, vy, vz) != 0) { // 0 = MAT_AIR
      _pos_y[i] = (float)(vy + 1) * scale;
      return;
    }
  }
  // No ground found — leave at current Y
}

// ═══════════════════════════════════════════════════════════════════════
//  Tactical AI — Batch Updates
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_update_attackers_count() {
  std::fill(_attackers_count.begin(), _attackers_count.begin() + _count, 0);
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    int32_t t = _target_id[i];
    if (t >= 0 && t < _count) {
      _attackers_count[t]++;
    }
  }
}

void SimulationServer::_update_cover_values() {
  TacticalCoverMap *tcm = TacticalCoverMap::get_singleton();
  if (!tcm) {
    std::fill(_cover_value.begin(), _cover_value.begin() + _count, 0.0f);
    return;
  }
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    Vector3 pos(_pos_x[i], _pos_y[i], _pos_z[i]);
    _cover_value[i] = tcm->get_best_cover_at(pos);
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Tactical AI — Target Scoring
// ═══════════════════════════════════════════════════════════════════════

float SimulationServer::_role_optimal_range(uint8_t role) const {
  switch (role) {
  case ROLE_RIFLEMAN:
    return 20.0f;
  case ROLE_LEADER:
    return 20.0f;
  case ROLE_MEDIC:
    return 15.0f;
  case ROLE_MG:
    return 30.0f;
  case ROLE_MARKSMAN:
    return 45.0f;
  case ROLE_GRENADIER:
    return 25.0f;
  case ROLE_MORTAR:
    return 65.0f;
  default:
    return 20.0f;
  }
}

float SimulationServer::_score_target(int32_t unit, int32_t candidate) const {
  float score = 0.0f;
  float dist2 = _distance_sq(unit, candidate);
  float dist = std::sqrt(dist2);

  // 0. VISIBILITY CHECK (Critical Fix)
  // If the unit's team cannot see the candidate, do not target them.
  // We allow a small grace period if they were seen very recently,
  // but for direct fire we prefer visible targets.
  // _team is 1 or 2, so index is _team[unit] - 1.
  int ti = _team[unit] - 1;
  if (ti >= 0 && ti < 2) {
    if (!_team_can_see(ti, candidate)) {
      // Not currently visible.
      // If we haven't seen them for > 0.5s, completely ignore.
      if (_time_since_seen(candidate) > 0.5f) {
        return -10000.0f; // Strongly discourage / effectively ban
      }
      // If recently seen, massive penalty but maybe allows
      // dumbfire/suppression? For now, consistent with user request "shooting
      // things they can't see":
      score -= 500.0f;
    }
  }

  // 1. Range preference: bell curve around role's optimal range (+30 max)
  float optimal = _role_optimal_range(_role[unit]);
  float range_diff = dist - optimal;
  score += 30.0f * std::exp(-(range_diff * range_diff) / 200.0f);

  // 2. Threat reciprocity: enemy targeting me gets +20
  if (_target_id[candidate] == unit) {
    score += 20.0f;
  }

  // 3. Exposure: enemy NOT in cover is +15 (height degrades target's cover)
  TacticalCoverMap *tcm = TacticalCoverMap::get_singleton();
  if (tcm) {
    Vector3 cand_pos(_pos_x[candidate], _pos_y[candidate], _pos_z[candidate]);
    Vector3 my_pos(_pos_x[unit], _pos_y[unit], _pos_z[unit]);
    Vector3 threat_dir = (my_pos - cand_pos);
    float cover = tcm->get_cover_value(cand_pos, threat_dir);

    // Height degrades cover: at 6m+ above target, their cover is worthless
    float h_diff = _pos_y[unit] - _pos_y[candidate];
    if (h_diff > 1.0f) {
      float degrade = std::clamp((h_diff - 1.0f) / 5.0f, 0.0f, 1.0f);
      cover *= (1.0f - degrade);
    }

    score += (1.0f - cover) * 15.0f;
  }

  // 4. Wounded: easier to finish (+10 max at 0 hp)
  score += (1.0f - _health[candidate]) * 10.0f;

  // 5. Height advantage: bonus for shooting downhill only
  //    (uphill disadvantage already modeled by cover degradation above)
  float height_diff = _pos_y[unit] - _pos_y[candidate];
  if (height_diff > 1.0f) {
    score += std::min(height_diff * 3.0f, 15.0f);
  }

  // 6. Distance tiebreaker: slight closer preference
  score -= dist * 0.1f;

  // 7. Contact intel bonus: prefer targets in high-CONTACT cells (allies
  // spotted them)

  if (ti >= 0 && ti < 2 && _pheromones[ti].is_valid()) {
    Vector3 cand_pos(_pos_x[candidate], 0.0f, _pos_z[candidate]);
    float contact = _pheromones[ti]->sample(cand_pos, CH_CONTACT);
    if (contact > 0.3f) {
      score += 5.0f; // Intel-backed target is higher priority
    }
  }

  return score;
}

// ═══════════════════════════════════════════════════════════════════════
//  Tactical AI — Behavioral Helpers
// ═══════════════════════════════════════════════════════════════════════

// ── Field-of-fire estimation via height map ray march ────────────────
float SimulationServer::_compute_field_of_fire(float wx, float wy,
                                               float wz) const {
  if (!_gpu_map.is_valid())
    return 0.5f;

  const auto &hmap = _gpu_map->get_height_map_data();
  if (hmap.empty())
    return 0.5f;
  int cover_w = _gpu_map->get_cover_width();
  int cover_h = _gpu_map->get_cover_height();

  VoxelWorld *vw_fof = VoxelWorld::get_singleton();
  float inv_scale = vw_fof ? vw_fof->get_inv_voxel_scale() : 4.0f;
  float eye_y_voxels = (wy + EYE_HEIGHT) * inv_scale;
  int clear_rays = 0;

  for (int r = 0; r < FOF_RAY_COUNT; r++) {
    float angle = (float)r * (6.28318f / (float)FOF_RAY_COUNT);
    float dir_x = std::cos(angle);
    float dir_z = std::sin(angle);

    bool blocked = false;
    for (float t = 1.0f; t <= FOF_RAY_RANGE_M; t += 1.0f) {
      float sx = wx + dir_x * t;
      float sz = wz + dir_z * t;

      int hcx = _gpu_map->cover_to_cell_x(sx);
      int hcz = _gpu_map->cover_to_cell_z(sz);

      if (hcx < 0 || hcx >= cover_w || hcz < 0 || hcz >= cover_h)
        break;

      float terrain_h_voxels = (float)hmap[hcz * cover_w + hcx];
      if (terrain_h_voxels >= eye_y_voxels) {
        blocked = true;
        break;
      }
    }
    if (!blocked)
      clear_rays++;
  }

  return (float)clear_rays / (float)FOF_RAY_COUNT;
}

// ── Multi-axis tactical position finder ──────────────────────────────
void SimulationServer::_find_tactical_position(int32_t unit) {
  TacticalCoverMap *tcm = TacticalCoverMap::get_singleton();
  VoxelWorld *vw = VoxelWorld::get_singleton();
  if (!tcm)
    return;

  int32_t threat = _target_id[unit];
  if (threat < 0 || !_alive[threat])
    return;

  Vector3 my_pos(_pos_x[unit], _pos_y[unit], _pos_z[unit]);
  Vector3 threat_pos(_pos_x[threat], _pos_y[threat], _pos_z[threat]);

  auto weights = _role_tpos_weights(_role[unit]);
  float search_radius = weights.search_radius;

  // ── Gather nearest enemies for shootability checks ──────────────
  struct EnemyRef {
    int32_t id;
    float dist2;
  };
  EnemyRef nearby_enemies[MAX_SHOOTABILITY_ENEMIES];
  int enemy_count = 0;
  float range = _attack_range[unit];

  int min_gx =
      (int)((_pos_x[unit] - range + _map_half_w) / (float)SPATIAL_CELL_M);
  int max_gx =
      (int)((_pos_x[unit] + range + _map_half_w) / (float)SPATIAL_CELL_M);
  int min_gz =
      (int)((_pos_z[unit] - range + _map_half_h) / (float)SPATIAL_CELL_M);
  int max_gz =
      (int)((_pos_z[unit] + range + _map_half_h) / (float)SPATIAL_CELL_M);
  min_gx = std::clamp(min_gx, 0, _spatial_w - 1);
  max_gx = std::clamp(max_gx, 0, _spatial_w - 1);
  min_gz = std::clamp(min_gz, 0, _spatial_h - 1);
  max_gz = std::clamp(max_gz, 0, _spatial_h - 1);

  for (int gz = min_gz; gz <= max_gz; gz++) {
    for (int gx = min_gx; gx <= max_gx; gx++) {
      int32_t idx = _spatial_cells[gz * _spatial_w + gx];
      while (idx >= 0) {
        if (_alive[idx] && _team[idx] != _team[unit]) {
          // Fog of war: skip fully-lost enemies
          int vis_idx = _team[unit] - 1;
          if (!_team_can_see(vis_idx, idx) &&
              _time_since_seen(idx) > CONTACT_DECAY_TIME) {
            idx = _spatial_next[idx];
            continue;
          }

          float d2 = _distance_sq(unit, idx);
          if (d2 < range * range) {
            if (enemy_count < MAX_SHOOTABILITY_ENEMIES) {
              nearby_enemies[enemy_count++] = {idx, d2};
            } else {
              int worst = 0;
              for (int e = 1; e < MAX_SHOOTABILITY_ENEMIES; e++) {
                if (nearby_enemies[e].dist2 > nearby_enemies[worst].dist2)
                  worst = e;
              }
              if (d2 < nearby_enemies[worst].dist2) {
                nearby_enemies[worst] = {idx, d2};
              }
            }
          }
        }
        idx = _spatial_next[idx];
      }
    }
  }

  if (enemy_count == 0)
    return;

  float avg_enemy_y = 0.0f;
  for (int e = 0; e < enemy_count; e++) {
    avg_enemy_y += _pos_y[nearby_enemies[e].id];
  }
  avg_enemy_y /= (float)enemy_count;

  // ── Search grid candidates ──────────────────────────────────────
  int search_cells = (int)search_radius;
  int from_cx = tcm->world_to_cell_x(my_pos.x);
  int from_cz = tcm->world_to_cell_z(my_pos.z);

  float best_score = -1e18f;
  Vector3 best_pos;

  for (int dz = -search_cells; dz <= search_cells; dz++) {
    for (int dx = -search_cells; dx <= search_cells; dx++) {
      int cx = from_cx + dx;
      int cz = from_cz + dz;
      if (!tcm->cell_in_bounds(cx, cz))
        continue;

      float wx = tcm->cell_to_world_x(cx);
      float wz = tcm->cell_to_world_z(cz);
      float flat_dist2 =
          (wx - my_pos.x) * (wx - my_pos.x) + (wz - my_pos.z) * (wz - my_pos.z);
      if (flat_dist2 > search_radius * search_radius)
        continue;
      float flat_dist = std::sqrt(flat_dist2);

      // Terrain Y at candidate
      float wy = my_pos.y;
      if (_gpu_map.is_valid()) {
        wy = _gpu_map->get_terrain_height_m(wx, wz);
      }

      // Reject candidates inside solid voxels (wall clipping prevention)
      if (vw) {
        float vs = vw->get_voxel_scale();
        int cvx = (int)((wx + _map_half_w) / vs);
        int cvz = (int)((wz + _map_half_h) / vs);
        int cvy = (int)(wy / vs);
        int bv = _body_voxels(unit);
        bool inside_wall = false;
        for (int dy = 1; dy <= bv; dy++) {
          if (vw->is_solid(cvx, cvy + dy, cvz)) {
            inside_wall = true;
            break;
          }
        }
        if (inside_wall)
          continue;
      }

      // Score 1: COVER
      Vector3 cand_pos(wx, wy, wz);
      Vector3 threat_dir = threat_pos - cand_pos;
      float cover = tcm->get_cover_value(cand_pos, threat_dir);

      // Score 2: SHOOTABILITY (LOS to nearby enemies)
      float shootability = 0.0f;
      if (vw) {
        Vector3 eye(wx, wy + _eye_height(unit), wz);
        for (int e = 0; e < enemy_count; e++) {
          int32_t eid = nearby_enemies[e].id;
          Vector3 enemy_chest(_pos_x[eid], _pos_y[eid] + _center_mass(eid),
                              _pos_z[eid]);
          if (vw->check_los(eye, enemy_chest)) {
            shootability += 1.0f;
          }
          _los_checks++;
        }
        shootability /= (float)enemy_count;
      }

      // Score 3: FIELD OF FIRE
      float fof = _compute_field_of_fire(wx, wy, wz);

      // Score 4: HEIGHT ADVANTAGE
      float height_factor = std::clamp((wy - avg_enemy_y) / 8.0f, -0.5f, 1.0f);

      // Pheromone influence on position scoring
      float phero_penalty = 0.0f;
      float phero_flank_bonus = 0.0f;
      float phero_contact_bonus = 0.0f;
      if (_team[unit] > 0 && _team[unit] <= 2) {
        int ti = _team[unit] - 1;
        if (_pheromones[ti].is_valid()) {
          auto pw = _role_pheromone_weights(_role[unit]);
          phero_penalty =
              _pheromones[ti]->sample(cand_pos, CH_DANGER) * 2.0f * pw.danger +
              _pheromones[ti]->sample(cand_pos, CH_SUPPRESSION) * 1.5f *
                  pw.suppression;
          phero_flank_bonus = _pheromones[ti]->sample(cand_pos, CH_FLANK_OPP) *
                              3.0f * pw.flank_opp;
          phero_contact_bonus =
              _pheromones[ti]->sample(cand_pos, CH_CONTACT) * 1.0f * pw.contact;
        }
      }

      // Weighted composite
      float score = cover * TPOS_COVER_WEIGHT * weights.cover +
                    shootability * TPOS_SHOOT_WEIGHT * weights.shootability +
                    fof * TPOS_FOF_WEIGHT * weights.field_of_fire +
                    height_factor * TPOS_HEIGHT_WEIGHT * weights.height -
                    flat_dist * TPOS_DIST_WEIGHT * weights.distance_cost -
                    phero_penalty + phero_flank_bonus + phero_contact_bonus;

      if (score > best_score) {
        best_score = score;
        best_pos = Vector3(wx, wy, wz);
      }
    }
  }

  if (best_score <= -1e17f)
    return;

  _order_x[unit] = best_pos.x;
  _order_y[unit] = best_pos.y;
  _order_z[unit] = best_pos.z;
  _state[unit] = ST_IN_COVER;

  // ── Compute peek direction ──────────────────────────────────────
  float to_threat_x = _pos_x[threat] - best_pos.x;
  float to_threat_z = _pos_z[threat] - best_pos.z;
  float ttd = std::sqrt(to_threat_x * to_threat_x + to_threat_z * to_threat_z);
  if (ttd > 0.1f) {
    to_threat_x /= ttd;
    to_threat_z /= ttd;
  }

  // Perpendicular: left = (-z, x), right = (z, -x)
  float perp_lx = -to_threat_z, perp_lz = to_threat_x;
  float perp_rx = to_threat_z, perp_rz = -to_threat_x;

  float po = _peek_offset_for(unit);
  bool left_clear = true, right_clear = true;
  if (vw) {
    Vector3 enemy_chest(_pos_x[threat], _pos_y[threat] + _center_mass(threat),
                        _pos_z[threat]);
    Vector3 left_eye(best_pos.x + perp_lx * po, best_pos.y + _eye_height(unit),
                     best_pos.z + perp_lz * po);
    Vector3 right_eye(best_pos.x + perp_rx * po, best_pos.y + _eye_height(unit),
                      best_pos.z + perp_rz * po);
    left_clear = vw->check_los(left_eye, enemy_chest);
    right_clear = vw->check_los(right_eye, enemy_chest);
  }

  if (left_clear && right_clear) {
    _peek_side[unit] = (_randf() > 0.5f) ? 1 : -1;
  } else if (left_clear) {
    _peek_side[unit] = -1;
  } else if (right_clear) {
    _peek_side[unit] = 1;
  } else {
    _peek_side[unit] = (_randf() > 0.5f) ? 1 : -1;
  }

  if (_peek_side[unit] < 0) {
    _peek_offset_x[unit] = perp_lx * po;
    _peek_offset_z[unit] = perp_lz * po;
  } else {
    _peek_offset_x[unit] = perp_rx * po;
    _peek_offset_z[unit] = perp_rz * po;
  }

  // Start hidden with suppression-based timer
  _is_peeking[unit] = false;
  _peek_timer[unit] =
      _tune_peek_hide_min +
      (_tune_peek_hide_max - _tune_peek_hide_min) * _suppression[unit] +
      _randf() * 0.3f;
}

// ═══════════════════════════════════════════════════════════════════════
//  Peek Behavior Tick
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_tick_peek(float delta) {
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    if (_state[i] != ST_IN_COVER) {
      _is_peeking[i] = false;
      continue;
    }

    // Prone in cover: always exposed (fires from low position, no sidestep)
    if (_posture[i] == POST_PRONE) {
      _is_peeking[i] = true;
      continue;
    }

    _peek_timer[i] -= delta;
    if (_peek_timer[i] > 0.0f)
      continue;

    // Toggle peek state
    _is_peeking[i] = !_is_peeking[i];

    float supp = _suppression[i];
    // Role-based peek multiplier: MG sustains fire, Marksman aims, Rifleman
    // quick peeks
    float expose_mult = 1.0f, hide_mult = 1.0f;
    switch (_role[i]) {
    case ROLE_MG:
      expose_mult = 1.8f;
      hide_mult = 0.6f;
      break; // Long bursts, short hide
    case ROLE_MARKSMAN:
      expose_mult = 1.3f;
      hide_mult = 1.2f;
      break; // Moderate peek, patient
    case ROLE_MEDIC:
      expose_mult = 0.6f;
      hide_mult = 1.3f;
      break; // Quick peek, stay safe
    case ROLE_MORTAR:
      expose_mult = 0.5f;
      hide_mult = 0.5f;
      break; // Barely peeks (indirect fire)
    default:
      break; // RIFLEMAN, LEADER, GRENADIER = 1.0x
    }
    if (_is_peeking[i]) {
      // Now peeking: set timer for how long to stay exposed
      float expose_dur = _tune_peek_expose_max -
                         (_tune_peek_expose_max - _tune_peek_expose_min) * supp;
      _peek_timer[i] = (expose_dur + _randf() * 0.2f) * expose_mult;
    } else {
      // Now hiding: set timer for how long to stay hidden
      float hide_dur = _tune_peek_hide_min +
                       (_tune_peek_hide_max - _tune_peek_hide_min) * supp;
      _peek_timer[i] = (hide_dur + _randf() * 0.2f) * hide_mult;
    }
  }
}

// ── Posture System ──────────────────────────────────────────────────

SimulationServer::PostureProfile
SimulationServer::_posture_profile(uint8_t posture) {
  //                    eye    muzzle  cm    hit_r  speed  acc    supp   vox
  //                    peek
  switch (posture) {
  case POST_STAND:
    return {1.5f, 1.4f, 1.0f, 0.35f, 1.0f, 1.0f, 1.0f, 6, 1.0f};
  case POST_CROUCH:
    return {1.0f, 0.9f, 0.6f, 0.30f, 0.6f, 0.85f, 1.0f, 4, 0.6f};
  case POST_PRONE:
    return {0.3f, 0.25f, 0.15f, 0.20f, 0.2f, 0.7f, 1.5f, 1, 0.0f};
  default:
    return {1.5f, 1.4f, 1.0f, 0.35f, 1.0f, 1.0f, 1.0f, 6, 1.0f};
  }
}

float SimulationServer::_get_posture_transition_time(uint8_t from,
                                                     uint8_t to) const {
  if (from == to)
    return 0.0f;
  if (from == POST_STAND && to == POST_CROUCH)
    return POSTURE_STAND_TO_CROUCH;
  if (from == POST_CROUCH && to == POST_STAND)
    return POSTURE_CROUCH_TO_STAND;
  if (from == POST_CROUCH && to == POST_PRONE)
    return POSTURE_CROUCH_TO_PRONE;
  if (from == POST_PRONE && to == POST_CROUCH)
    return POSTURE_PRONE_TO_CROUCH;
  if (from == POST_STAND && to == POST_PRONE)
    return POSTURE_STAND_TO_PRONE;
  if (from == POST_PRONE && to == POST_STAND)
    return POSTURE_PRONE_TO_STAND;
  return 0.5f;
}

void SimulationServer::_request_posture(int32_t i, uint8_t target) {
  if (_posture[i] == target)
    return;
  if (_posture_target[i] == target && _posture_timer[i] > 0.0f)
    return;
  _posture_target[i] = target;
  _posture_timer[i] = _get_posture_transition_time(_posture[i], target);
}

void SimulationServer::_sys_posture(flecs::iter &it,
                                    ecs::Posture *posture_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    if (posture_comp[row].transition_timer <= 0.0f)
      continue;
    posture_comp[row].transition_timer -= delta;
    if (posture_comp[row].transition_timer <= 0.0f) {
      posture_comp[row].transition_timer = 0.0f;
      posture_comp[row].current = posture_comp[row].target;
    }
  }
}

int SimulationServer::get_posture(int32_t unit_id) const {
  if (!_valid(unit_id))
    return POST_STAND;
  return (int)_posture[unit_id];
}

void SimulationServer::set_posture(int32_t unit_id, int posture) {
  if (!_valid(unit_id))
    return;
  uint8_t p = (uint8_t)std::clamp(posture, 0, (int)POST_PRONE);
  _posture[unit_id] = p;
  _posture_target[unit_id] = p;
  _posture_timer[unit_id] = 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════
//  Movement Mode / Context Steering API
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::set_unit_movement_mode(int32_t unit_id, int mode) {
  if (!_valid(unit_id))
    return;
  uint8_t m = (uint8_t)std::clamp(mode, 0, (int)(MMODE_COUNT - 1));
  _move_mode[unit_id] = m;
  _noise_level[unit_id] = NOISE_TABLE[m];
}

int SimulationServer::get_unit_movement_mode(int32_t unit_id) const {
  if (!_valid(unit_id))
    return MMODE_COMBAT;
  return _move_mode[unit_id];
}

void SimulationServer::set_squad_movement_mode(int squad_id, int mode) {
  uint8_t m = (uint8_t)std::clamp(mode, 0, (int)(MMODE_COUNT - 1));
  for (int i = 0; i < _count; i++) {
    if (_alive[i] && _squad_id[i] == (uint16_t)squad_id) {
      _move_mode[i] = m;
      _noise_level[i] = NOISE_TABLE[m];
    }
  }
}

void SimulationServer::set_context_steering_enabled(bool enabled) {
  _use_context_steering = enabled;
}

void SimulationServer::set_orca_enabled(bool enabled) { _use_orca = enabled; }

PackedFloat32Array SimulationServer::get_steer_interest(int32_t unit_id) const {
  PackedFloat32Array result;
  if (!_valid(unit_id))
    return result;
  result.resize(STEER_SLOTS);
  int base = unit_id * STEER_SLOTS;
  for (int s = 0; s < STEER_SLOTS; s++) {
    result[s] = _steer_interest[base + s];
  }
  return result;
}

PackedFloat32Array SimulationServer::get_steer_danger(int32_t unit_id) const {
  PackedFloat32Array result;
  if (!_valid(unit_id))
    return result;
  result.resize(STEER_SLOTS);
  int base = unit_id * STEER_SLOTS;
  for (int s = 0; s < STEER_SLOTS; s++) {
    result[s] = _steer_danger[base + s];
  }
  return result;
}

// ═══════════════════════════════════════════════════════════════════════
//  Fog of War / Visibility
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_sys_visibility(flecs::iter &it,
                                       const ecs::LegacyIndex *idx_comp,
                                       const ecs::Transform3DData *xform_comp,
                                       const ecs::Role *role_comp) {
  VoxelWorld *vw = VoxelWorld::get_singleton();

  int start_c = _vis_cursor;
  int end_c = start_c + VIS_BATCH_SIZE;

  for (int idx : it) {
    int32_t i = idx_comp[idx].val;
    if (!_alive[i])
      continue;

    // Amortize: only process if `i` is within the current batch
    bool in_batch = false;
    if (end_c <= _count) {
      in_batch = (i >= start_c && i < end_c);
    } else {
      in_batch = (i >= start_c || i < (end_c - _count));
    }
    if (!in_batch)
      continue;

    int my_team = _team[i];    // 1 or 2
    int vis_idx = my_team - 1; // 0 = team1's view, 1 = team2's view
    if (vis_idx < 0 || vis_idx > 1) {
      continue;
    }

    // Fallback if missing Role component, shouldn't happen but defensive
    float dr = _detect_range[i];

    // Gas penalty on detection range: units IN gas see less
    if (_gpu_map.is_valid() && _gpu_map->is_gpu_available()) {
      Vector3 my_pos(_pos_x[i], _pos_y[i], _pos_z[i]);
      float gas_here = _gpu_map->sample_gas_density(my_pos);
      if (gas_here > GAS_DENSITY_THRESHOLD) {
        uint8_t gt = _gpu_map->sample_gas_type(my_pos);
        if (gt == PAYLOAD_SMOKE)
          dr *= (1.0f - 0.8f * gas_here); // -80% at full
        else if (gt == PAYLOAD_TEAR_GAS)
          dr *= (1.0f - 0.6f * gas_here); // -60%
        else if (gt == PAYLOAD_TOXIC)
          dr *= (1.0f - 0.4f * gas_here); // -40%
      }
    }

    float dr2 = dr * dr;

    for (int e = 0; e < _count; e++) {
      if (!_alive[e] || _team[e] == my_team)
        continue;

      // Already visible to my team? Skip (another scanner already found them)
      if (_team_can_see(vis_idx, e))
        continue;

      // Acoustic detection: enemy noise extends effective detect range
      float eff_dr2 = dr2;
      float noise_r = _noise_level[e];
      if (noise_r > dr) {
        eff_dr2 = noise_r * noise_r;
      }

      // Distance check (cheap)
      float dx = _pos_x[i] - _pos_x[e];
      float dz = _pos_z[i] - _pos_z[e];
      float d2 = dx * dx + dz * dz;
      if (d2 > eff_dr2)
        continue;

      // Acoustic detection: beyond visual range, no LOS needed (heard through
      // walls) Do NOT set _team_set_vis() — heard enemies stay "invisible" in
      // FOW so suppressive fire fallback path still works. Only update
      // last-known position.
      bool heard_only = (d2 > dr2);
      if (heard_only) {
        _last_seen_time[e] = _game_time;
        _last_known_x[e] = _pos_x[e];
        _last_known_z[e] = _pos_z[e];
        continue;
      }

      // LOS check (single-hit DDA — cheaper than raycast_multi)
      bool can_see = true;
      if (vw) {
        Vector3 eye(_pos_x[i], _pos_y[i] + _eye_height(i), _pos_z[i]);
        Vector3 tgt(_pos_x[e], _pos_y[e] + _center_mass(e), _pos_z[e]);
        can_see = vw->check_los(eye, tgt);
        // Note: these counters might conflict slightly under multithreading if
        // unatomic but it's acceptable for FOW diagnostics
        _los_checks++;
        _fow_vis_checks++;
        _fow_total_vis_checks++;
      }

      // Gas blocks LOS: sample along ray, blocked if density > 0.3
      if (can_see && _gpu_map.is_valid() && _gpu_map->is_gpu_available()) {
        Vector3 eye(_pos_x[i], _pos_y[i] + _eye_height(i), _pos_z[i]);
        Vector3 tgt(_pos_x[e], _pos_y[e] + _center_mass(e), _pos_z[e]);
        float gas_along = _gpu_map->sample_gas_along_ray(eye, tgt);
        if (gas_along > 0.3f) {
          can_see = false;
          _fow_targets_skipped++;
        }
      }

      if (can_see) {
        _team_set_vis(vis_idx, e);
        _last_seen_time[e] = _game_time;
        _last_known_x[e] = _pos_x[e];
        _last_known_z[e] = _pos_z[e];
        _fow_vis_hits++;
        _fow_total_vis_hits++;
        _fow_contacts_gained++;
      }
    }
  }
}

bool SimulationServer::team_can_see(int team, int32_t unit_id) const {
  if (team < 1 || team > 2 || !_valid(unit_id))
    return false;
  return _team_can_see(team - 1, unit_id); // public API uses 1-based teams
}

float SimulationServer::get_last_seen_time(int32_t unit_id) const {
  if (!_valid(unit_id))
    return -100.0f;
  return _last_seen_time[unit_id];
}

bool SimulationServer::_should_flank(int32_t unit) const {
  int32_t target = _target_id[unit];
  if (target < 0 || !_alive[target])
    return false;

  // Heavy/precision weapons don't flank — they suppress or snipe
  if (_role[unit] == ROLE_MG)
    return false;
  if (_role[unit] == ROLE_MARKSMAN)
    return false;

  // Target must be pinned by 2+ allies (or 1 if suppression pheromone is high
  // toward target)
  int min_allies = FLANK_DETECT_ALLIES;
  int ti = _team[unit] - 1;
  if (ti >= 0 && ti < 2 && _pheromones[ti].is_valid()) {
    Vector3 tgt_pos(_pos_x[target], 0.0f, _pos_z[target]);
    float supp_at_target = _pheromones[ti]->sample(tgt_pos, CH_SUPPRESSION);
    if (supp_at_target > 0.5f) {
      min_allies = 1; // Enemy is pinned by suppression — easier to flank
    }
  }
  if (_attackers_count[target] < min_allies)
    return false;

  // Don't flank if suppressed or wounded
  if (_suppression[unit] > 0.4f || _health[unit] < 0.5f)
    return false;

  // Don't bother flanking if destination is too close (trivial flanks look like
  // jitter)
  Vector3 flank_dest = _compute_flank_destination(unit);
  float fd2 = (flank_dest.x - _pos_x[unit]) * (flank_dest.x - _pos_x[unit]) +
              (flank_dest.z - _pos_z[unit]) * (flank_dest.z - _pos_z[unit]);
  if (fd2 < FLANK_MIN_MOVE_DIST * FLANK_MIN_MOVE_DIST)
    return false;

  return true;
}

Vector3 SimulationServer::_compute_flank_destination(int32_t unit) const {
  int32_t target = _target_id[unit];
  TacticalCoverMap *tcm = TacticalCoverMap::get_singleton();

  // Direction from target toward unit
  float ax = _pos_x[unit] - _pos_x[target];
  float az = _pos_z[unit] - _pos_z[target];

  // Perpendicular direction (90 degrees in XZ plane)
  float perp_x = -az;
  float perp_z = ax;
  float perp_len = std::sqrt(perp_x * perp_x + perp_z * perp_z);
  if (perp_len < 0.01f)
    return Vector3(_pos_x[unit], _pos_y[unit], _pos_z[unit]);
  perp_x /= perp_len;
  perp_z /= perp_len;

  // Two candidate positions (left and right of the engagement axis)
  float lx = _pos_x[target] + perp_x * FLANK_PERP_DIST;
  float lz = _pos_z[target] + perp_z * FLANK_PERP_DIST;
  float rx = _pos_x[target] - perp_x * FLANK_PERP_DIST;
  float rz = _pos_z[target] - perp_z * FLANK_PERP_DIST;

  // Score each side: distance + cover at destination - own suppression
  int ti = _team[unit] - 1;
  auto score_pos = [&](float fx, float fz) -> float {
    float s = 0.0f;
    // Prefer closer side (less exposure)
    float d2 = (_pos_x[unit] - fx) * (_pos_x[unit] - fx) +
               (_pos_z[unit] - fz) * (_pos_z[unit] - fz);
    s -= std::sqrt(d2) * 1.0f;

    // Cover at destination
    if (tcm) {
      Vector3 cand(fx, _pos_y[unit], fz);
      Vector3 threat(_pos_x[target], _pos_y[target], _pos_z[target]);
      float cover = tcm->get_cover_value(cand, threat - cand);
      s += cover * 30.0f;
    }

    // Avoid approaching through own team's suppression cone
    if (ti >= 0 && ti < 2 && _pheromones[ti].is_valid()) {
      Vector3 cand_pos(fx, 0.0f, fz);
      float supp = _pheromones[ti]->sample(cand_pos, CH_SUPPRESSION);
      s -= supp * 15.0f; // Penalty for flanking into friendly fire zone
    }
    return s;
  };

  float sl = score_pos(lx, lz);
  float sr = score_pos(rx, rz);

  if (sl >= sr) {
    return Vector3(lx, _pos_y[unit], lz);
  } else {
    return Vector3(rx, _pos_y[unit], rz);
  }
}

bool SimulationServer::_should_suppress(int32_t unit) const {
  if (_order[unit] == ORDER_SUPPRESS)
    return true;

  int32_t target = _target_id[unit];
  if (target < 0)
    return false;

  // Any unit with a suppressive target (FOW fallback) enters suppressive fire
  // mode. This lets all roles contribute area denial at last-known enemy
  // positions.
  if (_target_suppressive[unit])
    return true;

  // MG units suppress when a squadmate is flanking (O(1) via precomputed flag)
  if (_role[unit] == ROLE_MG) {
    int sid = _squad_id[unit];
    if (sid >= 0 && sid < MAX_SQUADS && _squad_has_flanker[sid]) {
      return true;
    }
    // MG with 3+ allies on same target: suppress to pin
    if (_attackers_count[target] >= 3)
      return true;
  }

  return false;
}

void SimulationServer::_update_squad_cohesion(int32_t unit) {
  _tac_nearby.clear();
  _get_units_in_radius(_pos_x[unit], _pos_z[unit], SQUAD_COHESION_RADIUS,
                       _tac_nearby);

  int16_t count = 0;
  for (int32_t idx : _tac_nearby) {
    if (idx != unit && _alive[idx] && _squad_id[idx] == _squad_id[unit] &&
        _team[idx] == _team[unit]) {
      count++;
    }
  }
  _nearby_squad_count[unit] = count;
}

// ═══════════════════════════════════════════════════════════════════════
//  Tactical AI — Influence Map Integration
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_tick_influence_maps() {
  if (!_influence_map[0].is_valid() || !_influence_map[1].is_valid())
    return;

  // Build per-team arrays: include all friendlies + only visible enemies
  for (int t = 0; t < 2; t++) {
    int my_team = t + 1; // 1 or 2
    PackedVector3Array positions;
    PackedInt32Array teams;
    PackedFloat32Array in_combat;
    positions.resize(_alive_count);
    teams.resize(_alive_count);
    in_combat.resize(_alive_count);

    int j = 0;
    for (int i = 0; i < _count; i++) {
      if (!_alive[i])
        continue;

      // Fog of war: skip invisible enemies
      if ((int)_team[i] != my_team && !_team_can_see(t, i)) {
        _fow_influence_filtered++;
        continue;
      }

      positions.set(j, Vector3(_pos_x[i], _pos_y[i], _pos_z[i]));
      teams.set(j, (int32_t)_team[i]);
      float combat = (_state[i] == ST_ENGAGING || _state[i] == ST_SUPPRESSING ||
                      _state[i] == ST_FLANKING || _state[i] == ST_BERSERK)
                         ? 1.0f
                         : 0.0f;
      in_combat.set(j, combat);
      j++;
    }

    // Shrink to actual count
    positions.resize(j);
    teams.resize(j);
    in_combat.resize(j);

    _influence_map[t]->update(positions, teams, in_combat);
    _influence_map[t]->update_cover_quality();
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  ORCA Collision Avoidance — Half-Plane + LP Solver
// ═══════════════════════════════════════════════════════════════════════

namespace {

struct OrcaLine {
  float nx, nz; // outward half-plane normal (unit vector)
  float px, pz; // a point on the half-plane boundary
};

// 2D cross product (determinant): a × b = ax*bz - az*bx
inline float cross2d(float ax, float az, float bx, float bz) {
  return ax * bz - az * bx;
}

// Project point onto half-plane boundary line and clamp to max_speed disc.
// Returns the closest point on the line of `line` to `vx,vz` that also
// satisfies the half-plane, clamped to max_speed.
inline void project_on_line(const OrcaLine &line, float vx, float vz,
                            float max_speed, float &out_x, float &out_z) {
  // Line direction = perpendicular to normal (rotate 90° CW: (nz, -nx))
  float dx = line.nz, dz = -line.nx;
  // Vector from line point to v
  float rvx = vx - line.px, rvz = vz - line.pz;
  // Project onto line direction
  float t = rvx * dx + rvz * dz;
  out_x = line.px + dx * t;
  out_z = line.pz + dz * t;
  // Clamp to max_speed disc
  float s2 = out_x * out_x + out_z * out_z;
  if (s2 > max_speed * max_speed) {
    float inv = max_speed / sqrtf(s2);
    out_x *= inv;
    out_z *= inv;
  }
}

// Solve 2D linear program: find velocity closest to (pref_vx, pref_vz) that
// satisfies all ORCA half-plane constraints, within a max_speed disc.
// Based on van den Berg et al. (RVO2) incremental LP.
void orca_solve(const OrcaLine *lines, int n_lines, float pref_vx,
                float pref_vz, float max_speed, float &out_vx, float &out_vz) {
  static constexpr float EPS = 0.00001f;

  out_vx = pref_vx;
  out_vz = pref_vz;

  // Clamp preferred to max_speed disc
  float pref_sq = out_vx * out_vx + out_vz * out_vz;
  if (pref_sq > max_speed * max_speed) {
    float inv = max_speed / sqrtf(pref_sq);
    out_vx *= inv;
    out_vz *= inv;
  }

  for (int i = 0; i < n_lines; i++) {
    // Check if current velocity satisfies constraint i
    // Constraint: dot(normal, v - point) >= 0
    float det = (out_vx - lines[i].px) * lines[i].nx +
                (out_vz - lines[i].pz) * lines[i].nz;
    if (det >= 0.0f)
      continue; // already satisfied

    // Project current velocity onto line i
    float new_vx, new_vz;
    project_on_line(lines[i], out_vx, out_vz, max_speed, new_vx, new_vz);

    // Check if projected point satisfies all previous constraints
    bool feasible = true;
    for (int j = 0; j < i; j++) {
      float det_j = (new_vx - lines[j].px) * lines[j].nx +
                    (new_vz - lines[j].pz) * lines[j].nz;
      if (det_j < -EPS) {
        feasible = false;

        // Try to find intersection of lines i and j
        float d_ix = lines[i].nz, d_iz = -lines[i].nx; // direction of line i
        float d_jx = lines[j].nz, d_jz = -lines[j].nx; // direction of line j
        float denom = cross2d(d_ix, d_iz, d_jx, d_jz);

        if (fabsf(denom) > EPS) {
          // Lines are not parallel — find intersection
          float diff_x = lines[j].px - lines[i].px;
          float diff_z = lines[j].pz - lines[i].pz;
          float t_i = cross2d(d_jx, d_jz, diff_x, diff_z) / denom;
          float ix = lines[i].px + d_ix * t_i;
          float iz = lines[i].pz + d_iz * t_i;

          // Check which side of line i to go (toward the feasible side of j)
          // The valid region is the ray from intersection in the direction
          // that satisfies both half-planes.
          float cand_vx = ix, cand_vz = iz;

          // Clamp to max_speed disc
          float c2 = cand_vx * cand_vx + cand_vz * cand_vz;
          if (c2 > max_speed * max_speed) {
            float inv = max_speed / sqrtf(c2);
            cand_vx *= inv;
            cand_vz *= inv;
          }

          // Verify candidate satisfies all constraints 0..i
          bool cand_ok = true;
          for (int k = 0; k <= i; k++) {
            float det_k = (cand_vx - lines[k].px) * lines[k].nx +
                          (cand_vz - lines[k].pz) * lines[k].nz;
            if (det_k < -EPS) {
              cand_ok = false;
              break;
            }
          }
          if (cand_ok) {
            new_vx = cand_vx;
            new_vz = cand_vz;
            feasible = true;
          }
        }

        if (!feasible)
          break;
      }
    }

    if (feasible) {
      out_vx = new_vx;
      out_vz = new_vz;
    } else {
      // Infeasible: find safest velocity — maximize minimum penetration depth.
      // Search along half-plane normals as candidate directions on max_speed
      // circle.
      float best_min_pen = -1e18f;
      float best_vx = 0.0f, best_vz = 0.0f;

      for (int c = 0; c <= i; c++) {
        // Candidate: move along normal[c] direction at max_speed
        float cvx = lines[c].nx * max_speed;
        float cvz = lines[c].nz * max_speed;

        // Find minimum penetration depth across ALL constraints 0..i
        float min_pen = 1e18f;
        for (int k = 0; k <= i; k++) {
          float pen = (cvx - lines[k].px) * lines[k].nx +
                      (cvz - lines[k].pz) * lines[k].nz;
          if (pen < min_pen)
            min_pen = pen;
        }
        if (min_pen > best_min_pen) {
          best_min_pen = min_pen;
          best_vx = cvx;
          best_vz = cvz;
        }

        // Also try the opposite direction
        cvx = -lines[c].nx * max_speed;
        cvz = -lines[c].nz * max_speed;
        min_pen = 1e18f;
        for (int k = 0; k <= i; k++) {
          float pen = (cvx - lines[k].px) * lines[k].nx +
                      (cvz - lines[k].pz) * lines[k].nz;
          if (pen < min_pen)
            min_pen = pen;
        }
        if (min_pen > best_min_pen) {
          best_min_pen = min_pen;
          best_vx = cvx;
          best_vz = cvz;
        }
      }

      out_vx = best_vx;
      out_vz = best_vz;
      return; // Can't do better, exit early
    }
  }
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════
//  Tick Subsystems
// ═══════════════════════════════════════════════════════════════════════
void SimulationServer::_sys_movement_climb_fall(
    flecs::iter &it, const ecs::LegacyIndex *idx_comp,
    const ecs::MovementBridging *mb_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    int i = idx_comp[row].val;

    if (_state[i] == ST_DEAD || _state[i] == ST_DOWNED ||
        _state[i] == ST_FROZEN)
      continue;

    // Tick climb cooldown
    if (_climb_cooldown[i] > 0.0f)
      _climb_cooldown[i] -= delta;

    // ── Climbing handler ──────────────
    if (_state[i] == ST_CLIMBING) {
      VoxelWorld *vw = VoxelWorld::get_singleton();
      if (vw) {
        float vs = vw->get_voxel_scale();
        int cvx = (int)((_pos_x[i] + _map_half_w) / vs);
        int cvz = (int)((_pos_z[i] + _map_half_h) / vs);
        int cvy = (int)(_pos_y[i] / vs);

        bool wall_intact = false;
        const int cdx[4] = {1, -1, 0, 0};
        const int cdz[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4 && !wall_intact; d++) {
          if (vw->is_solid(cvx + cdx[d], cvy, cvz + cdz[d]))
            wall_intact = true;
        }

        if (!wall_intact) {
          _state[i] = ST_FALLING;
          _fall_start_y[i] = _pos_y[i];
          _vel_y[i] = 0.0f;
          _climb_cooldown[i] = CLIMB_COOLDOWN_SEC;
          _fall_started_tick++;
          _total_fall_events++;
          continue;
        }

        _pos_y[i] += CLIMB_SPEED * delta;
        if (_pos_y[i] >= _climb_target_y[i]) {
          _pos_y[i] = _climb_target_y[i];
          _pos_x[i] = _climb_dest_x[i];
          _pos_z[i] = _climb_dest_z[i];
          _state[i] = ST_MOVING;
        }
      }
      continue;
    }

    // ── Falling handler ───────────────
    if (_state[i] == ST_FALLING) {
      _vel_y[i] -= FALL_GRAVITY * delta;
      float new_y = _pos_y[i] + _vel_y[i] * delta;

      VoxelWorld *vw = VoxelWorld::get_singleton();
      if (vw) {
        float vs = vw->get_voxel_scale();
        int fvx = (int)((_pos_x[i] + _map_half_w) / vs);
        int fvz = (int)((_pos_z[i] + _map_half_h) / vs);
        int fvy = (int)(new_y / vs);

        bool landed = (fvy <= 0);
        if (!landed && vw->is_solid(fvx, fvy, fvz))
          landed = true;

        if (landed) {
          _clamp_to_terrain(i);
          float fall_dist = _fall_start_y[i] - _pos_y[i];
          _climb_cooldown[i] = CLIMB_COOLDOWN_SEC;

          if (fall_dist >= FALL_LETHAL_HEIGHT) {
            int ft = _team[i] - 1;
            if (ft >= 0 && ft < 2 && _pheromones[ft].is_valid()) {
              Vector3 dp(_pos_x[i], 0.0f, _pos_z[i]);
              _pheromones[ft]->deposit_radius(dp, CH_DANGER, 4.0f, 6.0f);
            }
            _health[i] = 0.0f;
            kill_unit(i);
            _fall_damage_tick++;
            _total_fall_damage_events++;
          } else if (fall_dist > FALL_DAMAGE_THRESH) {
            float dmg = (fall_dist - FALL_DAMAGE_THRESH) * FALL_DAMAGE_PER_M;
            _health[i] -= dmg;
            _fall_damage_tick++;
            _total_fall_damage_events++;
            if (_health[i] <= 0.0f) {
              kill_unit(i);
            } else {
              _state[i] = ST_IDLE;
            }
          } else {
            _state[i] = ST_IDLE;
          }
          _vel_y[i] = 0.0f;
        } else {
          _pos_y[i] = new_y;
        }
      }
      continue;
    }
  }
}

void SimulationServer::_sys_movement_steering(flecs::iter &it,
                                              const ecs::LegacyIndex *idx_comp,
                                              ecs::DesiredVelocity *dv_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    int i = idx_comp[row].val;

    if (_state[i] == ST_DEAD || _state[i] == ST_DOWNED ||
        _state[i] == ST_FROZEN)
      continue;

    if (_state[i] == ST_CLIMBING || _state[i] == ST_FALLING)
      continue;

    float vx = 0.0f, vz = 0.0f;
    float base_speed = _use_context_steering
                           ? SPEED_TABLE[_posture[i]][_move_mode[i]]
                           : _tune_move_speed * _speed_mult(i);

    // Units in static combat states: face target, don't march
    bool in_static_combat =
        (_state[i] == ST_ENGAGING || _state[i] == ST_SUPPRESSING ||
         _state[i] == ST_RELOADING);
    // Units in cover: move toward cover position while facing target
    bool in_cover_moving = (_state[i] == ST_IN_COVER);
    // Units flanking: move toward flank destination
    bool is_flanking = (_state[i] == ST_FLANKING);

    if (in_static_combat && _target_id[i] >= 0 && _alive[_target_id[i]]) {
      // Face toward target
      int32_t t = _target_id[i];
      float dx = _pos_x[t] - _pos_x[i];
      float dz = _pos_z[t] - _pos_z[i];
      float d = std::sqrt(dx * dx + dz * dz);
      if (d > 0.1f) {
        _face_x[i] = dx / d;
        _face_z[i] = dz / d;
      }
      // Tick settle timer while stationary
      if (_settle_timer[i] > 0.0f)
        _settle_timer[i] -= delta;
      // Tick deploy timer (posture-aware: prone/crouch deploy faster)
      if (_deploy_timer[i] > 0.0f) {
        float deploy_rate = 1.0f;
        if (_posture[i] == POST_PRONE)
          deploy_rate = 1.5f;
        else if (_posture[i] == POST_CROUCH)
          deploy_rate = 1.2f;
        _deploy_timer[i] -= delta * deploy_rate;
      }
      // Weak combat formation drift: maintain squad spacing during
      // firefights. Slow enough (0.5 m/s max) to avoid visible "sliding while
      // shooting" artifact. Only activates when >5m from formation slot —
      // in-position units stay put.
      if (_order[i] == ORDER_FOLLOW_SQUAD) {
        int sid = _squad_id[i];
        if (sid >= 0 && sid < MAX_SQUADS && _squads[sid].active &&
            _squad_alive_counts[sid] > 0) {
          Vector3 c = _squad_centroids[sid];
          Vector3 adv = _squads[sid].advance_dir;
          float adv_len = adv.length();
          if (adv_len > 0.01f) {
            adv /= adv_len;
            float lead = GOAL_LEAD_DIST + _squads[sid].advance_offset;
            float spread = _squads[sid].formation_spread;
            int idx = _squad_member_idx[i];
            int total = _squad_alive_counts[sid];
            float perp_x = -adv.z, perp_z = adv.x;
            float sx = c.x + adv.x * lead;
            float sz = c.z + adv.z * lead;
            switch (_squads[sid].formation) {
            case FORM_LINE: {
              float ft =
                  (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f : 0.0f;
              sx += perp_x * ft * spread;
              sz += perp_z * ft * spread;
            } break;
            case FORM_WEDGE: {
              float ft =
                  (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f : 0.0f;
              sx += perp_x * ft * spread;
              sz += perp_z * ft * spread;
              float fb = std::abs(ft) * spread * 0.5f;
              sx -= adv.x * fb;
              sz -= adv.z * fb;
            } break;
            case FORM_COLUMN: {
              float ft = (total > 1) ? (float)idx / (total - 1) : 0.0f;
              sx -= adv.x * ft * spread;
              sz -= adv.z * ft * spread;
            } break;
            case FORM_CIRCLE: {
              float angle = 6.28318530f * idx / std::max(total, 1);
              sx = c.x + std::cos(angle) * spread;
              sz = c.z + std::sin(angle) * spread;
            } break;
            default:
              break;
            }
            float sdx = sx - _pos_x[i], sdz = sz - _pos_z[i];
            float sd = std::sqrt(sdx * sdx + sdz * sdz);
            if (sd > 5.0f) { // wider dead zone — units hold position in combat
              float drift = std::min(_tune_combat_drift,
                                     sd * 0.15f); // ramp up gradually
              vx += (sdx / sd) * drift;
              vz += (sdz / sd) * drift;
            }
          }
        }
      }
    } else if (in_cover_moving) {
      // ── Move to cover position + peek offset while facing target ─
      float target_x = _order_x[i];
      float target_z = _order_z[i];

      // Apply peek offset when peeking (physical sidestep)
      if (_is_peeking[i]) {
        target_x += _peek_offset_x[i];
        target_z += _peek_offset_z[i];
      }

      float dx = target_x - _pos_x[i];
      float dz = target_z - _pos_z[i];
      float dist = std::sqrt(dx * dx + dz * dz);
      if (dist > 0.2f) { // Tighter threshold for peek sliding
        float speed = _is_peeking[i] ? base_speed * 0.8f : base_speed;
        float inv = speed / dist;
        vx += dx * inv;
        vz += dz * inv;
      }
      // Face target while in cover
      if (_target_id[i] >= 0 && _alive[_target_id[i]]) {
        int32_t t = _target_id[i];
        float fdx = _pos_x[t] - _pos_x[i];
        float fdz = _pos_z[t] - _pos_z[i];
        float fd = std::sqrt(fdx * fdx + fdz * fdz);
        if (fd > 0.1f) {
          _face_x[i] = fdx / fd;
          _face_z[i] = fdz / fd;
        }
      }
      // Tick deploy timer while in cover
      if (_deploy_timer[i] > 0.0f) {
        float deploy_rate = 1.0f;
        if (_posture[i] == POST_PRONE)
          deploy_rate = 1.5f;
        else if (_posture[i] == POST_CROUCH)
          deploy_rate = 1.2f;
        _deploy_timer[i] -= delta * deploy_rate;
      }
    } else if (is_flanking) {
      // ── Move to flank position ───────────────────────────────
      float dx = _order_x[i] - _pos_x[i];
      float dz = _order_z[i] - _pos_z[i];
      float dist = std::sqrt(dx * dx + dz * dz);
      if (dist > _tune_arrive_dist) {
        float inv = base_speed * 1.2f / dist; // Slightly faster when flanking
        vx += dx * inv;
        vz += dz * inv;
      }
    } else if (_state[i] == ST_BERSERK) {
      // ── Berserk charge: straight at target, no flow/avoidance ─
      if (_target_id[i] >= 0 && _alive[_target_id[i]]) {
        int32_t t = _target_id[i];
        float dx = _pos_x[t] - _pos_x[i];
        float dz = _pos_z[t] - _pos_z[i];
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > 0.5f) {
          float inv = (_tune_move_speed * BERSERK_SPEED_MULT) / dist;
          vx += dx * inv;
          vz += dz * inv;
        }
        // Face target
        if (dist > 0.1f) {
          _face_x[i] = dx / dist;
          _face_z[i] = dz / dist;
        }
      }
    } else if (!in_static_combat) {
      // ── Context Steering (replaces additive forces) ──────────
      if (_use_context_steering) {
        int base = i * STEER_SLOTS;

        // 1. Clear interest and danger maps
        for (int s = 0; s < STEER_SLOTS; s++) {
          _steer_interest[base + s] = 0.0f;
          _steer_danger[base + s] = 0.0f;
        }

        // 2a. Compute order direction (formation slot or direct order)
        float order_dx = 0.0f, order_dz = 0.0f;
        float order_dist = 0.0f; // distance to formation slot (for urgency)
        bool has_order_dir = false;

        if (_order[i] == ORDER_FOLLOW_SQUAD) {
          int sid = _squad_id[i];
          if (sid >= 0 && sid < MAX_SQUADS && _squads[sid].active &&
              _squad_alive_counts[sid] > 0) {
            Vector3 centroid = _squad_centroids[sid];
            Vector3 dir = _squads[sid].advance_dir;
            float dir_len = dir.length();
            if (dir_len > 0.01f) {
              dir /= dir_len;
              float lead = GOAL_LEAD_DIST + _squads[sid].advance_offset;
              // Detected enemy beyond weapon range: extra pull to close gap
              if (_has_visible_enemy[i] && _target_id[i] < 0) {
                lead += 4.0f;
              }
              float spread = _squads[sid].formation_spread;
              int idx = _squad_member_idx[i];
              int total = _squad_alive_counts[sid];
              float perp_x = -dir.z, perp_z = dir.x;
              float slot_x = centroid.x + dir.x * lead;
              float slot_z = centroid.z + dir.z * lead;

              switch (_squads[sid].formation) {
              case FORM_LINE: {
                float t = (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f
                                      : 0.0f;
                slot_x += perp_x * t * spread;
                slot_z += perp_z * t * spread;
              } break;
              case FORM_WEDGE: {
                float t = (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f
                                      : 0.0f;
                slot_x += perp_x * t * spread;
                slot_z += perp_z * t * spread;
                float fallback = std::abs(t) * spread * 0.5f;
                slot_x -= dir.x * fallback;
                slot_z -= dir.z * fallback;
              } break;
              case FORM_COLUMN: {
                float t = (total > 1) ? (float)idx / (total - 1) : 0.0f;
                slot_x -= dir.x * t * spread;
                slot_z -= dir.z * t * spread;
              } break;
              case FORM_CIRCLE: {
                float angle = 6.28318530f * idx / std::max(total, 1);
                slot_x = centroid.x + std::cos(angle) * spread;
                slot_z = centroid.z + std::sin(angle) * spread;
              } break;
              default:
                break;
              }

              float raw_dx = slot_x - _pos_x[i];
              float raw_dz = slot_z - _pos_z[i];
              // Decompose into along-advance and perpendicular components.
              // Formation pull is STRONG perpendicular (maintains LINE shape)
              // but WEAK along-advance (flow handles forward movement).
              // This prevents combat-frozen units from accumulating slot
              // distance.
              float along = raw_dx * dir.x + raw_dz * dir.z;
              float perp_raw = raw_dx * perp_x + raw_dz * perp_z;
              float along_weight =
                  0.3f; // advance handled by flow, not formation
              order_dx = perp_x * perp_raw + dir.x * along * along_weight;
              order_dz = perp_z * perp_raw + dir.z * along * along_weight;
              float dist = std::sqrt(order_dx * order_dx + order_dz * order_dz);
              float perp_dist =
                  std::abs(perp_raw); // perpendicular dist drives urgency
              // Hysteresis: HARD start threshold, SOFT stop threshold
              float start_thresh = _tune_arrive_dist * 1.5f;
              float stop_thresh = _tune_arrive_dist * 0.8f;

              if (_state[i] == ST_IDLE) {
                if (dist > start_thresh) {
                  _state[i] = ST_MOVING;
                }
              } else if (_state[i] == ST_MOVING) {
                if (dist < stop_thresh) {
                  _state[i] = ST_IDLE;
                }
              }

              if (_state[i] == ST_MOVING) {
                // Determine damping factor based on distance
                float ramp = std::clamp((dist - stop_thresh) /
                                            (start_thresh - stop_thresh),
                                        0.1f, 1.0f);

                order_dx /= dist;
                order_dz /= dist;
                has_order_dir = true;
                order_dist = std::max(perp_dist, dist * 0.5f);
                _avg_formation_pull += base_speed;

                // Pass ramp to steering via member var or encode in
                // order_dist? Actually, relying on context steering to damp
                // is tricky. Let's modify base_speed for this unit
                // effectively. But base_speed is local var. We can modify the
                // weights.
              }
            }
          }
        } else if (_order[i] == ORDER_MOVE || _order[i] == ORDER_ATTACK ||
                   _order[i] == ORDER_RETREAT) {
          order_dx = _order_x[i] - _pos_x[i];
          order_dz = _order_z[i] - _pos_z[i];
          float dist = std::sqrt(order_dx * order_dx + order_dz * order_dz);
          if (dist > _tune_arrive_dist) {
            order_dx /= dist;
            order_dz /= dist;
            has_order_dir = true;
            if (_state[i] == ST_IDLE)
              _state[i] = ST_MOVING;
          } else {
            if (_state[i] == ST_MOVING)
              _state[i] = ST_IDLE;
          }
        }

        // 2b. Project order direction onto interest slots (distance-scaled
        // urgency) Also add catch-up interest for units behind their
        // formation slot
        float formation_urgency =
            0.0f; // stored for pheromone scaling + danger immunity
        if (has_order_dir) {
          formation_urgency = std::min(order_dist / FORMATION_URGENCY_SCALE,
                                       FORMATION_URGENCY_MAX);
          for (int s = 0; s < STEER_SLOTS; s++) {
            float dot = order_dx * SLOT_DIR_X[s] + order_dz * SLOT_DIR_Z[s];
            if (dot > 0.0f) {
              _steer_interest[base + s] +=
                  dot * _tune_steer_order * formation_urgency;
            } else if (dot > -0.5f && order_dist > _tune_arrive_dist * 2.0f) {
              // Catch-up: unit is roughly perpendicular or slightly behind
              // the slot direction. Add weak interest so it can rejoin.
              _steer_interest[base + s] +=
                  (-dot) * _tune_catchup_weight * formation_urgency;
            }
          }
        }
        // Pheromone scaling: reduce pheromone influence when far from
        // formation slot. Near slot (urgency~0): full pheromone for tactical
        // intelligence. Far from slot (urgency~5): pheromone at ~17%,
        // formation dominates.
        float phero_urgency_scale = 1.0f / (1.0f + formation_urgency);

        // 2c. Project GPU flow field onto interest slots (always sample when
        // available)
        if (_gpu_map.is_valid()) {
          float fw = FLOW_WEIGHT_IDLE;
          if (_order[i] == ORDER_FOLLOW_SQUAD || _order[i] == ORDER_RETREAT)
            fw = _tune_flow_weight_squad;
          else if (_order[i] == ORDER_MOVE || _order[i] == ORDER_ATTACK)
            fw = _tune_flow_weight_move;

          // Spatial noise: offset lookup by hash to prevent conga-line
          float noise_off =
              ((float)(((uint32_t)i * 2654435761u) & 0xFFFF) / 65536.0f -
               0.5f) *
              1.0f;
          Vector3 flow_pos(_pos_x[i] + noise_off, _pos_y[i],
                           _pos_z[i] + noise_off);
          Vector3 flow = _gpu_map->get_flow_vector(flow_pos);
          // GPU flow field seeded with Team 1 goals; invert for Team 2
          // so both teams advance toward center contact zone.
          if (_team[i] == 2) {
            flow.x = -flow.x;
            flow.z = -flow.z;
          }
          float flow_mag = std::sqrt(flow.x * flow.x + flow.z * flow.z);
          if (flow_mag > 0.01f) {
            float inv_m = 1.0f / flow_mag;
            float fdx = flow.x * inv_m, fdz = flow.z * inv_m;
            for (int s = 0; s < STEER_SLOTS; s++) {
              float dot = fdx * SLOT_DIR_X[s] + fdz * SLOT_DIR_Z[s];
              if (dot > 0.0f) {
                _steer_interest[base + s] += dot * _tune_steer_flow * fw;
              }
            }
            _avg_flow_push += flow_mag * fw;
          }
        }

        // 2d. Positive pheromone gradients → interest
        int ti = _team[i] > 0 && _team[i] <= 2 ? _team[i] - 1 : -1;
        if (ti >= 0 && _pheromones[ti].is_valid()) {
          Vector3 pos(_pos_x[i], 0.0f, _pos_z[i]);
          auto pw = _role_pheromone_weights(_role[i]);

          // SAFE_ROUTE: interest toward safe paths (raw gradient preserves
          // magnitude)
          Vector3 sg = _pheromones[ti]->gradient_raw(pos, CH_SAFE_ROUTE);
          float sg_mag = std::sqrt(sg.x * sg.x + sg.z * sg.z);
          if (sg_mag > 0.01f) {
            float inv_sg = 1.0f / sg_mag;
            float sg_w = std::min(sg_mag, 4.0f);
            for (int s = 0; s < STEER_SLOTS; s++) {
              float dot = (sg.x * inv_sg) * SLOT_DIR_X[s] +
                          (sg.z * inv_sg) * SLOT_DIR_Z[s];
              if (dot > 0.0f)
                _steer_interest[base + s] += dot * _tune_steer_pheromone *
                                             pw.safe_route * sg_w *
                                             phero_urgency_scale;
            }
          }

          // FLANK_OPP: interest toward flanking opportunities
          Vector3 fg = _pheromones[ti]->gradient_raw(pos, CH_FLANK_OPP);
          float fg_mag = std::sqrt(fg.x * fg.x + fg.z * fg.z);
          if (fg_mag > 0.01f) {
            float inv_fg = 1.0f / fg_mag;
            float fg_w = std::min(fg_mag, 4.0f);
            for (int s = 0; s < STEER_SLOTS; s++) {
              float dot = (fg.x * inv_fg) * SLOT_DIR_X[s] +
                          (fg.z * inv_fg) * SLOT_DIR_Z[s];
              if (dot > 0.0f)
                _steer_interest[base + s] += dot * _tune_steer_pheromone *
                                             pw.flank_opp * fg_w *
                                             phero_urgency_scale;
            }
          }

          // RALLY pull for broken/retreating units
          if (_morale[i] < 0.3f || _state[i] == ST_RETREATING) {
            Vector3 rg = _pheromones[ti]->gradient_raw(pos, CH_RALLY);
            float rg_mag = std::sqrt(rg.x * rg.x + rg.z * rg.z);
            if (rg_mag > 0.01f) {
              float inv_rg = 1.0f / rg_mag;
              float rg_w = std::min(rg_mag, 4.0f);
              for (int s = 0; s < STEER_SLOTS; s++) {
                float dot = (rg.x * inv_rg) * SLOT_DIR_X[s] +
                            (rg.z * inv_rg) * SLOT_DIR_Z[s];
                if (dot > 0.0f)
                  _steer_interest[base + s] += dot * _tune_steer_pheromone *
                                               pw.rally * rg_w * 1.5f *
                                               phero_urgency_scale;
              }
            }
          }

          // CH_STRATEGIC: LLM stigmergic pull (all units follow strategic
          // gradient)
          Vector3 stg = _pheromones[ti]->gradient_raw(pos, CH_STRATEGIC);
          float stg_mag = std::sqrt(stg.x * stg.x + stg.z * stg.z);
          if (stg_mag > 0.01f) {
            float inv_stg = 1.0f / stg_mag;
            float stg_w = std::min(stg_mag, 4.0f);
            for (int s = 0; s < STEER_SLOTS; s++) {
              float dot = (stg.x * inv_stg) * SLOT_DIR_X[s] +
                          (stg.z * inv_stg) * SLOT_DIR_Z[s];
              if (dot > 0.0f)
                _steer_interest[base + s] += dot * _tune_steer_pheromone *
                                             pw.strategic * stg_w *
                                             phero_urgency_scale;
            }
          }

          // 3. Populate danger maps
          for (int s = 0; s < STEER_SLOTS; s++) {
            float sample_x =
                _pos_x[i] + SLOT_DIR_X[s] * _tune_steer_sample_dist;
            float sample_z =
                _pos_z[i] + SLOT_DIR_Z[s] * _tune_steer_sample_dist;
            Vector3 sample_pos(sample_x, 0.0f, sample_z);

            // DANGER pheromone at sample point
            float danger_val = _pheromones[ti]->sample(sample_pos, CH_DANGER);
            // SUPPRESSION pheromone at sample point
            float supp_val =
                _pheromones[ti]->sample(sample_pos, CH_SUPPRESSION);
            float total_danger =
                danger_val * pw.danger + supp_val * pw.suppression;

            // DDA LOS gate: if wall blocks LOS to sample point, zero out
            // danger (prevents fleeing from explosions behind cover)
            if (total_danger > 0.1f) {
              VoxelWorld *vw_steer = VoxelWorld::get_singleton();
              if (vw_steer) {
                Vector3 from(_pos_x[i], _pos_y[i] + 1.0f, _pos_z[i]);
                float dx_ray = sample_x - _pos_x[i];
                float dz_ray = sample_z - _pos_z[i];
                float ray_len = std::sqrt(dx_ray * dx_ray + dz_ray * dz_ray);
                if (ray_len > 0.1f) {
                  Vector3 ray_dir(dx_ray / ray_len, 0.0f, dz_ray / ray_len);
                  VoxelHit vhit;
                  if (vw_steer->raycast(from, ray_dir, ray_len, vhit) &&
                      vhit.hit) {
                    total_danger = 0.0f; // wall blocks — ignore this danger
                  }
                }
              }
            }

            _steer_danger[base + s] += total_danger * _tune_steer_danger;
          }
        }

        // 3b. Map border danger
        for (int s = 0; s < STEER_SLOTS; s++) {
          float edge_x = _pos_x[i] + SLOT_DIR_X[s] * _tune_steer_border_dist;
          float edge_z = _pos_z[i] + SLOT_DIR_Z[s] * _tune_steer_border_dist;
          if (edge_x < -_map_half_w || edge_x > _map_half_w ||
              edge_z < -_map_half_h || edge_z > _map_half_h) {
            _steer_danger[base + s] += 1.0f;
          }
        }

        // 3c. Voxel obstacle look-ahead
        {
          VoxelWorld *vw_obs = VoxelWorld::get_singleton();
          if (vw_obs) {
            float vs = vw_obs->get_voxel_scale();
            int body_v = _body_voxels(i);
            int feet_vy = (int)(_pos_y[i] / vs);
            for (int s = 0; s < STEER_SLOTS; s++) {
              float probe_x =
                  _pos_x[i] + SLOT_DIR_X[s] * _tune_steer_obstacle_dist;
              float probe_z =
                  _pos_z[i] + SLOT_DIR_Z[s] * _tune_steer_obstacle_dist;
              int pvx = (int)((probe_x + _map_half_w) / vs);
              int pvz = (int)((probe_z + _map_half_h) / vs);
              // Check if body would collide (above vault height)
              int wall_h = 0;
              for (int dy = 1; dy <= body_v + VAULT_MAX_VOXELS; dy++) {
                if (vw_obs->is_solid(pvx, feet_vy + dy, pvz))
                  wall_h = dy;
                else if (dy > wall_h + 1)
                  break;
              }
              if (wall_h > VAULT_MAX_VOXELS) {
                _steer_danger[base + s] += 0.8f; // wall too tall to vault
              }
            }
          }
        }

        // 4. Gaussian blur both maps (circular: wrap around)
        {
          float temp[STEER_SLOTS];
          // Blur interest
          for (int s = 0; s < STEER_SLOTS; s++) {
            int prev = (s + STEER_SLOTS - 1) % STEER_SLOTS;
            int next = (s + 1) % STEER_SLOTS;
            temp[s] = _steer_interest[base + prev] * STEER_BLUR_KERNEL[0] +
                      _steer_interest[base + s] * STEER_BLUR_KERNEL[1] +
                      _steer_interest[base + next] * STEER_BLUR_KERNEL[2];
          }
          for (int s = 0; s < STEER_SLOTS; s++)
            _steer_interest[base + s] = temp[s];
          // Blur danger
          for (int s = 0; s < STEER_SLOTS; s++) {
            int prev = (s + STEER_SLOTS - 1) % STEER_SLOTS;
            int next = (s + 1) % STEER_SLOTS;
            temp[s] = _steer_danger[base + prev] * STEER_BLUR_KERNEL[0] +
                      _steer_danger[base + s] * STEER_BLUR_KERNEL[1] +
                      _steer_danger[base + next] * STEER_BLUR_KERNEL[2];
          }
          for (int s = 0; s < STEER_SLOTS; s++)
            _steer_danger[base + s] = temp[s];
        }

        // 5. Temporal EMA smoothing (using previous frame values already in
        // arrays) Note: first frame values are 0, so EMA bootstraps naturally
        // EMA is applied implicitly since we didn't clear the arrays to 0 at
        // the top — Actually we did clear them. So we apply EMA here by
        // re-blending: This is a simplification: on first pass, prev=0 so
        // result = alpha * current On subsequent frames, the arrays carry
        // forward from last frame. Since we cleared at step 1, we need to NOT
        // clear and instead blend. Fix: remove the clear and do: new = alpha
        // * computed + (1-alpha) * old But that requires temp storage.
        // Simpler: just use the blur result directly. The locomotion physics
        // (springs) already provides output smoothing. Skip separate EMA for
        // now — double hysteresis from blur + springs is sufficient.

        // 6. Combine: multiplicative masking (with formation danger immunity)
        // Formation-aligned directions get partial danger immunity so that
        // units can push through danger zones to reach their formation slots.
        // Without this, danger toward the enemy zeroes out ALL interest in
        // that direction, scattering units sideways and destroying formation
        // shapes.
        float combined[STEER_SLOTS];
        float danger_immune = std::min(formation_urgency * 0.15f,
                                       0.6f); // up to 60% danger reduction
        for (int s = 0; s < STEER_SLOTS; s++) {
          float d = std::min(_steer_danger[base + s], 1.0f);
          if (danger_immune > 0.0f && has_order_dir) {
            // Reduce danger masking in directions aligned with formation slot
            float align = order_dx * SLOT_DIR_X[s] + order_dz * SLOT_DIR_Z[s];
            if (align > 0.0f) {
              d *= (1.0f - danger_immune * align);
            }
          }
          combined[s] = _steer_interest[base + s] * (1.0f - d);
        }

        // 7. Resolve: best slot + quadratic sub-slot interpolation
        int best = 0;
        float best_val = combined[0];
        for (int s = 1; s < STEER_SLOTS; s++) {
          if (combined[s] > best_val) {
            best_val = combined[s];
            best = s;
          }
        }

        if (best_val > 0.001f) {
          // Sub-slot interpolation for smoother output
          int prev = (best + STEER_SLOTS - 1) % STEER_SLOTS;
          int next = (best + 1) % STEER_SLOTS;
          float vp = combined[prev], vn = combined[next];
          float offset = 0.0f;
          float denom = 2.0f * (2.0f * best_val - vp - vn);
          if (std::abs(denom) > 0.001f) {
            offset = (vp - vn) / denom;
            offset = std::clamp(offset, -0.5f, 0.5f);
          }

          // Interpolate direction
          float frac = offset;
          int other = (frac >= 0.0f) ? next : prev;
          float abs_frac = std::abs(frac);
          float dir_x = SLOT_DIR_X[best] * (1.0f - abs_frac) +
                        SLOT_DIR_X[other] * abs_frac;
          float dir_z = SLOT_DIR_Z[best] * (1.0f - abs_frac) +
                        SLOT_DIR_Z[other] * abs_frac;
          float dir_len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
          if (dir_len > 0.001f) {
            dir_x /= dir_len;
            dir_z /= dir_len;
          }

          // 8. Speed from SPEED_TABLE
          // 8. Speed from SPEED_TABLE, damped by arrival proximity if
          // following order
          float final_speed = base_speed;
          if (has_order_dir && best_val > 0.001f) {
            // Check alignment with order
            float dot = dir_x * order_dx + dir_z * order_dz;
            if (dot > 0.7f) {
              // We are moving towards the objective.
              // Calculate distance again (cached in order_dist? no)
              // Re-calculate dist roughly or pass it down.
              // Let's use the earlier hysteresis result.
              // If we are very close to formation slot, we should slow down.
              // We defined start/stop thresholds above.
              float start_thresh = _tune_arrive_dist * 1.5f;
              float stop_thresh = _tune_arrive_dist * 0.8f;
              // Ideally we'd have the raw distance but it is lost in the
              // steering logic. However, we can approximate: if best slot is
              // aligned with order_dx/dz, and order magnitude was small...
              // Actually, simpler: scaling velocity by how strongly the
              // context system "wants" to move in that direction (best_val)
              // is natural damping. best_val is combined interest (0..1). If
              // we close to target, interest generated by order should be
              // lower? Currently order interest is binary/constant 1.0
              // (modified by curve). Better fix: scale 'base_speed' up in the
              // hysteresis block.
            }
          }
          vx = dir_x * final_speed;
          vz = dir_z * base_speed;
        } else {
          // No interest anywhere — stay put
          vx = 0.0f;
          vz = 0.0f;
        }

      } else {
        // ── OLD ADDITIVE FORCES (A/B toggle fallback) ─────────
        // ── 1. Order-based velocity ──────────────────────────────
        if (_order[i] == ORDER_FOLLOW_SQUAD) {
          int sid = _squad_id[i];
          if (sid >= 0 && sid < MAX_SQUADS && _squads[sid].active &&
              _squad_alive_counts[sid] > 0) {
            Vector3 centroid = _squad_centroids[sid];
            Vector3 dir = _squads[sid].advance_dir;
            float dir_len = dir.length();
            if (dir_len > 0.01f) {
              dir /= dir_len;
              float lead = GOAL_LEAD_DIST + _squads[sid].advance_offset;
              float spread = _squads[sid].formation_spread;
              int idx = _squad_member_idx[i];
              int total = _squad_alive_counts[sid];
              float perp_x = -dir.z, perp_z = dir.x;
              float slot_x = centroid.x + dir.x * lead;
              float slot_z = centroid.z + dir.z * lead;
              switch (_squads[sid].formation) {
              case FORM_LINE: {
                float t = (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f
                                      : 0.0f;
                slot_x += perp_x * t * spread;
                slot_z += perp_z * t * spread;
              } break;
              case FORM_WEDGE: {
                float t = (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f
                                      : 0.0f;
                slot_x += perp_x * t * spread;
                slot_z += perp_z * t * spread;
                float fallback = std::abs(t) * spread * 0.5f;
                slot_x -= dir.x * fallback;
                slot_z -= dir.z * fallback;
              } break;
              case FORM_COLUMN: {
                float t = (total > 1) ? (float)idx / (total - 1) : 0.0f;
                slot_x -= dir.x * t * spread;
                slot_z -= dir.z * t * spread;
              } break;
              case FORM_CIRCLE: {
                float angle = 6.28318530f * idx / std::max(total, 1);
                slot_x = centroid.x + std::cos(angle) * spread;
                slot_z = centroid.z + std::sin(angle) * spread;
              } break;
              default:
                break;
              }
              float dx = slot_x - _pos_x[i];
              float dz = slot_z - _pos_z[i];
              float dist = std::sqrt(dx * dx + dz * dz);
              if (dist > _tune_arrive_dist) {
                float inv = base_speed / dist;
                vx += dx * inv;
                vz += dz * inv;
                _avg_formation_pull += base_speed;
                if (_state[i] == ST_IDLE)
                  _state[i] = ST_MOVING;
              } else {
                if (_state[i] == ST_MOVING)
                  _state[i] = ST_IDLE;
              }
            }
          }
        } else if (_order[i] == ORDER_MOVE || _order[i] == ORDER_ATTACK ||
                   _order[i] == ORDER_RETREAT) {
          float dx = _order_x[i] - _pos_x[i];
          float dz = _order_z[i] - _pos_z[i];
          float dist = std::sqrt(dx * dx + dz * dz);
          if (dist > _tune_arrive_dist) {
            float inv = base_speed / dist;
            vx += dx * inv;
            vz += dz * inv;
            if (_state[i] == ST_IDLE)
              _state[i] = ST_MOVING;
          } else {
            if (_state[i] == ST_MOVING)
              _state[i] = ST_IDLE;
          }
        }

        // ── 2. GPU pressure flow bias (adaptive weight) ─────────
        if (_gpu_map.is_valid()) {
          float fw = FLOW_WEIGHT_IDLE;
          if (_order[i] == ORDER_FOLLOW_SQUAD || _order[i] == ORDER_RETREAT)
            fw = _tune_flow_weight_squad;
          else if (_order[i] == ORDER_MOVE || _order[i] == ORDER_ATTACK)
            fw = _tune_flow_weight_move;
          Vector3 pos(_pos_x[i], _pos_y[i], _pos_z[i]);
          Vector3 flow = _gpu_map->get_flow_vector(pos);
          // GPU flow field seeded with Team 1 goals; invert for Team 2
          if (_team[i] == 2) {
            flow.x = -flow.x;
            flow.z = -flow.z;
          }
          vx += flow.x * fw;
          vz += flow.z * fw;
          _avg_flow_push += std::sqrt(flow.x * flow.x + flow.z * flow.z) * fw;
        }

        // ── 3. Influence map threat avoidance ────────────────────
        if (_state[i] != ST_ENGAGING && _state[i] != ST_SUPPRESSING &&
            _team[i] > 0 && _team[i] <= 2) {
          int team_idx = _team[i] - 1;
          if (_influence_map[team_idx].is_valid()) {
            Vector3 pos(_pos_x[i], _pos_y[i], _pos_z[i]);
            float threat = _influence_map[team_idx]->get_threat_at(pos);
            if (threat > 2.0f) {
              float step = 4.0f;
              float tN = _influence_map[team_idx]->get_threat_at(
                  pos + Vector3(0, 0, step));
              float tS = _influence_map[team_idx]->get_threat_at(
                  pos + Vector3(0, 0, -step));
              float tE = _influence_map[team_idx]->get_threat_at(
                  pos + Vector3(step, 0, 0));
              float tW = _influence_map[team_idx]->get_threat_at(
                  pos + Vector3(-step, 0, 0));
              float grad_x = tW - tE, grad_z = tS - tN;
              float grad_len = std::sqrt(grad_x * grad_x + grad_z * grad_z);
              if (grad_len > 0.1f) {
                vx += (grad_x / grad_len) * 1.0f;
                vz += (grad_z / grad_len) * 1.0f;
                _avg_threat_push += 1.0f;
              }
            }
          }
        }

        // ── 4. Pheromone movement biases ─────────────────────────
        if (_team[i] > 0 && _team[i] <= 2) {
          int pti = _team[i] - 1;
          if (_pheromones[pti].is_valid()) {
            Vector3 pos(_pos_x[i], 0.0f, _pos_z[i]);
            auto pw = _role_pheromone_weights(_role[i]);
            if (_state[i] != ST_ENGAGING && _state[i] != ST_SUPPRESSING) {
              Vector3 dg = _pheromones[pti]->gradient(pos, CH_DANGER);
              vx -= dg.x * 0.8f * pw.danger;
              vz -= dg.z * 0.8f * pw.danger;
              Vector3 suppg = _pheromones[pti]->gradient(pos, CH_SUPPRESSION);
              vx -= suppg.x * 1.0f * pw.suppression;
              vz -= suppg.z * 1.0f * pw.suppression;
            }
            Vector3 sg = _pheromones[pti]->gradient(pos, CH_SAFE_ROUTE);
            vx += sg.x * 0.3f * pw.safe_route;
            vz += sg.z * 0.3f * pw.safe_route;
            if (_morale[i] < 0.3f || _state[i] == ST_RETREATING) {
              Vector3 rg = _pheromones[pti]->gradient(pos, CH_RALLY);
              vx += rg.x * 1.5f * pw.rally;
              vz += rg.z * 1.5f * pw.rally;
            }
          }
        }
      } // end A/B toggle
    }

    dv_comp[row].vx = vx;
    dv_comp[row].vz = vz;
  }
}

void SimulationServer::_sys_movement_orca(flecs::iter &it,
                                          const ecs::LegacyIndex *idx_comp,
                                          ecs::DesiredVelocity *dv_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    int i = idx_comp[row].val;

    if (_state[i] == ST_DEAD || _state[i] == ST_DOWNED ||
        _state[i] == ST_FROZEN)
      continue;

    if (_state[i] == ST_CLIMBING || _state[i] == ST_FALLING)
      continue;

    float vx = dv_comp[row].vx;
    float vz = dv_comp[row].vz;
    float base_speed = _use_context_steering
                           ? SPEED_TABLE[_posture[i]][_move_mode[i]]
                           : _tune_move_speed * _speed_mult(i);
    bool in_static_combat =
        (_state[i] == ST_ENGAGING || _state[i] == ST_SUPPRESSING ||
         _state[i] == ST_RELOADING);

    // ── 3. ORCA collision avoidance / boids fallback ──────────────
    // Static combat units must be 100% stationary (no drift)
    if (_state[i] != ST_BERSERK && !in_static_combat) {
      if (_use_orca) {
        // ── ORCA: compute half-planes from nearby agents + walls, solve LP
        // ──
        OrcaLine orca_lines[ORCA_MAX_NEIGHBORS + ORCA_MAX_WALL_LINES];
        int n_lines = 0;

        float pref_vx = vx, pref_vz = vz;
        float inv_tau = 1.0f / _tune_orca_time_horizon;

        // ── Wall constraints: probe 8 directions (cardinal + diagonal) for
        // nearby walls ──
        {
          VoxelWorld *vw_orca = VoxelWorld::get_singleton();
          if (vw_orca) {
            float vs = vw_orca->get_voxel_scale();
            int feet_vy = (int)(_pos_y[i] / vs);
            int bv = _body_voxels(i);
            static constexpr float INV_SQRT2 = 0.70710678f;
            static const float PROBE_DX[] = {1.0f,      -1.0f,     0.0f,
                                             0.0f,      INV_SQRT2, -INV_SQRT2,
                                             INV_SQRT2, -INV_SQRT2};
            static const float PROBE_DZ[] = {0.0f,       0.0f,      1.0f,
                                             -1.0f,      INV_SQRT2, -INV_SQRT2,
                                             -INV_SQRT2, INV_SQRT2};
            for (int d = 0; d < 8 && n_lines < ORCA_MAX_WALL_LINES; d++) {
              float probe_x = _pos_x[i] + PROBE_DX[d] * _tune_orca_wall_probe;
              float probe_z = _pos_z[i] + PROBE_DZ[d] * _tune_orca_wall_probe;
              int pvx = (int)((probe_x + _map_half_w) / vs);
              int pvz = (int)((probe_z + _map_half_h) / vs);
              bool wall = false;
              for (int dy = 1; dy <= bv; dy++) {
                if (vw_orca->is_solid(pvx, feet_vy + dy, pvz)) {
                  wall = true;
                  break;
                }
              }
              if (wall) {
                // Half-plane: push velocity away from wall
                OrcaLine &line = orca_lines[n_lines];
                line.nx = -PROBE_DX[d]; // normal points AWAY from wall
                line.nz = -PROBE_DZ[d];
                // Point on half-plane: preferred velocity projected to wall
                // boundary
                line.px = pref_vx;
                line.pz = pref_vz;
                // Remove any component toward the wall
                float dot = pref_vx * PROBE_DX[d] + pref_vz * PROBE_DZ[d];
                if (dot > 0.0f) {
                  line.px -= PROBE_DX[d] * dot;
                  line.pz -= PROBE_DZ[d] * dot;
                }
                n_lines++;
              }
            }
          }
        }

        // Gather neighbors via spatial hash (keep closest
        // ORCA_MAX_NEIGHBORS)
        struct Neighbor {
          int32_t id;
          float dist_sq;
        };
        Neighbor neighbors[ORCA_MAX_NEIGHBORS];
        int n_neighbors = 0;

        int cx_cell = (int)((_pos_x[i] + _map_half_w) / (float)SPATIAL_CELL_M);
        int cz_cell = (int)((_pos_z[i] + _map_half_h) / (float)SPATIAL_CELL_M);
        cx_cell = std::clamp(cx_cell, 0, _spatial_w - 1);
        cz_cell = std::clamp(cz_cell, 0, _spatial_h - 1);

        float nd2 = _tune_orca_neighbor_dist * _tune_orca_neighbor_dist;
        for (int dz2 = -1; dz2 <= 1; dz2++) {
          for (int dx2 = -1; dx2 <= 1; dx2++) {
            int ncx = cx_cell + dx2;
            int ncz = cz_cell + dz2;
            if (ncx < 0 || ncx >= _spatial_w || ncz < 0 || ncz >= _spatial_h)
              continue;

            int32_t idx = _spatial_cells[ncz * _spatial_w + ncx];
            while (idx >= 0) {
              if (idx != i && _alive[idx]) {
                float rx = _pos_x[idx] - _pos_x[i];
                float rz = _pos_z[idx] - _pos_z[i];
                float d2 = rx * rx + rz * rz;
                if (d2 < nd2) {
                  // Insert into sorted neighbor list (ascending dist_sq)
                  if (n_neighbors < ORCA_MAX_NEIGHBORS) {
                    int ins = n_neighbors;
                    while (ins > 0 && neighbors[ins - 1].dist_sq > d2) {
                      neighbors[ins] = neighbors[ins - 1];
                      ins--;
                    }
                    neighbors[ins] = {idx, d2};
                    n_neighbors++;
                  } else if (d2 < neighbors[ORCA_MAX_NEIGHBORS - 1].dist_sq) {
                    int ins = ORCA_MAX_NEIGHBORS - 1;
                    while (ins > 0 && neighbors[ins - 1].dist_sq > d2) {
                      neighbors[ins] = neighbors[ins - 1];
                      ins--;
                    }
                    neighbors[ins] = {idx, d2};
                  }
                }
              }
              idx = _spatial_next[idx];
            }
          }
        }

        // Compute ORCA half-plane for each neighbor
        for (int ni = 0; ni < n_neighbors; ni++) {
          int j = neighbors[ni].id;
          float rel_px = _pos_x[j] - _pos_x[i];
          float rel_pz = _pos_z[j] - _pos_z[i];
          float dist_sq = rel_px * rel_px + rel_pz * rel_pz;
          float dist = sqrtf(dist_sq);

          // Neighbor's current velocity (use actual smoothed velocity)
          float ov_x = _actual_vx[j];
          float ov_z = _actual_vz[j];

          // Relative velocity: our preferred - their actual
          float rv_x = pref_vx - ov_x;
          float rv_z = pref_vz - ov_z;

          // Squad-aware radius: tight for same-squad allies (medieval
          // formations)
          bool same_squad =
              (_squad_id[i] == _squad_id[j]) && (_team[i] == _team[j]);
          bool j_static =
              (_state[j] == ST_ENGAGING || _state[j] == ST_SUPPRESSING ||
               _state[j] == ST_RELOADING || _state[j] == ST_IN_COVER);
          float agent_r =
              same_squad ? ORCA_SQUAD_RADIUS : _tune_orca_agent_radius;
          float combined_r = 2.0f * agent_r;
          // Responsibility: 30% for same-squad, 50% other allies, 100%
          // enemies/static
          float resp = same_squad                            ? 0.3f
                       : (_team[i] == _team[j] && !j_static) ? 0.5f
                                                             : 1.0f;

          OrcaLine &line = orca_lines[n_lines];

          if (dist < combined_r) {
            // ── Case A: Already overlapping — push apart ──
            if (dist > ORCA_EPSILON) {
              // Normal points from j toward i (push i away from j)
              float inv_d = 1.0f / dist;
              line.nx = -rel_px * inv_d; // points toward i (away from j)
              line.nz = -rel_pz * inv_d;
            } else {
              // Degenerate: on top of each other — arbitrary direction
              line.nx = 1.0f;
              line.nz = 0.0f;
            }
            // Penalty velocity along normal (capped to avoid extreme
            // deflection)
            float pen_speed = (combined_r - dist) / delta;
            float max_pen = base_speed * 2.0f; // cap to 2× move speed
            if (pen_speed > max_pen)
              pen_speed = max_pen;
            float u_x = line.nx * pen_speed;
            float u_z = line.nz * pen_speed;
            line.px = pref_vx + u_x * resp;
            line.pz = pref_vz + u_z * resp;
          } else {
            // ── Case B: Normal ORCA — truncated velocity obstacle ──
            // Center of truncated VO circle in velocity space
            float cut_cx = rel_px * inv_tau;
            float cut_cz = rel_pz * inv_tau;
            float cut_r = combined_r * inv_tau;

            // Vector from truncated circle center to relative velocity
            float w_x = rv_x - cut_cx;
            float w_z = rv_z - cut_cz;
            float w_len_sq = w_x * w_x + w_z * w_z;

            float dot_rp = w_x * rel_px + w_z * rel_pz;

            if (dot_rp < 0.0f && dot_rp * dot_rp > cut_r * cut_r * w_len_sq) {
              // Closest to the truncation circle
              float w_len = sqrtf(w_len_sq);
              if (w_len > ORCA_EPSILON) {
                float inv_wl = 1.0f / w_len;
                // Normal points outward from circle
                line.nx = w_x * inv_wl;
                line.nz = w_z * inv_wl;
                // u = project relative velocity onto boundary
                float u_mag = cut_r - w_len;
                float u_x = line.nx * u_mag;
                float u_z = line.nz * u_mag;
                line.px = pref_vx + u_x * resp;
                line.pz = pref_vz + u_z * resp;
              } else {
                // On the circle center — use relative position as normal
                float inv_d = 1.0f / dist;
                line.nx = -rel_px * inv_d;
                line.nz = -rel_pz * inv_d;
                line.px = pref_vx;
                line.pz = pref_vz;
              }
            } else {
              // Closest to one of the cone legs
              // Leg direction: tangent from origin to the Minkowski disc
              float leg_sq = dist_sq - combined_r * combined_r;
              if (leg_sq < ORCA_EPSILON)
                leg_sq = ORCA_EPSILON;
              float leg = sqrtf(leg_sq);
              float inv_dist = 1.0f / dist;

              // Left leg direction (rotate rel_pos by angle)
              float cos_a = leg * inv_dist;
              float sin_a = combined_r * inv_dist;
              // Check which side the relative velocity is on
              float det = rel_px * rv_z - rel_pz * rv_x;

              float dir_x, dir_z;
              if (det >= 0.0f) {
                // Left leg
                dir_x = (rel_px * cos_a + rel_pz * sin_a) * inv_dist;
                dir_z = (-rel_px * sin_a + rel_pz * cos_a) * inv_dist;
              } else {
                // Right leg (mirror)
                dir_x = (rel_px * cos_a - rel_pz * sin_a) * inv_dist;
                dir_z = (rel_px * sin_a + rel_pz * cos_a) * inv_dist;
              }

              // Project relative velocity onto leg
              float dot_leg = rv_x * dir_x + rv_z * dir_z;
              float proj_x = dir_x * dot_leg;
              float proj_z = dir_z * dot_leg;

              float u_x = proj_x - rv_x;
              float u_z = proj_z - rv_z;

              // Normal: perpendicular to leg, pointing outward
              if (det >= 0.0f) {
                line.nx = -dir_z;
                line.nz = dir_x;
              } else {
                line.nx = dir_z;
                line.nz = -dir_x;
              }

              line.px = pref_vx + u_x * resp;
              line.pz = pref_vz + u_z * resp;
            }
          }

          n_lines++;
        }

        // Solve LP: find velocity closest to preferred that satisfies all
        // half-planes
        float safe_vx, safe_vz;
        float ms = base_speed * 1.5f; // max speed for LP solver disc
        orca_solve(orca_lines, n_lines, pref_vx, pref_vz, ms, safe_vx, safe_vz);

        // Graduated ORCA blend: when LP severely constrains velocity,
        // blend in a fraction of preferred intent proportional to
        // constraint severity. At ratio=0 (full freeze): 30% intent
        // preserved. At ratio=0.5 (50% reduction): 0% intent (pure ORCA).
        // Above 0.5: pure ORCA output (no correction needed).
        float safe_spd = std::sqrt(safe_vx * safe_vx + safe_vz * safe_vz);
        float pref_spd = std::sqrt(pref_vx * pref_vx + pref_vz * pref_vz);
        if (pref_spd > 0.1f) {
          float ratio = safe_spd / pref_spd; // 0=frozen, 1=unconstrained
          if (ratio < 0.5f) {
            float blend = ORCA_INTENT_BLEND * (1.0f - ratio * 2.0f);
            vx = safe_vx + pref_vx * blend;
            vz = safe_vz + pref_vz * blend;
          } else {
            vx = safe_vx;
            vz = safe_vz;
          }
        } else {
          vx = safe_vx;
          vz = safe_vz;
        }

      } else {
        // ── Old boids separation fallback ──
        int cx_cell = (int)((_pos_x[i] + _map_half_w) / (float)SPATIAL_CELL_M);
        int cz_cell = (int)((_pos_z[i] + _map_half_h) / (float)SPATIAL_CELL_M);
        cx_cell = std::clamp(cx_cell, 0, _spatial_w - 1);
        cz_cell = std::clamp(cz_cell, 0, _spatial_h - 1);

        for (int dz2 = -1; dz2 <= 1; dz2++) {
          for (int dx2 = -1; dx2 <= 1; dx2++) {
            int nx = cx_cell + dx2;
            int nz = cz_cell + dz2;
            if (nx < 0 || nx >= _spatial_w || nz < 0 || nz >= _spatial_h)
              continue;

            int32_t idx = _spatial_cells[nz * _spatial_w + nx];
            while (idx >= 0) {
              if (idx != i && _alive[idx]) {
                float sx = _pos_x[i] - _pos_x[idx];
                float sz = _pos_z[i] - _pos_z[idx];
                float d2 = sx * sx + sz * sz;
                if (d2 > 0.01f &&
                    d2 < _tune_separation_radius * _tune_separation_radius) {
                  float inv_d = 1.0f / std::sqrt(d2);
                  float force =
                      _tune_separation_force *
                      (1.0f - std::sqrt(d2) / _tune_separation_radius);
                  vx += sx * inv_d * force;
                  vz += sz * inv_d * force;
                }
              }
              idx = _spatial_next[idx];
            }
          }
        }
      } // end ORCA / boids toggle
    } // end skip for berserkers + static combat

    dv_comp[row].vx = vx;
    dv_comp[row].vz = vz;
  }
}
void SimulationServer::_sys_movement_apply(
    flecs::iter &it, const ecs::LegacyIndex *idx_comp,
    const ecs::DesiredVelocity *dv_comp) {
  float delta = it.delta_time();
  // ── Precompute locomotion alphas (once per frame, not per unit) ──
  float alphas_accel[POST_COUNT], alphas_decel[POST_COUNT];
  for (int p = 0; p < POST_COUNT; p++) {
    alphas_accel[p] = 1.0f - expf(-LOCO_ACCEL_RATES[p] * delta);
    alphas_decel[p] = 1.0f - expf(-LOCO_DECEL_RATES[p] * delta);
  }
  float face_alpha = 1.0f - expf(-_tune_face_smooth_rate * delta);

  for (auto row : it) {
    int i = idx_comp[row].val;

    if (_state[i] == ST_DEAD || _state[i] == ST_DOWNED ||
        _state[i] == ST_FROZEN)
      continue;

    // Skip units that are climbing or falling (handled in
    // _sys_movement_climb_fall)
    if (_state[i] == ST_CLIMBING || _state[i] == ST_FALLING)
      continue;

    float vx = dv_comp[row].vx;
    float vz = dv_comp[row].vz;
    float base_speed = _use_context_steering
                           ? SPEED_TABLE[_posture[i]][_move_mode[i]]
                           : _tune_move_speed * _speed_mult(i);
    bool in_static_combat =
        (_state[i] == ST_ENGAGING || _state[i] == ST_SUPPRESSING ||
         _state[i] == ST_RELOADING);
    bool in_cover_moving = (_state[i] == ST_IN_COVER);

    // ── 4. Apply velocity ────────────────────────────────────
    // Clamp max speed (berserkers get higher cap)
    float speed_sq = vx * vx + vz * vz;
    float max_speed = (_state[i] == ST_BERSERK)
                          ? _tune_move_speed * BERSERK_SPEED_MULT
                          : base_speed * 1.5f;
    if (speed_sq > max_speed * max_speed) {
      float inv_speed = max_speed / std::sqrt(speed_sq);
      vx *= inv_speed;
      vz *= inv_speed;
    }

    // ── 4b. Locomotion physics (acceleration + turn rate) ───────
    {
      bool skip_loco = (_state[i] == ST_BERSERK || _state[i] == ST_CLIMBING ||
                        _state[i] == ST_FALLING);
      if (!skip_loco && !in_static_combat) {
        float desired_vx = vx, desired_vz = vz;
        float desired_sq = desired_vx * desired_vx + desired_vz * desired_vz;
        float actual_sq =
            _actual_vx[i] * _actual_vx[i] + _actual_vz[i] * _actual_vz[i];

        // Turn rate clamping: only when both desired and actual are
        // moving
        if (desired_sq > 0.01f && actual_sq > 0.25f) {
          float inv_d = 1.0f / sqrtf(desired_sq);
          float inv_a = 1.0f / sqrtf(actual_sq);
          float dx = desired_vx * inv_d, dz = desired_vz * inv_d;
          float ax = _actual_vx[i] * inv_a, az = _actual_vz[i] * inv_a;
          float dot = dx * ax + dz * az;
          if (dot < LOCO_TURN_CHECK_DOT) {
            float cur_speed = sqrtf(actual_sq);
            float max_turn = (_tune_turn_rate_base +
                              _tune_turn_rate_bonus / (1.0f + cur_speed)) *
                             delta;
            float angle_diff =
                acosf(dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot));
            if (angle_diff > max_turn) {
              // Lerp desired direction toward actual direction to clamp
              // turn
              float t = max_turn / angle_diff;
              desired_vx = ax + (dx - ax) * t;
              desired_vz = az + (dz - az) * t;
              // Restore desired magnitude
              float l =
                  sqrtf(desired_vx * desired_vx + desired_vz * desired_vz);
              if (l > 0.001f) {
                float desired_speed = sqrtf(desired_sq);
                desired_vx = desired_vx / l * desired_speed;
                desired_vz = desired_vz / l * desired_speed;
              }
            }
          }
        }

        // Exponential accel/decel
        bool accelerating =
            (desired_vx * desired_vx + desired_vz * desired_vz) >= actual_sq;
        float alpha = accelerating ? alphas_accel[_posture[i]]
                                   : alphas_decel[_posture[i]];
        _actual_vx[i] += (desired_vx - _actual_vx[i]) * alpha;
        _actual_vz[i] += (desired_vz - _actual_vz[i]) * alpha;

        // Dead band: snap to zero when nearly stopped with no desire
        if (desired_sq < 0.01f &&
            _actual_vx[i] * _actual_vx[i] + _actual_vz[i] * _actual_vz[i] <
                _tune_dead_band_sq) {
          _actual_vx[i] = 0.0f;
          _actual_vz[i] = 0.0f;
        }
        vx = _actual_vx[i];
        vz = _actual_vz[i];

      } else if (in_static_combat) {
        // Static combat: blend toward desired velocity (includes combat
        // drift). Uses fast alpha so units stop quickly when drift=0, but
        // actually FOLLOW the drift when it's nonzero (old code just
        // decayed to zero).
        float fast_alpha = alphas_decel[_posture[i]];
        fast_alpha = 1.0f - (1.0f - fast_alpha) * (1.0f - fast_alpha);
        _actual_vx[i] += (vx - _actual_vx[i]) * fast_alpha;
        _actual_vz[i] += (vz - _actual_vz[i]) * fast_alpha;
        if (vx * vx + vz * vz < 0.01f &&
            _actual_vx[i] * _actual_vx[i] + _actual_vz[i] * _actual_vz[i] <
                _tune_dead_band_sq) {
          _actual_vx[i] = 0.0f;
          _actual_vz[i] = 0.0f;
        }
        vx = _actual_vx[i];
        vz = _actual_vz[i];

      } else {
        // Berserk/climbing/falling: sync actual to desired (no smoothing)
        _actual_vx[i] = vx;
        _actual_vz[i] = vz;
      }
    }

    // ── 4c. Hard formation leash ─────────────────────────────────
    // Placed AFTER locomotion physics so it directly overrides
    // _actual_vx/vz, bypassing turn-rate clamping and exponential
    // smoothing that would otherwise defeat the hard override (units
    // couldn't reverse direction).
    if (_order[i] == ORDER_FOLLOW_SQUAD && _state[i] != ST_BERSERK &&
        _state[i] != ST_CLIMBING && _state[i] != ST_FALLING &&
        _state[i] != ST_DOWNED && _state[i] != ST_DEAD) {
      int sid = _squad_id[i];
      if (sid >= 0 && sid < MAX_SQUADS && _squads[sid].active &&
          _squad_alive_counts[sid] > 0) {
        Vector3 c = _squad_centroids[sid];
        Vector3 adv = _squads[sid].advance_dir;
        float adv_len = adv.length();
        if (adv_len > 0.01f) {
          adv /= adv_len;
          float lead = GOAL_LEAD_DIST + _squads[sid].advance_offset;
          float spr = _squads[sid].formation_spread;
          int slot_idx = _squad_member_idx[i];
          int total = _squad_alive_counts[sid];
          float perp_x = -adv.z, perp_z = adv.x;
          float lsx = c.x + adv.x * lead;
          float lsz = c.z + adv.z * lead;
          switch (_squads[sid].formation) {
          case FORM_LINE: {
            float ft = (total > 1)
                           ? ((float)slot_idx / (total - 1) - 0.5f) * 2.0f
                           : 0.0f;
            lsx += perp_x * ft * spr;
            lsz += perp_z * ft * spr;
          } break;
          case FORM_WEDGE: {
            float ft = (total > 1)
                           ? ((float)slot_idx / (total - 1) - 0.5f) * 2.0f
                           : 0.0f;
            lsx += perp_x * ft * spr;
            lsz += perp_z * ft * spr;
            float fb = std::abs(ft) * spr * 0.5f;
            lsx -= adv.x * fb;
            lsz -= adv.z * fb;
          } break;
          case FORM_COLUMN: {
            float ft = (total > 1) ? (float)slot_idx / (total - 1) : 0.0f;
            lsx -= adv.x * ft * spr;
            lsz -= adv.z * ft * spr;
          } break;
          case FORM_CIRCLE: {
            float angle = 6.28318530f * slot_idx / std::max(total, 1);
            lsx = c.x + std::cos(angle) * spr;
            lsz = c.z + std::sin(angle) * spr;
          } break;
          default:
            break;
          }
          float ldx = lsx - _pos_x[i], ldz = lsz - _pos_z[i];
          float ld = std::sqrt(ldx * ldx + ldz * ldz);
          if (ld > FORMATION_LEASH_SOFT) {
            float slot_speed = base_speed;
            float slot_vx = (ldx / ld) * slot_speed;
            float slot_vz = (ldz / ld) * slot_speed;
            if (ld >= FORMATION_LEASH_HARD) {
              // Hard override: bypass locomotion smoothing entirely
              vx = slot_vx;
              vz = slot_vz;
              _actual_vx[i] = slot_vx;
              _actual_vz[i] = slot_vz;
            } else {
              // Soft blend
              float t = (ld - FORMATION_LEASH_SOFT) /
                        (FORMATION_LEASH_HARD - FORMATION_LEASH_SOFT);
              vx = vx * (1.0f - t) + slot_vx * t;
              vz = vz * (1.0f - t) + slot_vz * t;
              _actual_vx[i] = vx;
              _actual_vz[i] = vz;
            }
          }
        }
      }
    }

    // Save position before move (for step height check)
    float prev_x = _pos_x[i];
    float prev_z = _pos_z[i];
    float prev_y = _pos_y[i];

    _vel_x[i] = vx;
    _vel_z[i] = vz;
    _pos_x[i] += vx * delta;
    _pos_z[i] += vz * delta;

    // Accumulate animation phase (render-only, proportional to speed)
    float spd = sqrtf(vx * vx + vz * vz);
    _anim_phase[i] += spd * delta * 0.5f;
    _avg_total_speed += spd;

    // ── 5. Wall collision — vault, climb, or block ──────────
    {
      VoxelWorld *vw_col = VoxelWorld::get_singleton();
      if (vw_col) {
        float vs = vw_col->get_voxel_scale();
        int nvx = (int)((_pos_x[i] + _map_half_w) / vs);
        int nvz = (int)((_pos_z[i] + _map_half_h) / vs);
        int feet_vy = (int)(prev_y / vs);

        // Check body voxels (posture-dependent)
        int bv = _body_voxels(i);
        bool blocked = false;
        for (int dy = 1; dy <= bv; dy++) {
          if (vw_col->is_solid(nvx, feet_vy + dy, nvz)) {
            blocked = true;
            break;
          }
        }

        if (blocked) {
          // Prone units can't vault or climb — just block
          if (_posture[i] == POST_PRONE) {
            _pos_x[i] = prev_x;
            _pos_z[i] = prev_z;
            _vel_x[i] = 0.0f;
            _vel_z[i] = 0.0f;
            _actual_vx[i] = 0.0f;
            _actual_vz[i] = 0.0f;
            goto skip_terrain;
          }

          // Measure continuous wall height from feet
          int wall_height = 0;
          for (int dy = 1; dy <= CLIMB_MAX_VOXELS + bv; dy++) {
            if (vw_col->is_solid(nvx, feet_vy + dy, nvz))
              wall_height = dy;
            else if (dy > wall_height + 1)
              break; // air gap above wall top
          }
          int top_vy = feet_vy + wall_height;

          if (wall_height <= VAULT_MAX_VOXELS) {
            // Vault: check body clearance above obstacle
            bool can_vault = true;
            for (int dy = 1; dy <= bv; dy++) {
              if (vw_col->is_solid(nvx, top_vy + dy, nvz)) {
                can_vault = false;
                break;
              }
            }
            if (!can_vault) {
              // Can't vault — blocked
              _pos_x[i] = prev_x;
              _pos_z[i] = prev_z;
              _vel_x[i] = 0.0f;
              _vel_z[i] = 0.0f;
              _actual_vx[i] = 0.0f;
              _actual_vz[i] = 0.0f;
            }
            // If can_vault: allow move, terrain clamp handles Y
          } else if (wall_height <= CLIMB_MAX_VOXELS) {
            // Climb: check body clearance at wall top
            bool can_climb = true;
            for (int dy = 1; dy <= bv; dy++) {
              if (vw_col->is_solid(nvx, top_vy + dy, nvz)) {
                can_climb = false;
                break;
              }
            }
            if (can_climb && _state[i] != ST_CLIMBING && _state[i] != ST_DEAD) {
              if (_climb_cooldown[i] > 0.0f) {
                // Cooldown active — block climb, stop pushing into wall
                _vel_x[i] = 0.0f;
                _vel_z[i] = 0.0f;
                _actual_vx[i] = 0.0f;
                _actual_vz[i] = 0.0f;
              } else {
                _state[i] = ST_CLIMBING;
                _climb_target_y[i] = (float)(top_vy + 1) * vs;
                // Store wall-top XZ so unit lands ON the wall, not back
                // in the trench
                _climb_dest_x[i] = (float)nvx * vs - _map_half_w + vs * 0.5f;
                _climb_dest_z[i] = (float)nvz * vs - _map_half_h + vs * 0.5f;
                _vel_x[i] = 0.0f;
                _vel_z[i] = 0.0f;
                _actual_vx[i] = 0.0f;
                _actual_vz[i] = 0.0f;
                _climb_started_tick++;
                _total_climb_events++;
              }
            }
            // Revert XZ — climb is vertical only
            _pos_x[i] = prev_x;
            _pos_z[i] = prev_z;
          } else {
            // Wall too tall — fully blocked
            _pos_x[i] = prev_x;
            _pos_z[i] = prev_z;
            _vel_x[i] = 0.0f;
            _vel_z[i] = 0.0f;
            _actual_vx[i] = 0.0f;
            _actual_vz[i] = 0.0f;
          }
        }
      }
    }

    // ── 6. Update facing (smooth — skip for static combat + cover
    // moving)
    if (!in_static_combat && !in_cover_moving) {
      float actual_sq =
          _actual_vx[i] * _actual_vx[i] + _actual_vz[i] * _actual_vz[i];
      if (actual_sq > 0.25f) {
        float inv_s = 1.0f / sqrtf(actual_sq);
        float target_fx = _actual_vx[i] * inv_s;
        float target_fz = _actual_vz[i] * inv_s;
        _face_x[i] += (target_fx - _face_x[i]) * face_alpha;
        _face_z[i] += (target_fz - _face_z[i]) * face_alpha;
        // Renormalize
        float fl = sqrtf(_face_x[i] * _face_x[i] + _face_z[i] * _face_z[i]);
        if (fl > 0.001f) {
          _face_x[i] /= fl;
          _face_z[i] /= fl;
        }
      }
      // else: slow/stopped — keep current facing
    }

    // ── 7. Terrain clamp ─────────────────────────────────────
    _clamp_to_terrain(i);

    // ── 7b. Wall push-out — if body overlaps solid, nudge to nearest
    // clear column
    {
      VoxelWorld *vw_push = VoxelWorld::get_singleton();
      if (vw_push) {
        float vs = vw_push->get_voxel_scale();
        int fvx = (int)((_pos_x[i] + _map_half_w) / vs);
        int fvz = (int)((_pos_z[i] + _map_half_h) / vs);
        int fvy = (int)(_pos_y[i] / vs);
        int bv = _body_voxels(i);
        bool embedded = false;
        for (int dy = 1; dy <= bv; dy++) {
          if (vw_push->is_solid(fvx, fvy + dy, fvz)) {
            embedded = true;
            break;
          }
        }
        if (embedded) {
          // Probe 8 directions (cardinal + diagonal) for nearest clear
          // column
          static const int PUSH_DX[] = {1, -1, 0, 0, 1, -1, 1, -1};
          static const int PUSH_DZ[] = {0, 0, 1, -1, 1, -1, -1, 1};
          bool pushed = false;
          for (int r = 1; r <= 5 && !pushed; r++) {
            for (int d = 0; d < 8 && !pushed; d++) {
              int tx = fvx + PUSH_DX[d] * r;
              int tz = fvz + PUSH_DZ[d] * r;
              bool clear = true;
              for (int dy = 1; dy <= bv; dy++) {
                if (vw_push->is_solid(tx, fvy + dy, tz)) {
                  clear = false;
                  break;
                }
              }
              if (clear) {
                _pos_x[i] = (float)tx * vs - _map_half_w + vs * 0.5f;
                _pos_z[i] = (float)tz * vs - _map_half_h + vs * 0.5f;
                _clamp_to_terrain(i);
                pushed = true;
              }
            }
          }
          if (pushed) {
            _vel_x[i] = 0.0f;
            _vel_z[i] = 0.0f;
            _actual_vx[i] = 0.0f;
            _actual_vz[i] = 0.0f;
          }
        }
      }
    }

    // ── 8. Passive fall detection / step height safety ─────
    float y_drop = prev_y - _pos_y[i];
    if (y_drop > _tune_max_step_height) {
      // Ground destroyed beneath us — enter falling
      _state[i] = ST_FALLING;
      _fall_start_y[i] = prev_y;
      _vel_y[i] = 0.0f;
      _pos_y[i] = prev_y; // don't teleport — let fall physics handle it
      _climb_cooldown[i] = CLIMB_COOLDOWN_SEC;
      _fall_started_tick++;
      _total_fall_events++;
    } else if (_pos_y[i] - prev_y > _tune_max_step_height) {
      // Upward step too large — safety net (vault handles most cases)
      _pos_x[i] = prev_x;
      _pos_z[i] = prev_z;
      _pos_y[i] = prev_y;
      _vel_x[i] = 0.0f;
      _vel_z[i] = 0.0f;
      _actual_vx[i] = 0.0f;
      _actual_vz[i] = 0.0f;
    }
  skip_terrain:;
  }
}

void SimulationServer::_sys_decisions(flecs::iter &it,
                                      const ecs::LegacyIndex *idx_comp,
                                      ecs::State *state_comp,
                                      ecs::Posture *posture_comp) {
  float delta = it.delta_time();
  // Staggered: only units whose timer expired
  for (auto row : it) {
    int i = idx_comp[row].val;

    _decision_timer[i] -= delta;
    if (_decision_timer[i] > 0.0f)
      continue;

    UnitState prev_state = (UnitState)state_comp[row].current;
    _decision_timer[i] =
        _tune_decision_interval + _randf() * 0.05f; // Slight jitter

    // CONTACT alertness: faster decisions near known enemy contacts
    if (_team[i] > 0 && _team[i] <= 2) {
      int ti = _team[i] - 1;
      if (_pheromones[ti].is_valid()) {
        float contact = _pheromones[ti]->sample(
            Vector3(_pos_x[i], 0.0f, _pos_z[i]), CH_CONTACT);
        if (contact > 0.5f) {
          _decision_timer[i] *= 0.6f; // 40% faster decisions near contacts
        }
      }
    }

    // Compute aim quality (spread → quality mapping for AI decisions)
    _aim_quality[i] = _compute_aim_quality(i);

    // Skip locked states
    if (state_comp[row].current == ecs::ST_RELOADING ||
        state_comp[row].current == ecs::ST_DOWNED ||
        state_comp[row].current == ecs::ST_CLIMBING ||
        state_comp[row].current == ecs::ST_FALLING)
      continue;

    // ── Break state recovery ─────────────────────────────────
    if (state_comp[row].current == ecs::ST_BERSERK ||
        state_comp[row].current == ecs::ST_FROZEN ||
        state_comp[row].current == ecs::ST_RETREATING) {
      auto pers_mod = _personality_morale(_personality[i]);
      if (_morale[i] > pers_mod.recovery_threshold) {
        if (state_comp[row].current == ecs::ST_FROZEN) {
          // CATATONIC needs unfreeze timer
          _frozen_timer[i] -= _tune_decision_interval;
          if (_frozen_timer[i] > 0.0f)
            continue; // Still unfreezing
        }
        state_comp[row].current = ecs::ST_IDLE;
        _target_id[i] = -1;
        // Fall through to normal decision cascade
      } else {
        // Still broken — update targets then skip normal decisions
        if (state_comp[row].current == ecs::ST_BERSERK) {
          // Retarget if current target is dead
          int32_t t = _target_id[i];
          if (t < 0 || !_alive[t]) {
            float best_d2 = 1e18f;
            int32_t best_enemy = -1;
            for (int j = 0; j < _count; j++) {
              if (!_alive[j] || _team[j] == _team[i])
                continue;
              float d2 = _distance_sq(i, j);
              if (d2 < best_d2) {
                best_d2 = d2;
                best_enemy = j;
              }
            }
            _target_id[i] = best_enemy;
          }
          // Track target position for charge
          if (_target_id[i] >= 0) {
            int32_t t2 = _target_id[i];
            _order_x[i] = _pos_x[t2];
            _order_y[i] = _pos_y[t2];
            _order_z[i] = _pos_z[t2];
          }
        }
        // Paranoid: retarget nearest ally each decision cycle
        if (state_comp[row].current == ecs::ST_ENGAGING &&
            _personality[i] == PERS_PARANOID) {
          float best_d2 = 1e18f;
          int32_t best_ally = -1;
          float range2 = _attack_range[i] * _attack_range[i];
          for (int j = 0; j < _count; j++) {
            if (j == i || !_alive[j] || _team[j] != _team[i])
              continue;
            float d2 = _distance_sq(i, j);
            if (d2 < range2 && d2 < best_d2) {
              best_d2 = d2;
              best_ally = j;
            }
          }
          if (best_ally >= 0)
            _target_id[i] = best_ally;
        }
        continue;
      }
    }

    // ── Auto-posture selection ─────────────────────────────
    {
      uint8_t desired = ecs::POST_STAND;

      if (state_comp[row].current == ecs::ST_IN_COVER) {
        desired = ecs::POST_CROUCH;
      } else if (_suppression[i] > 0.7f &&
                 state_comp[row].current != ecs::ST_MOVING &&
                 state_comp[row].current != ecs::ST_FLANKING &&
                 state_comp[row].current != ecs::ST_BERSERK) {
        desired = ecs::POST_PRONE;
      } else if (_suppression[i] > 0.35f &&
                 (state_comp[row].current == ecs::ST_ENGAGING ||
                  state_comp[row].current == ecs::ST_SUPPRESSING)) {
        desired = ecs::POST_CROUCH;
      } else if ((_role[i] == ROLE_MG || _role[i] == ROLE_MARKSMAN) &&
                 (state_comp[row].current == ecs::ST_ENGAGING ||
                  state_comp[row].current == ecs::ST_SUPPRESSING)) {
        desired = ecs::POST_CROUCH;
      } else if (state_comp[row].current == ecs::ST_MOVING ||
                 state_comp[row].current == ecs::ST_FLANKING ||
                 state_comp[row].current == ecs::ST_RETREATING) {
        desired = ecs::POST_STAND;
      }

      // Override: Berserkers always stand
      if (state_comp[row].current == ecs::ST_BERSERK)
        desired = ecs::POST_STAND;
      // Override: Climbing/falling — force stand instantly
      if (state_comp[row].current == ecs::ST_CLIMBING ||
          state_comp[row].current == ecs::ST_FALLING) {
        posture_comp[row].current = ecs::POST_STAND;
        posture_comp[row].target = ecs::POST_STAND;
        posture_comp[row].transition_timer = 0.0f;
        desired = ecs::POST_STAND;
      }

      _request_posture(i, desired);
    }

    // ── Flanking arrival check ───────────────────────────────
    if (state_comp[row].current == ecs::ST_FLANKING) {
      float dx = _order_x[i] - _pos_x[i];
      float dz = _order_z[i] - _pos_z[i];
      if (dx * dx + dz * dz < _tune_arrive_dist * _tune_arrive_dist) {
        state_comp[row].current = ecs::ST_ENGAGING; // Arrived at flank position
        _settle_timer[i] = _role_settle_time(_role[i]); // was moving
        _deploy_timer[i] = _role_deploy_time(_role[i]);
      }
    }

    // ── Scored target acquisition ────────────────────────────
    float best_score = -1000.0f;
    int32_t best_target = -1;
    bool target_is_supp = false; // set true if target acquired via FOW fallback
    bool has_visible_enemy =
        false; // any visible enemy within detect range (for behavior)

    float range = _attack_range[i];
    // Search spatial hash out to detection range so we find enemies we
    // can SEE even if they're beyond weapon range. Firing still gated by
    // _attack_range.
    float search_range = std::max(range, _detect_range[i]);
    int min_gx =
        (int)((_pos_x[i] - search_range + _map_half_w) / (float)SPATIAL_CELL_M);
    int max_gx =
        (int)((_pos_x[i] + search_range + _map_half_w) / (float)SPATIAL_CELL_M);
    int min_gz =
        (int)((_pos_z[i] - search_range + _map_half_h) / (float)SPATIAL_CELL_M);
    int max_gz =
        (int)((_pos_z[i] + search_range + _map_half_h) / (float)SPATIAL_CELL_M);

    min_gx = std::clamp(min_gx, 0, _spatial_w - 1);
    max_gx = std::clamp(max_gx, 0, _spatial_w - 1);
    min_gz = std::clamp(min_gz, 0, _spatial_h - 1);
    max_gz = std::clamp(max_gz, 0, _spatial_h - 1);

    for (int gz = min_gz; gz <= max_gz; gz++) {
      for (int gx = min_gx; gx <= max_gx; gx++) {
        int32_t idx = _spatial_cells[gz * _spatial_w + gx];
        while (idx >= 0) {
          if (_alive[idx] && _team[idx] != _team[i]) {
            int vis_idx = _team[i] - 1; // team's visibility index

            // Fog of war gate: skip expensive raycast for invisible
            // enemies
            if (!_team_can_see(vis_idx, idx)) {
              _fow_targets_skipped++;
              _fow_total_skipped++;
              // Suppressive fire fallback: recently-seen targets
              if (_time_since_seen(idx) < CONTACT_DECAY_TIME &&
                  _suppression[i] < 0.5f && best_target < 0) {
                // Use last-known position for suppressive targeting
                float lkdx = _pos_x[i] - _last_known_x[idx];
                float lkdz = _pos_z[i] - _last_known_z[idx];
                float lkd2 = lkdx * lkdx + lkdz * lkdz;
                float min_range =
                    (_role[i] == ROLE_MORTAR) ? MORTAR_MIN_RANGE : 0.0f;
                if (lkd2 < range * range && lkd2 > min_range * min_range) {
                  // Wall Penetration Fix: Check LOS to last known
                  // position before deciding to suppress
                  bool clear_shot = true;
                  if (_role[i] != ROLE_MORTAR) {
                    // We can't use _check_los(i, idx) because idx is the
                    // unit's CURRENT pos (which might be hidden) We need
                    // to check LOS to the _last_known_x/z position.
                    VoxelWorld *vw = VoxelWorld::get_singleton();
                    if (vw) {
                      Vector3 eye(_pos_x[i], _pos_y[i] + _eye_height(i),
                                  _pos_z[i]);
                      Vector3 target_pos(_last_known_x[idx],
                                         _pos_y[idx] + _center_mass(idx),
                                         _last_known_z[idx]);
                      // Using the target's current Y height at the last
                      // known XZ is an approximation, but sufficient.
                      if (!vw->check_los(eye, target_pos)) {
                        clear_shot = false;
                      }
                    }
                  }

                  if (clear_shot) {
                    float supp_score = _score_target(i, idx) * 0.3f;
                    if (supp_score > best_score) {
                      best_score = supp_score;
                      best_target = idx;
                      target_is_supp = true;
                      _fow_suppressive_shots++;
                      _fow_total_suppressive++;
                      _engagements_suppressive++;
                    }
                  }
                }
              }
              idx = _spatial_next[idx];
              continue;
            }

            float d2 = _distance_sq(i, idx);
            // Mark visible enemy within detect range (even if beyond
            // weapon range)
            if (d2 < _detect_range[i] * _detect_range[i]) {
              has_visible_enemy = true;
            }
            if (d2 < range * range) {
              float min_range =
                  (_role[i] == ROLE_MORTAR) ? MORTAR_MIN_RANGE : 0.0f;
              if (d2 <= min_range * min_range) {
                idx = _spatial_next[idx];
                continue;
              }

              if (_role[i] == ROLE_MORTAR) {
                // Mortar fire is indirect: do not require direct
                // wall-penetration line.
                float score = _score_target(i, idx) + 8.0f;
                // Target stickiness: prefer current target to reduce
                // flicker
                if (idx == _target_id[i] && _target_id[i] >= 0)
                  score += TARGET_STICKINESS;
                if (score > best_score) {
                  best_score = score;
                  best_target = idx;
                  target_is_supp =
                      false; // direct target clears suppressive flag
                  _engagements_visible++;
                }
              } else {
                // Require clear LOS for targeting (bullets still
                // penetrate walls in flight)
                if (_check_los(i, idx)) {
                  float score = _score_target(i, idx);
                  // Target stickiness: prefer current target to reduce
                  // flicker
                  if (idx == _target_id[i] && _target_id[i] >= 0)
                    score += TARGET_STICKINESS;
                  if (score > best_score) {
                    best_score = score;
                    best_target = idx;
                    target_is_supp =
                        false; // direct target clears suppressive flag
                    _engagements_visible++;
                  }
                } else {
                  _wall_pen_blocked++;
                }
              }
              _los_checks++;
            }
          }
          idx = _spatial_next[idx];
        }
      }
    }
    _spatial_queries++;

    // CONTACT gradient suppressive fire: no target found, but CONTACT is
    // strong → pick nearest non-visible enemy aligned with the gradient
    // for suppressive fire
    if (best_target < 0 && _suppression[i] < 0.5f) {
      int ti = _team[i] - 1;
      if (ti >= 0 && ti < 2 && _pheromones[ti].is_valid()) {
        Vector3 my_pos(_pos_x[i], 0.0f, _pos_z[i]);
        Vector3 cgrad = _pheromones[ti]->gradient(my_pos, CH_CONTACT);
        float grad_len2 = cgrad.x * cgrad.x + cgrad.z * cgrad.z;
        if (grad_len2 > 0.01f) {
          float inv_len = 1.0f / std::sqrt(grad_len2);
          float gx = cgrad.x * inv_len;
          float gz = cgrad.z * inv_len;
          // Find nearest enemy in gradient direction — FOW-gated
          float best_cd2 = range * range;
          int32_t contact_target = -1;
          int team_idx = ti; // 0-based team index for FOW
          for (int j = 0; j < _count; j++) {
            if (!_alive[j] || _team[j] == _team[i])
              continue;
            // FOW gate: only target enemies we can see or recently saw
            if (!_team_can_see(team_idx, j) &&
                _time_since_seen(j) > CONTACT_DECAY_TIME)
              continue;
            float edx = _pos_x[j] - _pos_x[i];
            float edz = _pos_z[j] - _pos_z[i];
            float ed2 = edx * edx + edz * edz;
            if (ed2 > best_cd2)
              continue;
            // Dot product: is this enemy roughly in the contact
            // direction?
            float ed = std::sqrt(ed2);
            if (ed < 1.0f)
              continue;
            float dot = (edx * gx + edz * gz) / ed;
            if (dot > 0.5f && ed2 < best_cd2) {
              best_cd2 = ed2;
              contact_target = j;
            }
          }
          if (contact_target >= 0) {
            best_target = contact_target;
            best_score = _score_target(i, contact_target) *
                         0.2f; // Low-confidence suppressive
            target_is_supp = true;
            _fow_suppressive_shots++;
            _fow_total_suppressive++;
            _engagements_suppressive++; // was missing — Path B now
                                        // counted
          }
        }
      }
    }

    _target_id[i] = best_target;
    _target_score[i] = best_score;
    _target_suppressive[i] = target_is_supp;
    _has_visible_enemy[i] = has_visible_enemy;
    if (best_target >= 0)
      _engagements_this_tick++;

    // ── Priority-based state cascade ─────────────────────────

    // Priority 1: MORALE BREAK — personality determines behavior
    //   Pheromone FEAR lowers threshold (breaks sooner), COURAGE raises
    //   it
    {
      auto pers_mod = _personality_morale(_personality[i]);
      float break_thresh = pers_mod.break_threshold;
      if (_team[i] > 0 && _team[i] <= 2) {
        int ti = _team[i] - 1;
        if (_pheromones[ti].is_valid()) {
          Vector3 pos(_pos_x[i], 0.0f, _pos_z[i]);
          float fear = _pheromones[ti]->sample(pos, CH_FEAR);
          float courage = _pheromones[ti]->sample(pos, CH_COURAGE);
          break_thresh += fear * 0.02f - courage * 0.015f;
          break_thresh = std::clamp(break_thresh, 0.05f, 0.5f);
        }
      }
      if (_morale[i] < break_thresh) {
        switch (_personality[i]) {
        case PERS_BERSERKER: {
          // Charge nearest enemy — ignore cover, ignore range
          _state[i] = ST_BERSERK;
          float best_d2 = 1e18f;
          int32_t best_enemy = -1;
          for (int j = 0; j < _count; j++) {
            if (!_alive[j] || _team[j] == _team[i])
              continue;
            float d2 = _distance_sq(i, j);
            if (d2 < best_d2) {
              best_d2 = d2;
              best_enemy = j;
            }
          }
          if (best_enemy >= 0) {
            _target_id[i] = best_enemy;
            _order_x[i] = _pos_x[best_enemy];
            _order_y[i] = _pos_y[best_enemy];
            _order_z[i] = _pos_z[best_enemy];
          }
          break;
        }
        case PERS_CATATONIC:
          // Freeze in place — drop weapon, unresponsive
          _state[i] = ST_FROZEN;
          _target_id[i] = -1;
          _vel_x[i] = 0.0f;
          _vel_z[i] = 0.0f;
          _actual_vx[i] = 0.0f;
          _actual_vz[i] = 0.0f;
          _frozen_timer[i] = FROZEN_RECOVERY_TIME;
          break;

        case PERS_PARANOID: {
          // Fire on nearest ally (perceive as threat)
          _state[i] = ST_ENGAGING;
          float best_d2 = 1e18f;
          int32_t best_ally = -1;
          float range2 = _attack_range[i] * _attack_range[i];
          for (int j = 0; j < _count; j++) {
            if (j == i || !_alive[j])
              continue;
            if (_team[j] != _team[i])
              continue; // Must be same team
            float d2 = _distance_sq(i, j);
            if (d2 < range2 && d2 < best_d2) {
              best_d2 = d2;
              best_ally = j;
            }
          }
          if (best_ally >= 0) {
            _target_id[i] = best_ally;
          }
          // If no ally in range, fall through to normal engage/idle
          if (best_ally < 0)
            break;
          break;
        }
        case PERS_STEADY:
        default:
          // Existing behavior: retreat to rally
          _state[i] = ST_RETREATING;
          if (_squad_id[i] < MAX_SQUADS && _squads[_squad_id[i]].active) {
            auto &sq = _squads[_squad_id[i]];
            _order_x[i] = sq.rally_point.x;
            _order_z[i] = sq.rally_point.z;
          }
          break;
        }
        continue;
      }
    }

    // Priority 2: SEEK COVER when suppressed or wounded and NOT in good
    // cover Also seek cover if aim quality is terrible (< 0.3) and
    // exposed Note: no longer requires best_target >= 0; suppressed units
    // should seek cover even if they can't see who's shooting them
    // (FOW-friendly)
    bool bad_aim_exposed =
        (_aim_quality[i] < 0.3f && _cover_value[i] < 0.3f && best_target >= 0);
    if (((_suppression[i] > _tune_supp_cover_thresh ||
          _health[i] < HEALTH_COVER_THRESHOLD) &&
         _cover_value[i] < COVER_GOOD_THRESHOLD) ||
        bad_aim_exposed) {
      if (best_target >= 0) {
        _find_tactical_position(i); // use visible enemy for directional cover
      } else {
        // No visible target — seek cover facing last-known threat
        // direction
        _state[i] = ST_IN_COVER;
      }
      if (_state[i] == ST_IN_COVER)
        continue; // cover found
    }

    // Priority 3: SUPPRESSIVE FIRE (MG role coordination)
    if (best_target >= 0 && _should_suppress(i)) {
      if (prev_state == ST_MOVING || prev_state == ST_FLANKING ||
          prev_state == ST_RETREATING || prev_state == ST_BERSERK) {
        _settle_timer[i] = _role_settle_time(_role[i]);
        _deploy_timer[i] = _role_deploy_time(_role[i]);
      }
      _state[i] = ST_SUPPRESSING;
      continue;
    }

    // Priority 4: FLANKING (allies pin the target)
    if (best_target >= 0 && _should_flank(i)) {
      _state[i] = ST_FLANKING;
      Vector3 flank_dest = _compute_flank_destination(i);
      _order_x[i] = flank_dest.x;
      _order_y[i] = flank_dest.y;
      _order_z[i] = flank_dest.z;
      continue;
    }

    // Priority 5: ENGAGE (standard combat)
    // Don't stop advance for marginal targets while in RUSH mode
    if (best_target >= 0) {
      bool squad_advancing =
          (_order[i] == ORDER_FOLLOW_SQUAD && prev_state == ST_MOVING &&
           _move_mode[i] == MMODE_RUSH);
      float engage_threshold = squad_advancing ? RUSH_ENGAGE_THRESHOLD : 0.0f;

      if (best_score >= engage_threshold) {
        if (_suppression[i] > 0.7f) {
          _state[i] = ST_IN_COVER;
        } else {
          // Aim quality gate: if quality is very poor (< 0.2) and we have
          // good cover, hold fire and wait for settle/deploy instead of
          // spraying inaccurately
          if (_aim_quality[i] < 0.2f && _cover_value[i] >= 0.5f) {
            _state[i] = ST_IN_COVER; // Hold in cover, wait for aim to improve
          } else {
            if (prev_state == ST_MOVING || prev_state == ST_FLANKING ||
                prev_state == ST_RETREATING || prev_state == ST_BERSERK) {
              _settle_timer[i] = _role_settle_time(_role[i]);
              _deploy_timer[i] = _role_deploy_time(_role[i]);
            }
            _state[i] = ST_ENGAGING;
          }
        }
        continue;
      }
      // Marginal target while advancing — keep moving, will engage when
      // closer/in COMBAT mode
      continue;
    }

    // Priority 6: SQUAD COHESION — if isolated, move toward squad or
    // RALLY
    _update_squad_cohesion(i);
    if (_nearby_squad_count[i] == 0 && _order[i] != ORDER_MOVE) {
      float best_sq_d2 = 1e18f;
      int32_t best_sq_mate = -1;
      for (int j = 0; j < _count; j++) {
        if (j == i || !_alive[j])
          continue;
        if (_squad_id[j] != _squad_id[i] || _team[j] != _team[i])
          continue;
        float d2 = _distance_sq(i, j);
        if (d2 < best_sq_d2) {
          best_sq_d2 = d2;
          best_sq_mate = j;
        }
      }
      // Isolated: move toward nearest squad mate, biased by RALLY
      // gradient
      if (best_sq_mate >= 0 &&
          best_sq_d2 > SQUAD_COHESION_RADIUS * SQUAD_COHESION_RADIUS) {
        float dest_x = _pos_x[best_sq_mate];
        float dest_z = _pos_z[best_sq_mate];
        // RALLY gradient pull for isolated units
        int ti = _team[i] - 1;
        if (ti >= 0 && ti < 2 && _pheromones[ti].is_valid()) {
          Vector3 my_pos(_pos_x[i], 0.0f, _pos_z[i]);
          Vector3 rg = _pheromones[ti]->gradient(my_pos, CH_RALLY);
          dest_x += rg.x * 5.0f; // Bias toward rally point
          dest_z += rg.z * 5.0f;
        }
        _order_x[i] = dest_x;
        _order_z[i] = dest_z;
        _state[i] = ST_MOVING;
        continue;
      }
    }

    // Priority 7: HOLD vs MOVE utility evaluation
    // Units that were recently fighting should evaluate whether their
    // current position is worth keeping vs resuming formation movement.
    if (_order[i] == ORDER_MOVE || _order[i] == ORDER_FOLLOW_SQUAD) {
      float dx = _order_x[i] - _pos_x[i];
      float dz = _order_z[i] - _pos_z[i];
      float dist2_to_slot = dx * dx + dz * dz;

      if (dist2_to_slot <= _tune_arrive_dist * _tune_arrive_dist) {
        _state[i] = ST_IDLE; // Already at slot
      } else {
        // Score: is holding position better than moving to slot?
        float hold_score = 0.0f;

        // Position quality: cover value (already computed per tick)
        hold_score += _cover_value[i] * 20.0f;

        // Recent combat: was fighting last decision cycle
        bool was_fighting =
            (prev_state == ST_ENGAGING || prev_state == ST_SUPPRESSING ||
             prev_state == ST_IN_COVER);
        if (was_fighting)
          hold_score += 15.0f;

        // Settle cost: weapon-dependent accuracy loss from moving
        float max_settle = _role_settle_time(_role[i]);
        hold_score +=
            max_settle * 25.0f; // marksman: +30, MG: +25, rifleman: +10

        // Weapon deploy cost: heavy weapons strongly prefer holding
        float deploy_cost = _role_deploy_time(_role[i]);
        hold_score +=
            deploy_cost * 30.0f; // MG: +24, Marksman: +15, Rifleman: 0

        // Aim quality: good aim is valuable, don't throw it away by
        // moving Quality 1.0 = +20, Quality 0.5 = +10, Quality 0.0 = +0
        hold_score += _aim_quality[i] * 20.0f;

        // Move urgency: distance to formation slot
        float dist_to_slot = std::sqrt(dist2_to_slot);
        float move_score =
            dist_to_slot *
            5.0f; // was 2.0 — too low, units held position 22m+ from slot

        if (hold_score > move_score) {
          _state[i] = ST_IDLE; // Position worth keeping
        } else {
          _state[i] = ST_MOVING;
        }
      }
    } else {
      _state[i] = ST_IDLE;
    }

    // ── Auto movement mode: RUSH approach, COMBAT on VISUAL contact ──
    // Only drop to COMBAT when an actual shootable target is acquired.
    // has_visible_enemy (detect range) does NOT slow approach — units
    // need to close from 60m (detect) to 30m (weapon) at RUSH speed, not
    // COMBAT speed. Suppressive (FOW) targets do NOT slow approach —
    // units sprint + spray. STEALTH is never overridden (flanking squads
    // preserve their mode).
    if (_order[i] == ORDER_FOLLOW_SQUAD) {
      // Tick the mode transition cooldown
      _mode_transition_timer[i] -= _tune_decision_interval;

      bool has_visual_target = (best_target >= 0 && !_target_suppressive[i]);
      if (has_visual_target) {
        // Direct visual contact → drop to combat IMMEDIATELY (threat
        // response)
        if (_move_mode[i] != MMODE_STEALTH && _move_mode[i] != MMODE_COMBAT) {
          _move_mode[i] = MMODE_COMBAT;
          _noise_level[i] = NOISE_TABLE[MMODE_COMBAT];
          _mode_transition_timer[i] = MODE_TRANSITION_COOLDOWN;
        }
      } else {
        // No visual contact → cautiously speed up (with cooldown)
        if (_mode_transition_timer[i] <= 0.0f) {
          if (_move_mode[i] == MMODE_COMBAT) {
            // Step 1: COMBAT → TACTICAL (cautious acceleration)
            _move_mode[i] = MMODE_TACTICAL;
            _noise_level[i] = NOISE_TABLE[MMODE_TACTICAL];
            _mode_transition_timer[i] = MODE_TRANSITION_COOLDOWN;
          } else if (_move_mode[i] == MMODE_TACTICAL) {
            // Step 2: TACTICAL → RUSH (full sprint after 2× cooldown)
            _move_mode[i] = MMODE_RUSH;
            _noise_level[i] = NOISE_TABLE[MMODE_RUSH];
            _mode_transition_timer[i] = MODE_TRANSITION_COOLDOWN;
          }
        }
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  ECS Synchronization (Phase 1 Bridge)
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_sync_soa_to_flecs() {
  for (int i = 0; i < _count; i++) {
    if (!_alive[i] || !_flecs_id[i].is_alive())
      continue;

    _flecs_id[i].set<ecs::LegacyIndex>({i});
    _flecs_id[i].set<ecs::Position>({_pos_x[i], _pos_z[i]});
    _flecs_id[i].set<ecs::Transform3DData>(
        {_face_x[i], _face_z[i], _actual_vx[i], _actual_vz[i]});
    _flecs_id[i].set<ecs::State>({(ecs::UnitState)_state[i]});
    _flecs_id[i].set<ecs::AmmoInfo>({_ammo[i], _mag_size[i]});
    _flecs_id[i].set<ecs::Morale>({_morale[i], 1.0f});
    _flecs_id[i].set<ecs::CombatBridging>(
        {_deploy_timer[i], _target_id[i], _attack_timer[i], _reload_timer[i]});
    _flecs_id[i].set<ecs::Cooldowns>({_attack_cooldown[i]});
    _flecs_id[i].set<ecs::Role>({_role[i]});
    _flecs_id[i].set<ecs::Health>({_health[i], 100.0f});
    _flecs_id[i].set<ecs::Team>({_team[i]});
    _flecs_id[i].set<ecs::Posture>({(ecs::UnitPosture)_posture[i],
                                    (ecs::UnitPosture)_posture_target[i],
                                    _posture_timer[i]});
    _flecs_id[i].set<ecs::MovementBridging>(
        {_climb_cooldown[i], _climb_target_y[i], _climb_dest_x[i],
         _climb_dest_z[i], _fall_start_y[i], _vel_y[i], _pos_y[i],
         _move_mode[i], (uint8_t)_order[i], (int32_t)_squad_id[i],
         (int32_t)_squad_member_idx[i], _settle_timer[i]});

    if (_is_peeking[i]) {
      _flecs_id[i].add<ecs::IsPeeking>();
    } else {
      _flecs_id[i].remove<ecs::IsPeeking>();
    }
  }
}

void SimulationServer::_sync_flecs_to_soa() {
  for (int i = 0; i < _count; i++) {
    if (!_alive[i] || !_flecs_id[i].is_alive())
      continue;

    auto e = _flecs_id[i];
    auto *st = e.get<ecs::State>();
    if (st)
      _state[i] = (UnitState)st->current;

    auto *ammo = e.get<ecs::AmmoInfo>();
    if (ammo)
      _ammo[i] = ammo->current;

    auto *mor = e.get<ecs::Morale>();
    if (mor)
      _morale[i] = mor->current;

    auto *supp = e.get<ecs::Suppression>();
    if (supp)
      _suppression[i] = supp->level;

    auto *cb = e.get<ecs::CombatBridging>();
    if (cb) {
      _attack_timer[i] = cb->attack_timer;
      _reload_timer[i] = cb->reload_timer;
    }

    auto *mb = e.get<ecs::MovementBridging>();
    if (mb) {
      _climb_cooldown[i] = mb->climb_cooldown;
      _fall_start_y[i] = mb->fall_start_y;
      _vel_y[i] = mb->vel_y;
      _pos_y[i] = mb->pos_y;
      _move_mode[i] = mb->move_mode;
      _settle_timer[i] = mb->settle_timer;
    }

    auto *hp = e.get<ecs::Health>();
    if (hp)
      _health[i] = hp->current;

    auto *posture = e.get<ecs::Posture>();
    if (posture) {
      _posture[i] = posture->current;
      _posture_target[i] = posture->target;
      _posture_timer[i] = posture->transition_timer;
    }

    auto *xform = e.get<ecs::Transform3DData>();
    if (xform) {
      _face_x[i] = xform->face_x;
      _face_z[i] = xform->face_z;
      _actual_vx[i] = xform->actual_vx;
      _actual_vz[i] = xform->actual_vz;
    }
  }
}

void SimulationServer::_sys_combat(
    flecs::iter &it, const ecs::LegacyIndex *idx_comp, ecs::State *state_comp,
    ecs::CombatBridging *cb_comp, const ecs::Transform3DData *xform_comp,
    const ecs::Role *role_comp, ecs::AmmoInfo *ammo_comp,
    const ecs::Cooldowns *cd_comp, const ecs::Morale *morale_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    int i = idx_comp[row].val;
    uint8_t current_state = state_comp[row].current;

    if (current_state != ST_ENGAGING && current_state != ST_IN_COVER &&
        current_state != ST_SUPPRESSING && current_state != ST_BERSERK)
      continue;

    // Units in cover can only fire while peeking
    if (current_state == ST_IN_COVER && !_is_peeking[i])
      continue;

    // Weapon deployment gate: heavy weapons can't fire until deployed
    if (cb_comp[row].deploy_timer > 0.0f)
      continue;

    // Movement gate: must decelerate before firing (berserkers exempt)
    if (current_state != ST_BERSERK) {
      float speed_sq = xform_comp[row].actual_vx * xform_comp[row].actual_vx +
                       xform_comp[row].actual_vz * xform_comp[row].actual_vz;
      if (speed_sq > 1.0f) // > 1.0 m/s — still decelerating
        continue;
    }

    int32_t target = cb_comp[row].target_id;
    if (target < 0 ||
        !_alive[target]) // Fallback to _alive array for global lookups
      continue;

    // Decrement attack timer
    cb_comp[row].attack_timer -= delta;
    if (cb_comp[row].attack_timer > 0.0f)
      continue;

    // Mortar safety gate: do not fire too close/far even if target was
    // selected.
    if (role_comp[row].id == ROLE_MORTAR) {
      // Cross-entity spatial check still relies on _pos arrays for now
      // until spatial hash is ECSified
      float dx = _pos_x[target] - _pos_x[i];
      float dz = _pos_z[target] - _pos_z[i];
      float dist_xz = std::sqrt(dx * dx + dz * dz);
      if (dist_xz < MORTAR_MIN_RANGE || dist_xz > MORTAR_MAX_RANGE) {
        continue;
      }
    }

    // Fire! (berserkers fire faster)
    cb_comp[row].attack_timer =
        (current_state == ST_BERSERK)
            ? cd_comp[row].attack * BERSERK_COOLDOWN_MULT
            : cd_comp[row].attack;

    // Check ammo
    if (ammo_comp[row].current <= 0) {
      if (current_state == ST_BERSERK) {
        continue; // Berserkers keep charging without reloading
      }
      state_comp[row].current = (ecs::UnitState)ST_RELOADING;
      cb_comp[row].reload_timer = _tune_reload_time;
      continue;
    }

    // Paranoid indirect-fire operators...
    if (_personality[i] == PERS_PARANOID &&
        (role_comp[row].id == ROLE_GRENADIER ||
         role_comp[row].id == ROLE_MORTAR) &&
        morale_comp[row].current <
            _personality_morale(PERS_PARANOID).break_threshold) {
      continue; // Don't fire grenades at allies
    }

    ammo_comp[row].current--;

    // Wall Penetration Fix: Verify LOS before firing
    if (role_comp[row].id != ROLE_MORTAR) {
      if (!_check_los(i, target)) {
        ammo_comp[row].current++; // Refund ammo
        cb_comp[row].attack_timer = 0.0f;
        continue;
      }
    }

    // Spawn a real projectile instead of hitscan
    _spawn_projectile(i, target);
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Projectile Pool
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_spawn_projectile(int32_t shooter_id,
                                         int32_t target_id) {
  // Muzzle position: unit pos + facing * forward + height
  float mx = _pos_x[shooter_id] + _face_x[shooter_id] * MUZZLE_FWD;
  float my = _pos_y[shooter_id] + _muzzle_height(shooter_id);
  float mz = _pos_z[shooter_id] + _face_z[shooter_id] * MUZZLE_FWD;

  // Record muzzle flash event for VFX rendering
  if (_muzzle_event_count < MAX_MUZZLE_EVENTS) {
    auto &evt = _muzzle_events[_muzzle_event_count++];
    evt.pos_x = mx;
    evt.pos_y = my;
    evt.pos_z = mz;
    evt.face_x = _face_x[shooter_id];
    evt.face_z = _face_z[shooter_id];
    evt.team = _team[shooter_id];
    evt.role = _role[shooter_id];
  }

  // Aim toward target: suppressive fire uses last-known position +
  // scatter, direct fire uses current position for accurate tracking.
  float tx, ty, tz;
  if (_target_suppressive[shooter_id]) {
    // Suppressive: aim at last-known position (not current — prevents
    // wallhack)
    tx = _last_known_x[target_id] + (_randf() * 2.0f - 1.0f) * SUPPRESS_SCATTER;
    ty = _pos_y[target_id] +
         _center_mass(target_id); // Y not tracked in last-known
    tz = _last_known_z[target_id] + (_randf() * 2.0f - 1.0f) * SUPPRESS_SCATTER;
  } else {
    tx = _pos_x[target_id];
    ty = _pos_y[target_id] + _center_mass(target_id);
    tz = _pos_z[target_id];
  }

  bool is_mortar = (_role[shooter_id] == ROLE_MORTAR);
  if (is_mortar) {
    float tdx = tx - _pos_x[shooter_id];
    float tdz = tz - _pos_z[shooter_id];
    float dist_xz = std::sqrt(tdx * tdx + tdz * tdz);
    if (dist_xz < MORTAR_MIN_RANGE || dist_xz > MORTAR_MAX_RANGE) {
      return;
    }

    float scatter =
        std::clamp(MORTAR_MIN_SCATTER + dist_xz * MORTAR_SCATTER_PER_M,
                   MORTAR_MIN_SCATTER, _tune_mortar_max_scatter);
    tx += (_randf() * 2.0f - 1.0f) * scatter;
    tz += (_randf() * 2.0f - 1.0f) * scatter;
    ty = std::max(ty, 0.6f); // keep impact point above terrain floor
  }

  float dx = tx - mx, dy = ty - my, dz = tz - mz;
  float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist < 0.01f)
    dist = 0.01f;
  float inv_dist = 1.0f / dist;
  dx *= inv_dist;
  dy *= inv_dist;
  dz *= inv_dist;

  // Apply accuracy spread (random cone)
  RoleBallistics bal = _role_ballistics(_role[shooter_id]);
  float spread = bal.base_spread * (1.0f + _suppression[shooter_id] * 1.5f);

  // Settle penalty: recently-stopped units have degraded accuracy
  if (_settle_timer[shooter_id] > 0.0f) {
    float max_settle = _role_settle_time(_role[shooter_id]);
    if (max_settle > 0.0f)
      spread *= (1.0f + (_settle_timer[shooter_id] / max_settle) *
                            _tune_settle_spread);
  }

  // Posture accuracy: crouching/prone tightens spread
  spread *= _accuracy_mult(shooter_id);

  // Movement penalty: firing while moving degrades accuracy
  {
    float spd2 = _actual_vx[shooter_id] * _actual_vx[shooter_id] +
                 _actual_vz[shooter_id] * _actual_vz[shooter_id];
    if (spd2 > 1.0f) { // > 1 m/s
      float spd = std::sqrt(spd2);
      spread *= (1.0f + std::min(spd * 0.12f,
                                 0.8f)); // up to 80% wider at 6.7+ m/s
    }
  }

  // Suppressive fire: wider spread (area denial, not precision)
  if (_target_suppressive[shooter_id]) {
    spread *= SUPPRESS_SPREAD_MULT;
  }

  // Height advantage: up to 20% tighter spread when shooting downhill
  float h_diff_shoot = _pos_y[shooter_id] - _pos_y[target_id];
  if (h_diff_shoot > 0.0f) {
    float h_bonus = std::clamp(h_diff_shoot / 10.0f, 0.0f, 0.2f);
    spread *= (1.0f - h_bonus);
  }

  // Berserker: much worse accuracy (spray and pray)
  if (_state[shooter_id] == ST_BERSERK) {
    spread /= BERSERK_ACCURACY_MULT; // ~3.3x wider spread
  }

  // Gas along firing line: +100% spread (2x wider) when firing through
  // gas
  if (_gpu_map.is_valid() && _gpu_map->is_gpu_available()) {
    Vector3 s_pos(mx, my, mz);
    Vector3 t_pos(tx, ty, tz);
    float gas_los = _gpu_map->sample_gas_along_ray(s_pos, t_pos);
    if (gas_los > 0.2f) {
      spread *= (1.0f + gas_los); // Up to 2x at full density
    }
  }

  float angle = _randf() * 6.28318f;
  float deflection = _randf() * spread;

  // Build perpendicular basis for cone spread
  float px, py, pz;
  if (std::abs(dy) < 0.9f) {
    px = dz;
    py = 0.0f;
    pz = -dx;
  } else {
    px = 1.0f;
    py = 0.0f;
    pz = 0.0f;
  }
  float pl = std::sqrt(px * px + py * py + pz * pz);
  if (pl > 0.001f) {
    px /= pl;
    py /= pl;
    pz /= pl;
  }

  float qx = dy * pz - dz * py;
  float qy = dz * px - dx * pz;
  float qz = dx * py - dy * px;

  float sin_d = std::sin(deflection);
  float cos_d = std::cos(deflection);
  float sin_a = std::sin(angle);
  float cos_a = std::cos(angle);

  float sdx = cos_d * dx + sin_d * (cos_a * px + sin_a * qx);
  float sdy = cos_d * dy + sin_d * (cos_a * py + sin_a * qy);
  float sdz = cos_d * dz + sin_d * (cos_a * pz + sin_a * qz);

  float vel = bal.muzzle_velocity;
  float lifetime = is_mortar ? MORTAR_PROJ_MAX_LIFETIME : PROJ_MAX_LIFETIME;
  uint8_t type =
      is_mortar ? 3 : ((_role[shooter_id] == ROLE_GRENADIER) ? 1 : 0);
  uint8_t payload = PAYLOAD_KINETIC;

  if (type == 0 && _personality[shooter_id] == PERS_PARANOID &&
      _morale[shooter_id] <
          _personality_morale(PERS_PARANOID).break_threshold) {
    type = 2; // Paranoid bullet
  }

  float vx = sdx * vel;
  float vy = sdy * vel;
  float vz = sdz * vel;

  if (type == 1 || type == 3) {
    float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
    float arc = (type == 3) ? MORTAR_ARC_ANGLE : GRENADE_ARC_ANGLE;
    vy += speed * std::sin(arc);
  }

  ecs.entity()
      .set<ecs::ProjectileData>({bal.damage, bal.energy, lifetime, type,
                                 _team[shooter_id], payload, shooter_id})
      .set<ecs::ProjectileFlight>({mx, my, mz, vx, vy, vz});

  _proj_active_count++;

  if (is_mortar) {
    _mortar_rounds_fired_tick++;
    _mortar_total_rounds_fired++;
  }
}

void SimulationServer::_despawn_projectile(flecs::entity e) {
  e.destruct();
  _proj_active_count--;
}

// ═══════════════════════════════════════════════════════════════════════
//  Projectile Tick
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_sys_projectiles(flecs::iter &it,
                                        const ecs::ProjectileData *p_data,
                                        ecs::ProjectileFlight *p_flight) {
  VoxelWorld *vw = VoxelWorld::get_singleton();
  float delta = it.delta_time();

  // We can only reset this safely if we are running the system sequentially
  // or on a single thread. Assuming it's tracked globally for now.
  _wall_pen_count = 0;

  for (auto i : it) {
    auto e = it.entity(i);
    const auto &data = p_data[i];
    auto &flight = p_flight[i];

    // 1. Lifetime
    // Note: We need to mute the data to reduce lifetime, but Flecs system
    // signature requires we pass things in cleanly. We'll use get_mut for the
    // lifetime decrease to strictly preserve the const signature if required,
    // or we can just assume ProjectileData was passed as mutable. Given the
    // header is const ecs::ProjectileData*, we use get_mut.
    auto *mut_data = e.get_mut<ecs::ProjectileData>();
    if (mut_data) {
      mut_data->lifetime -= delta;
      if (mut_data->lifetime <= 0.0f) {
        _despawn_projectile(e);
        continue;
      }
    }

    // 2. Store previous position
    float px = flight.x;
    float py = flight.y;
    float pz = flight.z;

    // 3. Gravity
    flight.vy -= PROJ_GRAVITY * delta;

    // 4. Movement delta
    float dx = flight.vx * delta;
    float dy = flight.vy * delta;
    float dz = flight.vz * delta;
    float move_dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // 5. Voxel collision via multi-hit DDA raycast
    bool stopped = false;
    if (vw != nullptr && move_dist > 0.001f) {
      float inv_md = 1.0f / move_dist;
      Vector3 dir(dx * inv_md, dy * inv_md, dz * inv_md);

      VoxelHit pen_hits[MAX_PEN_VOXELS];
      int num_hits = vw->raycast_multi(Vector3(px, py, pz), dir, move_dist,
                                       pen_hits, MAX_PEN_VOXELS);

      if (num_hits > 0) {
        if (data.type == 1 || data.type == 3) {
          // Explosive projectile detonates on first solid hit.
          flight.x = pen_hits[0].world_pos.x;
          flight.y = pen_hits[0].world_pos.y;
          flight.z = pen_hits[0].world_pos.z;
          _explode(e, data, flight);
          stopped = true;
        } else {
          // Kinetic projectile stops on first solid voxel.
          _record_impact(pen_hits[0].world_pos, pen_hits[0].normal,
                         pen_hits[0].material);

          // Minor voxel damage at impact point
          float density = get_material_density(pen_hits[0].material);
          if (density > 0.0f) {
            _damage_voxel(pen_hits[0].voxel_pos.x, pen_hits[0].voxel_pos.y,
                          pen_hits[0].voxel_pos.z,
                          data.energy * 0.1f * VOXEL_DMG_FACTOR);
          }
          _despawn_projectile(e);
          stopped = true;
        }
      } else {
        // No voxel hit — full movement
        flight.x = px + dx;
        flight.y = py + dy;
        flight.z = pz + dz;
      }
    } else {
      flight.x = px + dx;
      flight.y = py + dy;
      flight.z = pz + dz;
    }

    if (stopped)
      continue;

    // 6. Ground-level explosive detonation
    if ((data.type == 1 || data.type == 3) && flight.y < 0.5f) {
      _explode(e, data, flight);
      continue;
    }

    // 7. Bounds check
    if (flight.y < -1.0f || flight.x < -_map_half_w - 10.0f ||
        flight.x > _map_half_w + 10.0f || flight.z < -_map_half_h - 10.0f ||
        flight.z > _map_half_h + 10.0f) {
      _despawn_projectile(e);
      continue;
    }

    // 8. Unit collision
    _proj_check_unit_hits(e, data, flight);
    if (!e.is_alive())
      continue;

    // 9. Near-miss suppression
    _proj_apply_near_miss(data, flight);
  }
}

void SimulationServer::_proj_check_unit_hits(
    flecs::entity e, const ecs::ProjectileData &data,
    const ecs::ProjectileFlight &flight) {
  float px = flight.x;
  float py = flight.y;
  float pz = flight.z;
  uint8_t proj_team = data.team;

  int gx = (int)((px + _map_half_w) / (float)SPATIAL_CELL_M);
  int gz = (int)((pz + _map_half_h) / (float)SPATIAL_CELL_M);
  gx = std::clamp(gx, 0, _spatial_w - 1);
  gz = std::clamp(gz, 0, _spatial_h - 1);

  bool is_paranoid = (data.type == 2);
  int32_t shooter = data.shooter;

  for (int dz = -1; dz <= 1; dz++) {
    for (int dx = -1; dx <= 1; dx++) {
      int nx = gx + dx;
      int nz = gz + dz;
      if (nx < 0 || nx >= _spatial_w || nz < 0 || nz >= _spatial_h)
        continue;

      int32_t idx = _spatial_cells[nz * _spatial_w + nx];
      while (idx >= 0) {
        // Paranoid bullets hit same team (except shooter); normal bullets
        // hit enemy team
        bool can_hit =
            _alive[idx] &&
            (is_paranoid ? (_team[idx] == proj_team && idx != shooter)
                         : (_team[idx] != proj_team));

        if (can_hit) {
          float ex = _pos_x[idx] - px;
          float ey = (_pos_y[idx] + _center_mass(idx)) - py;
          float ez = _pos_z[idx] - pz;
          float d2 = ex * ex + ey * ey + ez * ez;
          float hr = _hit_radius_for(idx);
          float hit_r2 = hr * hr;

          if (d2 < hit_r2) {
            if (data.type == 1 || data.type == 3) {
              // Explosives detonate on unit contact.
              _explode(e, data, flight);
              return;
            }
            // Cover interception: targets in cover can deflect incoming
            // rounds Cover value 0-1 → deflection probability 0-40% (max
            // at full cover)
            if (_cover_value[idx] > 0.1f &&
                _randf() < _cover_value[idx] * 0.4f) {
              // Round deflected by cover — apply suppression only
              _suppression[idx] = std::min(_suppression[idx] + 0.04f, 1.0f);
              // Using e.get_mut allows us to modify the energy inplace during
              // the system
              auto *mut_data = e.get_mut<ecs::ProjectileData>();
              if (mut_data) {
                mut_data->energy -= 0.5f;
                if (mut_data->energy <= 0.0f) {
                  _despawn_projectile(e);
                  return;
                }
              }
              break; // move to next cell
            }
            // Direct hit!
            _health[idx] -= data.damage;
            _suppression[idx] =
                std::min(_suppression[idx] + _tune_hit_supp, 1.0f);

            // Heavy damage pulse: FEAR if >30% HP lost in one hit
            if (data.damage > 0.3f && _health[idx] > 0.0f) {
              int team_idx = _team[idx] - 1;
              if (team_idx >= 0 && team_idx < 2 &&
                  _pheromones[team_idx].is_valid()) {
                Vector3 pos(_pos_x[idx], 0.0f, _pos_z[idx]);
                _pheromones[team_idx]->deposit_radius(pos, CH_FEAR, 5.0f, 4.0f);
              }
            }

            if (_health[idx] <= 0.0f) {
              _health[idx] = 0.0f;
              // Pheromone: DANGER + FEAR at death position
              bool was_ambush =
                  (_state[idx] == ST_IDLE || _state[idx] == ST_MOVING);
              _pheromone_deposit_danger(idx, shooter, was_ambush);
              kill_unit(idx);
            }

            // Bullet loses energy (over-penetration possible)
            auto *mut_data = e.get_mut<ecs::ProjectileData>();
            if (mut_data) {
              mut_data->energy -= 0.3f;
              mut_data->damage *= 0.55f;

              if (mut_data->energy <= 0.0f) {
                _despawn_projectile(e);
                return;
              }
            }
            break; // one hit per neighborhood check
          }
        }
        idx = _spatial_next[idx];
      }
    }
  }
}

void SimulationServer::_proj_apply_near_miss(
    const ecs::ProjectileData &data, const ecs::ProjectileFlight &flight) {
  float px = flight.x;
  float pz = flight.z;
  uint8_t proj_team = data.team;
  bool is_paranoid = (data.type == 2);
  int32_t shooter = data.shooter;

  int gx = (int)((px + _map_half_w) / (float)SPATIAL_CELL_M);
  int gz = (int)((pz + _map_half_h) / (float)SPATIAL_CELL_M);
  gx = std::clamp(gx, 0, _spatial_w - 1);
  gz = std::clamp(gz, 0, _spatial_h - 1);

  float nm_r2 = _tune_near_miss_dist * _tune_near_miss_dist;

  for (int dz = -1; dz <= 1; dz++) {
    for (int dx = -1; dx <= 1; dx++) {
      int nx = gx + dx;
      int nz = gz + dz;
      if (nx < 0 || nx >= _spatial_w || nz < 0 || nz >= _spatial_h)
        continue;

      int32_t idx = _spatial_cells[nz * _spatial_w + nx];
      while (idx >= 0) {
        // Paranoid bullets suppress same team; normal bullets suppress
        // enemy team
        bool can_suppress =
            _alive[idx] &&
            (is_paranoid ? (_team[idx] == proj_team && idx != shooter)
                         : (_team[idx] != proj_team));

        if (can_suppress) {
          float ex = _pos_x[idx] - px;
          float ez = _pos_z[idx] - pz;
          float d2 = ex * ex + ez * ez;
          float hr = _hit_radius_for(idx);

          if (d2 < nm_r2 && d2 > hr * hr) {
            float proximity = 1.0f - std::sqrt(d2) / _tune_near_miss_dist;
            _suppression[idx] = std::min(
                _suppression[idx] + _tune_near_miss_supp * proximity, 1.0f);
          }
        }
        idx = _spatial_next[idx];
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Explosions
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_explode(flecs::entity e,
                                const ecs::ProjectileData &data,
                                const ecs::ProjectileFlight &flight) {
  VoxelWorld *vw = VoxelWorld::get_singleton();
  float ex = flight.x;
  float ey = flight.y;
  float ez = flight.z;
  Vector3 epos(ex, ey, ez);
  bool is_mortar = (data.type == 3);
  uint8_t payload = data.payload;

  // ── Gas payload: spawn cloud instead of kinetic explosion ──────────
  if (payload >= PAYLOAD_SMOKE && payload <= PAYLOAD_TOXIC) {
    float cloud_radius =
        is_mortar ? GAS_CLOUD_RADIUS_MORTAR : GAS_CLOUD_RADIUS_GRENADE;
    if (_gpu_map.is_valid() && _gpu_map->is_gpu_available()) {
      _gpu_map->spawn_gas_cloud(epos, cloud_radius, GAS_CLOUD_DENSITY, payload);
    }

    // Light suppression burst from the canister pop (smaller than frag)
    float supp_radius = is_mortar ? 8.0f : 4.0f;
    _explosion_nearby.clear();
    _get_units_in_radius(ex, ez, supp_radius, _explosion_nearby);
    for (int32_t uid : _explosion_nearby) {
      if (!_alive[uid])
        continue;
      float dx = _pos_x[uid] - ex;
      float dz = _pos_z[uid] - ez;
      float dist = std::sqrt(dx * dx + dz * dz);
      if (dist < supp_radius) {
        float falloff = 1.0f - (dist / supp_radius);
        _suppression[uid] = std::min(_suppression[uid] + 0.15f * falloff, 1.0f);
      }
    }

    // Record as gas impact event (type=2, VFX will use payload to pick
    // particles)
    _record_explosion_impact(epos, cloud_radius, Dictionary(), payload);
    _despawn_projectile(e);
    return;
  }

  // ── Kinetic payload: standard frag/HE explosion ───────────────────
  float blast_radius = is_mortar ? MORTAR_BLAST_RADIUS : GRENADE_BLAST_RADIUS;
  float damage_radius =
      is_mortar ? _tune_mortar_dmg_radius : _tune_grenade_dmg_radius;
  float suppression_radius =
      is_mortar ? MORTAR_SUPPRESSION_RADIUS : GRENADE_SUPPRESSION_RADIUS;
  float max_damage = is_mortar ? _tune_mortar_max_dmg : _tune_grenade_max_dmg;
  float max_suppression =
      is_mortar ? MORTAR_MAX_SUPPRESSION : GRENADE_MAX_SUPPRESSION;

  // 1. Voxel destruction (with material data for VFX)
  Dictionary destroy_data;
  if (vw) {
    destroy_data = vw->destroy_sphere_ex(epos, blast_radius, MAX_INLINE_DEBRIS);

    // Update height map so tactical queries use post-destruction terrain
    if (_gpu_map.is_valid()) {
      int min_cx = _gpu_map->cover_to_cell_x(ex - blast_radius);
      int max_cx = _gpu_map->cover_to_cell_x(ex + blast_radius);
      int min_cz = _gpu_map->cover_to_cell_z(ez - blast_radius);
      int max_cz = _gpu_map->cover_to_cell_z(ez + blast_radius);
      _gpu_map->update_height_map_region(min_cx, max_cx, min_cz, max_cz);
    }
  }

  // 2. Area damage to all units (friendly fire enabled)
  _explosion_nearby.clear();
  _get_units_in_radius(ex, ez, damage_radius, _explosion_nearby);
  int kills_this_explosion = 0;

  for (int32_t uid : _explosion_nearby) {
    if (!_alive[uid])
      continue;

    float dx = _pos_x[uid] - ex;
    float dy = (_pos_y[uid] + _center_mass(uid)) - ey;
    float dz = _pos_z[uid] - ez;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (dist < damage_radius) {
      float falloff = 1.0f - (dist / damage_radius);
      float dmg = max_damage * falloff;
      _health[uid] -= dmg;
      if (_health[uid] <= 0.0f) {
        _health[uid] = 0.0f;
        kill_unit(uid);
        kills_this_explosion++;
      }
    }
  }

  // 3. Wider suppression wave
  _explosion_nearby.clear();
  _get_units_in_radius(ex, ez, suppression_radius, _explosion_nearby);
  int suppression_events = 0;

  for (int32_t uid : _explosion_nearby) {
    if (!_alive[uid])
      continue;

    float dx = _pos_x[uid] - ex;
    float dz = _pos_z[uid] - ez;
    float dist = std::sqrt(dx * dx + dz * dz);

    if (dist < suppression_radius) {
      float falloff = 1.0f - (dist / suppression_radius);
      _suppression[uid] =
          std::min(_suppression[uid] + max_suppression * falloff, 1.0f);
      suppression_events++;
    }
  }

  if (is_mortar) {
    _mortar_impacts_tick++;
    _mortar_total_impacts++;
    _mortar_suppression_events_tick += suppression_events;
    _mortar_total_suppression_events += suppression_events;
    _mortar_kills_tick += kills_this_explosion;
    _mortar_total_kills += kills_this_explosion;
  }

  // 4. Pheromone: DANGER + FEAR at explosion site
  _pheromone_deposit_explosion(epos, blast_radius, data.team);

  // 5. Record explosion impact event with material data (type=1)
  _record_explosion_impact(epos, blast_radius, destroy_data);

  // 6. Despawn the grenade
  _despawn_projectile(e);
}

// ═══════════════════════════════════════════════════════════════════════
//  Gas Grenade API
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::throw_gas_grenade(int32_t thrower, const Vector3 &target,
                                         int payload) {
  if (thrower < 0 || thrower >= _count || !_alive[thrower])
    return;
  if (payload < PAYLOAD_SMOKE || payload > PAYLOAD_TOXIC)
    return;

  // Calculate throw trajectory (same as grenadier)
  float sx = _pos_x[thrower];
  float sy = _pos_y[thrower] + MUZZLE_HEIGHT;
  float sz = _pos_z[thrower];
  float dx = target.x - sx;
  float dy = target.y - sy;
  float dz = target.z - sz;
  float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist < 0.01f)
    dist = 0.01f;

  float vel = 50.0f; // Grenade muzzle velocity
  float vx = (dx / dist) * vel;
  float vy = (dy / dist) * vel;
  float vz = (dz / dist) * vel;

  // Add grenade arc
  float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
  vy += speed * std::sin(GRENADE_ARC_ANGLE);

  ecs.entity()
      .set<ecs::ProjectileData>({0.0f, 2.0f, PROJ_MAX_LIFETIME, 1,
                                 _team[thrower], (uint8_t)payload, thrower})
      .set<ecs::ProjectileFlight>({sx + (dx / dist) * MUZZLE_FWD, sy,
                                   sz + (dz / dist) * MUZZLE_FWD, vx, vy, vz});

  _proj_active_count++;
}

void SimulationServer::spawn_gas_at(const Vector3 &pos, float radius,
                                    float density, int gas_type) {
  if (gas_type < PAYLOAD_SMOKE || gas_type > PAYLOAD_TOXIC)
    return;
  if (_gpu_map.is_valid() && _gpu_map->is_gpu_available()) {
    _gpu_map->spawn_gas_cloud(pos, radius, density, (uint8_t)gas_type);
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Main Tick
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::tick(float delta) {
  if (is_musket_mode)
    return;

  uint64_t t0 = Time::get_singleton()->get_ticks_usec();

  _game_time += delta;
  _los_checks = 0;
  _spatial_queries = 0;
  _impact_count = 0;       // Reset impact events for this frame
  _muzzle_event_count = 0; // Reset muzzle flash events

  // Reset per-tick fog-of-war + engagement diagnostics
  _fow_targets_skipped = 0;
  _fow_suppressive_shots = 0;
  _fow_vis_checks = 0;
  _fow_vis_hits = 0;
  _fow_contacts_gained = 0;
  _fow_contacts_lost = 0;
  _fow_influence_filtered = 0;
  _engagements_this_tick = 0;
  _engagements_visible = 0;
  _engagements_suppressive = 0;
  _wall_pen_blocked = 0;
  _mortar_rounds_fired_tick = 0;
  _mortar_impacts_tick = 0;
  _mortar_suppression_events_tick = 0;
  _mortar_kills_tick = 0;
  _climb_started_tick = 0;
  _fall_started_tick = 0;
  _fall_damage_tick = 0;
  _avg_formation_pull = 0.0f;
  _avg_flow_push = 0.0f;
  _avg_threat_push = 0.0f;
  _avg_total_speed = 0.0f;

// Profiling helper
#define PROF_BEGIN() auto _prof_t = std::chrono::high_resolution_clock::now()
#define PROF_END(id)                                                           \
  do {                                                                         \
    auto _prof_now = std::chrono::high_resolution_clock::now();                \
    _sub_us[id] =                                                              \
        std::chrono::duration<double, std::micro>(_prof_now - _prof_t)         \
            .count();                                                          \
    _sub_ema[id] += PROF_EMA_ALPHA * (_sub_us[id] - _sub_ema[id]);             \
    _prof_t = _prof_now;                                                       \
  } while (0)

  PROF_BEGIN();

  // Debug Logging
  if (_tune_debug_logging) {
    static std::ofstream _debug_log;
    if (!_debug_log.is_open()) {
      _debug_log.open("unit_debug_log.csv", std::ios::out | std::ios::trunc);
      _debug_log << "timestamp,unit_id,team,x,z,state,target_id\n";
    }
    for (int i = 0; i < _count; i++) {
      if (_alive[i]) {
        _debug_log << _game_time << "," << i << "," << (int)_team[i] << ","
                   << _pos_x[i] << "," << _pos_z[i] << "," << (int)_state[i]
                   << "," << _target_id[i] << "\n";
      }
    }
  }

  // 1. Spatial hash (must be first)
  _rebuild_spatial_hash();
  PROF_END(SUB_SPATIAL);

  // 1.5 Squad centroids (needed for goal projection)
  _compute_squad_centroids();
  PROF_END(SUB_CENTROIDS);

  // 2. Batch tactical updates (cheap, O(N))
  _update_attackers_count();
  PROF_END(SUB_ATTACKERS);

  _update_cover_values();
  PROF_END(SUB_COVER_VALUES);

  // 3. Influence map update (throttled to every 0.5s)
  _influence_timer -= delta;
  if (_influence_timer <= 0.0f) {
    _influence_timer = INFLUENCE_UPDATE_INTERVAL;
    _tick_influence_maps();
  }
  PROF_END(SUB_INFLUENCE);

  // 3.5 Fog of war visibility scan (Moved to shared ECS pipeline at Step 10)
  if (_count > 0) {
    if (_game_time - _vis_last_refresh >= VIS_REFRESH_INTERVAL) {
      _vis_last_refresh = _game_time;
      std::memset(_team_vis, 0, sizeof(_team_vis));
      _vis_cursor = 0;
    } else {
      _vis_cursor += VIS_BATCH_SIZE;
      if (_vis_cursor >= _count) {
        _vis_cursor = 0;
        for (int u = 0; u < _count; u++) {
          if (!_alive[u]) {
            _team_clear_vis(0, u);
            _team_clear_vis(1, u);
          }
        }
      }
    }
  }
  PROF_END(SUB_VISIBILITY);

  // 6.5 Peek cycle for units in cover
  _tick_peek(delta);
  PROF_END(SUB_PEEK);

  // 7. Combat (Moved to shared ECS pipeline at Step 10)
  PROF_END(SUB_COMBAT);

  // 10. ECS Pipeline (Multithreaded: Combat + Movement + Projectiles)
  _sync_soa_to_flecs();
  ecs.progress(delta);
  _sync_flecs_to_soa();
  PROF_END(SUB_MOVEMENT);

  // 11. Capture points
  _tick_capture_points(delta);
  PROF_END(SUB_CAPTURE);

  // 12. Location stats (formation distance, squad spread)
  _tick_location_stats();
  PROF_END(SUB_LOCATION);

  // 13. Gas effects (toxic damage, tear gas suppression)
  _tick_gas_effects(delta);
  PROF_END(SUB_GAS_EFFECTS);

  // 14. Pheromone deposits + CA update
  _tick_pheromones(delta);
  PROF_END(SUB_PHEROMONES);

#undef PROF_BEGIN
#undef PROF_END

  uint64_t t1 = Time::get_singleton()->get_ticks_usec();
  _last_tick_ms = (float)(t1 - t0) / 1000.0f;
}

// ═══════════════════════════════════════════════════════════════════════
//  Gas Effects Subsystem
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_tick_gas_effects(float delta) {
  if (!_gpu_map.is_valid() || !_gpu_map->is_gpu_available())
    return;

  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;

    Vector3 pos(_pos_x[i], _pos_y[i], _pos_z[i]);
    float density = _gpu_map->sample_gas_density(pos);
    if (density < GAS_DENSITY_THRESHOLD)
      continue;

    uint8_t gas_type = _gpu_map->sample_gas_type(pos);

    switch (gas_type) {
    case PAYLOAD_TEAR_GAS:
      // Suppression buildup + morale drain
      _suppression[i] = std::min(
          _suppression[i] + GAS_TEAR_SUPP_RATE * density * delta, 1.0f);
      _morale[i] -= GAS_TEAR_MORALE_DRAIN * density * delta;
      _morale[i] = std::max(0.0f, _morale[i]);
      break;

    case PAYLOAD_TOXIC:
      // Damage over time
      _health[i] -= GAS_TOXIC_DPS * density * delta;
      if (_health[i] <= 0.0f) {
        _health[i] = 0.0f;
        kill_unit(i);
      } else if (_health[i] < GAS_PANIC_HEALTH && _state[i] != ST_RETREATING) {
        // Panic retreat from toxic gas
        _state[i] = ST_RETREATING;
        _morale[i] = 0.0f;
      }
      break;

      // PAYLOAD_SMOKE: No direct combat effects (handled via LOS/accuracy
      // in Phase C)
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Impact Events + Voxel Damage
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_record_impact(const Vector3 &pos, const Vector3 &normal,
                                      uint8_t mat, uint8_t type) {
  if (_impact_count < MAX_IMPACT_EVENTS) {
    auto &evt = _impact_events[_impact_count];
    evt.position = pos;
    evt.normal = normal;
    evt.material = mat;
    evt.type = type;
    evt.payload = 0;
    evt.blast_radius = 0.0f;
    evt.destroyed = 0;
    evt.debris_count = 0;
    std::memset(evt.mat_histogram, 0, sizeof(evt.mat_histogram));
    _impact_count++;
  }
}

void SimulationServer::_record_explosion_impact(const Vector3 &pos,
                                                float blast_radius,
                                                const Dictionary &destroy_data,
                                                uint8_t payload_type) {
  if (_impact_count >= MAX_IMPACT_EVENTS)
    return;

  auto &evt = _impact_events[_impact_count];
  evt.position = pos;
  evt.normal = Vector3(0, 1, 0);
  evt.type = (payload_type > 0) ? 2 : 1; // 2=gas, 1=kinetic explosion
  evt.payload = payload_type;
  evt.blast_radius = blast_radius;

  // Extract data from destroy_sphere_ex result
  evt.material = (uint8_t)(int)destroy_data.get("dominant_material", 0);
  evt.destroyed = (int)destroy_data.get("destroyed", 0);

  // Copy histogram
  std::memset(evt.mat_histogram, 0, sizeof(evt.mat_histogram));
  if (destroy_data.has("material_histogram")) {
    PackedInt32Array hist = destroy_data["material_histogram"];
    int count = std::min((int)hist.size(), 16);
    for (int i = 0; i < count; i++) {
      evt.mat_histogram[i] = hist[i];
    }
  }

  // Copy inline debris samples
  evt.debris_count = 0;
  if (destroy_data.has("debris")) {
    Array debris = destroy_data["debris"];
    int n = std::min((int)debris.size(), (int)MAX_INLINE_DEBRIS);
    for (int i = 0; i < n; i++) {
      Dictionary d = debris[i];
      evt.debris_positions[i] = d.get("position", Vector3());
      evt.debris_materials[i] = (uint8_t)(int)d.get("material", 0);
    }
    evt.debris_count = (uint8_t)n;
  }

  _impact_count++;
}

void SimulationServer::_damage_voxel(int x, int y, int z, float dmg) {
  VoxelWorld *vw = VoxelWorld::get_singleton();
  if (!vw)
    return;

  uint8_t mat = vw->get_voxel(x, y, z);
  if (mat == 0)
    return; // MAT_AIR

  float max_hp = get_material_health(mat);
  if (max_hp <= 0.0f)
    return; // indestructible

  uint64_t key = _pack_voxel_key(x, y, z);
  auto it = _voxel_hp.find(key);
  float hp;
  if (it != _voxel_hp.end()) {
    hp = it->second - dmg;
  } else {
    hp = max_hp - dmg;
  }

  if (hp <= 0.0f) {
    _voxel_hp.erase(key);
    vw->set_voxel_dirty(x, y, z,
                        0); // MAT_AIR — marks boundary neighbors dirty too
    // Queue structural integrity check for column above
    vw->queue_collapse_check_voxel(x, y, z, 1);
  } else {
    _voxel_hp[key] = hp;
  }
}

void SimulationServer::_sys_suppression_decay(
    flecs::iter &it, ecs::Suppression *supp_comp,
    const ecs::Posture *posture_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    float posture_mult =
        _posture_profile(posture_comp[row].current).supp_decay_mult;
    float decay = 1.0f - _tune_suppression_decay * posture_mult * delta;
    supp_comp[row].level *= decay;
    if (supp_comp[row].level < 0.01f)
      supp_comp[row].level = 0.0f;
  }
}

void SimulationServer::_sys_morale(flecs::iter &it,
                                   const ecs::LegacyIndex *idx_comp,
                                   ecs::Morale *morale_comp,
                                   const ecs::Suppression *supp_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    int i = idx_comp[row].val;
    auto mod = _personality_morale(_personality[i]);

    // Drain from suppression (modified by personality)
    if (supp_comp[row].level > 0.5f) {
      morale_comp[row].current -= 0.1f * mod.suppression_decay_mult * delta;
    }

    // Drain from isolation (modified by personality)
    if (_nearby_squad_count[i] == 0) {
      morale_comp[row].current -= 0.05f * mod.isolation_decay_mult * delta;
    }

    // Recover based on nearby squad mates (modified by personality)
    float ally_recovery =
        0.02f + 0.01f * std::min((int)_nearby_squad_count[i], 4);
    morale_comp[row].current += ally_recovery * mod.ally_recovery_mult * delta;

    // Pheromone spatial morale modifiers
    if (_team[i] > 0 && _team[i] <= 2) {
      int ti = _team[i] - 1;
      if (_pheromones[ti].is_valid()) {
        Vector3 pos(_pos_x[i], 0.0f, _pos_z[i]);
        auto pw = _role_pheromone_weights(_role[i]);
        float fear = _pheromones[ti]->sample(pos, CH_FEAR);
        float courage = _pheromones[ti]->sample(pos, CH_COURAGE);
        float rally = _pheromones[ti]->sample(pos, CH_RALLY);

        morale_comp[row].current -= fear * 0.03f * pw.fear * delta;
        morale_comp[row].current += courage * 0.04f * pw.courage * delta;
        if (rally > 0.1f) {
          morale_comp[row].current += rally * 0.02f * pw.rally * delta;
        }
      }
    }

    morale_comp[row].current =
        std::clamp(morale_comp[row].current, 0.0f, morale_comp[row].max);
  }
}

void SimulationServer::_sys_reload(flecs::iter &it, ecs::State *state_comp,
                                   ecs::CombatBridging *cb_comp,
                                   ecs::AmmoInfo *ammo_comp) {
  float delta = it.delta_time();
  for (auto row : it) {
    if (state_comp[row].current != ecs::ST_RELOADING)
      continue;

    cb_comp[row].reload_timer -= delta;
    if (cb_comp[row].reload_timer <= 0.0f) {
      ammo_comp[row].current = ammo_comp[row].mag_size;
      cb_comp[row].reload_timer = 0.0f;
      state_comp[row].current =
          ecs::ST_IDLE; // Will be re-evaluated by decision tick
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Capture Points
// ═══════════════════════════════════════════════════════════════════════

int SimulationServer::add_capture_point(const Vector3 &pos) {
  if (_capture_count >= MAX_CAPTURE_POINTS)
    return -1;
  int idx = _capture_count++;
  _capture_points[idx].x = pos.x;
  _capture_points[idx].z = pos.z;
  _capture_points[idx].owner_team = 0;
  _capture_points[idx].progress = 0.0f;
  _capture_points[idx].capturing_team = 0;
  _capture_points[idx].active = true;
  return idx;
}

Dictionary SimulationServer::get_capture_data() const {
  PackedVector3Array positions;
  PackedInt32Array owners;
  PackedFloat32Array progress;
  PackedInt32Array capturing;
  PackedInt32Array contested; // 1 if both teams present, else 0

  for (int i = 0; i < _capture_count; i++) {
    positions.push_back(
        Vector3(_capture_points[i].x, 0.0f, _capture_points[i].z));
    owners.push_back(_capture_points[i].owner_team);
    progress.push_back(_capture_points[i].progress);
    capturing.push_back(_capture_points[i].capturing_team);
    contested.push_back(_capture_points[i].contested ? 1 : 0);
  }

  Dictionary d;
  d["positions"] = positions;
  d["owners"] = owners;
  d["progress"] = progress;
  d["capturing"] = capturing;
  d["contested"] = contested;
  d["count"] = _capture_count;
  return d;
}

int SimulationServer::get_capture_count_for_team(int team) const {
  int count = 0;
  for (int i = 0; i < _capture_count; i++) {
    if (_capture_points[i].owner_team == team)
      count++;
  }
  return count;
}

void SimulationServer::_tick_capture_points(float delta) {
  for (int i = 0; i < _capture_count; i++) {
    if (!_capture_points[i].active)
      continue;

    // Count units per team near this point using spatial hash
    _capture_nearby.clear();
    _get_units_in_radius(_capture_points[i].x, _capture_points[i].z,
                         CAPTURE_RADIUS, _capture_nearby);

    int t1_count = 0, t2_count = 0;
    for (int32_t uid : _capture_nearby) {
      if (!_alive[uid])
        continue;
      if (_team[uid] == 1)
        t1_count++;
      else if (_team[uid] == 2)
        t2_count++;
    }

    if (t1_count > 0 && t2_count > 0) {
      // Contested — freeze progress
      _capture_points[i].contested = true;
      continue;
    }
    _capture_points[i].contested = false;

    if (t1_count > 0) {
      if (_capture_points[i].owner_team == 1)
        continue; // already owned
      // Team 1 capturing
      if (_capture_points[i].capturing_team != 1) {
        _capture_points[i].capturing_team = 1;
        _capture_points[i].progress = 0.0f;
      }
      _capture_points[i].progress += CAPTURE_RATE * t1_count * delta;
      if (_capture_points[i].progress >= 1.0f) {
        _capture_points[i].owner_team = 1;
        _capture_points[i].progress = 0.0f;
        _capture_points[i].capturing_team = 0;
      }
    } else if (t2_count > 0) {
      if (_capture_points[i].owner_team == 2)
        continue; // already owned
      // Team 2 capturing
      if (_capture_points[i].capturing_team != 2) {
        _capture_points[i].capturing_team = 2;
        _capture_points[i].progress = 0.0f;
      }
      _capture_points[i].progress += CAPTURE_RATE * t2_count * delta;
      if (_capture_points[i].progress >= 1.0f) {
        _capture_points[i].owner_team = 2;
        _capture_points[i].progress = 0.0f;
        _capture_points[i].capturing_team = 0;
      }
    } else {
      // No one present — slow decay
      if (_capture_points[i].progress > 0.0f) {
        _capture_points[i].progress -= CAPTURE_DECAY * delta;
        if (_capture_points[i].progress <= 0.0f) {
          _capture_points[i].progress = 0.0f;
          _capture_points[i].capturing_team = 0;
        }
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Location Stats (formation distance, squad spread)
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_tick_location_stats() {
  float sum_dist_t1 = 0.0f, sum_dist_t2 = 0.0f;
  float max_dist_t1 = 0.0f, max_dist_t2 = 0.0f;
  int count_t1 = 0, count_t2 = 0;
  _units_beyond_20m = 0;
  _order_follow_squad = 0;
  _order_other = 0;

  // Reset per-state distance accumulators
  std::memset(_dist_by_state, 0, sizeof(_dist_by_state));
  std::memset(_count_by_state, 0, sizeof(_count_by_state));

  // Per-unit: distance to formation slot
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;

    // Order distribution
    if (_order[i] == ORDER_FOLLOW_SQUAD)
      _order_follow_squad++;
    else
      _order_other++;

    if (_order[i] != ORDER_FOLLOW_SQUAD)
      continue;

    int sid = _squad_id[i];
    if (sid < 0 || sid >= MAX_SQUADS || !_squads[sid].active)
      continue;
    if (_squad_alive_counts[sid] <= 0)
      continue;

    Vector3 centroid = _squad_centroids[sid];
    Vector3 dir = _squads[sid].advance_dir;
    float dir_len = dir.length();
    if (dir_len < 0.01f)
      continue;
    dir /= dir_len;

    float lead = GOAL_LEAD_DIST + _squads[sid].advance_offset;
    float spread = _squads[sid].formation_spread;
    int idx = _squad_member_idx[i];
    int total = _squad_alive_counts[sid];

    float perp_x = -dir.z, perp_z = dir.x;
    float slot_x = centroid.x + dir.x * lead;
    float slot_z = centroid.z + dir.z * lead;

    switch (_squads[sid].formation) {
    case FORM_LINE: {
      float t = (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f : 0.0f;
      slot_x += perp_x * t * spread;
      slot_z += perp_z * t * spread;
    } break;
    case FORM_WEDGE: {
      float t = (total > 1) ? ((float)idx / (total - 1) - 0.5f) * 2.0f : 0.0f;
      slot_x += perp_x * t * spread;
      slot_z += perp_z * t * spread;
      float fallback = std::abs(t) * spread * 0.5f;
      slot_x -= dir.x * fallback;
      slot_z -= dir.z * fallback;
    } break;
    case FORM_COLUMN: {
      float t = (total > 1) ? (float)idx / (total - 1) : 0.0f;
      slot_x -= dir.x * t * spread;
      slot_z -= dir.z * t * spread;
    } break;
    case FORM_CIRCLE: {
      float angle = 6.28318530f * idx / std::max(total, 1);
      slot_x = centroid.x + std::cos(angle) * spread;
      slot_z = centroid.z + std::sin(angle) * spread;
    } break;
    default:
      break;
    }

    float dx = _pos_x[i] - slot_x;
    float dz = _pos_z[i] - slot_z;
    float dist = std::sqrt(dx * dx + dz * dz);

    if (_team[i] == 1) {
      sum_dist_t1 += dist;
      if (dist > max_dist_t1)
        max_dist_t1 = dist;
      count_t1++;
    } else {
      sum_dist_t2 += dist;
      if (dist > max_dist_t2)
        max_dist_t2 = dist;
      count_t2++;
    }
    if (dist > 20.0f)
      _units_beyond_20m++;

    // Per-state breakdown
    int st = _state[i];
    if (st >= 0 && st < ST_COUNT) {
      _dist_by_state[st] += dist;
      _count_by_state[st]++;
    }
  }

  _avg_dist_to_slot_t1 = (count_t1 > 0) ? sum_dist_t1 / count_t1 : 0.0f;
  _avg_dist_to_slot_t2 = (count_t2 > 0) ? sum_dist_t2 / count_t2 : 0.0f;
  _max_dist_to_slot_t1 = max_dist_t1;
  _max_dist_to_slot_t2 = max_dist_t2;

  // Per-squad: average distance from members to centroid (squad spread)
  // + advance offset tracking
  float spread_sum = 0.0f;
  int spread_count = 0;
  float adv_sum = 0.0f;
  float adv_max = 0.0f;
  int adv_count = 0;
  for (int s = 0; s < MAX_SQUADS; s++) {
    if (!_squads[s].active || _squad_alive_counts[s] <= 0)
      continue;

    // Track advance offset
    float ao = _squads[s].advance_offset;
    adv_sum += ao;
    if (ao > adv_max)
      adv_max = ao;
    adv_count++;

    if (_squad_alive_counts[s] <= 1)
      continue;
    Vector3 c = _squad_centroids[s];
    float sq_sum = 0.0f;
    int sq_n = 0;
    for (int i = 0; i < _count; i++) {
      if (!_alive[i] || _squad_id[i] != s)
        continue;
      float ddx = _pos_x[i] - c.x;
      float ddz = _pos_z[i] - c.z;
      sq_sum += std::sqrt(ddx * ddx + ddz * ddz);
      sq_n++;
    }
    if (sq_n > 0) {
      spread_sum += sq_sum / sq_n;
      spread_count++;
    }
  }
  _avg_squad_spread = (spread_count > 0) ? spread_sum / spread_count : 0.0f;
  _avg_advance_offset = (adv_count > 0) ? adv_sum / adv_count : 0.0f;
  _max_advance_offset = adv_max;

  // Inter-team distance: average nearest-enemy distance per alive unit
  // Uses team center of mass for O(1) rather than O(N²) per-unit scan
  {
    float t1_cx = 0, t1_cz = 0, t2_cx = 0, t2_cz = 0;
    for (int i = 0; i < _count; i++) {
      if (!_alive[i])
        continue;
      if (_team[i] == 1) {
        t1_cx += _pos_x[i];
        t1_cz += _pos_z[i];
      } else if (_team[i] == 2) {
        t2_cx += _pos_x[i];
        t2_cz += _pos_z[i];
      }
    }
    if (count_t1 > 0) {
      t1_cx /= count_t1;
      t1_cz /= count_t1;
    }
    if (count_t2 > 0) {
      t2_cx /= count_t2;
      t2_cz /= count_t2;
    }
    float itdx = t1_cx - t2_cx, itdz = t1_cz - t2_cz;
    _avg_inter_team_dist = std::sqrt(itdx * itdx + itdz * itdz);
  }

  // Normalize movement vector accumulators by alive count
  int alive_total = count_t1 + count_t2;
  if (alive_total > 0) {
    _avg_formation_pull /= alive_total;
    _avg_flow_push /= alive_total;
    _avg_threat_push /= alive_total;
    _avg_total_speed /= alive_total;
  }

  // Compute per-state averages
  for (int s = 0; s < ST_COUNT; s++) {
    if (_count_by_state[s] > 0)
      _dist_by_state[s] /= _count_by_state[s];
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Main Tick
// ═══════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════
//  Orders
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::set_order(int32_t unit_id, int order_type,
                                 const Vector3 &target_pos, int32_t target_id) {
  if (!_valid(unit_id))
    return;

  _order[unit_id] = (OrderType)std::clamp(order_type, 0, (int)ORDER_RETREAT);
  _order_x[unit_id] = target_pos.x;
  _order_y[unit_id] = target_pos.y;
  _order_z[unit_id] = target_pos.z;
  _order_target_id[unit_id] = target_id;
}

void SimulationServer::set_squad_rally(int squad_id, const Vector3 &rally,
                                       const Vector3 &advance_dir) {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return;
  _squads[squad_id].rally_point = rally;
  _squads[squad_id].advance_dir = advance_dir;
  _squads[squad_id].active = true;
}

void SimulationServer::advance_squad(int squad_id, float offset_delta) {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return;
  _squads[squad_id].advance_offset = std::min(
      _squads[squad_id].advance_offset + offset_delta,
      10.0f); // cap at 10m (was 20 — too large, pushes slots 20m+ ahead)
}

void SimulationServer::set_squad_advance_offset(int squad_id, float offset) {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return;
  _squads[squad_id].advance_offset =
      std::min(offset, 10.0f); // cap at 10m (was 20)
}

float SimulationServer::get_squad_advance_offset(int squad_id) const {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return 0.0f;
  return _squads[squad_id].advance_offset;
}

Vector3 SimulationServer::get_squad_centroid(int squad_id) const {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return Vector3();
  return _squad_centroids[squad_id];
}

int SimulationServer::get_squad_alive_count(int squad_id) const {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return 0;
  return _squad_alive_counts[squad_id];
}

bool SimulationServer::is_squad_in_contact(int squad_id, float radius) const {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return false;
  if (_squad_alive_counts[squad_id] == 0)
    return false;

  Vector3 centroid = _squad_centroids[squad_id];
  float r2 = radius * radius;

  // Determine team from first alive member
  uint8_t my_team = 0;
  for (int i = 0; i < _count; i++) {
    if (_alive[i] && _squad_id[i] == (uint16_t)squad_id) {
      my_team = _team[i];
      break;
    }
  }
  if (my_team == 0)
    return false;

  // Spatial hash query — early-return on first enemy hit
  int min_gx = std::clamp(
      (int)((centroid.x - radius + _map_half_w) / (float)SPATIAL_CELL_M), 0,
      _spatial_w - 1);
  int max_gx = std::clamp(
      (int)((centroid.x + radius + _map_half_w) / (float)SPATIAL_CELL_M), 0,
      _spatial_w - 1);
  int min_gz = std::clamp(
      (int)((centroid.z - radius + _map_half_h) / (float)SPATIAL_CELL_M), 0,
      _spatial_h - 1);
  int max_gz = std::clamp(
      (int)((centroid.z + radius + _map_half_h) / (float)SPATIAL_CELL_M), 0,
      _spatial_h - 1);

  for (int gz = min_gz; gz <= max_gz; gz++) {
    for (int gx = min_gx; gx <= max_gx; gx++) {
      int32_t idx = _spatial_cells[gz * _spatial_w + gx];
      while (idx >= 0) {
        if (_alive[idx] && _team[idx] != my_team) {
          float dx = _pos_x[idx] - centroid.x;
          float dz = _pos_z[idx] - centroid.z;
          if (dx * dx + dz * dz <= r2)
            return true;
        }
        idx = _spatial_next[idx];
      }
    }
  }
  return false;
}

void SimulationServer::set_squad_formation(int squad_id, int formation_type) {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return;
  int clamped = formation_type < 0
                    ? 0
                    : (formation_type >= FORM_COUNT ? 0 : formation_type);
  _squads[squad_id].formation = (FormationType)clamped;
}

int SimulationServer::get_squad_formation(int squad_id) const {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return 0;
  return (int)_squads[squad_id].formation;
}

void SimulationServer::set_squad_formation_spread(int squad_id, float spread) {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return;
  _squads[squad_id].formation_spread =
      spread < 2.0f ? 2.0f : (spread > 30.0f ? 30.0f : spread);
}

float SimulationServer::get_squad_formation_spread(int squad_id) const {
  if (squad_id < 0 || squad_id >= MAX_SQUADS)
    return 8.0f;
  return _squads[squad_id].formation_spread;
}

// ═══════════════════════════════════════════════════════════════════════
//  Personality API
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::set_unit_personality(int32_t unit_id, int personality) {
  if (!_valid(unit_id))
    return;
  _personality[unit_id] =
      (uint8_t)std::clamp(personality, 0, (int)PERS_COUNT - 1);
}

int SimulationServer::get_unit_personality(int32_t unit_id) const {
  if (!_valid(unit_id))
    return PERS_STEADY;
  return (int)_personality[unit_id];
}

// ═══════════════════════════════════════════════════════════════════════
//  Squad Flow Field — Centroid + Goal Projection
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_compute_squad_centroids() {
  float sx[MAX_SQUADS] = {}, sz[MAX_SQUADS] = {};
  int member_counter[MAX_SQUADS] = {};
  std::memset(_squad_alive_counts, 0, sizeof(_squad_alive_counts));
  std::memset(_squad_has_flanker, 0, sizeof(_squad_has_flanker));

  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    int sq = _squad_id[i];
    if (sq < 0 || sq >= MAX_SQUADS)
      continue;
    sx[sq] += _pos_x[i];
    sz[sq] += _pos_z[i];
    _squad_alive_counts[sq]++;
    if (_state[i] == ST_FLANKING)
      _squad_has_flanker[sq] = true;
  }

  // Position-aware slot reassignment: every 2s, re-sort squad members by
  // perpendicular position so leftmost unit gets idx=0 (left slot) and
  // rightmost gets idx=N-1 (right slot). Prevents units needing to cross
  // through formation.
  if (_game_time - _last_slot_reassign >= SLOT_REASSIGN_INTERVAL) {
    _last_slot_reassign = _game_time;
    for (int s = 0; s < MAX_SQUADS; s++) {
      if (!_squads[s].active || _squad_alive_counts[s] <= 1)
        continue;
      Vector3 dir = _squads[s].advance_dir;
      float dir_len = dir.length();
      if (dir_len < 0.01f)
        continue;
      dir /= dir_len;
      float perp_x = -dir.z, perp_z = dir.x;

      // Collect alive members with their perpendicular projection
      struct MemberProj {
        int unit_id;
        float proj;
      };
      MemberProj members[64]; // MAX_UNITS_PER_SQUAD is typically ≤32
      int n = 0;
      for (int i = 0; i < _count && n < 64; i++) {
        if (!_alive[i] || _squad_id[i] != s)
          continue;
        members[n++] = {i, perp_x * _pos_x[i] + perp_z * _pos_z[i]};
      }
      // Simple insertion sort (squads are small, typically 8-16 units)
      for (int a = 1; a < n; a++) {
        MemberProj key = members[a];
        int b = a - 1;
        while (b >= 0 && members[b].proj > key.proj) {
          members[b + 1] = members[b];
          b--;
        }
        members[b + 1] = key;
      }
      // Assign sorted indices
      for (int a = 0; a < n; a++) {
        _squad_member_idx[members[a].unit_id] = (int16_t)a;
      }
    }
  }

  for (int s = 0; s < MAX_SQUADS; s++) {
    if (_squad_alive_counts[s] > 0) {
      float inv = 1.0f / _squad_alive_counts[s];
      Vector3 raw_centroid(sx[s] * inv, 0.0f, sz[s] * inv);

      // Anchor centroid toward rally point to prevent drift feedback
      // loop. Note: do NOT add GOAL_LEAD_DIST here — slot computation
      // already adds it. Adding it in both places double-counted the lead
      // distance.
      if (_squads[s].active) {
        Vector3 anchor = _squads[s].rally_point;
        float b = _tune_centroid_anchor;
        _squad_centroids[s] = anchor * b + raw_centroid * (1.0f - b);
      } else {
        _squad_centroids[s] = raw_centroid;
      }
    } else {
      _squad_centroids[s] = Vector3();
    }
  }
}

Dictionary SimulationServer::get_squad_goals(int team) const {
  PackedVector3Array positions;
  PackedFloat32Array strengths;

  // Collect active squads with alive units, optionally filtered by team.
  // GPU flow map is from Team 1's perspective — passing team=1 prevents
  // Team 2 goals from creating attractors that invert incorrectly.
  struct SquadEntry {
    int id;
    int alive;
  };
  SquadEntry entries[MAX_SQUADS];
  int num_active = 0;

  for (int s = 0; s < MAX_SQUADS; s++) {
    if (!_squads[s].active || _squad_alive_counts[s] == 0)
      continue;
    if (team > 0 && _squads[s].team != (uint8_t)team)
      continue;
    entries[num_active++] = {s, _squad_alive_counts[s]};
  }

  // Sort descending by alive count (simple insertion sort — N<=128)
  for (int i = 1; i < num_active; i++) {
    auto key = entries[i];
    int j = i - 1;
    while (j >= 0 && entries[j].alive < key.alive) {
      entries[j + 1] = entries[j];
      j--;
    }
    entries[j + 1] = key;
  }

  // 4 goals per squad, 64 max total => 16 squads
  int max_squads = std::min(num_active, 64 / MAX_GOALS_PER_SQUAD);

  for (int i = 0; i < max_squads; i++) {
    int s = entries[i].id;
    Vector3 centroid = _squad_centroids[s];
    Vector3 dir = _squads[s].advance_dir;
    float dir_len = dir.length();
    if (dir_len < 0.01f)
      continue;
    dir /= dir_len;

    // Use FLOW_GOAL_LEAD (30m) instead of GOAL_LEAD_DIST (2m) for GPU
    // flow field. This projects goals further forward, extending the
    // pressure gradient across the map so units get meaningful flow
    // guidance during the entire approach phase.
    float lead = FLOW_GOAL_LEAD + _squads[s].advance_offset;
    float spread = _squads[s].formation_spread;
    float str = 1.0f + (float)entries[i].alive * 0.1f;

    // Perpendicular vector on XZ plane
    Vector3 perp(-dir.z, 0.0f, dir.x);

    switch (_squads[s].formation) {
    case FORM_LINE: {
      // 4 goals spread perpendicular to advance_dir at lead distance
      float offsets[4] = {-1.5f, -0.5f, 0.5f, 1.5f};
      for (int g = 0; g < 4; g++) {
        positions.push_back(centroid + dir * lead +
                            perp * (offsets[g] * spread));
        strengths.push_back(str);
      }
    } break;

    case FORM_WEDGE: {
      // V-shape: front tip, two mid-flanks, rear center
      positions.push_back(centroid + dir * (lead + spread));
      strengths.push_back(str * 1.2f); // stronger pull at point
      positions.push_back(centroid + dir * lead + perp * (-spread * 0.6f));
      strengths.push_back(str);
      positions.push_back(centroid + dir * lead + perp * (spread * 0.6f));
      strengths.push_back(str);
      positions.push_back(centroid + dir * (lead - spread * 0.3f));
      strengths.push_back(str * 0.8f); // softer pull at rear
    } break;

    case FORM_COLUMN: {
      // 4 goals stacked along advance_dir (narrow, tight)
      for (int g = 0; g < 4; g++) {
        float dist = lead + g * (spread * 0.5f);
        positions.push_back(centroid + dir * dist);
        strengths.push_back(str);
      }
    } break;

    case FORM_CIRCLE: {
      // 4 goals in a ring around centroid (defensive, ignores
      // advance_offset)
      for (int g = 0; g < 4; g++) {
        float angle = g * (3.14159265f * 0.5f);
        Vector3 offset(std::cos(angle) * spread, 0.0f,
                       std::sin(angle) * spread);
        positions.push_back(centroid + offset);
        strengths.push_back(str);
      }
    } break;

    default: {
      // Fallback: linear goals along advance_dir
      for (int g = 0; g < MAX_GOALS_PER_SQUAD; g++) {
        float dist = lead + g * GOAL_SPACING;
        positions.push_back(centroid + dir * dist);
        strengths.push_back(str);
      }
    } break;
    }
  }

  Dictionary result;
  result["positions"] = positions;
  result["strengths"] = strengths;
  return result;
}

// ═══════════════════════════════════════════════════════════════════════
//  Queries
// ═══════════════════════════════════════════════════════════════════════

int SimulationServer::get_alive_count_for_team(int team) const {
  int count = 0;
  for (int i = 0; i < _count; i++) {
    if (_alive[i] && _team[i] == (uint8_t)team)
      count++;
  }
  return count;
}

Vector3 SimulationServer::get_position(int32_t unit_id) const {
  if (!_valid(unit_id))
    return Vector3();
  return Vector3(_pos_x[unit_id], _pos_y[unit_id], _pos_z[unit_id]);
}

int SimulationServer::get_state(int32_t unit_id) const {
  if (!_valid(unit_id))
    return ST_DEAD;
  return (int)_state[unit_id];
}

float SimulationServer::get_health(int32_t unit_id) const {
  if (!_valid(unit_id))
    return 0.0f;
  return _health[unit_id];
}

float SimulationServer::get_morale(int32_t unit_id) const {
  if (!_valid(unit_id))
    return 0.0f;
  return _morale[unit_id];
}

float SimulationServer::get_suppression(int32_t unit_id) const {
  if (!_valid(unit_id))
    return 0.0f;
  return _suppression[unit_id];
}

int SimulationServer::get_team(int32_t unit_id) const {
  if (!_valid(unit_id))
    return -1;
  return (int)_team[unit_id];
}

int SimulationServer::get_target(int32_t unit_id) const {
  if (!_valid(unit_id))
    return -1;
  return _target_id[unit_id];
}

bool SimulationServer::is_alive(int32_t unit_id) const {
  if (!_valid(unit_id))
    return false;
  return _alive[unit_id];
}

int SimulationServer::get_role(int32_t unit_id) const {
  if (!_valid(unit_id))
    return ROLE_RIFLEMAN;
  return (int)_role[unit_id];
}

int SimulationServer::get_role_count_for_team(int team, int role) const {
  int count = 0;
  for (int i = 0; i < _count; i++) {
    if (_alive[i] && _team[i] == (uint8_t)team && _role[i] == (uint8_t)role)
      count++;
  }
  return count;
}

int SimulationServer::get_squad_id(int32_t unit_id) const {
  if (!_valid(unit_id))
    return -1;
  return (int)_squad_id[unit_id];
}

int SimulationServer::get_ammo(int32_t unit_id) const {
  if (!_valid(unit_id))
    return 0;
  return (int)_ammo[unit_id];
}

int SimulationServer::get_mag_size(int32_t unit_id) const {
  if (!_valid(unit_id))
    return 1;
  return (int)_mag_size[unit_id];
}

// ═══════════════════════════════════════════════════════════════════════
//  Render Output
// ═══════════════════════════════════════════════════════════════════════

PackedVector3Array SimulationServer::get_alive_positions() const {
  PackedVector3Array out;
  out.resize(_alive_count);
  int j = 0;
  for (int i = 0; i < _count && j < _alive_count; i++) {
    if (_alive[i]) {
      out.set(j++, Vector3(_pos_x[i], _pos_y[i], _pos_z[i]));
    }
  }
  return out;
}

PackedVector3Array SimulationServer::get_alive_facings() const {
  PackedVector3Array out;
  out.resize(_alive_count);
  int j = 0;
  for (int i = 0; i < _count && j < _alive_count; i++) {
    if (_alive[i]) {
      out.set(j++, Vector3(_face_x[i], 0.0f, _face_z[i]));
    }
  }
  return out;
}

PackedInt32Array SimulationServer::get_alive_teams() const {
  PackedInt32Array out;
  out.resize(_alive_count);
  int j = 0;
  for (int i = 0; i < _count && j < _alive_count; i++) {
    if (_alive[i]) {
      out.set(j++, (int32_t)_team[i]);
    }
  }
  return out;
}

Dictionary SimulationServer::get_render_data() const {
  Dictionary d;
  d["positions"] = get_alive_positions();
  d["facings"] = get_alive_facings();
  d["teams"] = get_alive_teams();

  // Also include states for color-coding
  PackedInt32Array states;
  states.resize(_alive_count);
  int j = 0;
  for (int i = 0; i < _count && j < _alive_count; i++) {
    if (_alive[i]) {
      states.set(j++, (int32_t)_state[i]);
    }
  }
  d["states"] = states;
  d["alive_count"] = _alive_count;
  return d;
}

Dictionary SimulationServer::get_render_data_for_team(int team) const {
  // Count alive for this team
  int tc = get_alive_count_for_team(team);

  PackedVector3Array positions;
  PackedVector3Array facings;
  PackedInt32Array states;
  PackedFloat32Array anim_phases;
  PackedInt32Array squad_ids;
  PackedByteArray postures;
  PackedByteArray roles;
  positions.resize(tc);
  facings.resize(tc);
  states.resize(tc);
  anim_phases.resize(tc);
  squad_ids.resize(tc);
  postures.resize(tc);
  roles.resize(tc);

  int j = 0;
  for (int i = 0; i < _count && j < tc; i++) {
    if (_alive[i] && _team[i] == (uint8_t)team) {
      positions.set(j, Vector3(_pos_x[i], _pos_y[i], _pos_z[i]));
      facings.set(j, Vector3(_face_x[i], 0.0f, _face_z[i]));
      states.set(j, (int32_t)_state[i]);
      anim_phases.set(j, _anim_phase[i]);
      squad_ids.set(j, (int32_t)_squad_id[i]);
      postures.ptrw()[j] = _posture[i];
      roles.ptrw()[j] = _role[i];
      j++;
    }
  }

  // Visibility data: for each unit, whether the opposing team can see
  // them Used by renderer to show ghost silhouettes for recently-seen
  // enemies
  int viewer_team = (team == 1) ? 2 : 1;
  int vis_idx = viewer_team - 1;
  PackedByteArray vis;
  PackedFloat32Array vis_times;
  vis.resize(tc);
  vis_times.resize(tc);
  j = 0;
  for (int i = 0; i < _count && j < tc; i++) {
    if (_alive[i] && _team[i] == (uint8_t)team) {
      vis.ptrw()[j] = _team_can_see(vis_idx, i) ? 1 : 0;
      vis_times.set(j, _last_seen_time[i]);
      j++;
    }
  }

  Dictionary d;
  d["positions"] = positions;
  d["facings"] = facings;
  d["states"] = states;
  d["anim_phases"] = anim_phases;
  d["squad_ids"] = squad_ids;
  d["postures"] = postures;
  d["roles"] = roles;
  d["visible"] = vis;
  d["last_seen_times"] = vis_times;
  d["alive_count"] = tc;
  return d;
}

Dictionary SimulationServer::get_dead_render_data() const {
  // Count dead (not alive but spawned)
  int dc = 0;
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      dc++;
  }

  PackedVector3Array positions;
  PackedVector3Array facings;
  PackedByteArray teams;
  positions.resize(dc);
  facings.resize(dc);
  teams.resize(dc);

  int j = 0;
  for (int i = 0; i < _count && j < dc; i++) {
    if (!_alive[i]) {
      positions.set(j, Vector3(_pos_x[i], _pos_y[i], _pos_z[i]));
      facings.set(j, Vector3(_face_x[i], 0.0f, _face_z[i]));
      teams.ptrw()[j] = _team[i];
      j++;
    }
  }

  Dictionary d;
  d["positions"] = positions;
  d["facings"] = facings;
  d["teams"] = teams;
  d["count"] = dc;
  return d;
}

// ═══════════════════════════════════════════════════════════════════════
//  Debug
// ═══════════════════════════════════════════════════════════════════════

Dictionary SimulationServer::get_projectile_render_data() const {
  PackedVector3Array positions;
  PackedVector3Array velocities;
  PackedByteArray teams;
  PackedByteArray types;
  PackedByteArray payloads;
  positions.resize(_proj_active_count);
  velocities.resize(_proj_active_count);
  teams.resize(_proj_active_count);
  types.resize(_proj_active_count);
  payloads.resize(_proj_active_count);

  int j = 0;
  auto q = ecs.filter<const ecs::ProjectileData, const ecs::ProjectileFlight>();
  q.each([&](const ecs::ProjectileData &data,
             const ecs::ProjectileFlight &flight) {
    if (j >= _proj_active_count)
      return;
    positions.set(j, Vector3(flight.x, flight.y, flight.z));
    velocities.set(j, Vector3(flight.vx, flight.vy, flight.vz));
    teams.push_back(data.team);
    types.push_back(data.type);
    payloads.push_back(data.payload);
    j++;
  });

  Dictionary d;
  d["positions"] = positions;
  d["velocities"] = velocities;
  d["teams"] = teams;
  d["types"] = types;
  d["payloads"] = payloads;
  d["count"] = _proj_active_count;
  return d;
}

Array SimulationServer::get_impact_events() {
  Array out;
  for (int i = 0; i < _impact_count; i++) {
    auto &evt = _impact_events[i];
    Dictionary d;
    d["position"] = evt.position;
    d["normal"] = evt.normal;
    d["material"] = (int)evt.material;
    d["type"] = (int)evt.type;
    d["payload"] = (int)evt.payload;

    // Explosion-specific data (type==1 kinetic, type==2 gas)
    if (evt.type == 1 || evt.type == 2) {
      d["blast_radius"] = evt.blast_radius;
      d["destroyed"] = evt.destroyed;

      // Material histogram
      PackedInt32Array hist;
      hist.resize(16);
      for (int m = 0; m < 16; m++) {
        hist[m] = evt.mat_histogram[m];
      }
      d["material_histogram"] = hist;

      // Debris samples
      Array debris;
      for (int j = 0; j < evt.debris_count; j++) {
        Dictionary dd;
        dd["position"] = evt.debris_positions[j];
        dd["material"] = (int)evt.debris_materials[j];
        debris.push_back(dd);
      }
      d["debris"] = debris;
    }

    out.push_back(d);
  }
  return out;
}

Array SimulationServer::get_muzzle_flash_events() {
  Array out;
  for (int i = 0; i < _muzzle_event_count; i++) {
    auto &evt = _muzzle_events[i];
    Dictionary d;
    d["position"] = Vector3(evt.pos_x, evt.pos_y, evt.pos_z);
    d["facing"] = Vector3(evt.face_x, 0.0f, evt.face_z);
    d["team"] = (int)evt.team;
    d["role"] = (int)evt.role;
    out.push_back(d);
  }
  return out;
}

Dictionary SimulationServer::get_debug_stats() const {
  Dictionary d;
  d["total_units"] = _count;
  d["alive_units"] = _alive_count;
  d["tick_ms"] = _last_tick_ms;
  d["los_checks"] = _los_checks;
  d["spatial_queries"] = _spatial_queries;
  d["active_projectiles"] = _proj_active_count;

  // Personality break state counts
  int berserk_count = 0, frozen_count = 0, paranoid_ff = 0;
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    if (_state[i] == ST_BERSERK)
      berserk_count++;
    if (_state[i] == ST_FROZEN)
      frozen_count++;
    if (_state[i] == ST_ENGAGING && _personality[i] == PERS_PARANOID &&
        _morale[i] < _personality_morale(PERS_PARANOID).break_threshold)
      paranoid_ff++;
  }
  d["berserk_units"] = berserk_count;
  d["frozen_units"] = frozen_count;
  d["paranoid_ff_units"] = paranoid_ff;
  d["wall_pen_voxels"] = _wall_pen_count;

  // Cover & peek stats
  int in_cover_count = 0, peeking_count = 0;
  int stand_count = 0, crouch_count = 0, prone_count = 0;
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    if (_state[i] == ST_IN_COVER) {
      in_cover_count++;
      if (_is_peeking[i])
        peeking_count++;
    }
    switch (_posture[i]) {
    case POST_STAND:
      stand_count++;
      break;
    case POST_CROUCH:
      crouch_count++;
      break;
    case POST_PRONE:
      prone_count++;
      break;
    }
  }
  d["in_cover_units"] = in_cover_count;
  d["peeking_units"] = peeking_count;
  d["posture_stand"] = stand_count;
  d["posture_crouch"] = crouch_count;
  d["posture_prone"] = prone_count;

  // Visibility / fog of war stats
  int vis_team1 = 0, vis_team2 = 0;
  for (int w = 0; w < VIS_WORDS; w++) {
#ifdef _MSC_VER
    vis_team1 += (int)__popcnt64(_team_vis[0][w]);
    vis_team2 += (int)__popcnt64(_team_vis[1][w]);
#else
    vis_team1 += __builtin_popcountll(_team_vis[0][w]);
    vis_team2 += __builtin_popcountll(_team_vis[1][w]);
#endif
  }
  d["vis_team1"] = vis_team1;
  d["vis_team2"] = vis_team2;

  // Fog-of-war diagnostic counters (per-tick)
  d["fow_targets_skipped"] = _fow_targets_skipped;
  d["fow_suppressive_shots"] = _fow_suppressive_shots;
  d["fow_vis_checks"] = _fow_vis_checks;
  d["fow_vis_hits"] = _fow_vis_hits;
  d["fow_contacts_gained"] = _fow_contacts_gained;
  d["fow_contacts_lost"] = _fow_contacts_lost;
  d["fow_influence_filtered"] = _fow_influence_filtered;

  // Cumulative fog-of-war stats
  d["fow_total_suppressive"] = _fow_total_suppressive;
  d["fow_total_skipped"] = _fow_total_skipped;
  d["fow_total_vis_checks"] = _fow_total_vis_checks;
  d["fow_total_vis_hits"] = _fow_total_vis_hits;

  // Engagement quality (per-tick)
  d["engagements_visible"] = _engagements_visible;
  d["engagements_suppressive"] = _engagements_suppressive;
  d["wall_pen_blocked"] = _wall_pen_blocked;
  d["mortar_rounds_fired"] = _mortar_rounds_fired_tick;
  d["mortar_impacts"] = _mortar_impacts_tick;
  d["mortar_suppression_events"] = _mortar_suppression_events_tick;
  d["mortar_kills"] = _mortar_kills_tick;
  d["mortar_total_rounds_fired"] = _mortar_total_rounds_fired;
  d["mortar_total_impacts"] = _mortar_total_impacts;
  d["mortar_total_suppression_events"] = _mortar_total_suppression_events;
  d["mortar_total_kills"] = _mortar_total_kills;

  // Per-team alive counts
  int t1_alive = 0, t2_alive = 0;
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    if (_team[i] == 1)
      t1_alive++;
    else if (_team[i] == 2)
      t2_alive++;
  }
  d["team1_alive"] = t1_alive;
  d["team2_alive"] = t2_alive;

  // State distribution
  int state_counts[ST_COUNT] = {};
  for (int i = 0; i < _count; i++) {
    if (_alive[i] && _state[i] < ST_COUNT)
      state_counts[_state[i]]++;
  }
  d["st_idle"] = state_counts[ST_IDLE];
  d["st_moving"] = state_counts[ST_MOVING];
  d["st_engaging"] = state_counts[ST_ENGAGING];
  d["st_in_cover"] = state_counts[ST_IN_COVER];
  d["st_suppressing"] = state_counts[ST_SUPPRESSING];
  d["st_flanking"] = state_counts[ST_FLANKING];
  d["st_retreating"] = state_counts[ST_RETREATING];
  d["st_reloading"] = state_counts[ST_RELOADING];
  d["st_climbing"] = state_counts[ST_CLIMBING];
  d["st_falling"] = state_counts[ST_FALLING];

  // Location quality metrics
  d["avg_dist_slot_t1"] = _avg_dist_to_slot_t1;
  d["avg_dist_slot_t2"] = _avg_dist_to_slot_t2;
  d["max_dist_slot_t1"] = _max_dist_to_slot_t1;
  d["max_dist_slot_t2"] = _max_dist_to_slot_t2;
  d["avg_squad_spread"] = _avg_squad_spread;
  d["units_beyond_20m"] = _units_beyond_20m;
  d["inter_team_dist"] = _avg_inter_team_dist;

  // Per-state distance to slot (top combat states)
  d["dist_st_idle"] = _dist_by_state[ST_IDLE];
  d["dist_st_moving"] = _dist_by_state[ST_MOVING];
  d["dist_st_engaging"] = _dist_by_state[ST_ENGAGING];
  d["dist_st_in_cover"] = _dist_by_state[ST_IN_COVER];
  d["dist_st_suppressing"] = _dist_by_state[ST_SUPPRESSING];
  d["dist_st_flanking"] = _dist_by_state[ST_FLANKING];
  d["dist_st_retreating"] = _dist_by_state[ST_RETREATING];
  d["dist_st_climbing"] = _dist_by_state[ST_CLIMBING];

  // Order distribution
  d["order_follow_squad"] = _order_follow_squad;
  d["order_other"] = _order_other;

  // Movement vector decomposition
  d["avg_formation_pull"] = _avg_formation_pull;
  d["avg_flow_push"] = _avg_flow_push;
  d["avg_threat_push"] = _avg_threat_push;
  d["avg_total_speed"] = _avg_total_speed;

  // Advance offset tracking
  d["avg_advance_offset"] = _avg_advance_offset;
  d["max_advance_offset"] = _max_advance_offset;

  // Climb/fall event counters
  d["climb_events"] = _climb_started_tick;
  d["fall_events"] = _fall_started_tick;
  d["fall_damage_events"] = _fall_damage_tick;
  d["total_climb_events"] = _total_climb_events;
  d["total_fall_events"] = _total_fall_events;
  d["total_fall_damage_events"] = _total_fall_damage_events;

  // Per-subsystem profiling
  PackedFloat64Array sub_us, sub_ema;
  sub_us.resize(SUB_COUNT);
  sub_ema.resize(SUB_COUNT);
  for (int s = 0; s < SUB_COUNT; s++) {
    sub_us[s] = _sub_us[s];
    sub_ema[s] = _sub_ema[s];
  }
  d["sub_us"] = sub_us;
  d["sub_ema"] = sub_ema;

  return d;
}

// ═══════════════════════════════════════════════════════════════════════
//  Combat Pheromone System
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_pheromone_deposit_danger(int32_t killed_unit,
                                                 int32_t killer,
                                                 bool was_ambush) {
  int team_idx = _team[killed_unit] - 1;
  if (team_idx < 0 || team_idx > 1)
    return;
  if (!_pheromones[team_idx].is_valid())
    return;

  Vector3 pos(_pos_x[killed_unit], 0.0f, _pos_z[killed_unit]);

  // Base danger + fear deposit
  float danger_str = 8.0f;
  float fear_str = 5.0f;
  float radius = 8.0f;

  // Ambush kills are more terrifying (unit wasn't in combat state)
  if (was_ambush) {
    danger_str *= 1.5f;
    fear_str *= 2.0f;
    radius = 12.0f;
  }

  _pheromones[team_idx]->deposit_radius(pos, CH_DANGER, danger_str, radius);
  _pheromones[team_idx]->deposit_radius(pos, CH_FEAR, fear_str, radius);

  // CONTACT on killer's team for awareness (we made a kill here)
  if (killer >= 0 && killer < _count && _alive[killer]) {
    int killer_team = _team[killer] - 1;
    if (killer_team >= 0 && killer_team <= 1 &&
        _pheromones[killer_team].is_valid()) {
      _pheromones[killer_team]->deposit(pos, CH_CONTACT, 3.0f);
    }
  }
}

void SimulationServer::_pheromone_deposit_explosion(const Vector3 &pos,
                                                    float blast_radius,
                                                    uint8_t team_that_fired) {
  float danger_str = 10.0f * (blast_radius / 4.0f);
  float radius = blast_radius * 2.5f;

  // Both teams get DANGER deposits (explosions are universally scary)
  for (int t = 0; t < 2; t++) {
    if (_pheromones[t].is_valid()) {
      _pheromones[t]->deposit_radius(pos, CH_DANGER, danger_str, radius);
    }
  }

  // Opposing team gets FEAR (they're being shelled)
  if (team_that_fired >= 1 && team_that_fired <= 2) {
    int enemy_idx = 2 - team_that_fired; // 1→1 (team 2), 2→0 (team 1)
    if (_pheromones[enemy_idx].is_valid()) {
      _pheromones[enemy_idx]->deposit_radius(pos, CH_FEAR, danger_str * 0.6f,
                                             radius);
    }
  }
}

SimulationServer::RolePheromoneWeights
SimulationServer::_role_pheromone_weights(uint8_t role) {
  //                         danger  supp  contact rally  fear    courage
  //                         safe flank  strategic
  switch (role) {
  case ROLE_LEADER:
    return {0.8f, 0.6f, 1.2f, 1.5f, -0.5f, 1.5f, 0.8f, 1.0f, 1.2f};
  case ROLE_MEDIC:
    return {1.0f, 0.5f, 0.8f, 1.2f, -0.3f, 1.0f, 1.5f, 0.3f, 1.0f};
  case ROLE_MG:
    return {0.6f, 1.2f, 1.0f, 0.8f, 1.0f, 0.8f, 0.5f, 0.8f, 0.8f};
  case ROLE_MARKSMAN:
    return {0.5f, 0.8f, 1.0f, 0.6f, 0.8f, 0.9f, 0.5f, 1.2f, 0.8f};
  case ROLE_GRENADIER:
    return {0.7f, 0.7f, 1.0f, 0.9f, 1.0f, 1.0f, 0.7f, 1.0f, 1.0f};
  case ROLE_MORTAR:
    return {0.4f, 0.5f, 0.8f, 0.7f, 0.6f, 0.8f, 0.4f, 0.6f, 0.6f};
  default:
    return {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  }
}

void SimulationServer::_tick_pheromones(float delta) {
  // Accumulate timer for CA update
  _pheromone_tick_timer += delta;
  bool do_ca = (_pheromone_tick_timer >= PHEROMONE_TICK_INTERVAL);
  if (do_ca)
    _pheromone_tick_timer -= PHEROMONE_TICK_INTERVAL;

  // Per-unit deposits
  for (int i = 0; i < _count; i++) {
    if (!_alive[i])
      continue;
    int team_idx = _team[i] - 1;
    if (team_idx < 0 || team_idx > 1)
      continue;
    int enemy_team = 1 - team_idx;

    Vector3 pos(_pos_x[i], 0.0f, _pos_z[i]);

    // ── MG sustained fire → SUPPRESSION cone (on enemy's map) ──────
    if (_role[i] == ROLE_MG &&
        (_state[i] == ST_ENGAGING || _state[i] == ST_SUPPRESSING)) {
      _sustained_fire_timer[i] += delta;
      if (_sustained_fire_timer[i] > 1.0f &&
          _pheromones[enemy_team].is_valid()) {
        Vector3 dir(_face_x[i], 0.0f, _face_z[i]);
        _pheromones[enemy_team]->deposit_cone(
            pos, dir, CH_SUPPRESSION, 3.0f * delta, 0.35f,
            30.0f); // 20° half-angle, 30m range
      }
    } else {
      _sustained_fire_timer[i] = 0.0f;
    }

    // ── Any unit firing → CONTACT on enemy's map ───────────────────
    if ((_state[i] == ST_ENGAGING || _state[i] == ST_SUPPRESSING) &&
        _pheromones[enemy_team].is_valid()) {
      _pheromones[enemy_team]->deposit(pos, CH_CONTACT, 2.0f * delta);
    }

    // ── Low morale → FEAR emission (own team) ─────────────────────
    if (_morale[i] < 0.3f && _pheromones[team_idx].is_valid()) {
      float fear_str = (0.3f - _morale[i]) * 5.0f * delta;
      _pheromones[team_idx]->deposit(pos, CH_FEAR, fear_str);
    }

    // ── Broken state → extra FEAR (own team, panic is contagious) ──
    if ((_state[i] == ST_RETREATING || _state[i] == ST_FROZEN) &&
        _pheromones[team_idx].is_valid()) {
      _pheromones[team_idx]->deposit(pos, CH_FEAR, 3.0f * delta);
    }

    // ── Leader → COURAGE + RALLY aura (own team) ──────────────────
    if (_role[i] == ROLE_LEADER && _pheromones[team_idx].is_valid()) {
      _pheromones[team_idx]->deposit_radius(pos, CH_COURAGE, 4.0f * delta,
                                            12.0f);
      _pheromones[team_idx]->deposit_radius(pos, CH_RALLY, 3.0f * delta, 15.0f);
    }

    // ── Survived heavy suppression → COURAGE (own team, bravery) ──
    if (_suppression[i] > 0.5f) {
      _survived_supp_timer[i] += delta;
      if (_survived_supp_timer[i] > 3.0f && _pheromones[team_idx].is_valid()) {
        _pheromones[team_idx]->deposit(pos, CH_COURAGE, 1.5f * delta);
      }
    } else {
      _survived_supp_timer[i] = 0.0f;
    }

    // ── Safe movement → SAFE_ROUTE trail (own team) ────────────────
    if ((_state[i] == ST_MOVING || _state[i] == ST_FLANKING) &&
        _suppression[i] < 0.2f && _health[i] > 0.5f) {
      float dx = _pos_x[i] - _prev_pos_x[i];
      float dz = _pos_z[i] - _prev_pos_z[i];
      float moved = std::sqrt(dx * dx + dz * dz);
      if (moved > 0.5f && _pheromones[team_idx].is_valid()) {
        Vector3 from(_prev_pos_x[i], 0.0f, _prev_pos_z[i]);
        _pheromones[team_idx]->deposit_trail(from, pos, CH_SAFE_ROUTE, 1.0f);
      }
    }

    // Update previous position for next tick's trail calculation
    _prev_pos_x[i] = _pos_x[i];
    _prev_pos_z[i] = _pos_z[i];

    // ── Flank opportunity detection ────────────────────────────────
    // If we're behind/beside an enemy (dot product < -0.3), mark the area
    if ((_state[i] == ST_ENGAGING || _state[i] == ST_FLANKING) &&
        _pheromones[team_idx].is_valid()) {
      int32_t t = _target_id[i];
      if (t >= 0 && t < _count && _alive[t]) {
        float dx = _pos_x[i] - _pos_x[t];
        float dz = _pos_z[i] - _pos_z[t];
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist > 1.0f) {
          float rel_nx = dx / dist;
          float rel_nz = dz / dist;
          // Dot of enemy's facing vs our relative direction
          float dot = _face_x[t] * rel_nx + _face_z[t] * rel_nz;
          if (dot < -0.3f) {
            // We're behind the enemy — mark as flank opportunity
            _pheromones[team_idx]->deposit(pos, CH_FLANK_OPP, 2.0f * delta);
          }
        }
      }
    }
  }

  // Run CA update at ~30Hz
  if (do_ca) {
    for (int t = 0; t < 2; t++) {
      if (_pheromones[t].is_valid()) {
        _pheromones[t]->tick(PHEROMONE_TICK_INTERVAL);
      }
    }
  }
}

// ── Pheromone Query API (for GDScript debug overlay + economy) ────────

Ref<PheromoneMapCPP> SimulationServer::get_pheromone_map(int team) const {
  if (team < 0 || team > 1)
    return Ref<PheromoneMapCPP>();
  return _pheromones[team];
}

PackedFloat32Array SimulationServer::get_pheromone_data(int team,
                                                        int channel) const {
  if (team < 0 || team > 1)
    return PackedFloat32Array();
  if (_pheromones[team].is_valid() && channel >= 0 &&
      channel < CH_CHANNEL_COUNT) {
    return _pheromones[team]->get_channel_data(channel);
  }
  return PackedFloat32Array();
}

Dictionary SimulationServer::get_pheromone_stats() const {
  Dictionary d;
  for (int ch = 0; ch < CH_CHANNEL_COUNT; ch++) {
    Dictionary ch_data;
    float max_t1 = 0.0f, max_t2 = 0.0f, total_t1 = 0.0f, total_t2 = 0.0f;
    if (_pheromones[0].is_valid()) {
      max_t1 = _pheromones[0]->get_max_value(ch);
      total_t1 = _pheromones[0]->get_total_value(ch);
    }
    if (_pheromones[1].is_valid()) {
      max_t2 = _pheromones[1]->get_max_value(ch);
      total_t2 = _pheromones[1]->get_total_value(ch);
    }
    ch_data["max_t1"] = max_t1;
    ch_data["max_t2"] = max_t2;
    ch_data["total_t1"] = total_t1;
    ch_data["total_t2"] = total_t2;
    d[ch] = ch_data;
  }
  return d;
}

float SimulationServer::get_pheromone_at(const Vector3 &pos, int team,
                                         int channel) const {
  if (team < 0 || team > 1)
    return 0.0f;
  if (_pheromones[team].is_valid() && channel >= 0 &&
      channel < CH_CHANNEL_COUNT) {
    return _pheromones[team]->sample(pos, channel);
  }
  return 0.0f;
}

Vector3 SimulationServer::get_pheromone_gradient(const Vector3 &pos, int team,
                                                 int channel) const {
  if (team < 0 || team > 1)
    return Vector3();
  if (_pheromones[team].is_valid() && channel >= 0 &&
      channel < CH_CHANNEL_COUNT) {
    return _pheromones[team]->gradient(pos, channel);
  }
  return Vector3();
}

// ═══════════════════════════════════════════════════════════════════════
//  Tuning API
// ═══════════════════════════════════════════════════════════════════════

Dictionary SimulationServer::get_tuning_params() const {
  Dictionary d;
  // Movement
  d["move_speed"] = _tune_move_speed;
  d["separation_radius"] = _tune_separation_radius;
  d["separation_force"] = _tune_separation_force;
  d["arrive_dist"] = _tune_arrive_dist;
  d["centroid_anchor"] = _tune_centroid_anchor;
  d["catchup_weight"] = _tune_catchup_weight;
  d["combat_drift"] = _tune_combat_drift;
  d["max_step_height"] = _tune_max_step_height;
  // Locomotion
  d["turn_rate_base"] = _tune_turn_rate_base;
  d["turn_rate_bonus"] = _tune_turn_rate_bonus;
  d["face_smooth_rate"] = _tune_face_smooth_rate;
  d["dead_band_sq"] = _tune_dead_band_sq;
  // Context Steering
  d["steer_order"] = _tune_steer_order;
  d["steer_flow"] = _tune_steer_flow;
  d["steer_pheromone"] = _tune_steer_pheromone;
  d["steer_danger"] = _tune_steer_danger;
  d["steer_obstacle_dist"] = _tune_steer_obstacle_dist;
  d["steer_sample_dist"] = _tune_steer_sample_dist;
  d["steer_temporal"] = _tune_steer_temporal;
  d["steer_border_dist"] = _tune_steer_border_dist;
  // Combat
  d["decision_interval"] = _tune_decision_interval;
  d["reload_time"] = _tune_reload_time;
  d["suppression_decay"] = _tune_suppression_decay;
  d["settle_spread"] = _tune_settle_spread;
  d["near_miss_dist"] = _tune_near_miss_dist;
  d["near_miss_supp"] = _tune_near_miss_supp;
  d["hit_supp"] = _tune_hit_supp;
  d["wall_pen_penalty"] = _tune_wall_pen_penalty;
  // Tactical AI
  d["cover_seek_radius"] = _tune_cover_seek_radius;
  d["supp_cover_thresh"] = _tune_supp_cover_thresh;
  d["peek_offset"] = _tune_peek_offset;
  d["peek_hide_min"] = _tune_peek_hide_min;
  d["peek_hide_max"] = _tune_peek_hide_max;
  d["peek_expose_min"] = _tune_peek_expose_min;
  d["peek_expose_max"] = _tune_peek_expose_max;
  // Explosions
  d["grenade_dmg_radius"] = _tune_grenade_dmg_radius;
  d["grenade_max_dmg"] = _tune_grenade_max_dmg;
  d["mortar_dmg_radius"] = _tune_mortar_dmg_radius;
  d["mortar_max_dmg"] = _tune_mortar_max_dmg;
  d["mortar_max_scatter"] = _tune_mortar_max_scatter;
  // ORCA
  d["orca_agent_radius"] = _tune_orca_agent_radius;
  d["orca_time_horizon"] = _tune_orca_time_horizon;
  d["orca_neighbor_dist"] = _tune_orca_neighbor_dist;
  d["orca_wall_probe"] = _tune_orca_wall_probe;
  // Debug
  d["debug_log"] = _tune_debug_logging ? 1.0f : 0.0f;
  return d;
}

void SimulationServer::set_tuning_param(const String &name, float value) {
  // Movement
  if (name == "move_speed")
    _tune_move_speed = value;
  else if (name == "separation_radius")
    _tune_separation_radius = value;
  else if (name == "separation_force")
    _tune_separation_force = value;
  else if (name == "arrive_dist")
    _tune_arrive_dist = value;
  else if (name == "centroid_anchor")
    _tune_centroid_anchor = value;
  else if (name == "catchup_weight")
    _tune_catchup_weight = value;
  else if (name == "combat_drift")
    _tune_combat_drift = value;
  else if (name == "max_step_height")
    _tune_max_step_height = value;
  // Locomotion
  else if (name == "turn_rate_base")
    _tune_turn_rate_base = value;
  else if (name == "turn_rate_bonus")
    _tune_turn_rate_bonus = value;
  else if (name == "face_smooth_rate")
    _tune_face_smooth_rate = value;
  else if (name == "dead_band_sq")
    _tune_dead_band_sq = value;
  // Context Steering
  else if (name == "steer_order")
    _tune_steer_order = value;
  else if (name == "steer_flow")
    _tune_steer_flow = value;
  else if (name == "steer_pheromone")
    _tune_steer_pheromone = value;
  else if (name == "steer_danger")
    _tune_steer_danger = value;
  else if (name == "steer_obstacle_dist")
    _tune_steer_obstacle_dist = value;
  else if (name == "steer_sample_dist")
    _tune_steer_sample_dist = value;
  else if (name == "steer_temporal")
    _tune_steer_temporal = value;
  else if (name == "steer_border_dist")
    _tune_steer_border_dist = value;
  // Combat
  else if (name == "decision_interval")
    _tune_decision_interval = value;
  else if (name == "reload_time")
    _tune_reload_time = value;
  else if (name == "suppression_decay")
    _tune_suppression_decay = value;
  else if (name == "settle_spread")
    _tune_settle_spread = value;
  else if (name == "near_miss_dist")
    _tune_near_miss_dist = value;
  else if (name == "near_miss_supp")
    _tune_near_miss_supp = value;
  else if (name == "hit_supp")
    _tune_hit_supp = value;
  else if (name == "wall_pen_penalty")
    _tune_wall_pen_penalty = value;
  // Tactical AI
  else if (name == "cover_seek_radius")
    _tune_cover_seek_radius = value;
  else if (name == "supp_cover_thresh")
    _tune_supp_cover_thresh = value;
  else if (name == "peek_offset")
    _tune_peek_offset = value;
  else if (name == "peek_hide_min")
    _tune_peek_hide_min = value;
  else if (name == "peek_hide_max")
    _tune_peek_hide_max = value;
  else if (name == "peek_expose_min")
    _tune_peek_expose_min = value;
  else if (name == "peek_expose_max")
    _tune_peek_expose_max = value;
  // Explosions
  else if (name == "grenade_dmg_radius")
    _tune_grenade_dmg_radius = value;
  else if (name == "grenade_max_dmg")
    _tune_grenade_max_dmg = value;
  else if (name == "mortar_dmg_radius")
    _tune_mortar_dmg_radius = value;
  else if (name == "mortar_max_dmg")
    _tune_mortar_max_dmg = value;
  else if (name == "mortar_max_scatter")
    _tune_mortar_max_scatter = value;
  // ORCA
  else if (name == "orca_agent_radius")
    _tune_orca_agent_radius = value;
  else if (name == "orca_time_horizon")
    _tune_orca_time_horizon = value;
  else if (name == "orca_neighbor_dist")
    _tune_orca_neighbor_dist = value;
  else if (name == "orca_wall_probe")
    _tune_orca_wall_probe = value;
  // Flow field weights
  else if (name == "flow_weight_squad")
    _tune_flow_weight_squad = value;
  else if (name == "flow_weight_move")
    _tune_flow_weight_move = value;
  // Debug
  else if (name == "debug_log")
    _tune_debug_logging = (value > 0.5f);
}

void SimulationServer::reset_tuning_params() {
  // Movement
  _tune_move_speed = MOVE_SPEED;
  _tune_separation_radius = SEPARATION_RADIUS;
  _tune_separation_force = SEPARATION_FORCE;
  _tune_arrive_dist = ARRIVE_DIST;
  _tune_centroid_anchor = CENTROID_ANCHOR_BLEND;
  _tune_catchup_weight = CATCHUP_WEIGHT;
  _tune_combat_drift = COMBAT_FORMATION_DRIFT;
  _tune_max_step_height = MAX_STEP_HEIGHT;
  // Locomotion
  _tune_turn_rate_base = LOCO_TURN_RATE_BASE;
  _tune_turn_rate_bonus = LOCO_TURN_RATE_BONUS;
  _tune_face_smooth_rate = FACE_SMOOTH_RATE;
  _tune_dead_band_sq = LOCO_DEAD_BAND_SQ;
  // Context Steering
  _tune_steer_order = STEER_ORDER_WEIGHT;
  _tune_steer_flow = STEER_FLOW_WEIGHT;
  _tune_steer_pheromone = STEER_PHEROMONE_WEIGHT;
  _tune_steer_danger = STEER_DANGER_SCALE;
  _tune_steer_obstacle_dist = STEER_OBSTACLE_DIST;
  _tune_steer_sample_dist = STEER_SAMPLE_DIST;
  _tune_steer_temporal = STEER_TEMPORAL_ALPHA;
  _tune_steer_border_dist = STEER_MAP_BORDER_DIST;
  // Combat
  _tune_decision_interval = DECISION_INTERVAL;
  _tune_reload_time = RELOAD_TIME;
  _tune_suppression_decay = SUPPRESSION_DECAY;
  _tune_settle_spread = SETTLE_SPREAD_MULT;
  _tune_near_miss_dist = PROJ_NEAR_MISS_DIST;
  _tune_near_miss_supp = PROJ_NEAR_MISS_SUPP;
  _tune_hit_supp = PROJ_HIT_SUPP;
  _tune_wall_pen_penalty = WALL_PEN_SCORE_PENALTY;
  // Tactical AI
  _tune_cover_seek_radius = COVER_SEEK_RADIUS;
  _tune_supp_cover_thresh = SUPPRESSION_COVER_THRESHOLD;
  _tune_peek_offset = PEEK_OFFSET_DIST;
  _tune_peek_hide_min = PEEK_HIDE_MIN;
  _tune_peek_hide_max = PEEK_HIDE_MAX;
  _tune_peek_expose_min = PEEK_EXPOSE_MIN;
  _tune_peek_expose_max = PEEK_EXPOSE_MAX;
  // Explosions
  _tune_grenade_dmg_radius = GRENADE_DAMAGE_RADIUS;
  _tune_grenade_max_dmg = GRENADE_MAX_DAMAGE;
  _tune_mortar_dmg_radius = MORTAR_DAMAGE_RADIUS;
  _tune_mortar_max_dmg = MORTAR_MAX_DAMAGE;
  _tune_mortar_max_scatter = MORTAR_MAX_SCATTER;
  // ORCA
  _tune_orca_agent_radius = ORCA_AGENT_RADIUS;
  _tune_orca_time_horizon = ORCA_TIME_HORIZON;
  _tune_orca_neighbor_dist = ORCA_NEIGHBOR_DIST;
  _tune_orca_wall_probe = ORCA_WALL_PROBE_DIST;
  // Flow field weights
  _tune_flow_weight_squad = FLOW_WEIGHT_SQUAD;
  _tune_flow_weight_move = FLOW_WEIGHT_MOVE;
  // Debug
  _tune_debug_logging = false;
}

// ═══════════════════════════════════════════════════════════════════════
//  GDScript Binding
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::_bind_methods() {
  // ── Musket Sandbox API ────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("set_musket_mode", "enabled"),
                       &SimulationServer::set_musket_mode);
  ClassDB::bind_method(D_METHOD("test_spawn_battalion", "files", "ranks"),
                       &SimulationServer::test_spawn_battalion);
  ClassDB::bind_method(D_METHOD("tick_musket_combat", "delta"),
                       &SimulationServer::tick_musket_combat);
  ClassDB::bind_method(D_METHOD("get_musket_render_buffer"),
                       &SimulationServer::get_musket_render_buffer);

  // ── Setup ─────────────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("setup", "map_w", "map_h"),
                       &SimulationServer::setup);
  ClassDB::bind_method(D_METHOD("set_gpu_tactical_map", "map"),
                       &SimulationServer::set_gpu_tactical_map);
  ClassDB::bind_method(D_METHOD("set_seed", "seed"),
                       &SimulationServer::set_seed);
  ClassDB::bind_method(D_METHOD("get_seed"), &SimulationServer::get_seed);

  // ── Spawn / Despawn ───────────────────────────────────────────
  ClassDB::bind_method(
      D_METHOD("spawn_unit", "pos", "team", "role", "squad_id"),
      &SimulationServer::spawn_unit);
  ClassDB::bind_method(D_METHOD("kill_unit", "unit_id"),
                       &SimulationServer::kill_unit);
  ClassDB::bind_method(D_METHOD("despawn_unit", "unit_id"),
                       &SimulationServer::despawn_unit);

  // ── Tick ──────────────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("tick", "delta"), &SimulationServer::tick);

  // ── Orders ────────────────────────────────────────────────────
  ClassDB::bind_method(
      D_METHOD("set_order", "unit_id", "order_type", "target_pos", "target_id"),
      &SimulationServer::set_order, DEFVAL(-1));
  ClassDB::bind_method(
      D_METHOD("set_squad_rally", "squad_id", "rally", "advance_dir"),
      &SimulationServer::set_squad_rally);

  // ── Squad Flow Field ─────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("advance_squad", "squad_id", "offset_delta"),
                       &SimulationServer::advance_squad);
  ClassDB::bind_method(
      D_METHOD("set_squad_advance_offset", "squad_id", "offset"),
      &SimulationServer::set_squad_advance_offset);
  ClassDB::bind_method(D_METHOD("get_squad_advance_offset", "squad_id"),
                       &SimulationServer::get_squad_advance_offset);
  ClassDB::bind_method(D_METHOD("get_squad_centroid", "squad_id"),
                       &SimulationServer::get_squad_centroid);
  ClassDB::bind_method(D_METHOD("get_squad_alive_count", "squad_id"),
                       &SimulationServer::get_squad_alive_count);
  ClassDB::bind_method(
      D_METHOD("set_squad_formation", "squad_id", "formation_type"),
      &SimulationServer::set_squad_formation);
  ClassDB::bind_method(D_METHOD("get_squad_formation", "squad_id"),
                       &SimulationServer::get_squad_formation);
  ClassDB::bind_method(
      D_METHOD("set_squad_formation_spread", "squad_id", "spread"),
      &SimulationServer::set_squad_formation_spread);
  ClassDB::bind_method(D_METHOD("get_squad_formation_spread", "squad_id"),
                       &SimulationServer::get_squad_formation_spread);
  ClassDB::bind_method(D_METHOD("get_squad_goals", "team"),
                       &SimulationServer::get_squad_goals, DEFVAL(0));
  ClassDB::bind_method(D_METHOD("is_squad_in_contact", "squad_id", "radius"),
                       &SimulationServer::is_squad_in_contact);

  // ── Personality ───────────────────────────────────────────────
  ClassDB::bind_method(
      D_METHOD("set_unit_personality", "unit_id", "personality"),
      &SimulationServer::set_unit_personality);
  ClassDB::bind_method(D_METHOD("get_unit_personality", "unit_id"),
                       &SimulationServer::get_unit_personality);

  // ── Gas Grenades ─────────────────────────────────────────────
  ClassDB::bind_method(
      D_METHOD("throw_gas_grenade", "thrower", "target", "payload"),
      &SimulationServer::throw_gas_grenade);
  ClassDB::bind_method(
      D_METHOD("spawn_gas_at", "pos", "radius", "density", "gas_type"),
      &SimulationServer::spawn_gas_at);

  // ── Movement Mode / Context Steering ────────────────────────
  ClassDB::bind_method(D_METHOD("set_unit_movement_mode", "unit_id", "mode"),
                       &SimulationServer::set_unit_movement_mode);
  ClassDB::bind_method(D_METHOD("get_unit_movement_mode", "unit_id"),
                       &SimulationServer::get_unit_movement_mode);
  ClassDB::bind_method(D_METHOD("set_squad_movement_mode", "squad_id", "mode"),
                       &SimulationServer::set_squad_movement_mode);
  ClassDB::bind_method(D_METHOD("set_context_steering_enabled", "enabled"),
                       &SimulationServer::set_context_steering_enabled);
  ClassDB::bind_method(D_METHOD("is_context_steering_enabled"),
                       &SimulationServer::is_context_steering_enabled);
  ClassDB::bind_method(D_METHOD("get_steer_interest", "unit_id"),
                       &SimulationServer::get_steer_interest);
  ClassDB::bind_method(D_METHOD("get_steer_danger", "unit_id"),
                       &SimulationServer::get_steer_danger);

  // ── ORCA ────────────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("set_orca_enabled", "enabled"),
                       &SimulationServer::set_orca_enabled);
  ClassDB::bind_method(D_METHOD("is_orca_enabled"),
                       &SimulationServer::is_orca_enabled);

  // ── Posture ──────────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("get_posture", "unit_id"),
                       &SimulationServer::get_posture);
  ClassDB::bind_method(D_METHOD("set_posture", "unit_id", "posture"),
                       &SimulationServer::set_posture);

  // ── Visibility / Fog of War ──────────────────────────────────
  ClassDB::bind_method(D_METHOD("team_can_see", "team", "unit_id"),
                       &SimulationServer::team_can_see);
  ClassDB::bind_method(D_METHOD("get_last_seen_time", "unit_id"),
                       &SimulationServer::get_last_seen_time);
  ClassDB::bind_method(D_METHOD("get_game_time"),
                       &SimulationServer::get_game_time);

  // ── Capture Points ────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("add_capture_point", "pos"),
                       &SimulationServer::add_capture_point);
  ClassDB::bind_method(D_METHOD("get_capture_data"),
                       &SimulationServer::get_capture_data);
  ClassDB::bind_method(D_METHOD("get_capture_count_for_team", "team"),
                       &SimulationServer::get_capture_count_for_team);

  // ── Queries ───────────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("get_unit_count"),
                       &SimulationServer::get_unit_count);
  ClassDB::bind_method(D_METHOD("get_alive_count"),
                       &SimulationServer::get_alive_count);
  ClassDB::bind_method(D_METHOD("get_alive_count_for_team", "team"),
                       &SimulationServer::get_alive_count_for_team);
  ClassDB::bind_method(D_METHOD("get_position", "unit_id"),
                       &SimulationServer::get_position);
  ClassDB::bind_method(D_METHOD("get_state", "unit_id"),
                       &SimulationServer::get_state);
  ClassDB::bind_method(D_METHOD("get_health", "unit_id"),
                       &SimulationServer::get_health);
  ClassDB::bind_method(D_METHOD("get_morale", "unit_id"),
                       &SimulationServer::get_morale);
  ClassDB::bind_method(D_METHOD("get_suppression", "unit_id"),
                       &SimulationServer::get_suppression);
  ClassDB::bind_method(D_METHOD("get_team", "unit_id"),
                       &SimulationServer::get_team);
  ClassDB::bind_method(D_METHOD("get_target", "unit_id"),
                       &SimulationServer::get_target);
  ClassDB::bind_method(D_METHOD("is_alive", "unit_id"),
                       &SimulationServer::is_alive);
  ClassDB::bind_method(D_METHOD("get_role", "unit_id"),
                       &SimulationServer::get_role);
  ClassDB::bind_method(D_METHOD("get_role_count_for_team", "team", "role"),
                       &SimulationServer::get_role_count_for_team);
  ClassDB::bind_method(D_METHOD("get_squad_id", "unit_id"),
                       &SimulationServer::get_squad_id);
  ClassDB::bind_method(D_METHOD("get_ammo", "unit_id"),
                       &SimulationServer::get_ammo);
  ClassDB::bind_method(D_METHOD("get_mag_size", "unit_id"),
                       &SimulationServer::get_mag_size);

  // ── Render Output ─────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("get_alive_positions"),
                       &SimulationServer::get_alive_positions);
  ClassDB::bind_method(D_METHOD("get_alive_facings"),
                       &SimulationServer::get_alive_facings);
  ClassDB::bind_method(D_METHOD("get_alive_teams"),
                       &SimulationServer::get_alive_teams);
  ClassDB::bind_method(D_METHOD("get_render_data"),
                       &SimulationServer::get_render_data);
  ClassDB::bind_method(D_METHOD("get_render_data_for_team", "team"),
                       &SimulationServer::get_render_data_for_team);
  ClassDB::bind_method(D_METHOD("get_dead_render_data"),
                       &SimulationServer::get_dead_render_data);

  // ── Projectile Output ─────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("get_projectile_render_data"),
                       &SimulationServer::get_projectile_render_data);
  ClassDB::bind_method(D_METHOD("get_active_projectile_count"),
                       &SimulationServer::get_active_projectile_count);
  ClassDB::bind_method(D_METHOD("get_impact_events"),
                       &SimulationServer::get_impact_events);
  ClassDB::bind_method(D_METHOD("get_muzzle_flash_events"),
                       &SimulationServer::get_muzzle_flash_events);

  // ── Debug ─────────────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("get_last_tick_ms"),
                       &SimulationServer::get_last_tick_ms);
  ClassDB::bind_method(D_METHOD("get_debug_stats"),
                       &SimulationServer::get_debug_stats);

  // ── Pheromone Queries ─────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("get_pheromone_map", "team"),
                       &SimulationServer::get_pheromone_map);
  ClassDB::bind_method(D_METHOD("get_pheromone_data", "team", "channel"),
                       &SimulationServer::get_pheromone_data);
  ClassDB::bind_method(D_METHOD("get_pheromone_stats"),
                       &SimulationServer::get_pheromone_stats);
  ClassDB::bind_method(D_METHOD("get_pheromone_at", "pos", "team", "channel"),
                       &SimulationServer::get_pheromone_at);
  ClassDB::bind_method(
      D_METHOD("get_pheromone_gradient", "pos", "team", "channel"),
      &SimulationServer::get_pheromone_gradient);

  // ── Tuning API ───────────────────────────────────────────────
  ClassDB::bind_method(D_METHOD("get_tuning_params"),
                       &SimulationServer::get_tuning_params);
  ClassDB::bind_method(D_METHOD("set_tuning_param", "name", "value"),
                       &SimulationServer::set_tuning_param);
  ClassDB::bind_method(D_METHOD("reset_tuning_params"),
                       &SimulationServer::reset_tuning_params);

  // ── Enum Constants ────────────────────────────────────────────
  BIND_ENUM_CONSTANT(ROLE_RIFLEMAN);
  BIND_ENUM_CONSTANT(ROLE_LEADER);
  BIND_ENUM_CONSTANT(ROLE_MEDIC);
  BIND_ENUM_CONSTANT(ROLE_MG);
  BIND_ENUM_CONSTANT(ROLE_MARKSMAN);
  BIND_ENUM_CONSTANT(ROLE_GRENADIER);
  BIND_ENUM_CONSTANT(ROLE_MORTAR);

  BIND_ENUM_CONSTANT(ST_IDLE);
  BIND_ENUM_CONSTANT(ST_MOVING);
  BIND_ENUM_CONSTANT(ST_ENGAGING);
  BIND_ENUM_CONSTANT(ST_IN_COVER);
  BIND_ENUM_CONSTANT(ST_SUPPRESSING);
  BIND_ENUM_CONSTANT(ST_FLANKING);
  BIND_ENUM_CONSTANT(ST_RETREATING);
  BIND_ENUM_CONSTANT(ST_RELOADING);
  BIND_ENUM_CONSTANT(ST_DOWNED);
  BIND_ENUM_CONSTANT(ST_DEAD);

  BIND_ENUM_CONSTANT(ORDER_NONE);
  BIND_ENUM_CONSTANT(ORDER_MOVE);
  BIND_ENUM_CONSTANT(ORDER_ATTACK);
  BIND_ENUM_CONSTANT(ORDER_DEFEND);
  BIND_ENUM_CONSTANT(ORDER_SUPPRESS);
  BIND_ENUM_CONSTANT(ORDER_FOLLOW_SQUAD);
  BIND_ENUM_CONSTANT(ORDER_RETREAT);

  BIND_ENUM_CONSTANT(FORM_LINE);
  BIND_ENUM_CONSTANT(FORM_WEDGE);
  BIND_ENUM_CONSTANT(FORM_COLUMN);
  BIND_ENUM_CONSTANT(FORM_CIRCLE);

  // ── Personality ───────────────────────────────────────────────
  BIND_ENUM_CONSTANT(PERS_STEADY);
  BIND_ENUM_CONSTANT(PERS_BERSERKER);
  BIND_ENUM_CONSTANT(PERS_CATATONIC);
  BIND_ENUM_CONSTANT(PERS_PARANOID);

  // ── New States ────────────────────────────────────────────────
  BIND_ENUM_CONSTANT(ST_BERSERK);
  BIND_ENUM_CONSTANT(ST_FROZEN);
  BIND_ENUM_CONSTANT(ST_CLIMBING);
  BIND_ENUM_CONSTANT(ST_FALLING);

  // ── Posture ──────────────────────────────────────────────────
  BIND_ENUM_CONSTANT(POST_STAND);
  BIND_ENUM_CONSTANT(POST_CROUCH);
  BIND_ENUM_CONSTANT(POST_PRONE);

  // ── Movement Modes ─────────────────────────────────────────
  BIND_ENUM_CONSTANT(MMODE_PATROL);
  BIND_ENUM_CONSTANT(MMODE_TACTICAL);
  BIND_ENUM_CONSTANT(MMODE_COMBAT);
  BIND_ENUM_CONSTANT(MMODE_STEALTH);
  BIND_ENUM_CONSTANT(MMODE_RUSH);

  // ── Gas Payload ─────────────────────────────────────────────
  BIND_CONSTANT(PAYLOAD_KINETIC);
  BIND_CONSTANT(PAYLOAD_SMOKE);
  BIND_CONSTANT(PAYLOAD_TEAR_GAS);
  BIND_CONSTANT(PAYLOAD_TOXIC);

  // ── Pheromone Channels (Combat) ─────────────────────────────
  BIND_ENUM_CONSTANT(CH_DANGER);
  BIND_ENUM_CONSTANT(CH_SUPPRESSION);
  BIND_ENUM_CONSTANT(CH_CONTACT);
  BIND_ENUM_CONSTANT(CH_RALLY);
  BIND_ENUM_CONSTANT(CH_FEAR);
  BIND_ENUM_CONSTANT(CH_COURAGE);
  BIND_ENUM_CONSTANT(CH_SAFE_ROUTE);
  BIND_ENUM_CONSTANT(CH_FLANK_OPP);
  BIND_ENUM_CONSTANT(CH_COMBAT_COUNT);
  // ── Pheromone Channels (Economy) ─────────────────────────────
  BIND_ENUM_CONSTANT(CH_METAL);
  BIND_ENUM_CONSTANT(CH_CRYSTAL);
  BIND_ENUM_CONSTANT(CH_ENERGY);
  BIND_ENUM_CONSTANT(CH_CONGESTION);
  BIND_ENUM_CONSTANT(CH_BUILD_URGENCY);
  BIND_ENUM_CONSTANT(CH_EXPLORED);
  BIND_ENUM_CONSTANT(CH_STRATEGIC);
  BIND_ENUM_CONSTANT(CH_CHANNEL_COUNT);
}

// ═══════════════════════════════════════════════════════════════════════
//  Musket Sandbox Implementation
// ═══════════════════════════════════════════════════════════════════════

void SimulationServer::set_musket_mode(bool enabled) {
  is_musket_mode = enabled;
  if (enabled && !_musket_systems_registered) {
    godot::ecs::register_musket_systems(ecs);
    _musket_systems_registered = true;
  }
}

void SimulationServer::test_spawn_battalion(int files, int ranks) {
  // Note: We do NOT delete all entities — that would nuke Flecs system
  // entities.
  auto battalion_ent = ecs.entity("TestBattalion");

  godot::ecs::Battalion b;
  b.files = files;
  b.ranks = ranks;
  b.spacing_x = 1.0f;
  b.spacing_z = 2.0f;
  b.center_x = 0.0f;
  b.center_z = 0.0f;
  b.dir_x = 0.0f;
  b.dir_z = 1.0f;
  b.right_x = -1.0f;
  b.right_z = 0.0f;
  battalion_ent.set<godot::ecs::Battalion>(b);

  godot::ecs::SquadRoster roster;
  for (int i = 0; i < godot::ecs::MAX_SQUAD_MEMBERS; i++)
    roster.slots[i] = 0;

  int total_men = files * ranks;
  if (total_men > godot::ecs::MAX_SQUAD_MEMBERS)
    total_men = godot::ecs::MAX_SQUAD_MEMBERS;

  for (int idx = 0; idx < total_men; idx++) {
    int r = idx / files;
    int f = idx % files;
    float ox = (f * b.spacing_x) - ((files - 1) * b.spacing_x * 0.5f);
    float oz = -(r * b.spacing_z) + ((ranks - 1) * b.spacing_z * 0.5f);
    float wx = b.center_x + (b.right_x * ox) + (b.dir_x * oz);
    float wz = b.center_z + (b.right_z * ox) + (b.dir_z * oz);

    flecs::entity soldier =
        ecs.entity()
            .set<godot::ecs::Position>({wx, wz})
            .set<godot::ecs::Velocity>({0.0f, 0.0f})
            .set<godot::ecs::Health>({1.0f, 1.0f})
            .set<godot::ecs::SoldierFormationTarget>({wx, wz, 10.0f, 2.0f})
            .add<godot::ecs::IsAlive>();

    roster.slots[idx] = soldier.id();
  }
  battalion_ent.set<godot::ecs::SquadRoster>(roster);

  godot::ecs::PanicGrid panic;
  panic.width = 64;
  panic.height = 64;
  panic.cell_size = 4.0f;
  panic.chunk_size = 16;
  panic.read_buffer.resize(64 * 64, 0.0f);
  panic.write_buffer.resize(64 * 64, 0.0f);
  panic.active_chunks.resize((64 / 16) * (64 / 16), 0);
  battalion_ent.set<godot::ecs::PanicGrid>(panic);
}

void SimulationServer::tick_musket_combat(float delta) {
  if (!is_musket_mode)
    return;
  ecs.progress(delta);
}

PackedFloat32Array SimulationServer::get_musket_render_buffer() {
  PackedFloat32Array buffer;
  godot::ecs::sync_muskets_to_godot(ecs, buffer);
  return buffer;
}
