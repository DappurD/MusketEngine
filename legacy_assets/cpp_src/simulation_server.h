#ifndef SIMULATION_SERVER_H
#define SIMULATION_SERVER_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

// Include Flecs for the ECS backend
#include "ecs/components.h"
#include "ecs/musket_rendering.h"
#include "ecs/musket_systems.h"
#include "flecs.h"

namespace godot {

class GpuTacticalMap;
class InfluenceMapCPP;
class PheromoneMapCPP;
class VoxelWorld;

/// Data-oriented simulation backend for 1000+ units.
///
/// All per-unit data stored in Structure-of-Arrays layout for cache-friendly
/// batch processing. GDScript orchestrators (ColonyAI, Squad) write orders;
/// a MultiMesh renderer reads positions out.
///
/// Usage:
///   var sim = SimulationServer.new()
///   sim.setup(600.0, 400.0)
///   sim.set_gpu_tactical_map(gpu_map)
///   var id = sim.spawn_unit(pos, 1, 0, 0)
///   sim.set_order(id, SimulationServer.ORDER_MOVE, target_pos)
///   # each frame:
///   sim.tick(delta)
///   var data = sim.get_render_data()
class SimulationServer : public RefCounted {
  GDCLASS(SimulationServer, RefCounted)

public:
  // ── Capacity ────────────────────────────────────────────────────
  static constexpr int MAX_UNITS = 12288;
  static constexpr int MAX_SQUADS = 2048;
  static constexpr int SPATIAL_CELL_M = 8; // meters per spatial cell

  // ── Unit Roles ──────────────────────────────────────────────────
  enum Role : uint8_t {
    ROLE_RIFLEMAN = 0,
    ROLE_LEADER,
    ROLE_MEDIC,
    ROLE_MG,
    ROLE_MARKSMAN,
    ROLE_GRENADIER,
    ROLE_MORTAR,
    ROLE_COUNT
  };

  // ── Simplified Unit States ──────────────────────────────────────
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
    ST_BERSERK, // Charging nearest enemy (morale break)
    ST_FROZEN,  // Catatonic freeze (morale break)
    ST_DEAD,
    ST_CLIMBING, // Scaling a wall vertically
    ST_FALLING,  // Airborne after edge/destruction
    ST_COUNT
  };

  // ── Order Types ─────────────────────────────────────────────────
  enum OrderType : uint8_t {
    ORDER_NONE = 0,
    ORDER_MOVE,
    ORDER_ATTACK,
    ORDER_DEFEND,
    ORDER_SUPPRESS,
    ORDER_FOLLOW_SQUAD,
    ORDER_RETREAT,
  };

  // ── Formation Types ──────────────────────────────────────────────
  enum FormationType : uint8_t {
    FORM_LINE = 0, // Horizontal firing line perpendicular to advance_dir
    FORM_WEDGE,    // V-shape, leader at point
    FORM_COLUMN,   // Narrow stack along advance_dir
    FORM_CIRCLE,   // Defensive ring around centroid (ignores advance_offset)
    FORM_COUNT
  };

  // ── Personality Traits ──────────────────────────────────────────
  enum Personality : uint8_t {
    PERS_STEADY = 0, // Retreats to rally on morale break (default)
    PERS_BERSERKER,  // Charges nearest enemy on break
    PERS_CATATONIC,  // Freezes in place on break
    PERS_PARANOID,   // Fires on allies on break
    PERS_COUNT
  };

  // ── Posture (orthogonal to UnitState) ────────────────────────────
  enum Posture : uint8_t {
    POST_STAND = 0,
    POST_CROUCH = 1,
    POST_PRONE = 2,
    POST_COUNT
  };

  // ── Movement Modes (context steering speed/noise profiles) ──────────
  enum MovementMode : uint8_t {
    MMODE_PATROL = 0, // Normal patrol speed, moderate noise
    MMODE_TACTICAL,   // Cautious advance, lower noise
    MMODE_COMBAT,     // Combat movement, balanced
    MMODE_STEALTH,    // Slow and quiet
    MMODE_RUSH,       // Fast sprint, very noisy
    MMODE_COUNT
  };

  // ── Unified Pheromone Channels (Combat 0-7 + Economy 8-14) ─────────
  enum PheromoneChannel : uint8_t {
    // Combat channels (SimulationServer owns)
    CH_DANGER = 0,
    CH_SUPPRESSION,
    CH_CONTACT,
    CH_RALLY,
    CH_FEAR,
    CH_COURAGE,
    CH_SAFE_ROUTE,
    CH_FLANK_OPP,
    CH_COMBAT_COUNT = 8, // sentinel for combat-only iteration

    // Economy channels (ColonyAI owns via GDScript)
    CH_METAL = 8,
    CH_CRYSTAL,
    CH_ENERGY,
    CH_CONGESTION,
    CH_BUILD_URGENCY,
    CH_EXPLORED,
    CH_STRATEGIC,         // LLM stigmergic command channel (was CH_SPARE)
    CH_CHANNEL_COUNT = 15 // total unified channel count
  };

  // ── Tick Subsystem IDs (for profiling) ────────────────────────────
  enum SubsystemID : uint8_t {
    SUB_SPATIAL = 0,
    SUB_CENTROIDS,
    SUB_ATTACKERS,
    SUB_COVER_VALUES,
    SUB_INFLUENCE,
    SUB_VISIBILITY,
    SUB_SUPPRESSION,
    SUB_RELOAD,
    SUB_POSTURE,
    SUB_DECISIONS,
    SUB_PEEK,
    SUB_COMBAT,
    SUB_PROJECTILES,
    SUB_MORALE,
    SUB_MOVEMENT,
    SUB_CAPTURE,
    SUB_LOCATION,
    SUB_GAS_EFFECTS,
    SUB_PHEROMONES,
    SUB_COUNT
  };

  SimulationServer();
  ~SimulationServer();

  // ── Setup ───────────────────────────────────────────────────────
  void setup(float map_w, float map_h);
  void set_gpu_tactical_map(Ref<GpuTacticalMap> map);

  // ── Singleton ───────────────────────────────────────────────────
  static SimulationServer *get_singleton() { return _singleton; }

  // ── Spawn / Despawn ─────────────────────────────────────────────
  int32_t spawn_unit(const Vector3 &pos, int team, int role, int squad_id);
  void kill_unit(int32_t unit_id);
  void despawn_unit(int32_t unit_id);

  // ── Tick ────────────────────────────────────────────────────────
  void tick(float delta);

  // ── Orders (GDScript → C++) ─────────────────────────────────────
  void set_order(int32_t unit_id, int order_type, const Vector3 &target_pos,
                 int32_t target_id = -1);
  void set_squad_rally(int squad_id, const Vector3 &rally,
                       const Vector3 &advance_dir);

  // ── Squad Flow Field API ─────────────────────────────────────────
  void advance_squad(int squad_id, float offset_delta);
  void set_squad_advance_offset(int squad_id, float offset);
  float get_squad_advance_offset(int squad_id) const;
  Vector3 get_squad_centroid(int squad_id) const;
  int get_squad_alive_count(int squad_id) const;
  void set_squad_formation(int squad_id, int formation_type);
  int get_squad_formation(int squad_id) const;
  void set_squad_formation_spread(int squad_id, float spread);
  float get_squad_formation_spread(int squad_id) const;
  Dictionary get_squad_goals(int team = 0) const; // team=0: all, 1/2: filter

