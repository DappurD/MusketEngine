#include "theater_commander.h"
#include "simulation_server.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/time.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════════
//  Static Data
// ═══════════════════════════════════════════════════════════════════════

TheaterCommander *TheaterCommander::_instances[2] = { nullptr, nullptr };

const char *TheaterCommander::AXIS_NAMES[AXIS_COUNT] = {
    "aggression",
    "concentration",
    "tempo",
    "risk_tolerance",
    "exploitation",
    "terrain_control",
    "medical_priority",
    "suppression_dominance",
    "intel_coverage"
};

// ═══════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════

TheaterCommander::TheaterCommander() {
    // Defer per-team slot assignment to setup() where _team is known
    for (int i = 0; i < AXIS_COUNT; i++) {
        _weight_modifiers[i] = 1.0f;
        _axis_scores[i] = 0.5f;
        _posture_cooldowns[i] = 0.0f;
    }
    std::memset(_sensors, 0, sizeof(_sensors));
    std::memset(_casualty_history, 0, sizeof(_casualty_history));
    _init_axis_configs();
}

TheaterCommander::~TheaterCommander() {
    for (int i = 0; i < 2; i++) {
        if (_instances[i] == this) {
            _instances[i] = nullptr;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::setup(int team, float map_w, float map_h) {
    _team = team;
    _map_w = map_w;
    _map_h = map_h;
    // Register in per-team slot
    int idx = team - 1;
    if (idx >= 0 && idx < 2) {
        _instances[idx] = this;
    }
    _tick_timer = 0.0f;
    _total_elapsed = 0.0f;
    _current_posture = -1;
    _posture_time = 0.0f;
    _history_head = 0;
    _last_alive_count = 0;
    _last_advance_time = 0.0f;
    _prev_snapshot = BattlefieldSnapshot();
    _snapshot = BattlefieldSnapshot();
    for (int i = 0; i < AXIS_COUNT; i++) {
        _axis_scores[i] = 0.5f;
        _posture_cooldowns[i] = 0.0f;
    }
}

void TheaterCommander::set_influence_map(Ref<InfluenceMapCPP> map) {
    _influence_map = map;
}

void TheaterCommander::set_tick_interval(float interval) {
    _tick_interval = std::max(0.1f, interval);
}

// ═══════════════════════════════════════════════════════════════════════
//  Axis Configuration — Response Curve Tuning
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::_init_axis_configs() {
    // AGGRESSION: 3 sensors
    // 0: force_ratio       → Logistic steep at 0.5 (advantage triggers aggression)
    // 1: momentum (advance) → Linear (advancing = more aggressive)
    // 2: avg_morale        → Logistic steep at 0.4 (low morale kills aggression)
    _axis_configs[AXIS_AGGRESSION] = {
        3,
        {
            { CURVE_LOGISTIC,  10.0f, 0.5f, 0.0f },
            { CURVE_LINEAR,     2.0f, 0.5f, 0.0f },
            { CURVE_LOGISTIC,   8.0f, 0.4f, 0.0f },
        },
        { 0.45f, 0.25f, 0.30f }
    };

    // CONCENTRATION: 2 sensors
    // 0: 1 - active_front_ratio → Quadratic (fewer fronts = more concentrated)
    // 1: reserve_ratio          → Linear
    _axis_configs[AXIS_CONCENTRATION] = {
        2,
        {
            { CURVE_QUADRATIC, 1.5f, 0.2f, 0.0f },
            { CURVE_LINEAR,    1.0f, 0.0f, 0.0f },
            {},
        },
        { 0.6f, 0.4f, 0.0f }
    };

    // TEMPO: 2 sensors
    // 0: time_since_advance → Logistic (urgency rises monotonically with stall)
    // 1: enemy_weakness     → Linear (weak enemy = push now)
    _axis_configs[AXIS_TEMPO] = {
        2,
        {
            { CURVE_LOGISTIC,  8.0f, 0.5f, 0.0f },
            { CURVE_LINEAR,    1.5f, 0.0f, 0.0f },
            {},
        },
        { 0.5f, 0.5f, 0.0f }
    };

    // RISK TOLERANCE: 2 sensors
    // 0: casualty_rate → Inverted logistic (heavy losses = LOW tolerance)
    // 1: reserve_ratio → Linear (more reserves = can afford risk)
    _axis_configs[AXIS_RISK_TOLERANCE] = {
        2,
        {
            { CURVE_LOGISTIC, -10.0f, 0.5f, 0.0f },
            { CURVE_LINEAR,    1.2f,  0.1f, 0.0f },
            {},
        },
        { 0.6f, 0.4f, 0.0f }
    };

    // EXPLOITATION: 2 sensors
    // 0: newly_opened_flanks → Steep logistic (step-like at 0.15)
    // 1: enemy_retreating    → Logistic steep at 0.2
    _axis_configs[AXIS_EXPLOITATION] = {
        2,
        {
            { CURVE_LOGISTIC, 15.0f, 0.15f, 0.0f },
            { CURVE_LOGISTIC, 12.0f, 0.2f,  0.0f },
            {},
        },
        { 0.5f, 0.5f, 0.0f }
    };

    // TERRAIN CONTROL: 2 sensors
    // 0: poi_ownership_ratio    → Linear
    // 1: defensive_held_ratio   → Linear with offset
    _axis_configs[AXIS_TERRAIN_CONTROL] = {
        2,
        {
            { CURVE_LINEAR, 1.0f, 0.0f, 0.0f },
            { CURVE_LINEAR, 0.8f, 0.2f, 0.0f },
            {},
        },
        { 0.6f, 0.4f, 0.0f }
    };

    // MEDICAL PRIORITY: 2 sensors
    // 0: medical_ratio     → Logistic steep at 0.3 (30% wounded triggers urgency)
    // 1: 1 - medic_avail   → Linear (fewer medics = higher priority)
    _axis_configs[AXIS_MEDICAL_PRIORITY] = {
        2,
        {
            { CURVE_LOGISTIC, 12.0f, 0.3f, 0.0f },
            { CURVE_LINEAR,    1.0f, 0.0f, 0.0f },
            {},
        },
        { 0.7f, 0.3f, 0.0f }
    };

    // SUPPRESSION DOMINANCE: 2 sensors
    // 0: mg_ammo_ratio     → Quadratic (more ammo = more investment possible)
    // 1: enemy_exposure     → Quadratic (exposed enemies = good targets)
    _axis_configs[AXIS_SUPPRESSION_DOMINANCE] = {
        2,
        {
            { CURVE_QUADRATIC, 1.0f, 0.5f, 0.0f },
            { CURVE_QUADRATIC, 1.5f, 0.0f, 0.0f },
            {},
        },
        { 0.5f, 0.5f, 0.0f }
    };

    // INTEL COVERAGE: 1 sensor
    // 0: intel_ratio → Inverted logistic (low visibility = high intel urgency)
    _axis_configs[AXIS_INTEL_COVERAGE] = {
        1,
        {
            { CURVE_LOGISTIC, -8.0f, 0.4f, 0.0f },  // spikes when visibility < 40%
            {},
            {},
        },
        { 1.0f, 0.0f, 0.0f }
    };
}

// ═══════════════════════════════════════════════════════════════════════
//  Response Curve Functions
// ═══════════════════════════════════════════════════════════════════════

float TheaterCommander::_eval_curve(const CurveParams &curve, float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    switch (curve.type) {
        case CURVE_LOGISTIC:  return _logistic(x, curve.p0, curve.p1);
        case CURVE_GAUSSIAN:  return _gaussian(x, curve.p0, curve.p1);
        case CURVE_QUADRATIC: return _quadratic(x, curve.p0, curve.p1, curve.p2);
        case CURVE_LINEAR:    return _linear(x, curve.p0, curve.p1);
        default:              return x;
    }
}

float TheaterCommander::_logistic(float x, float k, float midpoint) {
    return 1.0f / (1.0f + std::exp(-k * (x - midpoint)));
}

float TheaterCommander::_gaussian(float x, float peak, float sigma) {
    float diff = x - peak;
    float s2 = std::max(sigma * sigma, 1e-6f);
    return std::exp(-(diff * diff) / (2.0f * s2));
}

float TheaterCommander::_quadratic(float x, float a, float b, float c) {
    return std::clamp(a * x * x + b * x + c, 0.0f, 1.0f);
}

float TheaterCommander::_linear(float x, float slope, float offset) {
    return std::clamp(slope * x + offset, 0.0f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════
//  Main Tick
// ═══════════════════════════════════════════════════════════════════════

bool TheaterCommander::tick(float delta) {
    _total_elapsed += delta;
    _tick_timer -= delta;

    // Decay posture cooldowns every frame
    for (int i = 0; i < AXIS_COUNT; i++) {
        _posture_cooldowns[i] = std::max(0.0f, _posture_cooldowns[i] - delta);
    }

    if (_tick_timer > 0.0f) {
        return false;
    }
    _tick_timer = _tick_interval;

    uint64_t start_us = Time::get_singleton()->get_ticks_usec();

    // Phase 1: Gather battlefield data
    _prev_snapshot = _snapshot;
    _compute_snapshot();

    // Phase 2: Compute per-axis sensor values
    _compute_sensors();

    // Phase 3: Evaluate curves and aggregate
    _aggregate_scores();

    // Phase 4: Momentum and hysteresis
    _apply_momentum_and_hysteresis();

    _last_tick_ms = float(Time::get_singleton()->get_ticks_usec() - start_us) / 1000.0f;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Battlefield Snapshot — Data Gathering
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::_compute_snapshot() {
    SimulationServer *sim = SimulationServer::get_singleton();
    if (!sim) return;

    BattlefieldSnapshot &s = _snapshot;
    s = BattlefieldSnapshot();  // reset

    int count = sim->get_unit_count();
    int enemy_team = (_team == 1) ? 2 : 1;

    float morale_sum = 0.0f;
    float suppression_sum = 0.0f;
    int enemy_retreating = 0;
    int enemy_not_in_cover = 0;
    int mg_alive = 0;
    float mg_ammo_sum = 0.0f;

    // Single O(N) pass over all units
    for (int i = 0; i < count; i++) {
        if (!sim->is_alive(i)) continue;

        int unit_team = sim->get_team(i);
        int unit_state = sim->get_state(i);

        if (unit_team == _team) {
            s.friendly_alive++;
            morale_sum += sim->get_morale(i);
            suppression_sum += sim->get_suppression(i);

            if (unit_state == SimulationServer::ST_DOWNED) {
                s.downed_count++;
            }
            float hp = sim->get_health(i);
            if (hp < 0.7f && hp > 0.0f &&
                unit_state != SimulationServer::ST_DOWNED &&
                unit_state != SimulationServer::ST_DEAD) {
                s.wounded_count++;
            }

            int role = sim->get_role(i);
            if (role == SimulationServer::ROLE_MEDIC) {
                s.medic_count++;
            }
            if (role == SimulationServer::ROLE_MG) {
                mg_alive++;
                int ammo = sim->get_ammo(i);
                int mag = sim->get_mag_size(i);
                mg_ammo_sum += (mag > 0) ? (float)ammo / (float)mag : 0.0f;
            }
        } else if (unit_team == enemy_team) {
            // Fog of war: only count visible enemies
            if (!sim->team_can_see(_team, i)) continue;
            s.enemy_alive++;
            if (unit_state == SimulationServer::ST_RETREATING) {
                enemy_retreating++;
            }
            if (unit_state != SimulationServer::ST_IN_COVER) {
                enemy_not_in_cover++;
            }
        }
    }

    // Force ratio
    int total_alive = s.friendly_alive + s.enemy_alive;
    s.force_ratio = (total_alive > 0)
        ? (float)s.friendly_alive / (float)total_alive
        : 0.5f;

    // Morale / suppression averages
    s.avg_morale = (s.friendly_alive > 0)
        ? morale_sum / (float)s.friendly_alive : 0.5f;
    s.avg_suppression = (s.friendly_alive > 0)
        ? suppression_sum / (float)s.friendly_alive : 0.0f;

    // Medical ratio
    s.medical_ratio = (s.friendly_alive > 0)
        ? (float)(s.wounded_count + s.downed_count) / (float)s.friendly_alive
        : 0.0f;

    // Enemy analysis
    s.enemy_retreating_ratio = (s.enemy_alive > 0)
        ? (float)enemy_retreating / (float)s.enemy_alive : 0.0f;
    s.enemy_exposure_rate = (s.enemy_alive > 0)
        ? (float)enemy_not_in_cover / (float)s.enemy_alive : 0.0f;

    // MG ammo
    s.mg_count = mg_alive;
    s.mg_ammo_ratio = (mg_alive > 0) ? mg_ammo_sum / (float)mg_alive : 0.0f;

    // ── Squad analysis ───────────────────────────────────────────
    s.active_squad_count = 0;
    int squads_engaged = 0;

    // Determine squad range for our team
    int sq_start = (_team == 1) ? 0 : 64;
    int sq_end = sq_start + 64;

    for (int sq = sq_start; sq < sq_end; sq++) {
        int alive = sim->get_squad_alive_count(sq);
        if (alive <= 0) continue;
        s.active_squad_count++;
        // Rough engagement check: if squad centroid is near front line
        // (we use advance offset as proxy — higher offset = closer to enemy)
        float adv = sim->get_squad_advance_offset(sq);
        if (adv > 10.0f) {
            squads_engaged++;
        }
    }

    s.reserve_ratio = (s.active_squad_count > 0)
        ? 1.0f - (float)squads_engaged / (float)s.active_squad_count
        : 0.3f;

    // ── Capture Points ───────────────────────────────────────────
    s.friendly_pois = sim->get_capture_count_for_team(_team);
    s.enemy_pois = sim->get_capture_count_for_team(enemy_team);
    // Neutral POIs = total - friendly - enemy
    // We get total from capture data
    Dictionary cap_data = sim->get_capture_data();
    PackedInt32Array owners = cap_data.get("owners", PackedInt32Array());
    s.total_pois = owners.size();
    s.poi_ownership_ratio = (s.total_pois > 0)
        ? (float)s.friendly_pois / (float)s.total_pois : 0.0f;

    // ── Defensive positions: squads within 15m of owned POI ──────
    s.defensive_positions_held = 0;
    PackedFloat32Array cap_x, cap_z;
    // Extract POI positions from capture data
    // The capture data includes positions implicitly (by index).
    // We need SimulationServer to expose capture positions. For now,
    // approximate: count squads near any capture point.
    // Check if capture data has position arrays
    if (s.friendly_pois > 0) {
        for (int sq = sq_start; sq < sq_end; sq++) {
            int alive = sim->get_squad_alive_count(sq);
            if (alive <= 0) continue;
            Vector3 centroid = sim->get_squad_centroid(sq);
            // If squad advance offset is low, they're holding near rally
            float adv = sim->get_squad_advance_offset(sq);
            if (adv < 15.0f && alive >= 3) {
                s.defensive_positions_held++;
            }
        }
    }

    // ── Influence Map Data ───────────────────────────────────────
    if (_influence_map.is_valid()) {
        float push_dir = (_team == 1) ? 1.0f : -1.0f;
        float fallback = (_team == 1) ? -_map_w * 0.3f : _map_w * 0.3f;
        s.front_line_x = _influence_map->get_front_line_x(fallback);

        PackedVector3Array opps = _influence_map->get_opportunity_sectors();
        s.opportunity_sector_count = opps.size();

        // Active front count: approximate from threat sectors
        // A "front" is a sector with both friendly and enemy presence
        // We approximate by counting opportunity sectors vs total threat
        Vector3 threat_sector = _influence_map->get_highest_threat_sector();
        s.active_front_count = std::max(1, s.opportunity_sector_count / 2 + 1);

        // Track front line advance
        float front_delta = (s.front_line_x - _prev_snapshot.front_line_x) * push_dir;
        if (front_delta > 2.0f) {
            _last_advance_time = _total_elapsed;
        }
    }

    // ── Casualty History ─────────────────────────────────────────
    _casualty_history[_history_head] = (float)s.friendly_alive;
    int oldest = (_history_head + 1) % HISTORY_SIZE;
    float oldest_count = _casualty_history[oldest];
    if (oldest_count > 1.0f && s.friendly_alive < (int)oldest_count) {
        float loss_ratio = (oldest_count - (float)s.friendly_alive) / oldest_count;
        s.casualty_rate_norm = std::clamp(loss_ratio, 0.0f, 1.0f);
    } else {
        s.casualty_rate_norm = 0.0f;
    }
    _history_head = (_history_head + 1) % HISTORY_SIZE;
    _last_alive_count = s.friendly_alive;
}

// ═══════════════════════════════════════════════════════════════════════
//  Sensor Computation — Per Axis
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::_compute_sensors() {
    _compute_aggression_sensors();
    _compute_concentration_sensors();
    _compute_tempo_sensors();
    _compute_risk_tolerance_sensors();
    _compute_exploitation_sensors();
    _compute_terrain_control_sensors();
    _compute_medical_priority_sensors();
    _compute_suppression_dominance_sensors();
    _compute_intel_coverage_sensors();
}

void TheaterCommander::_compute_aggression_sensors() {
    // Sensor 0: force ratio (already 0..1)
    _sensors[AXIS_AGGRESSION][0] = _snapshot.force_ratio;

    // Sensor 1: momentum = front advance delta normalized
    // Positive advance in push direction → 1.0, retreat → 0.0, stall → 0.5
    float push_dir = (_team == 1) ? 1.0f : -1.0f;
    float advance = (_snapshot.front_line_x - _prev_snapshot.front_line_x) * push_dir;
    _sensors[AXIS_AGGRESSION][1] = std::clamp(advance / 20.0f + 0.5f, 0.0f, 1.0f);

    // Sensor 2: average morale (already 0..1)
    _sensors[AXIS_AGGRESSION][2] = _snapshot.avg_morale;
}

void TheaterCommander::_compute_concentration_sensors() {
    // Sensor 0: concentration = 1 - front_ratio
    float max_fronts = std::max(1.0f, (float)_snapshot.active_squad_count * 2.0f);
    float front_ratio = (float)_snapshot.active_front_count / max_fronts;
    _sensors[AXIS_CONCENTRATION][0] = std::clamp(1.0f - front_ratio, 0.0f, 1.0f);

    // Sensor 1: reserve ratio
    _sensors[AXIS_CONCENTRATION][1] = std::clamp(_snapshot.reserve_ratio, 0.0f, 1.0f);
}

void TheaterCommander::_compute_tempo_sensors() {
    // Sensor 0: time since last advance (0..1 over 30 seconds)
    float time_since = _total_elapsed - _last_advance_time;
    _sensors[AXIS_TEMPO][0] = std::clamp(time_since / 30.0f, 0.0f, 1.0f);

    // Sensor 1: enemy weakness combined
    _sensors[AXIS_TEMPO][1] = std::clamp(
        _snapshot.enemy_retreating_ratio * 0.6f + _snapshot.enemy_exposure_rate * 0.4f,
        0.0f, 1.0f
    );
}

void TheaterCommander::_compute_risk_tolerance_sensors() {
    // Sensor 0: casualty rate (inverted by the logistic curve — k is negative)
    _sensors[AXIS_RISK_TOLERANCE][0] = _snapshot.casualty_rate_norm;

    // Sensor 1: reserve ratio (more reserves → can afford risk)
    _sensors[AXIS_RISK_TOLERANCE][1] = std::clamp(_snapshot.reserve_ratio, 0.0f, 1.0f);
}

void TheaterCommander::_compute_exploitation_sensors() {
    // Sensor 0: newly opened flanks (delta of opportunity sectors)
    int new_opps = std::max(0,
        _snapshot.opportunity_sector_count - _prev_snapshot.opportunity_sector_count);
    _sensors[AXIS_EXPLOITATION][0] = std::clamp((float)new_opps / 3.0f, 0.0f, 1.0f);

    // Sensor 1: enemy retreating ratio
    _sensors[AXIS_EXPLOITATION][1] = _snapshot.enemy_retreating_ratio;
}

void TheaterCommander::_compute_terrain_control_sensors() {
    // Sensor 0: POI ownership ratio
    _sensors[AXIS_TERRAIN_CONTROL][0] = _snapshot.poi_ownership_ratio;

    // Sensor 1: defensive positions held (normalized by squad count)
    float sq = std::max(1.0f, (float)_snapshot.active_squad_count);
    _sensors[AXIS_TERRAIN_CONTROL][1] = std::clamp(
        (float)_snapshot.defensive_positions_held / sq, 0.0f, 1.0f
    );
}

void TheaterCommander::_compute_medical_priority_sensors() {
    // Sensor 0: medical ratio (wounded + downed / alive)
    _sensors[AXIS_MEDICAL_PRIORITY][0] = std::clamp(_snapshot.medical_ratio, 0.0f, 1.0f);

    // Sensor 1: inverse medic availability
    // Expect ~10% of friendly force to be medics; fewer = higher urgency
    float expected_medics = (float)_snapshot.friendly_alive * 0.1f;
    float medic_fill = (expected_medics > 0.5f)
        ? (float)_snapshot.medic_count / expected_medics
        : 1.0f;
    _sensors[AXIS_MEDICAL_PRIORITY][1] = std::clamp(1.0f - medic_fill, 0.0f, 1.0f);
}

void TheaterCommander::_compute_suppression_dominance_sensors() {
    // Sensor 0: MG ammo ratio (average ammo% of alive MGs)
    _sensors[AXIS_SUPPRESSION_DOMINANCE][0] = std::clamp(_snapshot.mg_ammo_ratio, 0.0f, 1.0f);

    // Sensor 1: enemy exposure rate (fraction not in cover)
    _sensors[AXIS_SUPPRESSION_DOMINANCE][1] = std::clamp(_snapshot.enemy_exposure_rate, 0.0f, 1.0f);
}

void TheaterCommander::_compute_intel_coverage_sensors() {
    // Sensor 0: intel ratio = fraction of enemy team currently visible
    // Uses popcount on visibility bitfield from SimulationServer
    SimulationServer *sim = SimulationServer::get_singleton();
    if (!sim || _snapshot.enemy_alive <= 0) {
        _sensors[AXIS_INTEL_COVERAGE][0] = 1.0f;  // no enemies = full intel
        return;
    }

    int visible = 0;
    int vis_idx = _team - 1;
    int count = sim->get_unit_count();
    int enemy_team = (_team == 1) ? 2 : 1;
    for (int i = 0; i < count; i++) {
        if (!sim->is_alive(i)) continue;
        if (sim->get_team(i) != enemy_team) continue;
        if (sim->team_can_see(_team, i)) visible++;
    }

    // Note: enemy_alive in snapshot is already filtered by visibility,
    // so we need total_enemy_alive for the denominator
    int total_enemy = sim->get_alive_count_for_team(enemy_team);
    float intel = (total_enemy > 0) ? (float)visible / (float)total_enemy : 1.0f;
    _sensors[AXIS_INTEL_COVERAGE][0] = std::clamp(intel, 0.0f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════
//  Score Aggregation
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::_aggregate_scores() {
    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        const AxisConfig &cfg = _axis_configs[axis];
        float weighted_sum = 0.0f;
        float weight_total = 0.0f;

        for (int s = 0; s < cfg.sensor_count; s++) {
            float curve_out = _eval_curve(cfg.curves[s], _sensors[axis][s]);
            weighted_sum += curve_out * cfg.sensor_weights[s];
            weight_total += cfg.sensor_weights[s];
        }

        float raw_score = (weight_total > 0.0f) ? weighted_sum / weight_total : 0.5f;

        // Apply Tier-0 weight modifier (LLM bias)
        raw_score *= _weight_modifiers[axis];

        _axis_scores[axis] = std::clamp(raw_score, 0.0f, 1.0f);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Momentum & Hysteresis
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::_apply_momentum_and_hysteresis() {
    // Apply momentum bonus to current posture
    if (_current_posture >= 0 && _current_posture < AXIS_COUNT) {
        _axis_scores[_current_posture] = std::min(
            _axis_scores[_current_posture] + _tune_momentum_bonus, 1.0f
        );
    }

    // Apply cooldown penalty to recently abandoned postures (reduced to prevent lock-in)
    for (int i = 0; i < AXIS_COUNT; i++) {
        if (_posture_cooldowns[i] > 0.0f) {
            _axis_scores[i] *= 0.7f;
        }
    }

    // Find dominant axis (highest score)
    int best_axis = 0;
    float best_score = _axis_scores[0];
    for (int i = 1; i < AXIS_COUNT; i++) {
        if (_axis_scores[i] > best_score) {
            best_score = _axis_scores[i];
            best_axis = i;
        }
    }

    // Check if we can switch posture
    bool can_switch = (_posture_time >= _tune_min_commitment) || (_current_posture < 0);

    if (can_switch && best_axis != _current_posture) {
        // Switch: apply cooldown to abandoned posture
        if (_current_posture >= 0) {
            _posture_cooldowns[_current_posture] = _tune_cooldown;
        }
        _current_posture = best_axis;
        _posture_time = 0.0f;
    } else {
        _posture_time += _tick_interval;
    }

    // Final clamp
    for (int i = 0; i < AXIS_COUNT; i++) {
        _axis_scores[i] = std::clamp(_axis_scores[i], 0.0f, 1.0f);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Output Methods
// ═══════════════════════════════════════════════════════════════════════

Dictionary TheaterCommander::get_axis_values() const {
    Dictionary d;
    for (int i = 0; i < AXIS_COUNT; i++) {
        d[AXIS_NAMES[i]] = _axis_scores[i];
    }
    return d;
}

float TheaterCommander::get_axis(int axis_index) const {
    if (axis_index < 0 || axis_index >= AXIS_COUNT) return 0.0f;
    return _axis_scores[axis_index];
}

float TheaterCommander::get_axis_by_name(const String &name) const {
    for (int i = 0; i < AXIS_COUNT; i++) {
        if (name == AXIS_NAMES[i]) {
            return _axis_scores[i];
        }
    }
    return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════
//  LLM Weight Modifier Interface
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::set_weight_modifiers(const Dictionary &modifiers) {
    for (int i = 0; i < AXIS_COUNT; i++) {
        String key = AXIS_NAMES[i];
        if (modifiers.has(key)) {
            _weight_modifiers[i] = std::clamp((float)(double)modifiers[key], 0.5f, 2.0f);
        }
    }
}

Dictionary TheaterCommander::get_weight_modifiers() const {
    Dictionary d;
    for (int i = 0; i < AXIS_COUNT; i++) {
        d[AXIS_NAMES[i]] = _weight_modifiers[i];
    }
    return d;
}

// ═══════════════════════════════════════════════════════════════════════
//  Debug
// ═══════════════════════════════════════════════════════════════════════

Dictionary TheaterCommander::get_debug_info() const {
    Dictionary d;
    d["team"] = _team;
    d["tick_ms"] = _last_tick_ms;
    d["total_elapsed"] = _total_elapsed;
    d["current_posture"] = (_current_posture >= 0 && _current_posture < AXIS_COUNT)
        ? String(AXIS_NAMES[_current_posture]) : String("none");
    d["posture_time"] = _posture_time;

    // Per-axis detail
    Dictionary axes;
    for (int i = 0; i < AXIS_COUNT; i++) {
        Dictionary axis_info;
        axis_info["score"] = _axis_scores[i];
        axis_info["weight_modifier"] = _weight_modifiers[i];
        axis_info["cooldown"] = _posture_cooldowns[i];
        Dictionary sensors_dict;
        for (int s = 0; s < _axis_configs[i].sensor_count; s++) {
            sensors_dict[String::num_int64(s)] = _sensors[i][s];
        }
        axis_info["sensors"] = sensors_dict;
        axes[AXIS_NAMES[i]] = axis_info;
    }
    d["axes"] = axes;

    // Snapshot summary
    Dictionary snap;
    snap["friendly_alive"] = _snapshot.friendly_alive;
    snap["enemy_alive"] = _snapshot.enemy_alive;
    snap["force_ratio"] = _snapshot.force_ratio;
    snap["avg_morale"] = _snapshot.avg_morale;
    snap["casualty_rate"] = _snapshot.casualty_rate_norm;
    snap["poi_ownership"] = _snapshot.poi_ownership_ratio;
    snap["medical_ratio"] = _snapshot.medical_ratio;
    snap["enemy_retreating"] = _snapshot.enemy_retreating_ratio;
    snap["enemy_exposure"] = _snapshot.enemy_exposure_rate;
    snap["mg_ammo_ratio"] = _snapshot.mg_ammo_ratio;
    snap["reserve_ratio"] = _snapshot.reserve_ratio;
    d["snapshot"] = snap;

    return d;
}

// ═══════════════════════════════════════════════════════════════════════
//  Tuning API
// ═══════════════════════════════════════════════════════════════════════

Dictionary TheaterCommander::get_tuning_params() const {
    Dictionary d;
    d["tick_interval"] = _tick_interval;
    d["momentum_bonus"] = _tune_momentum_bonus;
    d["min_commitment"] = _tune_min_commitment;
    d["cooldown"] = _tune_cooldown;
    return d;
}

void TheaterCommander::set_tuning_param(const String &name, float value) {
    if (name == "tick_interval") _tick_interval = std::max(0.1f, value);
    else if (name == "momentum_bonus") _tune_momentum_bonus = value;
    else if (name == "min_commitment") _tune_min_commitment = value;
    else if (name == "cooldown") _tune_cooldown = value;
}

void TheaterCommander::reset_tuning_params() {
    _tick_interval = DEFAULT_TICK_INTERVAL;
    _tune_momentum_bonus = MOMENTUM_BONUS;
    _tune_min_commitment = MIN_COMMITMENT_SEC;
    _tune_cooldown = COOLDOWN_SEC;
}

// ═══════════════════════════════════════════════════════════════════════
//  ClassDB Bindings
// ═══════════════════════════════════════════════════════════════════════

void TheaterCommander::_bind_methods() {
    // ── Setup ────────────────────────────────────────────────────
    ClassDB::bind_method(D_METHOD("setup", "team", "map_w", "map_h"),
                         &TheaterCommander::setup);
    ClassDB::bind_method(D_METHOD("set_influence_map", "map"),
                         &TheaterCommander::set_influence_map);

    // ── Tick ─────────────────────────────────────────────────────
    ClassDB::bind_method(D_METHOD("tick", "delta"), &TheaterCommander::tick);

    // ── Output ───────────────────────────────────────────────────
    ClassDB::bind_method(D_METHOD("get_axis_values"),
                         &TheaterCommander::get_axis_values);
    ClassDB::bind_method(D_METHOD("get_axis", "axis_index"),
                         &TheaterCommander::get_axis);
    ClassDB::bind_method(D_METHOD("get_axis_by_name", "name"),
                         &TheaterCommander::get_axis_by_name);

    // ── LLM Weight Modifiers ─────────────────────────────────────
    ClassDB::bind_method(D_METHOD("set_weight_modifiers", "modifiers"),
                         &TheaterCommander::set_weight_modifiers);
    ClassDB::bind_method(D_METHOD("get_weight_modifiers"),
                         &TheaterCommander::get_weight_modifiers);

    // ── Configuration ────────────────────────────────────────────
    ClassDB::bind_method(D_METHOD("set_tick_interval", "interval"),
                         &TheaterCommander::set_tick_interval);
    ClassDB::bind_method(D_METHOD("get_tick_interval"),
                         &TheaterCommander::get_tick_interval);

    // ── Debug ────────────────────────────────────────────────────
    ClassDB::bind_method(D_METHOD("get_debug_info"),
                         &TheaterCommander::get_debug_info);
    ClassDB::bind_method(D_METHOD("get_last_tick_ms"),
                         &TheaterCommander::get_last_tick_ms);

    // ── Tuning API ──────────────────────────────────────────────
    ClassDB::bind_method(D_METHOD("get_tuning_params"), &TheaterCommander::get_tuning_params);
    ClassDB::bind_method(D_METHOD("set_tuning_param", "name", "value"),
                         &TheaterCommander::set_tuning_param);
    ClassDB::bind_method(D_METHOD("reset_tuning_params"), &TheaterCommander::reset_tuning_params);

    // ── Enum Constants ───────────────────────────────────────────
    BIND_ENUM_CONSTANT(AXIS_AGGRESSION);
    BIND_ENUM_CONSTANT(AXIS_CONCENTRATION);
    BIND_ENUM_CONSTANT(AXIS_TEMPO);
    BIND_ENUM_CONSTANT(AXIS_RISK_TOLERANCE);
    BIND_ENUM_CONSTANT(AXIS_EXPLOITATION);
    BIND_ENUM_CONSTANT(AXIS_TERRAIN_CONTROL);
    BIND_ENUM_CONSTANT(AXIS_MEDICAL_PRIORITY);
    BIND_ENUM_CONSTANT(AXIS_SUPPRESSION_DOMINANCE);
    BIND_ENUM_CONSTANT(AXIS_INTEL_COVERAGE);

    BIND_ENUM_CONSTANT(CURVE_LOGISTIC);
    BIND_ENUM_CONSTANT(CURVE_GAUSSIAN);
    BIND_ENUM_CONSTANT(CURVE_QUADRATIC);
    BIND_ENUM_CONSTANT(CURVE_LINEAR);
}
