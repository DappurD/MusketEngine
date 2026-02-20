#include "utility_evaluator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════
//  Static tables
// ═══════════════════════════════════════════════════════════════════

const char *UtilityEvaluatorCPP::ACTION_NAMES[ACT_COUNT] = {
    "engage",             // 0
    "take_cover",         // 1
    "suppress",           // 2
    "flank",              // 3
    "help_ally",          // 4
    "carry_ally",         // 5
    "inject_morphine",    // 6
    "treat_self",         // 7
    "reload",             // 8
    "deploy_mortar",      // 9
    "fire_mortar",        // 10
    "launch_recon",       // 11
    "launch_attack_drone",// 12
    "follow_squad",       // 13
    "retreat",            // 14
    "idle",               // 15
};

const UtilityEvaluatorCPP::ScorerFn UtilityEvaluatorCPP::SCORERS[ACT_COUNT] = {
    &_score_engage,
    &_score_take_cover,
    &_score_suppress,
    &_score_flank,
    &_score_help_ally,
    &_score_carry_ally,
    &_score_inject_morphine,
    &_score_treat_self,
    &_score_reload,
    &_score_deploy_mortar,
    &_score_fire_mortar,
    &_score_launch_recon,
    &_score_launch_attack_drone,
    &_score_follow_squad,
    &_score_retreat,
    &_score_idle,
};

// Role weights: [action][role]
// Roles: rifleman=0, mg=1, marksman=2, at=3, medic=4, leader=5, grenadier=6, mortar=7, drone_op=8
// Unspecified = 1.0
const float UtilityEvaluatorCPP::ROLE_WEIGHTS[ACT_COUNT][ROLE_COUNT] = {
    // ACT_ENGAGE:             mg=1.2, marksman=1.3, at=1.1, medic=0.4, leader=0.75
    { 1.0f, 1.2f, 1.3f, 1.1f, 0.4f, 0.75f, 1.0f, 1.0f, 1.0f },
    // ACT_TAKE_COVER:         medic=1.4, marksman=1.2, mg=0.8, leader=1.4
    { 1.0f, 0.8f, 1.2f, 1.0f, 1.4f, 1.4f, 1.0f, 1.0f, 1.0f },
    // ACT_SUPPRESS:           mg=2.0, rifleman=0.6, marksman=0.2, medic=0.1, grenadier=0.5
    { 0.6f, 2.0f, 0.2f, 1.0f, 0.1f, 1.0f, 0.5f, 1.0f, 1.0f },
    // ACT_FLANK:              rifleman=1.3, grenadier=1.2, mg=0.2, marksman=0.5, medic=0.2, leader=0.3
    { 1.3f, 0.2f, 0.5f, 1.0f, 0.2f, 0.3f, 1.2f, 1.0f, 1.0f },
    // ACT_HELP_ALLY:          medic=1.8, leader=0.8, mg=0.5, marksman=0.5, rifleman=0.7
    { 0.7f, 0.5f, 0.5f, 1.0f, 1.8f, 0.8f, 1.0f, 1.0f, 1.0f },
    // ACT_CARRY_ALLY:         medic=1.5, rifleman=1.0, mg=0.3, marksman=0.4
    { 1.0f, 0.3f, 0.4f, 1.0f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f },
    // ACT_INJECT_MORPHINE:    medic=1.8, leader=0.6, rifleman=0.4
    { 0.4f, 1.0f, 1.0f, 1.0f, 1.8f, 0.6f, 1.0f, 1.0f, 1.0f },
    // ACT_TREAT_SELF:         medic=1.2, leader=1.1
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.2f, 1.1f, 1.0f, 1.0f, 1.0f },
    // ACT_RELOAD:             mg=1.1, at=1.3
    { 1.0f, 1.1f, 1.0f, 1.3f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
    // ACT_DEPLOY_MORTAR:      mortar=2.0, all others=0.0
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f },
    // ACT_FIRE_MORTAR:        mortar=2.0, all others=0.0
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f },
    // ACT_LAUNCH_RECON:       drone_operator=2.0, all others=0.0
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f },
    // ACT_LAUNCH_ATTACK_DRONE: drone_operator=2.0, all others=0.0
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f },
    // ACT_FOLLOW_SQUAD:       leader=0.9
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, 1.0f, 1.0f, 1.0f },
    // ACT_RETREAT:            medic=1.3, leader=1.4
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.3f, 1.4f, 1.0f, 1.0f, 1.0f },
    // ACT_IDLE:               all=1.0
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// ═══════════════════════════════════════════════════════════════════
//  Bindings
// ═══════════════════════════════════════════════════════════════════