  // ── Personality API ──────────────────────────────────────────────
  void set_unit_personality(int32_t unit_id, int personality);
  int get_unit_personality(int32_t unit_id) const;

  // ── Capture Points ─────────────────────────────────────────────
  int add_capture_point(const Vector3 &pos);
  Dictionary get_capture_data() const;
  int get_capture_count_for_team(int team) const;

  // ── Gas Grenade API ────────────────────────────────────────────────
  /// Spawn a gas grenade projectile from a unit toward target position.
  /// payload: 1=smoke, 2=tear gas, 3=toxic
  void throw_gas_grenade(int32_t thrower, const Vector3 &target, int payload);

  /// Directly spawn a gas cloud at position (for mortars, airstrikes,
  /// abilities).
  void spawn_gas_at(const Vector3 &pos, float radius, float density,
                    int gas_type);

  // ── Movement Mode / Context Steering API ────────────────────────
  void set_unit_movement_mode(int32_t unit_id, int mode);
  int get_unit_movement_mode(int32_t unit_id) const;
  void set_squad_movement_mode(int squad_id, int mode);
  void set_context_steering_enabled(bool enabled);
  bool is_context_steering_enabled() const { return _use_context_steering; }
  PackedFloat32Array get_steer_interest(int32_t unit_id) const;
  PackedFloat32Array get_steer_danger(int32_t unit_id) const;

  // ── ORCA API ────────────────────────────────────────────────────────
  void set_orca_enabled(bool enabled);
  bool is_orca_enabled() const { return _use_orca; }

  // ── Posture API ───────────────────────────────────────────────────
  int get_posture(int32_t unit_id) const;
  void set_posture(int32_t unit_id, int posture);

  // ── Visibility API ──────────────────────────────────────────────
  bool team_can_see(int team, int32_t unit_id) const;
  float get_last_seen_time(int32_t unit_id) const;
  float get_game_time() const { return _game_time; }

  // ── Queries ─────────────────────────────────────────────────────
  int get_unit_count() const { return _count; }
  int get_alive_count() const { return _alive_count; }
  int get_alive_count_for_team(int team) const;
  Vector3 get_position(int32_t unit_id) const;
  int get_state(int32_t unit_id) const;
  float get_health(int32_t unit_id) const;
  float get_morale(int32_t unit_id) const;
  float get_suppression(int32_t unit_id) const;
  int get_team(int32_t unit_id) const;
  int get_target(int32_t unit_id) const;
  bool is_alive(int32_t unit_id) const;
  int get_role(int32_t unit_id) const;
  int get_role_count_for_team(int team, int role) const;
  int get_squad_id(int32_t unit_id) const;
  bool is_squad_in_contact(int squad_id, float radius) const;
  int get_ammo(int32_t unit_id) const;
  int get_mag_size(int32_t unit_id) const;

  // ── Render Output ───────────────────────────────────────────────
  PackedVector3Array get_alive_positions() const;
  PackedVector3Array get_alive_facings() const;
  PackedInt32Array get_alive_teams() const;
  Dictionary get_render_data() const;
  Dictionary get_render_data_for_team(int team) const;
  Dictionary get_dead_render_data() const;

  // ── Projectile Queries ──────────────────────────────────────────
  Dictionary get_projectile_render_data() const;
  int get_active_projectile_count() const { return _proj_active_count; }
  Array get_impact_events();

  // ── Muzzle Flash Events ─────────────────────────────────────────
  Array get_muzzle_flash_events();

  // ── Debug ───────────────────────────────────────────────────────
  float get_last_tick_ms() const { return _last_tick_ms; }
  Dictionary get_debug_stats() const;

  // ── Pheromone Queries ────────────────────────────────────────────
  /// Get unified pheromone map for a team (for ColonyAI economy deposits)
  Ref<PheromoneMapCPP> get_pheromone_map(int team) const;

  PackedFloat32Array get_pheromone_data(int team, int channel) const;
  Dictionary get_pheromone_stats() const;
  float get_pheromone_at(const Vector3 &pos, int team, int channel) const;
  Vector3 get_pheromone_gradient(const Vector3 &pos, int team,
                                 int channel) const;

  // ── Tuning API (runtime parameter adjustment) ────────────────────
  Dictionary get_tuning_params() const;
  void set_tuning_param(const String &name, float value);
  void reset_tuning_params();

  // ── Musket Sandbox API ──────────────────────────────────────────
  bool is_musket_mode = false;
  void set_musket_mode(bool enabled);
  bool _musket_systems_registered = false;
  void test_spawn_battalion(int files, int ranks);
  void tick_musket_combat(float delta);
  PackedFloat32Array get_musket_render_buffer();

  // ── ECS Backend ─────────────────────────────────────────────────
  flecs::world ecs;

protected:
  static void _bind_methods();

private:
  static SimulationServer *_singleton;

  // ── SoA Unit Data ───────────────────────────────────────────────
  int _count = 0;
  int _alive_count = 0;

  // Incremental ECS Migration: Track the Flecs entity for each index
  std::vector<flecs::entity> _flecs_id;

  // Position & movement (hot)
  std::vector<float> _pos_x, _pos_y, _pos_z;
  std::vector<float> _vel_x, _vel_y, _vel_z;
  std::vector<float> _face_x, _face_z;
  std::vector<float> _actual_vx,
      _actual_vz; // locomotion physics: smoothed velocity
  // Context steering maps (per-unit × STEER_SLOTS)
  std::vector<float> _steer_interest; // [MAX_UNITS * STEER_SLOTS]
  std::vector<float> _steer_danger;   // [MAX_UNITS * STEER_SLOTS]
  std::vector<uint8_t> _move_mode;    // MovementMode per unit
  std::vector<float>
      _noise_level; // detection radius in meters (derived from move_mode)
  bool _use_context_steering = true; // A/B toggle: false = old additive forces
  bool _use_orca = true;             // A/B toggle: false = old boids separation
  std::vector<float> _climb_target_y; // world Y to reach when climbing
  std::vector<float>
      _climb_dest_x; // world X to land on after climbing (wall top position)
  std::vector<float> _climb_dest_z; // world Z to land on after climbing
  std::vector<float> _fall_start_y; // world Y when fall began (for damage calc)
  std::vector<float>
      _climb_cooldown; // seconds remaining before unit can climb again

  // Combat (warm)
  std::vector<float> _health;
  std::vector<float> _morale;
  std::vector<float> _suppression;
  std::vector<float> _attack_range;
  std::vector<float> _attack_timer;
  std::vector<float> _attack_cooldown;
  std::vector<float> _accuracy;
  std::vector<int16_t> _ammo;
  std::vector<int16_t> _mag_size;

  // Identity (cold)
  std::vector<uint8_t> _team;
  std::vector<uint8_t> _role;
  std::vector<uint16_t> _squad_id;
  std::vector<UnitState> _state;
  std::vector<bool> _alive;

  // Personality (cold)
  std::vector<uint8_t> _personality;
  std::vector<float> _frozen_timer; // CATATONIC unfreeze countdown

  // Animation (render-only, not read by AI)
  std::vector<float> _anim_phase; // 0.0-inf, fmod to 1.0 for shader

  // Formation (rebuilt each tick during centroid pass)
  std::vector<int16_t>
      _squad_member_idx; // unit's index within its squad (for slot assignment)

  // Targeting
  std::vector<int32_t> _target_id;

