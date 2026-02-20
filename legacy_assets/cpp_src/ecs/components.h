#ifndef ECS_COMPONENTS_H
#define ECS_COMPONENTS_H

#include "flecs.h"
#include <cstdint>
#include <godot_cpp/variant/vector3.hpp>

namespace godot {
namespace ecs {

// ── Shared Types ────────────────────────────────────────────────────────

enum UnitRole : uint8_t {
  ROLE_RIFLEMAN = 0,
  ROLE_LEADER,
  ROLE_MEDIC,
  ROLE_MG,
  ROLE_MARKSMAN,
  ROLE_GRENADIER,
  ROLE_MORTAR,
  ROLE_COUNT
};

enum UnitState : uint8_t {
  ST_IDLE = 0,
  ST_MOVING,
  ST_ENGAGING,
  ST_IN_COVER,
  ST_SUPPRESSING,
  ST_FLANKING,
  ST_RETREATING,
  ST_RELOADING,
  ST_DOWNED,
  ST_BERSERK,
  ST_FROZEN,
  ST_DEAD,
  ST_CLIMBING,
  ST_FALLING,
  ST_COUNT
};

enum UnitPosture : uint8_t {
  POST_STAND = 0,
  POST_CROUCH = 1,
  POST_PRONE = 2,
  POST_COUNT
};

// ── Core Spatial Components ─────────────────────────────────────────────

struct Position {
  float x;
  float z;
};

struct Velocity {
  float vx;
  float vz;
};

struct Transform3DData {
  float face_x;
  float face_z;
  float actual_vx;
  float actual_vz;
};

// ── Identity & State Components ─────────────────────────────────────────

struct Team {
  uint8_t id;
};

struct Role {
  uint8_t id;
};

struct State {
  UnitState current;
};

struct Posture {
  UnitPosture current;
  UnitPosture target;
  float transition_timer;
};

// ── Combat Components ───────────────────────────────────────────────────

struct Health {
  float current;
  float max;
};

struct Morale {
  float current;
  float max;
};

struct Suppression {
  float level;
};

struct AmmoInfo {
  int16_t current;
  int16_t mag_size;
};

// ── Projectile Components ──────────────────────────────────────────────────

struct ProjectileData {
  float damage;
  float energy;
  float lifetime;
  uint8_t type;
  uint8_t team;
  uint8_t payload;
  int32_t shooter;
};

struct ProjectileFlight {
  float x;
  float y;
  float z;
  float vx;
  float vy;
  float vz;
};

// ── AI Intent Components ────────────────────────────────────────────────

struct DesiredTarget {
  float x;
  float z;
};

struct DesiredVelocity {
  float vx;
  float vz;
};

// Tags (Components with no data)
struct IsAlive {};
struct IsPeeking {};
struct IsPlayerControlled {};

// ── Bridging Components (Phase 1) ───────────────────────────────────────
struct LegacyIndex {
  int32_t val;
};

struct CombatBridging {
  float deploy_timer;
  int32_t target_id;
  float attack_timer;
  float reload_timer;
};

struct Cooldowns {
  float attack;
};

struct MovementBridging {
  float climb_cooldown;
  float climb_target_y;
  float climb_dest_x;
  float climb_dest_z;
  float fall_start_y;
  float vel_y;
  float pos_y;
  uint8_t move_mode;
  uint8_t order;
  int32_t squad_id;
  int32_t squad_member_idx;
  float settle_timer;
};

} // namespace ecs
} // namespace godot

#endif // ECS_COMPONENTS_H