void UtilityEvaluatorCPP::_bind_methods() {
    ClassDB::bind_static_method("UtilityEvaluatorCPP",
        D_METHOD("evaluate", "context", "role_index", "current_action_index", "goap_bias_action_index"),
        &UtilityEvaluatorCPP::evaluate);
    ClassDB::bind_static_method("UtilityEvaluatorCPP",
        D_METHOD("get_action_name", "action_index"),
        &UtilityEvaluatorCPP::get_action_name);
    ClassDB::bind_static_method("UtilityEvaluatorCPP",
        D_METHOD("get_action_index", "action_name"),
        &UtilityEvaluatorCPP::get_action_index);
    ClassDB::bind_static_method("UtilityEvaluatorCPP",
        D_METHOD("get_role_index", "role_name"),
        &UtilityEvaluatorCPP::get_role_index);
    ClassDB::bind_static_method("UtilityEvaluatorCPP",
        D_METHOD("get_context_size"),
        &UtilityEvaluatorCPP::get_context_size);

    // Expose enum constants so GDScript can use UtilityEvaluatorCPP.CTX_HP_RATIO etc.
    BIND_CONSTANT(CTX_HP_RATIO);
    BIND_CONSTANT(CTX_MORALE_RATIO);
    BIND_CONSTANT(CTX_SUPPRESSION_PRESSURE);
    BIND_CONSTANT(CTX_BEST_ENEMY_DIST);
    BIND_CONSTANT(CTX_OPTIMAL_RANGE);
    BIND_CONSTANT(CTX_NEARBY_ENEMIES);
    BIND_CONSTANT(CTX_NEARBY_ALLIES);
    BIND_CONSTANT(CTX_HAS_BEST_ENEMY);
    BIND_CONSTANT(CTX_HAS_LINE_OF_FIRE);
    BIND_CONSTANT(CTX_HAS_SQUAD_ORDER);
    BIND_CONSTANT(CTX_IS_IN_COVER);
    BIND_CONSTANT(CTX_IS_FLANKED);
    BIND_CONSTANT(CTX_HAS_NEAREST_COVER);
    BIND_CONSTANT(CTX_NEAREST_COVER_DIST);
    BIND_CONSTANT(CTX_SQUAD_DIST);
    BIND_CONSTANT(CTX_SQUAD_STRENGTH);
    BIND_CONSTANT(CTX_ALLIES_ADVANCING);
    BIND_CONSTANT(CTX_IS_SHAKEN);
    BIND_CONSTANT(CTX_HAS_HELP_TARGET);
    BIND_CONSTANT(CTX_HELP_TARGET_DIST);
    BIND_CONSTANT(CTX_HELP_TARGET_BLEED_RATE);
    BIND_CONSTANT(CTX_HAS_CARRY_TARGET);
    BIND_CONSTANT(CTX_IS_CURRENTLY_CARRYING);
    BIND_CONSTANT(CTX_HAS_MORPHINE_TARGET);
    BIND_CONSTANT(CTX_HAS_NEAREST_DOWNED_ALLY);
    BIND_CONSTANT(CTX_DOWNED_NEEDS_MORPHINE);
    BIND_CONSTANT(CTX_SUPPRESSION_ACTIVE);
    BIND_CONSTANT(CTX_DARKNESS);
    BIND_CONSTANT(CTX_FLASHLIGHT_ON);
    BIND_CONSTANT(CTX_AMMO_RATIO);
    BIND_CONSTANT(CTX_IS_RELOADING);
    BIND_CONSTANT(CTX_FIRE_MISSION_ACTIVE);
    BIND_CONSTANT(CTX_MORTAR_DEPLOYED);
    BIND_CONSTANT(CTX_HAS_RECON_DRONE_AVAILABLE);
    BIND_CONSTANT(CTX_HAS_ACTIVE_RECON_DRONE);
    BIND_CONSTANT(CTX_HAS_ATTACK_DRONE_AVAILABLE);
    BIND_CONSTANT(CTX_ENEMY_IN_COVER);
    BIND_CONSTANT(CTX_HAS_UNTREATED_WOUNDS);
    BIND_CONSTANT(CTX_SQUAD_COMMITTED);
    BIND_CONSTANT(CTX_COUNT);
}