  // Orders
  std::vector<OrderType> _order;
  std::vector<float> _order_x, _order_y, _order_z;
  std::vector<int32_t> _order_target_id;

  // Decision stagger
  std::vector<float> _decision_timer;

  // Reload
  std::vector<float> _reload_timer;

  // Settle time (accuracy recovery after stopping)
  std::vector<float> _settle_timer;

  // Deploy time (weapon setup before firing — hard gate)
  std::vector<float> _deploy_timer;

  // Mode transition cooldown (prevents RUSH↔COMBAT oscillation)
  std::vector<float> _mode_transition_timer;

  // Aim quality (0.0-1.0, computed from current spread — AI decision factor)
  std::vector<float> _aim_quality;

  // ── Tactical AI SoA ─────────────────────────────────────────────
  std::vector<float> _target_score;      // best scored target's value
  std::vector<bool> _target_suppressive; // true = target acquired via FOW
                                         // fallback (suppressive fire)
  std::vector<int32_t>
      _attackers_count;            // how many enemies are targeting unit i
  std::vector<float> _cover_value; // TacticalCoverMap cover at current pos
  std::vector<int16_t>
      _nearby_squad_count; // same-squad allies within cohesion radius
  std::vector<bool> _has_visible_enemy; // visible enemy within detect range
                                        // (persists across tick phases)

  // ── Peek Behavior SoA ────────────────────────────────────────────
  std::vector<float> _peek_timer;    // countdown to next peek/hide toggle
  std::vector<float> _peek_offset_x; // cached sidestep offset X
  std::vector<float> _peek_offset_z; // cached sidestep offset Z
  std::vector<bool> _is_peeking;  // true = exposed (can fire), false = hidden
  std::vector<int8_t> _peek_side; // -1=left, +1=right

  // ── Posture SoA (orthogonal to UnitState) ─────────────────────────
  std::vector<uint8_t> _posture; // POST_STAND / POST_CROUCH / POST_PRONE
  std::vector<uint8_t>
      _posture_target;               // desired posture (transition in progress)
  std::vector<float> _posture_timer; // transition countdown (0 = done)

  // ── Fog of War / Visibility ──────────────────────────────────────
  static constexpr int VIS_WORDS = (MAX_UNITS + 63) / 64; // 32 for 2048
  uint64_t _team_vis[2][VIS_WORDS] = {}; // team X sees enemy Y → bit Y set

  std::vector<float>
      _last_seen_time;              // game_time when last visible to enemy team
  std::vector<float> _last_known_x; // last-known position (suppressive fire)
  std::vector<float> _last_known_z;
  std::vector<float> _detect_range; // max detection range by role

  int _vis_cursor = 0; // round-robin scan position
  static constexpr int VIS_BATCH_SIZE =
      160; // was 80 — too slow, enemies invisible for 0.2s gaps
  static constexpr float CONTACT_DECAY_TIME =
      4.0f; // seconds before target fully lost
  static constexpr float VIS_REFRESH_INTERVAL =
      0.35f;                        // full bitfield clear interval
  float _game_time = 0.0f;          // accumulated sim time
  float _vis_last_refresh = 0.0f;   // game_time of last full visibility clear
  float _last_slot_reassign = 0.0f; // game_time of last slot reassignment

  // ── Unit Constants ───────────────────────────────────────────────
  static constexpr float MOVE_SPEED = 4.0f;
  // Flow field weights (context-sensitive, replaces old constant FLOW_WEIGHT)
  static constexpr float FLOW_WEIGHT_SQUAD =
      1.5f; // ORDER_FOLLOW_SQUAD / ORDER_RETREAT (advance needs flow; formation
            // dominance via high ORDER_WEIGHT instead)
  static constexpr float FLOW_WEIGHT_MOVE = 1.5f; // ORDER_MOVE / ORDER_ATTACK
  static constexpr float FLOW_WEIGHT_IDLE = 0.0f; // idle units should not drift
  static constexpr float GOAL_LEAD_DIST =
      3.0f; // meters ahead of centroid for FORMATION SLOTS (perpendicular pull
            // is full weight, along-advance is 0.3x — flow handles forward
            // movement)
  static constexpr float FLOW_GOAL_LEAD =
      100.0f; // meters ahead for GPU FLOW FIELD goals (extends gradient across
              // entire approach)
  static constexpr float GOAL_SPACING = 10.0f; // between projected goals
  static constexpr int MAX_GOALS_PER_SQUAD =
      4; // 4 * 16 top squads = 64 = MAX_GOALS
  static constexpr float SEPARATION_RADIUS = 2.0f;
  static constexpr float SEPARATION_FORCE = 1.5f;
  static constexpr float DECISION_INTERVAL =
      0.35f; // was 0.20 — too fast, caused ant-like retargeting (contact
             // alertness × 0.6 = 0.21s in hot zones)
  static constexpr float RELOAD_TIME = 2.5f;
  static constexpr float SUPPRESSION_DECAY = 0.3f;
  static constexpr float SETTLE_SPREAD_MULT =
      3.0f; // at max settle, 4x worse accuracy (1+3)
  static constexpr float ARRIVE_DIST =
      1.5f; // tight arrival tolerance for precise formation
  static constexpr float CENTROID_ANCHOR_BLEND =
      0.1f; // 0=pure centroid, 1=pure rally anchor (was 0.5 — pulled centroid
            // too far from actual unit positions, making formation slots 30m+
            // away)
  static constexpr float FORMATION_URGENCY_SCALE =
      10.0f; // distance at which urgency = 1.0
  static constexpr float FORMATION_URGENCY_MAX =
      5.0f; // max urgency multiplier (was 3.0 — not strong enough at distance)
  static constexpr float CATCHUP_WEIGHT =
      1.2f; // interest weight for units behind formation slot (was 0.6 —
            // stragglers never caught up)
  static constexpr float COMBAT_FORMATION_DRIFT =
      0.5f; // m/s — pull toward slot while in combat (was 2.0 — too fast,
            // caused slide-while-shooting)
  static constexpr float FORMATION_LEASH_HARD =
      10.0f; // meters — beyond this, override all steering with direct slot
             // pull
  static constexpr float FORMATION_LEASH_SOFT =
      5.0f; // meters — between soft/hard, blend current velocity toward slot
  static constexpr float SLOT_REASSIGN_INTERVAL =
      2.0f; // seconds between position-aware slot reassignment

  // ── Behavioral Fidelity Constants ────────────────────────────
  static constexpr float MODE_TRANSITION_COOLDOWN =
      1.5f; // seconds before speed mode can change again
  static constexpr float TARGET_STICKINESS =
      10.0f; // score bonus for keeping current target (prevents flicker)
  static constexpr float RUSH_ENGAGE_THRESHOLD =
      25.0f; // min target score to stop advancing while in RUSH mode
  static constexpr float MAX_STEP_HEIGHT =
      0.85f; // max upward step per tick (3 voxels — vault height)

  // ── Vertical Traversal Constants ─────────────────────────
  static constexpr int VAULT_MAX_VOXELS =
      3; // max obstacle height for auto-vault
  static constexpr int CLIMB_MAX_VOXELS =
      12; // max wall height for climbing (3m)
  static constexpr float CLIMB_SPEED = 1.0f; // m/s vertical climb rate
  static constexpr float CLIMB_COOLDOWN_SEC =
      3.0f; // seconds before re-attempting climb after fall
  static constexpr float FALL_DAMAGE_THRESH =
      2.0f; // meters of fall before damage starts
  static constexpr float FALL_DAMAGE_PER_M =
      0.15f; // HP fraction lost per meter above threshold
  static constexpr float FALL_LETHAL_HEIGHT =
      8.0f;                                   // instant death height (meters)
  static constexpr float FALL_GRAVITY = 9.8f; // m/s² gravity for falling units

