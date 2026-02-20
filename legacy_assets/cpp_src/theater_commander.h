#ifndef THEATER_COMMANDER_H
#define THEATER_COMMANDER_H

#include "influence_map.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <cmath>
#include <cstring>

namespace godot {

/// Theater Commander: Tier-1 strategic AI using Infinite Axis Utility System.
///
/// Evaluates the entire battlefield every 1-2 seconds. Reads SimulationServer
/// SoA data and influence map data directly via C++ singletons (zero FFI).
/// Outputs 8 orthogonal bias multipliers consumed by ColonyAI's goal auction.
///
/// Usage (from GDScript):
///   var tc = TheaterCommander.new()
///   tc.setup(1, 300.0, 200.0)
///   tc.set_influence_map(my_influence_map)
///   # each frame:
///   tc.tick(delta)
///   var biases = tc.get_axis_values()
class TheaterCommander : public RefCounted {
    GDCLASS(TheaterCommander, RefCounted)

public:
    // ── Axis Indices ─────────────────────────────────────────────────
    enum Axis : uint8_t {
        AXIS_AGGRESSION = 0,
        AXIS_CONCENTRATION,
        AXIS_TEMPO,
        AXIS_RISK_TOLERANCE,
        AXIS_EXPLOITATION,
        AXIS_TERRAIN_CONTROL,
        AXIS_MEDICAL_PRIORITY,
        AXIS_SUPPRESSION_DOMINANCE,
        AXIS_INTEL_COVERAGE,
        AXIS_COUNT  // 9
    };

    // ── Response Curve Types ─────────────────────────────────────────
    enum CurveType : uint8_t {
        CURVE_LOGISTIC = 0,
        CURVE_GAUSSIAN,
        CURVE_QUADRATIC,
        CURVE_LINEAR,
        CURVE_COUNT
    };

    TheaterCommander();
    ~TheaterCommander();

    // ── Setup ────────────────────────────────────────────────────────
    void setup(int team, float map_w, float map_h);
    void set_influence_map(Ref<InfluenceMapCPP> map);

    // ── Per-Team Instance Access ──────────────────────────────────────
    static TheaterCommander *get_singleton() { return _instances[0] ? _instances[0] : _instances[1]; }
    static TheaterCommander *get_instance(int team) {
        int idx = team - 1;
        return (idx >= 0 && idx < 2) ? _instances[idx] : nullptr;
    }

    // ── Tick ─────────────────────────────────────────────────────────
    /// Main evaluation. Internally throttled to tick_interval.
    /// Returns true if axes were recalculated this call.
    bool tick(float delta);

    // ── Output ───────────────────────────────────────────────────────
    Dictionary get_axis_values() const;
    float get_axis(int axis_index) const;
    float get_axis_by_name(const String &name) const;

    // ── LLM Weight Adjuster Interface (Tier 0) ──────────────────────
    void set_weight_modifiers(const Dictionary &modifiers);
    Dictionary get_weight_modifiers() const;

    // ── Configuration ────────────────────────────────────────────────
    void set_tick_interval(float interval);
    float get_tick_interval() const { return _tick_interval; }

    // ── Debug ────────────────────────────────────────────────────────
    Dictionary get_debug_info() const;
    float get_last_tick_ms() const { return _last_tick_ms; }

    // ── Tuning API ──────────────────────────────────────────────────
    Dictionary get_tuning_params() const;
    void set_tuning_param(const String &name, float value);
    void reset_tuning_params();

protected:
    static void _bind_methods();

private:
    static TheaterCommander *_instances[2];  // per-team: [0]=team1, [1]=team2

    // ── Configuration ────────────────────────────────────────────────
    int _team = 1;
    float _map_w = 300.0f;
    float _map_h = 200.0f;
    Ref<InfluenceMapCPP> _influence_map;

    // ── Tick Timing ──────────────────────────────────────────────────
    static constexpr float DEFAULT_TICK_INTERVAL = 1.5f;
    float _tick_interval = DEFAULT_TICK_INTERVAL;
    float _tick_timer = 0.0f;
    float _last_tick_ms = 0.0f;
    float _total_elapsed = 0.0f;

    // ── Axis Scores (output) ─────────────────────────────────────────
    float _axis_scores[AXIS_COUNT] = {};

    // ── Raw Sensor Values (intermediate, for debug) ──────────────────
    static constexpr int MAX_SENSORS = 3;
    float _sensors[AXIS_COUNT][MAX_SENSORS] = {};

