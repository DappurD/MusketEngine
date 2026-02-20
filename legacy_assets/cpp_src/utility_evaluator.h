#ifndef UTILITY_EVALUATOR_H
#define UTILITY_EVALUATOR_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <algorithm>

namespace godot {

/// High-performance C++ utility scorer for combat AI.
/// All 16 built-in scorers run in C++ with a flat float context array.
/// GDScript UtilityAI builds the context array and calls evaluate().
class UtilityEvaluatorCPP : public RefCounted {
    GDCLASS(UtilityEvaluatorCPP, RefCounted)

public:
    // ── Context array indices ─────────────────────────────────────
    enum ContextKey {
        CTX_HP_RATIO = 0,
        CTX_MORALE_RATIO,
        CTX_SUPPRESSION_PRESSURE,
        CTX_BEST_ENEMY_DIST,
        CTX_OPTIMAL_RANGE,
        CTX_NEARBY_ENEMIES,
        CTX_NEARBY_ALLIES,
        CTX_HAS_BEST_ENEMY,
        CTX_HAS_LINE_OF_FIRE,
        CTX_HAS_SQUAD_ORDER,
        CTX_IS_IN_COVER,
        CTX_IS_FLANKED,
        CTX_HAS_NEAREST_COVER,
        CTX_NEAREST_COVER_DIST,
        CTX_SQUAD_DIST,
        CTX_SQUAD_STRENGTH,
        CTX_ALLIES_ADVANCING,
        CTX_IS_SHAKEN,
        CTX_HAS_HELP_TARGET,
        CTX_HELP_TARGET_DIST,
        CTX_HELP_TARGET_BLEED_RATE,
        CTX_HAS_CARRY_TARGET,
        CTX_IS_CURRENTLY_CARRYING,
        CTX_HAS_MORPHINE_TARGET,
        CTX_HAS_NEAREST_DOWNED_ALLY,
        CTX_DOWNED_NEEDS_MORPHINE,
        CTX_SUPPRESSION_ACTIVE,
        CTX_DARKNESS,
        CTX_FLASHLIGHT_ON,
        CTX_AMMO_RATIO,
        CTX_IS_RELOADING,
        CTX_FIRE_MISSION_ACTIVE,
        CTX_MORTAR_DEPLOYED,
        CTX_HAS_RECON_DRONE_AVAILABLE,
        CTX_HAS_ACTIVE_RECON_DRONE,
        CTX_HAS_ATTACK_DRONE_AVAILABLE,
        CTX_ENEMY_IN_COVER,
        CTX_HAS_UNTREATED_WOUNDS,
        CTX_SQUAD_COMMITTED,
        CTX_COUNT
    };

    // ── Action indices ────────────────────────────────────────────
    enum ActionID {
        ACT_ENGAGE = 0,
        ACT_TAKE_COVER,
        ACT_SUPPRESS,
        ACT_FLANK,
        ACT_HELP_ALLY,
        ACT_CARRY_ALLY,
        ACT_INJECT_MORPHINE,
        ACT_TREAT_SELF,
        ACT_RELOAD,
        ACT_DEPLOY_MORTAR,
        ACT_FIRE_MORTAR,
        ACT_LAUNCH_RECON,
        ACT_LAUNCH_ATTACK_DRONE,
        ACT_FOLLOW_SQUAD,
        ACT_RETREAT,
        ACT_IDLE,
        ACT_COUNT
    };

    // ── Role indices ──────────────────────────────────────────────
    enum RoleID {
        ROLE_RIFLEMAN = 0,
        ROLE_MG,
        ROLE_MARKSMAN,
        ROLE_AT,
        ROLE_MEDIC,
        ROLE_LEADER,
        ROLE_GRENADIER,
        ROLE_MORTAR,
        ROLE_DRONE_OPERATOR,
        ROLE_COUNT
    };

    static constexpr float HYSTERESIS_BONUS = 10.0f;
    static constexpr float GOAP_BIAS_BONUS  = 25.0f;

    // ── Public API ────────────────────────────────────────────────

    /// Run all 16 scorers on the given context.
    /// Returns Dictionary: { "action": int, "scores": PackedFloat32Array }
    static Dictionary evaluate(
        const PackedFloat32Array &context,
        int role_index,
        int current_action_index,
        int goap_bias_action_index
    );

    /// Map action index ↔ name.
    static String get_action_name(int action_index);
    static int get_action_index(const String &action_name);

    /// Map role name → index.
    static int get_role_index(const String &role_name);

    /// Context array size (for pre-allocation).
    static int get_context_size();

protected:
    static void _bind_methods();

private:
    // ── Built-in scorer functions ─────────────────────────────────
    static float _score_engage(const float *ctx);
    static float _score_take_cover(const float *ctx);
    static float _score_suppress(const float *ctx);
    static float _score_flank(const float *ctx);
    static float _score_help_ally(const float *ctx);
    static float _score_carry_ally(const float *ctx);
    static float _score_inject_morphine(const float *ctx);
    static float _score_treat_self(const float *ctx);
    static float _score_reload(const float *ctx);
    static float _score_deploy_mortar(const float *ctx);
    static float _score_fire_mortar(const float *ctx);
    static float _score_launch_recon(const float *ctx);
    static float _score_launch_attack_drone(const float *ctx);
    static float _score_follow_squad(const float *ctx);
    static float _score_retreat(const float *ctx);
    static float _score_idle(const float *ctx);

    // Scorer function pointer table (indexed by ActionID)
    using ScorerFn = float (*)(const float *);
    static const ScorerFn SCORERS[ACT_COUNT];

    // Role weight table: ROLE_WEIGHTS[action][role]
    static const float ROLE_WEIGHTS[ACT_COUNT][ROLE_COUNT];

    // Action name table
    static const char *ACTION_NAMES[ACT_COUNT];
};

} // namespace godot

#endif // UTILITY_EVALUATOR_H
