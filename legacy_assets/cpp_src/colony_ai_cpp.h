#ifndef COLONY_AI_CPP_H
#define COLONY_AI_CPP_H

#include "simulation_server.h"
#include "influence_map.h"
#include "theater_commander.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace godot {

/// ColonyAICPP: C++ auction/scoring layer for the strategic AI.
///
/// Replaces _plan_squad_intents() in colony_ai.gd. Reads SimulationServer,
/// InfluenceMapCPP, and TheaterCommander directly via C++ singletons (zero FFI).
/// Outputs a Dictionary in the same format colony_ai.gd::_commit_planned_intents()
/// expects: {"assignments": {sq_idx: goal_idx}, "squad_intents": {sq_idx: intent_dict}}.
///
/// Usage (from GDScript):
///   var cai = ColonyAICPP.new()
///   cai.setup(1, 300.0, 200.0, 5)
///   cai.set_influence_map(my_influence_map)
///   # each planning tick:
///   var batch = cai.plan_intents()
class ColonyAICPP : public RefCounted {
    GDCLASS(ColonyAICPP, RefCounted)

public:
    // ── Goal Indices (match _register_default_goals() order) ────────
    enum GoalIndex : uint8_t {
        GOAL_CAPTURE_POI = 0,
        GOAL_DEFEND_POI,
        GOAL_ASSAULT_ENEMY,
        GOAL_DEFEND_BASE,
        GOAL_FIRE_MISSION,
        GOAL_FLANK_ENEMY,
        GOAL_HOLD_POSITION,
        GOAL_RECONNAISSANCE,
        GOAL_COUNT  // 8 main goals; triage_medical is concurrent, stays GDScript
    };

    // ── Squad Role Enum (mirrors GDScript StringName roles) ─────────
    enum SquadRole : uint8_t {
        SQUAD_ASSAULT = 0,
        SQUAD_DEFEND,
        SQUAD_FLANK,
        SQUAD_SNIPER,
        SQUAD_RECON,
        SQUAD_MORTAR,
        SQUAD_ROLE_COUNT
    };

    // ── Coordination Tag Bitmask ────────────────────────────────────
    enum CoordTag : uint16_t {
        TAG_NONE      = 0,
        TAG_PIN       = 1 << 0,
        TAG_SUPPRESS  = 1 << 1,
        TAG_OVERWATCH = 1 << 2,
        TAG_FLANK     = 1 << 3,
        TAG_SPOTTER   = 1 << 4,
    };

    static constexpr int MAX_COLONY_SQUADS = 128;  // match SimulationServer::MAX_SQUADS

    ColonyAICPP();
    ~ColonyAICPP();

    // ── Per-Team Instance Access ─────────────────────────────────────
    static ColonyAICPP *get_singleton() { return _instances[0] ? _instances[0] : _instances[1]; }
    static ColonyAICPP *get_instance(int team) {
        int idx = team - 1;
        return (idx >= 0 && idx < 2) ? _instances[idx] : nullptr;
    }

    // ── Setup ───────────────────────────────────────────────────────
    void setup(int team, float map_w, float map_h, int squad_count);
    void set_influence_map(Ref<InfluenceMapCPP> map);
    void set_squad_role(int squad_idx, const String &role_str);
    void set_squad_sim_id(int squad_idx, int sim_squad_id);
    void set_push_direction(float dir);
    void set_base_x(float x);

    // ── LLM Directive Interface ───────────────────────────────────
    void set_llm_directive(int squad_idx, int sector_col, int sector_row, int intent, float confidence);
    void clear_llm_directive(int squad_idx);
    void clear_all_llm_directives();
    Dictionary get_llm_directive_debug() const;

    // ── Main Planning Entry Point ───────────────────────────────────
    Dictionary plan_intents();

    // ── Debug ───────────────────────────────────────────────────────
    Dictionary get_debug_info() const;
    Dictionary get_score_matrix() const;
    float get_last_plan_ms() const { return _last_plan_ms; }

    // ── Tuning API ──────────────────────────────────────────────────
    Dictionary get_tuning_params() const;
    void set_tuning_param(const String &name, float value);
    void reset_tuning_params();

protected:
    static void _bind_methods();

private:
    static ColonyAICPP *_instances[2];  // per-team: [0]=team1, [1]=team2

    // ── Configuration ───────────────────────────────────────────────
    int   _team = 1;
    float _map_w = 300.0f;
    float _map_h = 200.0f;
    float _push_direction = 1.0f;
    float _base_x = -120.0f;
    int   _squad_count = 0;

    // ── External References ─────────────────────────────────────────
    Ref<InfluenceMapCPP> _influence_map;

    // ── Per-Squad Colony Data ───────────────────────────────────────
    struct SquadSnapshot {
        int       sim_squad_id = -1;
        SquadRole role = SQUAD_ASSAULT;
        Vector3   center;
        float     strength = 0.0f;   // sum of HP ratios for alive members
        float     morale = 0.0f;     // average morale
        int       alive_count = 0;
        bool      is_broken = false;
        bool      has_mortar = false;
    };
    SquadSnapshot _squads[MAX_COLONY_SQUADS];

    // ── Goal Static Config ──────────────────────────────────────────
    struct GoalConfig {
        int      max_squads;
        float    min_strength;
        uint16_t provides_tags;
        uint16_t desires_tags;
    };
    static const GoalConfig GOAL_CONFIGS[GOAL_COUNT];
    static const char *GOAL_NAMES[GOAL_COUNT];
    static const char *INTENT_ACTIONS[GOAL_COUNT];