  // ── Locomotion Physics ──────────────────────────────────────────
  static constexpr float LOCO_ACCEL_RATES[3] = {
      4.0f, 3.0f, 1.5f}; // Hz by posture (STAND/CROUCH/PRONE)
  static constexpr float LOCO_DECEL_RATES[3] = {3.0f, 2.5f,
                                                2.0f}; // Hz by posture
  static constexpr float LOCO_TURN_RATE_BASE = 2.0f;   // rad/s at any speed
  static constexpr float LOCO_TURN_RATE_BONUS =
      6.0f; // additional rad/s at speed 0
  static constexpr float LOCO_DEAD_BAND_SQ =
      0.04f; // speed² below which snap to 0 (0.2 m/s)
  static constexpr float LOCO_TURN_CHECK_DOT =
      0.95f; // skip atan2 if cos(angle) > this (~18°)
  static constexpr float FACE_SMOOTH_RATE =
      6.0f; // Hz for facing direction smoothing

  // ── ORCA Collision Avoidance ──────────────────────────────────────
  static constexpr float ORCA_AGENT_RADIUS =
      0.4f; // avoidance radius for inter-squad/enemy (meters)
  static constexpr float ORCA_SQUAD_RADIUS =
      0.2f; // tight radius for same-squad allies (medieval formations)
  static constexpr float ORCA_TIME_HORIZON =
      0.5f; // seconds lookahead (was 1.0 — too conservative, units avoid 3-5m
            // ahead)
  static constexpr float ORCA_NEIGHBOR_DIST =
      3.0f; // meters, max neighbor query distance
  static constexpr int ORCA_MAX_NEIGHBORS = 8; // cap per unit for LP solver
  static constexpr int ORCA_MAX_WALL_LINES =
      8; // max wall half-planes (cardinal + diagonal probes)
  static constexpr float ORCA_WALL_PROBE_DIST =
      2.0f; // meters, wall probe distance
  static constexpr float ORCA_EPSILON = 0.00001f;
  static constexpr float ORCA_INTENT_BLEND =
      0.3f; // preserve 30% of intent when ORCA returns near-zero (prevents
            // freeze)

  // ── Context Steering Constants ───────────────────────────────────
  static constexpr int STEER_SLOTS = 8;
  static constexpr float STEER_SAMPLE_DIST =
      4.0f; // meters, matches pheromone cell size
  static constexpr float STEER_TEMPORAL_ALPHA =
      0.3f; // EMA smoothing for map stability
  static constexpr float STEER_BLUR_KERNEL[3] = {0.25f, 0.5f,
                                                 0.25f}; // circular Gaussian

  // 8 uniformly-spaced directions (N, NE, E, SE, S, SW, W, NW)
  static constexpr float SLOT_DIR_X[STEER_SLOTS] = {
      0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f};
  static constexpr float SLOT_DIR_Z[STEER_SLOTS] = {
      1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f, 0.0f, 0.7071f};

  // Speed table: [posture][movement_mode] in m/s
  static constexpr float SPEED_TABLE[3][MMODE_COUNT] = {
      {4.5f, 3.5f, 2.5f, 1.5f,
       6.0f}, // STAND:  patrol/tactical/combat/stealth/rush
              // (was 5.5/4.0/3.0/2.0/7.0)
      {3.0f, 2.2f, 1.7f, 0.8f, 4.0f}, // CROUCH (was 3.5/2.5/2.0/1.0/4.5)
      {0.7f, 0.4f, 0.3f, 0.2f, 0.9f}, // PRONE  (was 0.8/0.5/0.3/0.2/1.0)
  };
  // Detection radius by movement mode (meters)
  static constexpr float NOISE_TABLE[MMODE_COUNT] = {40.0f, 25.0f, 15.0f, 8.0f,
                                                     60.0f};

  // Context steering interest/danger weights
  static constexpr float STEER_ORDER_WEIGHT =
      4.0f; // weight for order direction interest (was 1.0→2.5→4.0 — must
            // dominate pheromone scatter for visible formations)
  static constexpr float STEER_FLOW_WEIGHT =
      0.6f; // weight for GPU flow field interest
  static constexpr float STEER_PHEROMONE_WEIGHT =
      0.4f; // weight for positive pheromone gradients
  static constexpr float STEER_DANGER_SCALE = 1.0f; // danger pheromone scaling
  static constexpr float STEER_OBSTACLE_DIST =
      2.0f; // meters ahead for voxel look-ahead
  static constexpr float STEER_MAP_BORDER_DIST =
      5.0f; // meters from map edge → danger

  // ── Suppressive Fire Constants ─────────────────────────────────
  static constexpr float SUPPRESS_SCATTER =
      1.5f; // meters, random scatter for suppressive fire
  static constexpr float SUPPRESS_SPREAD_MULT =
      2.0f; // accuracy penalty for suppressive fire

  // ── Projectile Constants ────────────────────────────────────────
  static constexpr int MAX_PROJECTILES = 4096;
  static constexpr int MAX_IMPACT_EVENTS = 128;
  static constexpr float PROJ_GRAVITY = 4.0f;        // m/s² (gameplay)
  static constexpr float PROJ_MAX_LIFETIME = 3.0f;   // seconds
  static constexpr float PROJ_HIT_RADIUS = 0.35f;    // body hit sphere
  static constexpr float PROJ_NEAR_MISS_DIST = 4.0f; // suppression range
  static constexpr float PROJ_NEAR_MISS_SUPP =
      0.06f;                                    // suppression per near-miss
  static constexpr float PROJ_HIT_SUPP = 0.15f; // suppression on hit
  static constexpr float PENETRATION_FACTOR =
      1000.0f; // effectively infinite resistance (was 0.002f)
  static constexpr int MAX_PEN_VOXELS = 8; // max voxels per penetration check
  static constexpr float WALL_PEN_SCORE_PENALTY =
      15.0f; // target score penalty per energy unit of wall
  static constexpr float MUZZLE_FWD = 0.6f;       // meters forward
  static constexpr float MUZZLE_HEIGHT = 1.4f;    // meters above ground
  static constexpr float VOXEL_DMG_FACTOR = 0.5f; // energy → voxel damage