// ═══════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════

Dictionary UtilityEvaluatorCPP::evaluate(
        const PackedFloat32Array &context,
        int role_index,
        int current_action_index,
        int goap_bias_action_index) {

    Dictionary result;

    // Validate inputs
    if (context.size() < CTX_COUNT) {
        result["action"] = (int)ACT_IDLE;
        result["scores"] = PackedFloat32Array();
        return result;
    }

    int role_idx = std::clamp(role_index, 0, (int)ROLE_COUNT - 1);
    const float *ctx = context.ptr();

    PackedFloat32Array scores;
    scores.resize(ACT_COUNT);
    float *scores_ptr = scores.ptrw();

    int best_action = ACT_IDLE;
    float best_score = -1e30f;

    for (int i = 0; i < ACT_COUNT; i++) {
        // Run scorer
        float raw = SCORERS[i](ctx);

        // Apply role weight
        float role_mult = ROLE_WEIGHTS[i][role_idx];
        float final_score = raw * role_mult;

        // GOAP bias
        if (goap_bias_action_index >= 0 && goap_bias_action_index < ACT_COUNT && i == goap_bias_action_index) {
            final_score += GOAP_BIAS_BONUS;
        }

        // Hysteresis
        if (current_action_index >= 0 && current_action_index < ACT_COUNT && i == current_action_index) {
            final_score += HYSTERESIS_BONUS;
        }

        scores_ptr[i] = final_score;

        if (final_score > best_score) {
            best_score = final_score;
            best_action = i;
        }
    }

    result["action"] = best_action;
    result["scores"] = scores;
    return result;
}

String UtilityEvaluatorCPP::get_action_name(int action_index) {
    if (action_index < 0 || action_index >= ACT_COUNT) return "idle";
    return String(ACTION_NAMES[action_index]);
}

int UtilityEvaluatorCPP::get_action_index(const String &action_name) {
    for (int i = 0; i < ACT_COUNT; i++) {
        if (action_name == ACTION_NAMES[i]) return i;
    }
    return ACT_IDLE; // Fallback
}

int UtilityEvaluatorCPP::get_role_index(const String &role_name) {
    if (role_name == "rifleman")       return ROLE_RIFLEMAN;
    if (role_name == "mg")             return ROLE_MG;
    if (role_name == "marksman")       return ROLE_MARKSMAN;
    if (role_name == "at")             return ROLE_AT;
    if (role_name == "medic")          return ROLE_MEDIC;
    if (role_name == "leader")         return ROLE_LEADER;
    if (role_name == "grenadier")      return ROLE_GRENADIER;
    if (role_name == "mortar")         return ROLE_MORTAR;
    if (role_name == "drone_operator") return ROLE_DRONE_OPERATOR;
    return ROLE_RIFLEMAN; // Fallback
}

int UtilityEvaluatorCPP::get_context_size() {
    return CTX_COUNT;
}

// ═══════════════════════════════════════════════════════════════════
//  Scorer implementations — direct port from utility_ai.gd
// ═══════════════════════════════════════════════════════════════════

static inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

// Helper to read bools from the context array (stored as 0.0/1.0)
static inline bool ctx_bool(const float *ctx, int key) {
    return ctx[key] > 0.5f;
}

float UtilityEvaluatorCPP::_score_engage(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_BEST_ENEMY)) return 0.0f;

    float score = 35.0f;
    float dist = ctx[CTX_BEST_ENEMY_DIST];
    float optimal = ctx[CTX_OPTIMAL_RANGE];
    float hp = ctx[CTX_HP_RATIO];
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];
    int allies = (int)ctx[CTX_NEARBY_ALLIES];
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    bool has_los = ctx_bool(ctx, CTX_HAS_LINE_OF_FIRE);
    bool has_squad_order = ctx_bool(ctx, CTX_HAS_SQUAD_ORDER);
    bool in_cover = ctx_bool(ctx, CTX_IS_IN_COVER);
    bool is_flanked = ctx_bool(ctx, CTX_IS_FLANKED);

    if (has_squad_order) score -= 12.0f;

    if (dist < optimal * 1.3f)       score += 15.0f;
    else if (dist > optimal * 2.0f)  score -= 10.0f;

    if (hp > 0.7f)       score += 10.0f;
    else if (hp < 0.5f)  score -= 20.0f;
    if (hp < 0.3f)       score -= 30.0f;

    if (has_los) score += 10.0f;
    else         score -= 15.0f;

    if (allies >= enemies * 2)       score += 15.0f;
    else if (allies >= enemies)      score += 8.0f;
    else if (enemies > allies + 1)   score -= 20.0f;
    if (enemies > allies * 2)        score -= 30.0f;

    if (allies == 0 && enemies > 0)  score -= 25.0f;
    if (in_cover)                    score += 12.0f;
    if (is_flanked)                  score -= 20.0f;

    score -= suppression * 30.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_take_cover(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_NEAREST_COVER)) return 0.0f;

    float score = 20.0f;
    float cover_dist = ctx[CTX_NEAREST_COVER_DIST];
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];
    float hp = ctx[CTX_HP_RATIO];
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    int allies = (int)ctx[CTX_NEARBY_ALLIES];
    bool in_cover = ctx_bool(ctx, CTX_IS_IN_COVER);
    bool is_flanked = ctx_bool(ctx, CTX_IS_FLANKED);

    if (suppression > 0.0f) score += 40.0f * clampf(suppression, 0.0f, 1.0f);

    if (hp < 0.7f) score += 15.0f;
    if (hp < 0.5f) score += 20.0f;
    if (hp < 0.3f) score += 25.0f;

    if (enemies > allies)     score += 20.0f;
    if (enemies > allies * 2) score += 15.0f;

    if (enemies > 0 && !in_cover) score += 15.0f;
    if (is_flanked)               score += 20.0f;

    if (cover_dist < 5.0f)        score += 15.0f;
    else if (cover_dist < 10.0f)  score += 8.0f;
    else if (cover_dist > 25.0f)  score -= 40.0f;
    else if (cover_dist > 18.0f)  score -= 15.0f;

    if (in_cover) score -= 25.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_suppress(const float *ctx) {
    bool active = ctx_bool(ctx, CTX_SUPPRESSION_ACTIVE);
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];

    if (!active && enemies == 0) return 0.0f;

    float score = 10.0f;
    if (active) score += 50.0f;
    if (ctx_bool(ctx, CTX_ALLIES_ADVANCING)) score += 15.0f;
    if (enemies > 0) score += 10.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_flank(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_BEST_ENEMY)) return 0.0f;

    float score = 10.0f;
    bool enemy_in_cover = ctx_bool(ctx, CTX_ENEMY_IN_COVER);
    int allies = (int)ctx[CTX_NEARBY_ALLIES];
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];
    float hp = ctx[CTX_HP_RATIO];

    if (enemy_in_cover) score += 20.0f;

    if (allies >= 3)      score += 18.0f;
    else if (allies >= 2) score += 10.0f;
    else                  score -= 30.0f;

    if (enemies > allies) score -= 20.0f;
    if (suppression > 0.3f) score -= 25.0f;
    if (hp < 0.5f) score -= 20.0f;
    if (hp < 0.3f) score -= 30.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_help_ally(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_HELP_TARGET)) return 0.0f;

    float score = 35.0f;
    float dist = ctx[CTX_HELP_TARGET_DIST];
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];
    float bleed = ctx[CTX_HELP_TARGET_BLEED_RATE];

    if (bleed > 3.0f)      score += 25.0f;
    else if (bleed > 1.0f) score += 10.0f;

    if (enemies == 0)       score += 15.0f;
    if (suppression > 0.5f) score -= 20.0f;
    if (dist > 30.0f)       score -= 10.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_carry_ally(const float *ctx) {
    bool has_target = ctx_bool(ctx, CTX_HAS_CARRY_TARGET);
    bool has_downed = ctx_bool(ctx, CTX_HAS_NEAREST_DOWNED_ALLY);
    bool is_carrying = ctx_bool(ctx, CTX_IS_CURRENTLY_CARRYING);

    if (!has_target && !has_downed && !is_carrying) return 0.0f;

    float score = 30.0f;
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];

    if (is_carrying)        score += 30.0f;
    if (enemies == 0)       score += 15.0f;
    if (suppression > 0.5f) score -= 25.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_inject_morphine(const float *ctx) {
    bool has_target = ctx_bool(ctx, CTX_HAS_MORPHINE_TARGET);
    if (!has_target) {
        bool has_downed = ctx_bool(ctx, CTX_HAS_NEAREST_DOWNED_ALLY);
        bool needs_morphine = ctx_bool(ctx, CTX_DOWNED_NEEDS_MORPHINE);
        if (!has_downed || !needs_morphine) return 0.0f;
    }

    float score = 45.0f;
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];

    if (enemies == 0)       score += 15.0f;
    if (suppression > 0.3f) score -= 15.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_treat_self(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_UNTREATED_WOUNDS)) return 0.0f;

    float score = 25.0f;
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    float hp = ctx[CTX_HP_RATIO];
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];

    if (enemies == 0)       score += 25.0f;
    if (hp < 0.5f)          score += 15.0f;
    if (suppression > 0.0f) score -= 30.0f;
    if (enemies > 0)        score -= 15.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_reload(const float *ctx) {
    float ammo = ctx[CTX_AMMO_RATIO];
    bool is_reloading = ctx_bool(ctx, CTX_IS_RELOADING);
    bool in_cover = ctx_bool(ctx, CTX_IS_IN_COVER);
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];

    if (is_reloading || ammo >= 1.0f) return 0.0f;

    if (ammo <= 0.0f) return 95.0f;

    float score = 0.0f;
    if (ammo < 0.3f && in_cover)          score = 60.0f;
    else if (ammo < 0.3f)                 score = 45.0f;
    else if (ammo < 0.5f && enemies == 0) score = 40.0f;

    return score;
}