    // ── Theater Bias Matrix [axis][goal] ────────────────────────────
    static const float THEATER_BIAS[TheaterCommander::AXIS_COUNT][GOAL_COUNT];

    // ── Colony Snapshot (recomputed each plan_intents call) ──────────
    struct ColonySnapshot {
        int friendly_alive = 0;
        int enemy_alive = 0;

        static constexpr int MAX_ENEMY_CACHE = 128;
        Vector3 enemy_positions[MAX_ENEMY_CACHE];
        int     enemy_cache_count = 0;

        // Capture points
        static constexpr int MAX_CAPTURE = 8;
        Vector3 capture_positions[MAX_CAPTURE];
        int8_t  capture_owners[MAX_CAPTURE];
        float   capture_progress[MAX_CAPTURE];
        int8_t  capture_capturing[MAX_CAPTURE];
        int     capture_count = 0;
        int     pois_owned = 0;
        int     capturable_count = 0;
        int     contested_count = 0;

        // Influence
        float   front_line_x = 0.0f;
        Vector3 highest_threat_sector;
        static constexpr int MAX_OPPORTUNITY = 32;
        Vector3 opportunity_sectors[MAX_OPPORTUNITY];
        int     opportunity_count = 0;

        float base_x = -120.0f;
        float push_direction = 1.0f;
    };
    ColonySnapshot _cs;

    // ── Score Matrix ────────────────────────────────────────────────
    float _score_matrix[MAX_COLONY_SQUADS][GOAL_COUNT];

    // ── Goal Hysteresis (reduce thrashing) ──────────────────────────
    int _prev_goal[MAX_COLONY_SQUADS];   // previous auction assignment (-1 = none)
    static constexpr float GOAL_SWITCH_MARGIN = 8.0f;  // incumbent gets this many bonus points

    // ── POI Deconfliction ────────────────────────────────────────────
    mutable bool _poi_claimed[ColonySnapshot::MAX_CAPTURE];

    // ── LLM Directive Override ─────────────────────────────────────
    struct LLMDirective {
        int8_t  sector_col = -1;  // -1 = no directive
        int8_t  sector_row = -1;
        uint8_t intent = 0;       // maps to GoalIndex via _intent_to_goal()
        float   confidence = 0.0f;// 0.0-1.0
        int64_t issued_ms = 0;    // for staleness (90s expiry)
    };
    LLMDirective _llm_directives[MAX_COLONY_SQUADS];

    static constexpr float LLM_DIRECTIVE_FLOOR       = 75.0f;
    static constexpr float LLM_DIRECTIVE_AGE_MAX     = 90.0f;  // hard expiry (seconds)
    static constexpr float LLM_DIRECTIVE_DECAY_START = 60.0f;  // soft decay begins
    static constexpr float LLM_COORD_DAMPING         = 0.5f;   // cross-sector coordination reduction

    // ── Tunable Parameters ───────────────────────────────────────────
    float _tune_llm_floor       = LLM_DIRECTIVE_FLOOR;
    float _tune_llm_age_max     = LLM_DIRECTIVE_AGE_MAX;
    float _tune_llm_decay_start = LLM_DIRECTIVE_DECAY_START;
    float _tune_coord_damping   = LLM_COORD_DAMPING;

    // ── Last Auction Result (for KPI tracking) ─────────────────────
    Dictionary _last_assignments;

    // ── Timing ──────────────────────────────────────────────────────
    float _last_plan_ms = 0.0f;

    // ── Internal Methods ────────────────────────────────────────────
    void _compute_colony_snapshot();
    void _compute_squad_snapshots();
    void _compute_score_matrix();
    void _apply_theater_bias();
    void _apply_llm_directives();
    Dictionary _run_auction();
    static int _intent_to_goal(int intent);

    // ── Per-Goal Scoring ────────────────────────────────────────────
    float _score_capture_poi(int sq) const;
    float _score_defend_poi(int sq) const;
    float _score_assault_enemy(int sq) const;
    float _score_defend_base(int sq) const;
    float _score_fire_mission(int sq) const;
    float _score_flank_enemy(int sq) const;
    float _score_hold_position(int sq) const;
    float _score_reconnaissance(int sq) const;
    float _score_flank_position(const Vector3 &squad_pos, const Vector3 &flank_pos,
                                const Vector3 &enemy_center) const;

    // ── Coordination ────────────────────────────────────────────────
    float _calc_coordination_bonus(int sq_idx, int goal_idx,
                                   const int *assignments) const;

    // ── Helpers ─────────────────────────────────────────────────────
    int     _count_enemies_near(const Vector3 &pos, float radius) const;
    int     _find_nearest_enemy_idx(const Vector3 &pos) const;
    float   _best_enemy_cluster_score() const;
    Vector3 _best_enemy_cluster_centroid() const;
    Dictionary _generate_intent(int sq_idx, int goal_idx) const;

    static SquadRole _role_from_string(const String &s);

    static inline float _clampf(float v, float lo, float hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }
    static inline float _distance_xz(const Vector3 &a, const Vector3 &b) {
        float dx = a.x - b.x;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }
};

} // namespace godot

VARIANT_ENUM_CAST(godot::ColonyAICPP::GoalIndex)
VARIANT_ENUM_CAST(godot::ColonyAICPP::SquadRole)

#endif // COLONY_AI_CPP_H