  // ── Explosion Constants ──────────────────────────────────────────
  static constexpr float GRENADE_BLAST_RADIUS =
      1.5f; // voxel destruction radius (m)
  static constexpr float GRENADE_DAMAGE_RADIUS =
      4.0f; // unit damage falloff (m)
  static constexpr float GRENADE_SUPPRESSION_RADIUS =
      8.0f;                                         // suppression reach (m)
  static constexpr float GRENADE_MAX_DAMAGE = 0.7f; // damage at epicenter
  static constexpr float GRENADE_MAX_SUPPRESSION =
      0.5f;                                         // suppression at epicenter
  static constexpr float GRENADE_ARC_ANGLE = 0.35f; // radians added upward
  static constexpr float MORTAR_BLAST_RADIUS =
      2.6f; // larger voxel destruction radius (m)
  static constexpr float MORTAR_DAMAGE_RADIUS = 7.5f; // larger lethal area (m)
  static constexpr float MORTAR_SUPPRESSION_RADIUS =
      14.0f; // broader suppression wave (m)
  static constexpr float MORTAR_MAX_DAMAGE =
      1.0f; // high HE damage at epicenter
  static constexpr float MORTAR_MAX_SUPPRESSION = 0.85f; // strong morale shock
  static constexpr float MORTAR_ARC_ANGLE = 0.78f;       // steep indirect arc
  static constexpr float MORTAR_MIN_RANGE = 18.0f; // avoid point-blank fire
  static constexpr float MORTAR_MAX_RANGE =
      95.0f; // long-range support envelope
  static constexpr float MORTAR_SCATTER_PER_M =
      0.05f; // scatter growth by distance
  static constexpr float MORTAR_MIN_SCATTER =
      1.2f; // base inaccuracy at short range
  static constexpr float MORTAR_MAX_SCATTER = 9.0f; // cap for long-range drift
  static constexpr float MORTAR_PROJ_MAX_LIFETIME =
      7.0f; // long airtime for high arc

  // ── Gas Grenade Constants ────────────────────────────────────────
  // Payload types (orthogonal to delivery: grenade/mortar/airstrike)
  static constexpr uint8_t PAYLOAD_KINETIC = 0;  // Frag / HE (default)
  static constexpr uint8_t PAYLOAD_SMOKE = 1;    // Vision block
  static constexpr uint8_t PAYLOAD_TEAR_GAS = 2; // Suppression + morale drain
  static constexpr uint8_t PAYLOAD_TOXIC = 3;    // Damage over time

  // Cloud parameters per payload type
  static constexpr float GAS_CLOUD_RADIUS_GRENADE =
      5.0f; // 5m radius for grenade delivery
  static constexpr float GAS_CLOUD_RADIUS_MORTAR =
      10.0f; // 10m radius for mortar delivery
  static constexpr float GAS_CLOUD_DENSITY = 0.8f; // Initial concentration

  // Gas effect rates (per second, scaled by density)
  static constexpr float GAS_TOXIC_DPS = 0.05f; // 20s to kill at full density
  static constexpr float GAS_TEAR_SUPP_RATE = 0.2f; // suppression gain per sec
  static constexpr float GAS_TEAR_MORALE_DRAIN = 0.05f; // morale loss per sec
  static constexpr float GAS_DENSITY_THRESHOLD = 0.1f;  // below this, no effect
  static constexpr float GAS_PANIC_HEALTH =
      0.3f; // flee from toxic below this HP

  // ── Tactical AI Constants ────────────────────────────────────────
  static constexpr float COVER_SEEK_RADIUS =
      10.0f; // meters to search for cover (riflemen)
  static constexpr float OVERWATCH_SEEK_RADIUS =
      20.0f; // meters to search (MG/marksman)
  static constexpr float SUPPRESSION_COVER_THRESHOLD =
      0.4f; // seek cover above this
  static constexpr float HEALTH_COVER_THRESHOLD =
      0.4f; // seek cover below this HP
  static constexpr float COVER_GOOD_THRESHOLD = 0.3f; // already in good cover
  static constexpr float EYE_HEIGHT = 1.5f; // meters above ground for LOS
  static constexpr int FOF_RAY_COUNT = 16;  // field-of-fire ray directions
  static constexpr float FOF_RAY_RANGE_M =
      25.0f; // field-of-fire check range (m)
  static constexpr int MAX_SHOOTABILITY_ENEMIES =
      5; // max enemies for LOS check per candidate
  static constexpr float TPOS_COVER_WEIGHT = 20.0f; // base score for cover=1.0
  static constexpr float TPOS_SHOOT_WEIGHT =
      25.0f; // base score for shootability=1.0
  static constexpr float TPOS_FOF_WEIGHT = 10.0f; // base field-of-fire score
  static constexpr float TPOS_HEIGHT_WEIGHT =
      8.0f;                                       // base height advantage score
  static constexpr float TPOS_DIST_WEIGHT = 1.0f; // distance penalty per meter
  static constexpr int FLANK_DETECT_ALLIES =
      2; // allies on same target to trigger
  static constexpr float FLANK_PERP_DIST =
      20.0f; // perpendicular flank distance (m) — was 12, too small to see at
             // RTS zoom
  static constexpr float FLANK_MIN_MOVE_DIST =
      8.0f; // min distance to flank pos or don't bother (trivial flanks look
            // like jitter)
  static constexpr float SQUAD_COHESION_RADIUS =
      15.0f; // meters for squad count
  static constexpr float INFLUENCE_UPDATE_INTERVAL =
      0.5f; // seconds between influence updates

  // ── Peek Behavior Constants ───────────────────────────────────────
  static constexpr float PEEK_OFFSET_DIST =
      1.0f; // meters sidestep from cover center
  static constexpr float PEEK_HIDE_MIN = 0.8f; // hide duration at 0 suppression
  static constexpr float PEEK_HIDE_MAX =
      2.0f; // hide duration at 1.0 suppression
  static constexpr float PEEK_EXPOSE_MIN =
      0.5f; // peek duration at 1.0 suppression
  static constexpr float PEEK_EXPOSE_MAX =
      1.5f; // peek duration at 0 suppression

  // ── Posture Transition Constants ─────────────────────────────────
  static constexpr float POSTURE_STAND_TO_CROUCH = 0.4f; // seconds
  static constexpr float POSTURE_CROUCH_TO_STAND = 0.35f;
  static constexpr float POSTURE_CROUCH_TO_PRONE = 0.6f;
  static constexpr float POSTURE_PRONE_TO_CROUCH = 0.7f;
  static constexpr float POSTURE_STAND_TO_PRONE = 0.9f; // sequential
  static constexpr float POSTURE_PRONE_TO_STAND = 1.0f; // sequential

  // ── Personality Constants ────────────────────────────────────────
  static constexpr float BERSERK_SPEED_MULT = 1.8f; // charge speed multiplier
  static constexpr float BERSERK_ACCURACY_MULT =
      0.3f; // worse accuracy (lower = worse)
  static constexpr float BERSERK_COOLDOWN_MULT =
      0.5f; // faster fire (lower = faster)
  static constexpr float FROZEN_RECOVERY_TIME =
      2.0f; // seconds to unfreeze after morale recovers