float UtilityEvaluatorCPP::_score_deploy_mortar(const float *ctx) {
    if (!ctx_bool(ctx, CTX_FIRE_MISSION_ACTIVE)) return 0.0f;
    if (ctx_bool(ctx, CTX_MORTAR_DEPLOYED))      return 0.0f;
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    if (enemies > 0 && ctx[CTX_BEST_ENEMY_DIST] < 15.0f) return 0.0f;

    float score = 45.0f;
    if (ctx_bool(ctx, CTX_HAS_SQUAD_ORDER)) score -= 10.0f;
    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_fire_mortar(const float *ctx) {
    if (!ctx_bool(ctx, CTX_FIRE_MISSION_ACTIVE)) return 0.0f;

    float score = 30.0f;
    if (ctx_bool(ctx, CTX_MORTAR_DEPLOYED))    score += 35.0f;
    if (ctx[CTX_BEST_ENEMY_DIST] < 12.0f)     score -= 25.0f;
    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_launch_recon(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_RECON_DRONE_AVAILABLE)) return 0.0f;
    if (ctx_bool(ctx, CTX_HAS_ACTIVE_RECON_DRONE))    return 0.0f;

    float score = 15.0f;
    if (!ctx_bool(ctx, CTX_HAS_BEST_ENEMY))   score += 35.0f;
    if (ctx_bool(ctx, CTX_HAS_SQUAD_ORDER))    score += 10.0f;
    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_launch_attack_drone(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_ATTACK_DRONE_AVAILABLE)) return 0.0f;
    if (!ctx_bool(ctx, CTX_HAS_BEST_ENEMY))             return 0.0f;

    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    float score = 25.0f + (float)enemies * 8.0f;
    if (ctx_bool(ctx, CTX_ENEMY_IN_COVER)) score += 12.0f;
    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_follow_squad(const float *ctx) {
    if (!ctx_bool(ctx, CTX_HAS_SQUAD_ORDER)) return 0.0f;

    float score = 85.0f;
    float squad_dist = ctx[CTX_SQUAD_DIST];
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    bool in_cover = ctx_bool(ctx, CTX_IS_IN_COVER);
    bool committed = ctx_bool(ctx, CTX_SQUAD_COMMITTED);

    if (committed) score += 15.0f;

    if (squad_dist > 25.0f)       score += 25.0f;
    else if (squad_dist > 15.0f)  score += 15.0f;
    else if (squad_dist > 8.0f)   score += 8.0f;

    float enemy_dist = ctx[CTX_BEST_ENEMY_DIST];
    if (enemies > 0 && enemy_dist < 10.0f) {
        score -= 15.0f;
    } else if (enemies > 0 && enemy_dist < 20.0f) {
        if (in_cover) score -= 8.0f;
        else          score -= 3.0f;
    }

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_retreat(const float *ctx) {
    float score = 0.0f;
    float hp = ctx[CTX_HP_RATIO];
    float morale = ctx[CTX_MORALE_RATIO];
    int enemies = (int)ctx[CTX_NEARBY_ENEMIES];
    int allies = (int)ctx[CTX_NEARBY_ALLIES];
    float squad_str = ctx[CTX_SQUAD_STRENGTH];
    bool is_shaken = ctx_bool(ctx, CTX_IS_SHAKEN);
    float suppression = ctx[CTX_SUPPRESSION_PRESSURE];
    bool is_flanked = ctx_bool(ctx, CTX_IS_FLANKED);
    bool in_cover = ctx_bool(ctx, CTX_IS_IN_COVER);
    bool has_squad_order = ctx_bool(ctx, CTX_HAS_SQUAD_ORDER);
    bool committed = ctx_bool(ctx, CTX_SQUAD_COMMITTED);

    if (hp < 0.2f)       score += 55.0f;
    else if (hp < 0.35f) score += 30.0f;
    else if (hp < 0.5f)  score += 15.0f;

    if (morale < 0.25f)      score += 40.0f;
    else if (morale < 0.4f)  score += 20.0f;
    else if (morale < 0.6f)  score += 8.0f;

    if (enemies > 0 && allies == 0)  score += 35.0f;
    else if (enemies > allies * 2)   score += 25.0f;
    else if (enemies > allies + 1)   score += 12.0f;

    if (squad_str < 0.2f)       score += 30.0f;
    else if (squad_str < 0.35f) score += 15.0f;

    if (is_shaken)   score += 18.0f;
    if (is_flanked)  score += 15.0f;

    if (suppression > 0.5f && !in_cover) score += 15.0f;

    if (hp < 0.5f && enemies > allies && !in_cover) score += 20.0f;

    if (has_squad_order && committed) score -= 28.0f;

    return clampf(score, 0.0f, 100.0f);
}

float UtilityEvaluatorCPP::_score_idle(const float * /*ctx*/) {
    return 5.0f;
}