    // ── Response Curve Parameters ────────────────────────────────────
    struct CurveParams {
        CurveType type = CURVE_LINEAR;
        float p0 = 1.0f, p1 = 0.0f, p2 = 0.0f;
        // Logistic:  p0=k (steepness), p1=midpoint
        // Gaussian:  p0=peak, p1=sigma
        // Quadratic: p0=a, p1=b, p2=c  (y = ax² + bx + c)
        // Linear:    p0=slope, p1=offset
    };

    struct AxisConfig {
        int sensor_count = 0;
        CurveParams curves[MAX_SENSORS];
        float sensor_weights[MAX_SENSORS] = {};
    };
    AxisConfig _axis_configs[AXIS_COUNT];

    void _init_axis_configs();

    // ── Weight Modifiers (Tier 0: LLM) ──────────────────────────────
    float _weight_modifiers[AXIS_COUNT];  // default 1.0

    // ── Momentum & Hysteresis ────────────────────────────────────────
    static constexpr float MOMENTUM_BONUS     = 0.15f;
    static constexpr float MIN_COMMITMENT_SEC = 8.0f;
    static constexpr float COOLDOWN_SEC       = 12.0f;

    // ── Tunable Parameters ───────────────────────────────────────────
    float _tune_tick_interval   = DEFAULT_TICK_INTERVAL;
    float _tune_momentum_bonus  = MOMENTUM_BONUS;
    float _tune_min_commitment  = MIN_COMMITMENT_SEC;
    float _tune_cooldown        = COOLDOWN_SEC;

    int   _current_posture = -1;
    float _posture_time = 0.0f;
    float _posture_cooldowns[AXIS_COUNT] = {};

    // ── Casualty History (ring buffer for trend) ─────────────────────
    static constexpr int HISTORY_SIZE = 8;
    float _casualty_history[HISTORY_SIZE] = {};
    int   _history_head = 0;
    int   _last_alive_count = 0;
    float _last_advance_time = 0.0f;

    // ── Battlefield Snapshot (recomputed each tick) ──────────────────
    struct BattlefieldSnapshot {
        // Force
        int   friendly_alive = 0;
        int   enemy_alive = 0;
        float force_ratio = 0.5f;

        // Morale
        float avg_morale = 0.7f;
        float avg_suppression = 0.0f;

        // Squads
        int   active_squad_count = 0;
        float reserve_ratio = 0.3f;

        // Casualties
        float casualty_rate_norm = 0.0f;

        // Capture points
        int   friendly_pois = 0;
        int   enemy_pois = 0;
        int   total_pois = 0;
        float poi_ownership_ratio = 0.0f;

        // Influence
        int   active_front_count = 0;
        int   opportunity_sector_count = 0;
        float front_line_x = 0.0f;

        // Enemy analysis
        float enemy_retreating_ratio = 0.0f;
        float enemy_exposure_rate = 0.0f;

        // Medical
        int   wounded_count = 0;
        int   downed_count = 0;
        int   medic_count = 0;
        float medical_ratio = 0.0f;

        // Suppression
        float mg_ammo_ratio = 0.0f;
        int   mg_count = 0;

        // Defense
        int   defensive_positions_held = 0;
    };
    BattlefieldSnapshot _snapshot;
    BattlefieldSnapshot _prev_snapshot;

    // ── Internal Methods ─────────────────────────────────────────────
    void _compute_snapshot();
    void _compute_sensors();
    void _aggregate_scores();
    void _apply_momentum_and_hysteresis();

    // Per-axis sensor computation
    void _compute_aggression_sensors();
    void _compute_concentration_sensors();
    void _compute_tempo_sensors();
    void _compute_risk_tolerance_sensors();
    void _compute_exploitation_sensors();
    void _compute_terrain_control_sensors();
    void _compute_medical_priority_sensors();
    void _compute_suppression_dominance_sensors();
    void _compute_intel_coverage_sensors();

    // Response curve evaluation
    static float _eval_curve(const CurveParams &curve, float x);
    static float _logistic(float x, float k, float midpoint);
    static float _gaussian(float x, float peak, float sigma);
    static float _quadratic(float x, float a, float b, float c);
    static float _linear(float x, float slope, float offset);

    // ── Axis Name Table ──────────────────────────────────────────────
    static const char *AXIS_NAMES[AXIS_COUNT];
};

} // namespace godot

VARIANT_ENUM_CAST(godot::TheaterCommander::Axis)
VARIANT_ENUM_CAST(godot::TheaterCommander::CurveType)

#endif // THEATER_COMMANDER_H