  // ── Tunable Parameters (runtime-adjustable via tuning panel) ─────
  // Initialized from the static constexpr defaults above.
  // Movement
  float _tune_move_speed = MOVE_SPEED;
  float _tune_separation_radius = SEPARATION_RADIUS;
  float _tune_separation_force = SEPARATION_FORCE;
  float _tune_arrive_dist = ARRIVE_DIST;
  float _tune_centroid_anchor = CENTROID_ANCHOR_BLEND;
  float _tune_catchup_weight = CATCHUP_WEIGHT;
  float _tune_combat_drift = COMBAT_FORMATION_DRIFT;
  float _tune_max_step_height = MAX_STEP_HEIGHT;
  // Locomotion
  float _tune_turn_rate_base = LOCO_TURN_RATE_BASE;
  float _tune_turn_rate_bonus = LOCO_TURN_RATE_BONUS;
  float _tune_face_smooth_rate = FACE_SMOOTH_RATE;
  float _tune_dead_band_sq = LOCO_DEAD_BAND_SQ;
  // Context Steering
  float _tune_steer_order = STEER_ORDER_WEIGHT;
  float _tune_steer_flow = STEER_FLOW_WEIGHT;
  float _tune_steer_pheromone = STEER_PHEROMONE_WEIGHT;
  float _tune_steer_danger = STEER_DANGER_SCALE;
  float _tune_steer_obstacle_dist = STEER_OBSTACLE_DIST;
  float _tune_steer_sample_dist = STEER_SAMPLE_DIST;
  float _tune_steer_temporal = STEER_TEMPORAL_ALPHA;
  float _tune_steer_border_dist = STEER_MAP_BORDER_DIST;
  // Combat
  float _tune_decision_interval = DECISION_INTERVAL;
  float _tune_reload_time = RELOAD_TIME;
  float _tune_suppression_decay = SUPPRESSION_DECAY;
  float _tune_settle_spread = SETTLE_SPREAD_MULT;
  float _tune_near_miss_dist = PROJ_NEAR_MISS_DIST;
  float _tune_near_miss_supp = PROJ_NEAR_MISS_SUPP;
  float _tune_hit_supp = PROJ_HIT_SUPP;
  float _tune_wall_pen_penalty = WALL_PEN_SCORE_PENALTY;
  // Tactical AI
  float _tune_cover_seek_radius = COVER_SEEK_RADIUS;
  float _tune_supp_cover_thresh = SUPPRESSION_COVER_THRESHOLD;
  float _tune_peek_offset = PEEK_OFFSET_DIST;
  float _tune_peek_hide_min = PEEK_HIDE_MIN;
  float _tune_peek_hide_max = PEEK_HIDE_MAX;
  float _tune_peek_expose_min = PEEK_EXPOSE_MIN;
  float _tune_peek_expose_max = PEEK_EXPOSE_MAX;
  // Explosions
  float _tune_grenade_dmg_radius = GRENADE_DAMAGE_RADIUS;
  float _tune_grenade_max_dmg = GRENADE_MAX_DAMAGE;
  float _tune_mortar_dmg_radius = MORTAR_DAMAGE_RADIUS;
  float _tune_mortar_max_dmg = MORTAR_MAX_DAMAGE;
  float _tune_mortar_max_scatter = MORTAR_MAX_SCATTER;
  // ORCA
  float _tune_orca_agent_radius = ORCA_AGENT_RADIUS;
  float _tune_orca_time_horizon = ORCA_TIME_HORIZON;
  float _tune_orca_neighbor_dist = ORCA_NEIGHBOR_DIST;
  float _tune_orca_wall_probe = ORCA_WALL_PROBE_DIST;
  // Flow field weights
  float _tune_flow_weight_squad = FLOW_WEIGHT_SQUAD;
  float _tune_flow_weight_move = FLOW_WEIGHT_MOVE;
  // Debug
  bool _tune_debug_logging = false;

  // ── Squad Data ──────────────────────────────────────────────────
  struct SquadData {
    Vector3 rally_point;
    Vector3 advance_dir;
    bool active = false;
    uint8_t team = 0; // team affiliation (set at spawn from first member)
    float advance_offset = 0.0f; // meters ahead of centroid along advance_dir
    FormationType formation = FORM_LINE;
    float formation_spread =
        3.0f; // meters between goal points in formation (was 8.0→5.0→3.0)
  };
  SquadData _squads[MAX_SQUADS];

  // ── Capture Points ──────────────────────────────────────────────
  static constexpr int MAX_CAPTURE_POINTS = 8;
  static constexpr float CAPTURE_RADIUS =
      12.0f; // meters (wide enough for formations)
  static constexpr float CAPTURE_RATE =
      0.05f; // progress/sec per unit (20s for 1 unit, 10s for 2)
  static constexpr float CAPTURE_DECAY =
      0.02f; // decay/sec when no one present (50s to fully decay)

  struct CapturePointData {
    float x = 0.0f, z = 0.0f;
    int8_t owner_team = 0;     // 0=neutral, 1=team1, 2=team2
    float progress = 0.0f;     // 0..1 toward capturing_team
    int8_t capturing_team = 0; // which team is currently capping
    bool contested = false;    // both teams present
    bool active = false;
  };
  CapturePointData _capture_points[MAX_CAPTURE_POINTS];
  int _capture_count = 0;
  mutable std::vector<int32_t> _capture_nearby; // scratch buffer

  void _tick_capture_points(float delta);
  void _tick_location_stats();

  // ── Squad Centroid Cache (rebuilt each tick) ─────────────────────
  Vector3 _squad_centroids[MAX_SQUADS];
  int _squad_alive_counts[MAX_SQUADS] = {};
  int _squad_spawn_counter[MAX_SQUADS] =
      {}; // monotonic counter for stable member indices
  bool _squad_has_flanker[MAX_SQUADS] = {}; // any unit in squad is ST_FLANKING

  // ── Spatial Hash ────────────────────────────────────────────────
  std::vector<int32_t> _spatial_cells;
  std::vector<int32_t> _spatial_next;
  int _spatial_w = 0, _spatial_h = 0;
  float _map_w = 0.0f, _map_h = 0.0f;
  float _map_half_w = 0.0f, _map_half_h = 0.0f;

  void _rebuild_spatial_hash();
  void _get_units_in_radius(float cx, float cz, float radius,
                            std::vector<int32_t> &out) const;

  // ── External references ─────────────────────────────────────────
  Ref<GpuTacticalMap> _gpu_map;

  // ── Influence Map integration ────────────────────────────────────
  Ref<InfluenceMapCPP> _influence_map[2]; // index 0 = team 1, index 1 = team 2
  float _influence_timer = 0.0f;

  // ── Unified Pheromone integration (Combat + Economy) ────────────
  Ref<PheromoneMapCPP>
      _pheromones[2]; // per-team unified maps (15 channels each)
  float _pheromone_tick_timer = 0.0f;
  static constexpr float PHEROMONE_TICK_INTERVAL = 0.033f; // ~30Hz CA update

  // Pheromone SoA (per-unit tracking for deposit logic)
  std::vector<float> _sustained_fire_timer; // seconds of continuous MG fire
  std::vector<float>
      _survived_supp_timer; // seconds survived under heavy suppression
  std::vector<float> _prev_pos_x,
      _prev_pos_z; // previous tick position (for trail deposits)

  // Pheromone tick + deposit helpers
  void _tick_pheromones(float delta);
  void _pheromone_deposit_danger(int32_t killed_unit, int32_t killer,
                                 bool was_ambush);
  void _pheromone_deposit_explosion(const Vector3 &pos, float blast_radius,
                                    uint8_t team_that_fired);

  // Role-specific pheromone response weights
  struct RolePheromoneWeights {
    float danger;      // tactical position penalty multiplier
    float suppression; // advance-when-suppressed multiplier
    float contact;     // alertness multiplier
    float rally;       // rally-follow multiplier
    float fear;        // morale drain multiplier (negative = move toward)
    float courage;     // morale recovery multiplier
    float safe_route;  // path preference multiplier
    float flank_opp;   // flank probability multiplier
    float strategic;   // LLM strategic pull multiplier
  };
  static RolePheromoneWeights _role_pheromone_weights(uint8_t role);

  // ── Posture Profile ─────────────────────────────────────────────
  struct PostureProfile {
    float eye_height;      // LOS origin above ground
    float muzzle_height;   // projectile spawn height
    float center_mass;     // target "chest" for incoming fire
    float hit_radius;      // body sphere for projectile hits
    float speed_mult;      // movement speed multiplier
    float accuracy_mult;   // spread multiplier (lower = tighter)
    float supp_decay_mult; // suppression decay rate multiplier
    int body_voxels;       // voxels above feet for collision check
    float peek_offset;     // sidestep distance (0 = no peek)
  };
  static PostureProfile _posture_profile(uint8_t posture);

  // Posture-aware height helpers (inline, zero overhead)
  float _eye_height(int32_t i) const {
    return _posture_profile(_posture[i]).eye_height;
  }
  float _muzzle_height(int32_t i) const {
    return _posture_profile(_posture[i]).muzzle_height;
  }
  float _center_mass(int32_t i) const {
    return _posture_profile(_posture[i]).center_mass;
  }
  float _hit_radius_for(int32_t i) const {
    return _posture_profile(_posture[i]).hit_radius;
  }
  float _speed_mult(int32_t i) const {
    return _posture_profile(_posture[i]).speed_mult;
  }
  float _accuracy_mult(int32_t i) const {
    return _posture_profile(_posture[i]).accuracy_mult;
  }
  int _body_voxels(int32_t i) const {
    return _posture_profile(_posture[i]).body_voxels;
  }
  float _peek_offset_for(int32_t i) const {
    return _posture_profile(_posture[i]).peek_offset;
  }

  void _tick_posture(float delta);
  void _request_posture(int32_t i, uint8_t target);
  float _get_posture_transition_time(uint8_t from, uint8_t to) const;

  // Fog of war helpers (inline for zero overhead)
  bool _team_can_see(int team, int32_t enemy_id) const {
    return (_team_vis[team][enemy_id >> 6] >> (enemy_id & 63)) & 1;
  }
  void _team_set_vis(int team, int32_t enemy_id) {
    _team_vis[team][enemy_id >> 6] |= (1ULL << (enemy_id & 63));
  }
  void _team_clear_vis(int team, int32_t enemy_id) {
    _team_vis[team][enemy_id >> 6] &= ~(1ULL << (enemy_id & 63));
  }
  float _time_since_seen(int32_t id) const {
    return _game_time - _last_seen_time[id];
  }
  static float _role_detect_range(uint8_t role);

  // ── Tick subsystems ─────────────────────────────────────────────
  void _sync_soa_to_flecs();
  void _sync_flecs_to_soa();
  void _sys_movement_climb_fall(flecs::iter &it,
                                const ecs::LegacyIndex *idx_comp,
                                const ecs::MovementBridging *mb_comp);
  void _sys_movement_steering(flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                              ecs::DesiredVelocity *dv_comp);
  void _sys_movement_orca(flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                          ecs::DesiredVelocity *dv_comp);
  void _sys_movement_apply(flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                           const ecs::DesiredVelocity *dv_comp);

  void _sys_decisions(flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                      ecs::State *state_comp, ecs::Posture *posture_comp);
  void _sys_combat(flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   ecs::State *state_comp, ecs::CombatBridging *cb_comp,
                   const ecs::Transform3DData *xform_comp,
                   const ecs::Role *role_comp, ecs::AmmoInfo *ammo_comp,
                   const ecs::Cooldowns *cd_comp,
                   const ecs::Morale *morale_comp);
  void _sys_projectiles(flecs::iter &it, const ecs::ProjectileData *p_data,
                        ecs::ProjectileFlight *p_flight);
  void _sys_visibility(flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                       const ecs::Transform3DData *xform_comp,
                       const ecs::Role *role_comp);

  // ── Fast-path Flecs state systems ───────────────────────────────
  void _sys_suppression_decay(flecs::iter &it, ecs::Suppression *supp_comp,
                              const ecs::Posture *posture_comp);
  void _sys_morale(flecs::iter &it, const ecs::LegacyIndex *idx_comp,
                   ecs::Morale *morale_comp, const ecs::Suppression *supp_comp);
  void _sys_reload(flecs::iter &it, ecs::State *state_comp,
                   ecs::CombatBridging *cb_comp, ecs::AmmoInfo *ammo_comp);
  void _sys_posture(flecs::iter &it, ecs::Posture *posture_comp);

  // ── Helpers ─────────────────────────────────────────────────────
  bool _valid(int32_t id) const { return id >= 0 && id < _count; }
  void _clamp_to_terrain(int32_t i);
  bool _check_los(int32_t from, int32_t to) const;
  float _check_wall_energy_cost(int32_t from, int32_t to) const;
  float _distance_sq(int32_t a, int32_t b) const;

  // ── Tactical AI helpers ─────────────────────────────────────────
  float _score_target(int32_t unit, int32_t candidate) const;
  float _role_optimal_range(uint8_t role) const;
  void _find_tactical_position(int32_t unit);
  float _compute_field_of_fire(float wx, float wy, float wz) const;
  void _tick_peek(float delta);
  bool _should_flank(int32_t unit) const;
  Vector3 _compute_flank_destination(int32_t unit) const;
  bool _should_suppress(int32_t unit) const;
  void _update_attackers_count();
  void _update_cover_values();
  void _update_squad_cohesion(int32_t unit);
  void _tick_influence_maps();
  void _compute_squad_centroids();
  mutable std::vector<int32_t> _tac_nearby; // reusable scratch buffer

  // ── Role defaults ───────────────────────────────────────────────
  static float _role_range(uint8_t role);
  static float _role_cooldown(uint8_t role);
  static float _role_accuracy(uint8_t role);
  static int16_t _role_mag_size(uint8_t role);

  // ── Role tactical position weights ────────────────────────────────
  struct TacticalPositionWeights {
    float cover;
    float shootability;
    float field_of_fire;
    float height;
    float distance_cost;
    float search_radius;
  };
  static TacticalPositionWeights _role_tpos_weights(uint8_t role);

  // ── Role ballistics (projectile physics) ────────────────────────
  struct RoleBallistics {
    float muzzle_velocity; // m/s
    float base_spread;     // radians half-angle
    float energy;          // penetration energy
    float damage;          // 0-1 damage per hit
  };
  static RoleBallistics _role_ballistics(uint8_t role);
  static float _role_settle_time(uint8_t role);
  static float _role_deploy_time(uint8_t role);
  float _compute_aim_quality(int32_t unit) const;

  // ── Personality morale modifiers ──────────────────────────────
  struct PersonalityMoraleModifiers {
    float suppression_decay_mult;
    float isolation_decay_mult;
    float ally_recovery_mult;
    float break_threshold;
    float recovery_threshold;
  };
  static PersonalityMoraleModifiers _personality_morale(uint8_t pers);

  // Pool management
  int _proj_active_count = 0; // Keeping count for GDScript inspector metrics

  void _spawn_projectile(int32_t shooter_id, int32_t target_id);
  void _despawn_projectile(flecs::entity e);
  void _proj_check_unit_hits(flecs::entity e, const ecs::ProjectileData &data,
                             const ecs::ProjectileFlight &flight);
  void _proj_apply_near_miss(const ecs::ProjectileData &data,
                             const ecs::ProjectileFlight &flight);
  void _explode(flecs::entity e, const ecs::ProjectileData &data,
                const ecs::ProjectileFlight &flight);
  void _tick_gas_effects(float delta);
  std::vector<int32_t> _explosion_nearby; // reusable scratch buffer

  // ── Muzzle flash events (ring buffer for GDScript VFX) ──────────
  static constexpr int MAX_MUZZLE_EVENTS = 128;
  struct MuzzleFlashEvent {
    float pos_x, pos_y, pos_z; // muzzle world position
    float face_x, face_z;      // firing direction (XZ)
    uint8_t team;
    uint8_t role;
  };
  std::vector<MuzzleFlashEvent> _muzzle_events;
  int _muzzle_event_count = 0;

  // ── Impact events (ring buffer for GDScript VFX) ────────────────
  static constexpr int MAX_INLINE_DEBRIS = 16;
  struct ImpactEvent {
    Vector3 position;
    Vector3 normal;
    uint8_t material;     // dominant material (actual for bullets, dominant for
                          // explosions)
    uint8_t type;         // 0=bullet, 1=explosion, 2=gas
    uint8_t payload;      // 0=kinetic, 1=smoke, 2=tear gas, 3=toxic
    float blast_radius;   // >0 for type==1 or type==2
    int destroyed;        // voxels destroyed (explosions only)
    uint8_t debris_count; // inline debris samples (max MAX_INLINE_DEBRIS)
    Vector3 debris_positions[MAX_INLINE_DEBRIS];
    uint8_t debris_materials[MAX_INLINE_DEBRIS];
    int mat_histogram[16]; // per-material destroy counts (explosions only)
  };
  std::vector<ImpactEvent> _impact_events;
  int _impact_count = 0;
  void _record_impact(const Vector3 &pos, const Vector3 &normal, uint8_t mat,
                      uint8_t type = 0);
  void _record_explosion_impact(const Vector3 &pos, float blast_radius,
                                const Dictionary &destroy_data,
                                uint8_t payload_type = 0);

  // ── Voxel health tracking (lazy map for bullet damage) ──────────
  std::unordered_map<uint64_t, float> _voxel_hp;
  static inline uint64_t _pack_voxel_key(int x, int y, int z) {
    return ((uint64_t)(uint16_t)x << 32) | ((uint64_t)(uint16_t)y << 16) |
           (uint16_t)z;
  }
  void _damage_voxel(int x, int y, int z, float dmg);

  // ── Fast RNG ────────────────────────────────────────────────────
  uint64_t _rng_state = 0x12345678ABCDEF01ULL;
  int64_t _original_seed = 0x12345678ABCDEF01LL;
  float _randf(); // 0.0-1.0

public:
  void set_seed(int64_t seed);
  int64_t get_seed() const;

  // ── Stats ───────────────────────────────────────────────────────
  float _last_tick_ms = 0.0f;
  int _los_checks = 0;
  int _spatial_queries = 0;
  int _wall_pen_count = 0;

  // ── Fog-of-War Diagnostic Counters (reset each tick) ─────────────
  int _fow_targets_skipped = 0;    // invisible targets skipped in target acq
  int _fow_suppressive_shots = 0;  // suppressive fire at last-known pos
  int _fow_vis_checks = 0;         // LOS checks in _tick_visibility
  int _fow_vis_hits = 0;           // successful visibility detections
  int _fow_contacts_gained = 0;    // targets that became visible this tick
  int _fow_contacts_lost = 0;      // targets that expired beyond CONTACT_DECAY
  int _fow_influence_filtered = 0; // enemies filtered from influence maps

  // ── Cumulative Fog-of-War Stats (across entire sim) ──────────────
  int _fow_total_suppressive = 0;
  int _fow_total_skipped = 0;
  int _fow_total_vis_checks = 0;
  int _fow_total_vis_hits = 0;

  // ── Engagement Quality Counters (reset each tick) ────────────────
  int _engagements_this_tick = 0;    // units that acquired a target
  int _engagements_visible = 0;      // targets acquired via direct LOS
  int _engagements_suppressive = 0;  // targets acquired via suppressive fire
  int _wall_pen_blocked = 0;         // visible but blocked by walls
  int _mortar_rounds_fired_tick = 0; // mortar rounds fired this tick
  int _mortar_impacts_tick = 0;      // mortar explosions this tick
  int _mortar_suppression_events_tick =
      0;                      // units affected by mortar suppression
  int _mortar_kills_tick = 0; // kills caused by mortar explosions this tick
  int _mortar_total_rounds_fired = 0; // cumulative rounds fired
  int _mortar_total_impacts = 0;      // cumulative impacts
  int _mortar_total_suppression_events =
      0;                       // cumulative suppression applications
  int _mortar_total_kills = 0; // cumulative mortar kills

  // ── Climb/Fall Event Counters ──────────────────────────────────
  int _climb_started_tick = 0;       // climb events this tick (reset each tick)
  int _fall_started_tick = 0;        // fall events this tick
  int _fall_damage_tick = 0;         // fall damage events this tick
  int _total_climb_events = 0;       // cumulative climb events
  int _total_fall_events = 0;        // cumulative fall events
  int _total_fall_damage_events = 0; // cumulative fall damage events

  // ── Location Quality Metrics (recomputed each tick) ────────────
  float _avg_dist_to_slot_t1 = 0.0f; // team 1 avg distance to formation slot
  float _avg_dist_to_slot_t2 = 0.0f;
  float _max_dist_to_slot_t1 = 0.0f;
  float _max_dist_to_slot_t2 = 0.0f;
  float _avg_squad_spread = 0.0f; // avg distance between members and centroid
  int _units_beyond_20m = 0;      // units >20m from their slot
  float _avg_inter_team_dist =
      0.0f; // avg distance between nearest enemies (per unit)

  // ── Per-State Distance Breakdown (avg dist to slot by state) ────
  float _dist_by_state[ST_COUNT] = {}; // avg dist per state
  int _count_by_state[ST_COUNT] = {};  // count per state (for averaging)

  // ── Order Distribution ──────────────────────────────────────────
  int _order_follow_squad = 0; // units with ORDER_FOLLOW_SQUAD
  int _order_other = 0;        // units with other orders

  // ── Movement Vector Decomposition (avg magnitudes) ──────────────
  float _avg_formation_pull = 0.0f; // avg magnitude of formation velocity
  float _avg_flow_push = 0.0f;      // avg magnitude of flow field velocity
  float _avg_threat_push = 0.0f; // avg magnitude of threat avoidance velocity
  float _avg_total_speed = 0.0f; // avg total velocity magnitude

  // ── Advance Offset Tracking ─────────────────────────────────────
  float _avg_advance_offset = 0.0f; // avg advance_offset across active squads
  float _max_advance_offset = 0.0f; // max advance_offset

  // ── Per-Subsystem Profiling ──────────────────────────────────────
  double _sub_us[SUB_COUNT] = {};  // last frame microseconds
  double _sub_ema[SUB_COUNT] = {}; // exponential moving average
  static constexpr double PROF_EMA_ALPHA = 0.05;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::SimulationServer::Role)
VARIANT_ENUM_CAST(godot::SimulationServer::UnitState)
VARIANT_ENUM_CAST(godot::SimulationServer::OrderType)
VARIANT_ENUM_CAST(godot::SimulationServer::FormationType)
VARIANT_ENUM_CAST(godot::SimulationServer::Personality)
VARIANT_ENUM_CAST(godot::SimulationServer::Posture)
VARIANT_ENUM_CAST(godot::SimulationServer::PheromoneChannel)
VARIANT_ENUM_CAST(godot::SimulationServer::MovementMode)

#endif // SIMULATION_SERVER_H
