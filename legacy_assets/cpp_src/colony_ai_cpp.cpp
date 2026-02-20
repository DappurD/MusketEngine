#include "colony_ai_cpp.h"
#include "tactical_cover_map.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ── Singleton ───────────────────────────────────────────────────────────────
ColonyAICPP *ColonyAICPP::_instances[2] = { nullptr, nullptr };

// ── Static Config Tables ────────────────────────────────────────────────────

const ColonyAICPP::GoalConfig ColonyAICPP::GOAL_CONFIGS[GOAL_COUNT] = {
    // max_squads, min_strength, provides_tags, desires_tags
    { 8,  0.25f, TAG_NONE,                    TAG_SUPPRESS | TAG_OVERWATCH },  // CAPTURE_POI
    { 8,  0.15f, TAG_SUPPRESS | TAG_OVERWATCH, TAG_NONE },                     // DEFEND_POI
    { 6,  0.40f, TAG_PIN | TAG_SUPPRESS,       TAG_OVERWATCH | TAG_FLANK },    // ASSAULT_ENEMY
    { 6,  0.00f, TAG_NONE,                    TAG_NONE },                      // DEFEND_BASE
    { 3,  0.35f, TAG_SUPPRESS,                TAG_SPOTTER | TAG_PIN },         // FIRE_MISSION
    { 6,  0.30f, TAG_FLANK,                   TAG_PIN },                       // FLANK_ENEMY
    { 30, 0.10f, TAG_SUPPRESS | TAG_OVERWATCH, TAG_NONE },                     // HOLD_POSITION
    { 4,  0.20f, TAG_SPOTTER,                TAG_NONE },                      // RECONNAISSANCE
};

const char *ColonyAICPP::GOAL_NAMES[GOAL_COUNT] = {
    "capture_poi", "defend_poi", "assault_enemy",
    "defend_base", "fire_mission", "flank_enemy", "hold_position",
    "reconnaissance"
};

const char *ColonyAICPP::INTENT_ACTIONS[GOAL_COUNT] = {
    "advance",         // capture_poi
    "hold_cover_arc",  // defend_poi
    "suppress_lane",   // assault_enemy
    "retreat",         // defend_base
    "suppress_lane",   // fire_mission
    "flank_slot",      // flank_enemy
    "hold_cover_arc",  // hold_position
    "advance",         // reconnaissance (move toward low-vis areas)
};

// ── Theater Bias Matrix ─────────────────────────────────────────────────────
// Rows: 9 axes (aggression, concentration, tempo, risk_tolerance,
//               exploitation, terrain_control, medical_priority, suppression_dom, intel_coverage)
// Cols: 8 goals (capture, defend_poi, assault, defend_base, fire_mission, flank, hold, recon)

const float ColonyAICPP::THEATER_BIAS
    [TheaterCommander::AXIS_COUNT][ColonyAICPP::GOAL_COUNT] = {
    //                     CAP    DEF_P  ASSLT  DEF_B  FIRE   FLANK  HOLD   RECON
    /* aggression */     { +0.3f, -0.2f, +0.5f, -0.4f, +0.2f, +0.3f, -0.3f, -0.1f },
    /* concentration */  { +0.1f, +0.3f, +0.2f, +0.3f, +0.1f, -0.2f, +0.2f, -0.1f },
    /* tempo */          { +0.4f, -0.1f, +0.3f, -0.2f, +0.1f, +0.2f, -0.3f, +0.0f },
    /* risk_tolerance */ { +0.2f, -0.1f, +0.3f, -0.3f, +0.0f, +0.4f, -0.2f, +0.1f },
    /* exploitation */   { +0.3f, -0.1f, +0.1f, -0.1f, +0.0f, +0.5f, -0.1f, +0.1f },
    /* terrain_ctrl */   { +0.2f, +0.5f, -0.1f, +0.3f, +0.0f, -0.1f, +0.3f, +0.0f },
    /* medical_prio */   { -0.2f, +0.1f, -0.3f, +0.4f, -0.1f, -0.2f, +0.2f, +0.0f },
    /* suppression */    { +0.0f, +0.1f, +0.2f, +0.1f, +0.5f, +0.0f, +0.1f, +0.0f },
    /* intel_coverage */ { -0.1f, +0.0f, -0.1f, +0.0f, +0.0f, +0.0f, +0.0f, +0.6f },
};

// ── Constructor / Destructor ────────────────────────────────────────────────

ColonyAICPP::ColonyAICPP() {
    // Defer per-team slot assignment to setup() where _team is known
    std::memset(_score_matrix, 0, sizeof(_score_matrix));
    std::memset(_prev_goal, -1, sizeof(_prev_goal));
    clear_all_llm_directives();
}

ColonyAICPP::~ColonyAICPP() {
    for (int i = 0; i < 2; i++) {
        if (_instances[i] == this) {
            _instances[i] = nullptr;
        }
    }
}

// ── Setup ───────────────────────────────────────────────────────────────────

void ColonyAICPP::setup(int team, float map_w, float map_h, int squad_count) {
    _team = team;
    _map_w = map_w;
    _map_h = map_h;
    _squad_count = (squad_count > MAX_COLONY_SQUADS) ? MAX_COLONY_SQUADS : squad_count;
    // Register in per-team slot
    int idx = team - 1;
    if (idx >= 0 && idx < 2) {
        _instances[idx] = this;
    }

    for (int i = 0; i < MAX_COLONY_SQUADS; i++) {
        _squads[i] = SquadSnapshot();
    }
    std::memset(_score_matrix, 0, sizeof(_score_matrix));
    clear_all_llm_directives();
}

void ColonyAICPP::set_influence_map(Ref<InfluenceMapCPP> map) {
    _influence_map = map;
}

void ColonyAICPP::set_squad_role(int squad_idx, const String &role_str) {
    if (squad_idx < 0 || squad_idx >= _squad_count) return;
    _squads[squad_idx].role = _role_from_string(role_str);
}

void ColonyAICPP::set_squad_sim_id(int squad_idx, int sim_squad_id) {
    if (squad_idx < 0 || squad_idx >= _squad_count) return;
    _squads[squad_idx].sim_squad_id = sim_squad_id;
}

void ColonyAICPP::set_push_direction(float dir) {
    _push_direction = dir;
}

void ColonyAICPP::set_base_x(float x) {
    _base_x = x;
}

ColonyAICPP::SquadRole ColonyAICPP::_role_from_string(const String &s) {
    if (s == "assault")  return SQUAD_ASSAULT;
    if (s == "defend")   return SQUAD_DEFEND;
    if (s == "flank")    return SQUAD_FLANK;
    if (s == "sniper")   return SQUAD_SNIPER;
    if (s == "recon")    return SQUAD_RECON;
    if (s == "mortar")   return SQUAD_MORTAR;
    return SQUAD_ASSAULT;  // default
}

// ── Main Entry Point ────────────────────────────────────────────────────────

Dictionary ColonyAICPP::plan_intents() {
    uint64_t start_us = Time::get_singleton()->get_ticks_usec();

    _compute_colony_snapshot();
    _compute_squad_snapshots();
    _compute_score_matrix();
    _apply_theater_bias();
    _apply_llm_directives();
    Dictionary result = _run_auction();

    // Store assignments for KPI tracking via get_debug_info()
    _last_assignments = result.get("assignments", Dictionary());

    _last_plan_ms = (float)(Time::get_singleton()->get_ticks_usec() - start_us) / 1000.0f;
    return result;
}

// ── Colony Snapshot ─────────────────────────────────────────────────────────

void ColonyAICPP::_compute_colony_snapshot() {
    SimulationServer *sim = SimulationServer::get_singleton();
    if (!sim) return;

    _cs = ColonySnapshot();
    _cs.base_x = _base_x;
    _cs.push_direction = _push_direction;

    int enemy_team = (_team == 1) ? 2 : 1;
    int unit_count = sim->get_unit_count();

    // Single pass: count alive, cache enemy positions
    for (int i = 0; i < unit_count; i++) {
        if (!sim->is_alive(i)) continue;
        int t = sim->get_team(i);
        if (t == _team) {
            _cs.friendly_alive++;
        } else if (t == enemy_team) {
            // Fog of war: only count/cache visible enemies
            if (!sim->team_can_see(_team, i)) continue;
            _cs.enemy_alive++;
            if (_cs.enemy_cache_count < ColonySnapshot::MAX_ENEMY_CACHE) {
                _cs.enemy_positions[_cs.enemy_cache_count++] = sim->get_position(i);
            }
        }
    }

    // Capture points
    Dictionary cap_data = sim->get_capture_data();
    PackedVector3Array cap_pos = cap_data.get("positions", PackedVector3Array());
    PackedInt32Array cap_owners = cap_data.get("owners", PackedInt32Array());
    PackedFloat32Array cap_progress = cap_data.get("progress", PackedFloat32Array());
    PackedInt32Array cap_capturing = cap_data.get("capturing", PackedInt32Array());
    int cap_count_raw = cap_data.get("count", 0);

    _cs.capture_count = (cap_count_raw > ColonySnapshot::MAX_CAPTURE)
                            ? ColonySnapshot::MAX_CAPTURE : cap_count_raw;

    for (int i = 0; i < _cs.capture_count; i++) {
        _cs.capture_positions[i] = cap_pos[i];
        _cs.capture_owners[i] = (int8_t)(int)cap_owners[i];
        _cs.capture_progress[i] = cap_progress[i];
        _cs.capture_capturing[i] = (int8_t)(int)cap_capturing[i];

        if (_cs.capture_owners[i] == _team) {
            _cs.pois_owned++;
            if (_cs.capture_capturing[i] != 0 &&
                _cs.capture_capturing[i] != _team) {
                _cs.contested_count++;
            }
        } else {
            _cs.capturable_count++;
        }
    }

    // Influence map data
    if (_influence_map.is_valid()) {
        float fallback_front = (_team == 1) ? -_map_w * 0.3f : _map_w * 0.3f;
        _cs.front_line_x = _influence_map->get_front_line_x(fallback_front);
        _cs.highest_threat_sector = _influence_map->get_highest_threat_sector();

        PackedVector3Array opps = _influence_map->get_opportunity_sectors();
        _cs.opportunity_count = ((int)opps.size() > ColonySnapshot::MAX_OPPORTUNITY)
                                    ? ColonySnapshot::MAX_OPPORTUNITY : (int)opps.size();
        for (int i = 0; i < _cs.opportunity_count; i++) {
            _cs.opportunity_sectors[i] = opps[i];
        }
    }
}

// ── Squad Snapshots ─────────────────────────────────────────────────────────

void ColonyAICPP::_compute_squad_snapshots() {
    SimulationServer *sim = SimulationServer::get_singleton();
    if (!sim) return;

    int unit_count = sim->get_unit_count();

    for (int sq = 0; sq < _squad_count; sq++) {
        SquadSnapshot &s = _squads[sq];
        int sim_id = s.sim_squad_id;
        if (sim_id < 0) {
            s.alive_count = 0;
            s.strength = 0.0f;
            s.morale = 0.0f;
            s.is_broken = true;
            continue;
        }

        s.center = sim->get_squad_centroid(sim_id);
        s.alive_count = sim->get_squad_alive_count(sim_id);

        // Iterate units in this squad for strength, morale, mortar check
        float hp_sum = 0.0f;
        float morale_sum = 0.0f;
        bool found_mortar = false;
        int alive_counted = 0;

        for (int i = 0; i < unit_count; i++) {
            if (!sim->is_alive(i)) continue;
            if (sim->get_squad_id(i) != sim_id) continue;

            hp_sum += sim->get_health(i);
            morale_sum += sim->get_morale(i);
            alive_counted++;

            int role = sim->get_role(i);
            if (role == SimulationServer::ROLE_MORTAR ||
                role == SimulationServer::ROLE_GRENADIER) {
                found_mortar = true;
            }
        }

        s.strength = hp_sum;
        s.morale = (alive_counted > 0) ? morale_sum / (float)alive_counted : 0.0f;
        s.has_mortar = found_mortar;
        s.is_broken = (s.alive_count <= 2 || s.morale < 0.25f);
    }
}

// ── Score Matrix ────────────────────────────────────────────────────────────

void ColonyAICPP::_compute_score_matrix() {
    for (int sq = 0; sq < _squad_count; sq++) {
        _score_matrix[sq][GOAL_CAPTURE_POI]   = _score_capture_poi(sq);
        _score_matrix[sq][GOAL_DEFEND_POI]    = _score_defend_poi(sq);
        _score_matrix[sq][GOAL_ASSAULT_ENEMY] = _score_assault_enemy(sq);
        _score_matrix[sq][GOAL_DEFEND_BASE]   = _score_defend_base(sq);
        _score_matrix[sq][GOAL_FIRE_MISSION]  = _score_fire_mission(sq);
        _score_matrix[sq][GOAL_FLANK_ENEMY]   = _score_flank_enemy(sq);
        _score_matrix[sq][GOAL_HOLD_POSITION] = _score_hold_position(sq);
        _score_matrix[sq][GOAL_RECONNAISSANCE] = _score_reconnaissance(sq);
    }
}

// ── Theater Bias ────────────────────────────────────────────────────────────

void ColonyAICPP::_apply_theater_bias() {
    TheaterCommander *tc = TheaterCommander::get_instance(_team);
    if (!tc) return;

    float axis_values[TheaterCommander::AXIS_COUNT];
    for (int a = 0; a < (int)TheaterCommander::AXIS_COUNT; a++) {
        axis_values[a] = tc->get_axis(a);
    }

    for (int g = 0; g < GOAL_COUNT; g++) {
        float bias_sum = 0.0f;
        for (int a = 0; a < (int)TheaterCommander::AXIS_COUNT; a++) {
            bias_sum += axis_values[a] * THEATER_BIAS[a][g];
        }
        float multiplier = _clampf(1.0f + bias_sum, 0.2f, 3.0f);

        for (int sq = 0; sq < _squad_count; sq++) {
            _score_matrix[sq][g] *= multiplier;
            _score_matrix[sq][g] = _clampf(_score_matrix[sq][g], 0.0f, 100.0f);
        }
    }
}

// ── Scoring Functions ───────────────────────────────────────────────────────

float ColonyAICPP::_score_capture_poi(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (s.strength < GOAL_CONFIGS[GOAL_CAPTURE_POI].min_strength) return 0.0f;
    if (_cs.capturable_count == 0) return 5.0f;

    float urgency = 50.0f;
    if (_cs.pois_owned == 0) urgency = 85.0f;
    else if (_cs.pois_owned == 1) urgency = 65.0f;

    float best_poi = -1e9f;
    for (int i = 0; i < _cs.capture_count; i++) {
        if (_cs.capture_owners[i] == _team) continue;
        float dist = _distance_xz(s.center, _cs.capture_positions[i]);
        float ps = 0.0f;
        ps -= dist * 0.4f;
        if (_cs.capture_owners[i] == 0) ps += 25.0f;
        else if (_cs.capture_capturing[i] != 0 &&
                 _cs.capture_capturing[i] != _cs.capture_owners[i])
            ps += 15.0f;
        int en = _count_enemies_near(_cs.capture_positions[i], 25.0f);
        ps -= en * 10.0f;
        if (ps > best_poi) best_poi = ps;
    }

    float score = urgency + best_poi;
    score += s.strength * 15.0f;

    switch (s.role) {
        case SQUAD_ASSAULT: score += 10.0f; break;
        case SQUAD_RECON:   score += 12.0f; break;
        case SQUAD_SNIPER:  score -= 4.0f;  break;
        case SQUAD_MORTAR:  score -= 14.0f; break;
        default: break;
    }

    if (s.morale < 0.3f) score -= 20.0f;

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_defend_poi(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (s.strength < GOAL_CONFIGS[GOAL_DEFEND_POI].min_strength) return 0.0f;
    if (_cs.pois_owned == 0) return 5.0f;

    float best_poi = -1e9f;
    for (int i = 0; i < _cs.capture_count; i++) {
        if (_cs.capture_owners[i] != _team) continue;
        float ps = 40.0f;
        float dist = _distance_xz(s.center, _cs.capture_positions[i]);

        if (dist < 15.0f) ps += 35.0f;
        else if (dist < 30.0f) ps += 15.0f;

        bool contested = (_cs.capture_capturing[i] != 0 &&
                          _cs.capture_capturing[i] != _team);
        if (contested) ps += 40.0f;

        int en = _count_enemies_near(_cs.capture_positions[i], 30.0f);
        ps += en * 8.0f;
        ps -= dist * 0.3f;

        if (ps > best_poi) best_poi = ps;
    }

    float score = best_poi;
    switch (s.role) {
        case SQUAD_DEFEND: score += 12.0f; break;
        case SQUAD_SNIPER: score += 9.0f;  break;
        case SQUAD_MORTAR: score += 7.0f;  break;
        case SQUAD_RECON:  score += 4.0f;  break;
        default: break;
    }
    if (_cs.pois_owned >= 2) score += 15.0f;
    if (s.strength < 0.5f) score += 10.0f;

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_assault_enemy(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (s.strength < GOAL_CONFIGS[GOAL_ASSAULT_ENEMY].min_strength) return 0.0f;
    if (_cs.enemy_cache_count == 0) return 10.0f;
    if (s.morale < 0.3f) return 5.0f;

    float score = 40.0f;
    score += s.strength * 20.0f;
    score += s.morale * 10.0f;

    switch (s.role) {
        case SQUAD_ASSAULT: score += 12.0f; break;
        case SQUAD_MORTAR:  score += 6.0f;  break;
        case SQUAD_SNIPER:  score -= 10.0f; break;
        case SQUAD_RECON:   score -= 4.0f;  break;
        default: break;
    }

    if (s.alive_count >= 4) score += 10.0f;
    else if (s.alive_count <= 2) score -= 20.0f;

    // Opportunity sector proximity
    for (int i = 0; i < _cs.opportunity_count; i++) {
        if (_distance_xz(s.center, _cs.opportunity_sectors[i]) < 60.0f) {
            score += 10.0f;
            break;
        }
    }

    // Local threat penalty via influence map
    if (_influence_map.is_valid()) {
        float local_threat = _influence_map->get_threat_at(s.center);
        if (local_threat > 3.0f) score -= 15.0f;
    }

    // Numerical comparison
    int enemies_near = _count_enemies_near(s.center, 50.0f);
    if (s.alive_count > enemies_near) score += 10.0f;
    else if (enemies_near > s.alive_count * 2) score -= 25.0f;

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_defend_base(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (_cs.enemy_cache_count == 0) return 5.0f;

    float score = 10.0f;

    // Strong squads can actually defend; broken squads retreat to base as triage
    if (s.strength > 0.5f) score += 20.0f;
    else if (s.strength > 0.3f) score += 10.0f;
    else if (s.strength < 0.15f) score += 35.0f;  // triage retreat

    // Broken morale → retreat to base
    if (s.morale < 0.25f) score += 30.0f;

    // Adequate manpower for defense; nearly wiped → triage retreat
    if (s.alive_count >= 4) score += 15.0f;
    else if (s.alive_count <= 1) score += 40.0f;  // triage retreat
    else if (s.alive_count <= 2) score += 20.0f;

    switch (s.role) {
        case SQUAD_MORTAR: score += 8.0f; break;
        case SQUAD_SNIPER: score += 6.0f; break;
        default: break;
    }

    // Active defense: enemies near base = urgent
    int enemy_near_base = _count_enemies_near(
        Vector3(_cs.base_x, 0.0f, 0.0f), 50.0f);
    score += enemy_near_base * 15.0f;

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_fire_mission(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (s.strength < GOAL_CONFIGS[GOAL_FIRE_MISSION].min_strength) return 0.0f;
    if (s.role != SQUAD_MORTAR) return 0.0f;

    float cluster = _best_enemy_cluster_score();
    float score = 35.0f + cluster;

    if (!s.has_mortar) score -= 40.0f;

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_flank_enemy(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (s.strength < GOAL_CONFIGS[GOAL_FLANK_ENEMY].min_strength) return 0.0f;
    if (_cs.enemy_cache_count < 2) return 5.0f;
    if (s.morale < 0.35f) return 5.0f;

    float score = 30.0f;

    if (s.alive_count >= 3 && s.alive_count <= 4) score += 10.0f;

    switch (s.role) {
        case SQUAD_FLANK:  score += 10.0f; break;
        case SQUAD_RECON:  score += 18.0f; break;
        case SQUAD_SNIPER: score += 5.0f;  break;
        case SQUAD_MORTAR: score -= 16.0f; break;
        default: break;
    }

    score += s.strength * 15.0f;
    score += s.morale * 10.0f;

    // Flanking works best when NOT already in the enemy cluster
    int enemies_near = _count_enemies_near(s.center, 25.0f);
    if (enemies_near == 0) score += 20.0f;        // clean approach lane
    else if (enemies_near >= 3) score -= 25.0f;    // already in the thick of it

    // Bonus: squad is roughly perpendicular to the push axis relative to enemy cluster
    Vector3 cluster = _best_enemy_cluster_centroid();
    if (cluster != Vector3()) {
        Vector3 to_squad = s.center - cluster;
        float to_len = std::sqrt(to_squad.x * to_squad.x + to_squad.z * to_squad.z);
        if (to_len > 5.0f) {
            float dot = to_squad.x * _cs.push_direction / to_len;
            float perp = 1.0f - std::abs(dot);  // 0=head-on, 1=perpendicular
            score += perp * 15.0f;  // reward squads already on the flank
        }
    }

    for (int i = 0; i < _cs.opportunity_count; i++) {
        if (_distance_xz(s.center, _cs.opportunity_sectors[i]) < 50.0f) {
            score += 12.0f;
            break;
        }
    }

    // High aggression from TheaterCommander implies likely pinning support
    TheaterCommander *tc = TheaterCommander::get_instance(_team);
    if (tc && tc->get_axis(TheaterCommander::AXIS_AGGRESSION) > 0.6f) {
        score += 10.0f;
    }

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_hold_position(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (s.strength < GOAL_CONFIGS[GOAL_HOLD_POSITION].min_strength) return 0.0f;
    if (s.is_broken) return 5.0f;

    float score = 35.0f;  // moderate base — fallback goal

    // Near a friendly POI → hold and defend it
    for (int i = 0; i < _cs.capture_count; i++) {
        if (_cs.capture_owners[i] != _team) continue;
        float dist = _distance_xz(s.center, _cs.capture_positions[i]);
        if (dist < 20.0f) { score += 20.0f; break; }
        else if (dist < 40.0f) { score += 10.0f; break; }
    }

    // In contact → hold and fight
    int enemies_near = _count_enemies_near(s.center, 35.0f);
    score += std::min(enemies_near * 8.0f, 25.0f);

    // Strength / morale factors
    score += s.strength * 10.0f;
    if (s.morale < 0.3f) score -= 20.0f;
    if (enemies_near == 0 && _cs.enemy_cache_count > 0) score -= 15.0f;  // no contact, go do something
    if (_cs.enemy_cache_count == 0) return 10.0f;

    // Role bonuses: defensive roles prefer holding
    switch (s.role) {
        case SQUAD_DEFEND: score += 10.0f; break;
        case SQUAD_SNIPER: score += 8.0f;  break;
        case SQUAD_MORTAR: score += 5.0f;  break;
        default: break;
    }

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_reconnaissance(int sq) const {
    const SquadSnapshot &s = _squads[sq];
    if (s.strength < GOAL_CONFIGS[GOAL_RECONNAISSANCE].min_strength) return 0.0f;

    // Base score driven by intel axis — when visibility is low, recon is critical
    SimulationServer *sim = SimulationServer::get_singleton();
    float intel_ratio = 1.0f;
    if (sim) {
        int enemy_team = (_team == 1) ? 2 : 1;
        int total_enemy = sim->get_alive_count_for_team(enemy_team);
        if (total_enemy > 0) {
            int visible = 0;
            int count = sim->get_unit_count();
            for (int i = 0; i < count; i++) {
                if (!sim->is_alive(i) || sim->get_team(i) != enemy_team) continue;
                if (sim->team_can_see(_team, i)) visible++;
            }
            intel_ratio = (float)visible / (float)total_enemy;
        }
    }

    // High score when visibility is low
    float score = (1.0f - intel_ratio) * 60.0f + 10.0f;

    // Recon squads strongly prefer this
    if (s.role == SQUAD_RECON) score += 25.0f;
    // Light squads work too
    if (s.role == SQUAD_FLANK) score += 10.0f;
    // Heavy squads should not be recon
    if (s.role == SQUAD_MORTAR || s.role == SQUAD_DEFEND) score -= 15.0f;

    // Don't recon if fully broken
    if (s.is_broken) return 5.0f;

    return _clampf(score, 0.0f, 100.0f);
}

float ColonyAICPP::_score_flank_position(const Vector3 &squad_pos,
                                          const Vector3 &flank_pos,
                                          const Vector3 &enemy_center) const {
    float score = 0.0f;

    // 1. Distance: prefer the closer side (less exposure time)
    float dist = _distance_xz(squad_pos, flank_pos);
    score -= dist * 0.5f;

    // 2. Cover at destination: query TacticalCoverMap
    TacticalCoverMap *tcm = TacticalCoverMap::get_singleton();
    if (tcm) {
        Vector3 threat_dir = enemy_center - flank_pos;
        float cover = tcm->get_cover_value(flank_pos, threat_dir);
        score += cover * 40.0f;
    }

    // 3. Angle of attack: reward perpendicular positions (true flanking)
    Vector3 attack_vec = enemy_center - flank_pos;
    float attack_len = std::sqrt(attack_vec.x * attack_vec.x + attack_vec.z * attack_vec.z);
    if (attack_len > 1.0f) {
        float dot = (attack_vec.x * _cs.push_direction) / attack_len;
        float angle_cos = std::abs(dot);  // 0=perpendicular (ideal), 1=head-on
        score += (1.0f - angle_cos) * 20.0f;
    }

    // 4. Map boundary penalty
    float half_w = _map_w * 0.5f;
    float half_h = _map_h * 0.5f;
    if (flank_pos.x < -half_w + 10.0f || flank_pos.x > half_w - 10.0f ||
        flank_pos.z < -half_h + 10.0f || flank_pos.z > half_h - 10.0f) {
        score -= 50.0f;
    }

    return score;
}

// ── Auction ─────────────────────────────────────────────────────────────────

float ColonyAICPP::_calc_coordination_bonus(
    int sq_idx, int goal_idx, const int *assignments) const
{
    uint16_t desired = GOAL_CONFIGS[goal_idx].desires_tags;
    if (desired == TAG_NONE) return 0.0f;

    float bonus = 0.0f;
    for (int i = 0; i < _squad_count; i++) {
        if (i == sq_idx || assignments[i] < 0) continue;
        uint16_t provides = GOAL_CONFIGS[assignments[i]].provides_tags;
        uint16_t match = desired & provides;
        // Count set bits
        int bits = 0;
        uint16_t tmp = match;
        while (tmp) { bits += (tmp & 1); tmp >>= 1; }
        bonus += bits * 15.0f;
    }
    return std::min(bonus, 75.0f);  // cap to prevent herding
}

Dictionary ColonyAICPP::_run_auction() {
    // 1. Compute regret per squad
    struct SquadRegret {
        int   idx;
        float regret;
    };
    SquadRegret regrets[MAX_COLONY_SQUADS];

    for (int sq = 0; sq < _squad_count; sq++) {
        regrets[sq].idx = sq;
        float best = -1e9f, second = -1e9f;
        for (int g = 0; g < GOAL_COUNT; g++) {
            float s = _score_matrix[sq][g];
            if (s > best) { second = best; best = s; }
            else if (s > second) { second = s; }
        }
        regrets[sq].regret = best - ((second > -1e8f) ? second : 0.0f);
    }

    // 2. Sort by regret descending
    std::sort(regrets, regrets + _squad_count,
        [](const SquadRegret &a, const SquadRegret &b) {
            return a.regret > b.regret;
        });

    // 3. Greedy assignment
    int assignments[MAX_COLONY_SQUADS];
    std::memset(assignments, -1, sizeof(assignments));
    int goal_counts[GOAL_COUNT] = {};

    for (int r = 0; r < _squad_count; r++) {
        int sq = regrets[r].idx;
        int best_goal = -1;
        float best_score = -1e9f;

        for (int g = 0; g < GOAL_COUNT; g++) {
            float score = _score_matrix[sq][g];
            if (score <= 0.0f) continue;

            // Capacity penalty (proportional to squad count)
            int dyn_max = std::min((int)GOAL_CONFIGS[g].max_squads,
                                   std::max(2, _squad_count / 4));
            if (goal_counts[g] >= dyn_max) {
                score *= 0.3f;
            }

            // Coordination bonus (damped when LLM directs squad to a different goal)
            float coord = _calc_coordination_bonus(sq, g, assignments);
            if (_llm_directives[sq].sector_col >= 0) {
                int llm_goal = _intent_to_goal(_llm_directives[sq].intent);
                if (llm_goal >= 0 && g != llm_goal) {
                    coord *= _tune_coord_damping;
                }
            }
            score += coord;

            // Hysteresis: incumbent goal gets bonus to prevent thrashing
            if (_prev_goal[sq] == g) {
                score += GOAL_SWITCH_MARGIN;
            }

            if (score > best_score) {
                best_score = score;
                best_goal = g;
            }
        }

        if (best_goal >= 0) {
            assignments[sq] = best_goal;
            goal_counts[best_goal]++;
        }
    }

    // 4. Emergency capture override
    if (_cs.pois_owned == 0 && _cs.capturable_count > 0) {
        bool has_capture = false;
        for (int sq = 0; sq < _squad_count; sq++) {
            if (assignments[sq] == GOAL_CAPTURE_POI) { has_capture = true; break; }
        }
        if (!has_capture) {
            int strongest = -1;
            float strongest_str = -1e9f;
            for (int sq = 0; sq < _squad_count; sq++) {
                if (_squads[sq].is_broken) continue;
                if (_squads[sq].strength > strongest_str) {
                    strongest_str = _squads[sq].strength;
                    strongest = sq;
                }
            }
            if (strongest >= 0) {
                assignments[strongest] = GOAL_CAPTURE_POI;
            }
        }
    }

    // 5. Store assignments for hysteresis on next cycle
    for (int sq = 0; sq < _squad_count; sq++) {
        _prev_goal[sq] = assignments[sq];
    }

    // 6. Build output (reset POI claims for deconfliction)
    std::memset(_poi_claimed, 0, sizeof(_poi_claimed));
    Dictionary result;
    Dictionary assign_dict;
    Dictionary intent_dict;

    for (int sq = 0; sq < _squad_count; sq++) {
        if (assignments[sq] >= 0) {
            assign_dict[sq] = assignments[sq];
            intent_dict[sq] = _generate_intent(sq, assignments[sq]);
        } else {
            Dictionary hold;
            hold["action"] = String("hold_cover_arc");
            hold["goal_name"] = String("hold_position");
            hold["target_pos"] = _squads[sq].center;
            hold["target_enemy_id"] = 0;
            hold["priority"] = 0.4f;
            hold["issued_ms"] = (int64_t)Time::get_singleton()->get_ticks_msec();
            hold["threat_center"] = _cs.highest_threat_sector;
            intent_dict[sq] = hold;
        }
    }

    result["assignments"] = assign_dict;
    result["squad_intents"] = intent_dict;
    return result;
}

// ── Intent Generation ───────────────────────────────────────────────────────

Dictionary ColonyAICPP::_generate_intent(int sq_idx, int goal_idx) const {
    const SquadSnapshot &s = _squads[sq_idx];

    Dictionary intent;
    intent["action"] = String(INTENT_ACTIONS[goal_idx]);
    intent["goal_name"] = String(GOAL_NAMES[goal_idx]);
    intent["provides_tags"] = (int)GOAL_CONFIGS[goal_idx].provides_tags;
    intent["priority"] = 1.0f;
    intent["issued_ms"] = (int64_t)Time::get_singleton()->get_ticks_msec();
    intent["threat_center"] = _cs.highest_threat_sector;
    intent["target_enemy_id"] = 0;

    Vector3 target_pos = s.center;

    switch ((GoalIndex)goal_idx) {
        case GOAL_CAPTURE_POI: {
            // Deconflict: skip POIs already claimed by another capture squad
            float best_dist = 1e9f;
            int best_poi = -1;
            for (int i = 0; i < _cs.capture_count; i++) {
                if (_cs.capture_owners[i] == _team) continue;
                if (_poi_claimed[i]) continue;
                float dist = _distance_xz(s.center, _cs.capture_positions[i]);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_poi = i;
                    target_pos = _cs.capture_positions[i];
                }
            }
            // If all unclaimed POIs exhausted, fall back to nearest uncaptured
            if (best_poi < 0) {
                for (int i = 0; i < _cs.capture_count; i++) {
                    if (_cs.capture_owners[i] == _team) continue;
                    float dist = _distance_xz(s.center, _cs.capture_positions[i]);
                    if (dist < best_dist) {
                        best_dist = dist;
                        target_pos = _cs.capture_positions[i];
                    }
                }
            } else {
                _poi_claimed[best_poi] = true;
            }
            break;
        }
        case GOAL_DEFEND_POI: {
            float best_score = -1e9f;
            for (int i = 0; i < _cs.capture_count; i++) {
                if (_cs.capture_owners[i] != _team) continue;
                float dist = _distance_xz(s.center, _cs.capture_positions[i]);
                float sc = -dist * 0.3f;
                bool contested = (_cs.capture_capturing[i] != 0 &&
                                  _cs.capture_capturing[i] != _team);
                if (contested) sc += 50.0f;
                if (dist < 15.0f) sc += 30.0f;
                if (sc > best_score) {
                    best_score = sc;
                    target_pos = _cs.capture_positions[i];
                }
            }
            break;
        }
        case GOAL_ASSAULT_ENEMY: {
            bool found_opp = false;
            float best_dist = 1e9f;
            for (int i = 0; i < _cs.opportunity_count; i++) {
                float d = _distance_xz(s.center, _cs.opportunity_sectors[i]);
                if (d < 80.0f && d < best_dist) {
                    best_dist = d;
                    target_pos = _cs.opportunity_sectors[i];
                    found_opp = true;
                }
            }
            if (!found_opp) {
                int nearest = _find_nearest_enemy_idx(s.center);
                if (nearest >= 0) {
                    target_pos = _cs.enemy_positions[nearest];
                } else {
                    target_pos = Vector3(_cs.base_x + _cs.push_direction * 40.0f,
                                         0.0f, 0.0f);
                }
            }
            break;
        }
        case GOAL_DEFEND_BASE: {
            float defend_z = (std::abs(s.center.z) > 5.0f)
                ? ((s.center.z > 0.0f) ? 8.0f : -8.0f) : 0.0f;
            target_pos = Vector3(_cs.base_x + _cs.push_direction * 12.0f,
                                 0.0f, defend_z);
            break;
        }
        case GOAL_FIRE_MISSION: {
            target_pos = _best_enemy_cluster_centroid();
            if (target_pos == Vector3()) {
                target_pos = s.center + Vector3(20.0f * _cs.push_direction, 0.0f, 0.0f);
            }
            break;
        }
        case GOAL_FLANK_ENEMY: {
            if (_cs.enemy_cache_count > 0) {
                // Target the densest enemy cluster, not the global average
                Vector3 cluster_center = _best_enemy_cluster_centroid();
                if (cluster_center == Vector3()) {
                    cluster_center = _cs.enemy_positions[0];
                }

                // Flank axis: perpendicular to squad→enemy direction
                Vector3 to_enemy = cluster_center - s.center;
                float to_len = std::sqrt(to_enemy.x * to_enemy.x + to_enemy.z * to_enemy.z);
                Vector3 flank_dir;
                if (to_len > 5.0f) {
                    flank_dir = Vector3(-to_enemy.z / to_len, 0.0f, to_enemy.x / to_len);
                } else {
                    flank_dir = Vector3(0.0f, 0.0f, _cs.push_direction);
                }

                // Two candidate flank positions at 25m perpendicular
                Vector3 flank_left  = cluster_center + flank_dir * 25.0f;
                Vector3 flank_right = cluster_center - flank_dir * 25.0f;

                // Score candidates: prefer side with better cover + shorter approach
                float score_left  = _score_flank_position(s.center, flank_left, cluster_center);
                float score_right = _score_flank_position(s.center, flank_right, cluster_center);

                target_pos = (score_left >= score_right) ? flank_left : flank_right;
            }
            break;
        }
        case GOAL_HOLD_POSITION: {
            // Hold near current position; gravitate toward nearby friendly POI if within 30m
            float best_poi_dist = 1e9f;
            for (int i = 0; i < _cs.capture_count; i++) {
                if (_cs.capture_owners[i] != _team) continue;
                float dist = _distance_xz(s.center, _cs.capture_positions[i]);
                if (dist < 30.0f && dist < best_poi_dist) {
                    best_poi_dist = dist;
                    target_pos = _cs.capture_positions[i];
                }
            }
            // If no nearby POI, stay put
            if (best_poi_dist >= 1e8f) target_pos = s.center;
            break;
        }
        case GOAL_RECONNAISSANCE: {
            // Move toward the front line, offset along push direction
            // Goal: advance into areas where we have no visibility
            target_pos = s.center + Vector3(_cs.push_direction * 30.0f, 0.0f, 0.0f);
            // If we have enemy intel, probe toward last-known cluster
            Vector3 cluster = _best_enemy_cluster_centroid();
            if (cluster != Vector3()) {
                // Go 20m in front of the cluster (scouting distance)
                Vector3 approach = s.center - cluster;
                float len = std::sqrt(approach.x * approach.x + approach.z * approach.z);
                if (len > 1.0f) {
                    approach.x /= len;
                    approach.z /= len;
                }
                target_pos = cluster + approach * 20.0f;
            }
            break;
        }
        default: break;
    }

    // Universal bounds clamp — prevent squads from targeting map edges
    target_pos.x = _clampf(target_pos.x, -_map_w * 0.47f, _map_w * 0.47f);
    target_pos.z = _clampf(target_pos.z, -_map_h * 0.45f, _map_h * 0.45f);

    intent["target_pos"] = target_pos;
    return intent;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

int ColonyAICPP::_count_enemies_near(const Vector3 &pos, float radius) const {
    int count = 0;
    float r2 = radius * radius;
    for (int i = 0; i < _cs.enemy_cache_count; i++) {
        float dx = pos.x - _cs.enemy_positions[i].x;
        float dz = pos.z - _cs.enemy_positions[i].z;
        if (dx * dx + dz * dz <= r2) count++;
    }
    return count;
}

int ColonyAICPP::_find_nearest_enemy_idx(const Vector3 &pos) const {
    int best = -1;
    float best_d2 = 1e18f;
    for (int i = 0; i < _cs.enemy_cache_count; i++) {
        float dx = pos.x - _cs.enemy_positions[i].x;
        float dz = pos.z - _cs.enemy_positions[i].z;
        float d2 = dx * dx + dz * dz;
        if (d2 < best_d2) { best_d2 = d2; best = i; }
    }
    return best;
}

float ColonyAICPP::_best_enemy_cluster_score() const {
    float best = 0.0f;
    for (int i = 0; i < _cs.enemy_cache_count; i++) {
        int count = 0;
        for (int j = 0; j < _cs.enemy_cache_count; j++) {
            if (_distance_xz(_cs.enemy_positions[i], _cs.enemy_positions[j]) <= 15.0f) {
                count++;
            }
        }
        float s = (float)count * 10.0f;
        if (s > best) best = s;
    }
    return best;
}

Vector3 ColonyAICPP::_best_enemy_cluster_centroid() const {
    Vector3 best_center;
    int best_count = 0;

    for (int i = 0; i < _cs.enemy_cache_count; i++) {
        Vector3 sum;
        int count = 0;
        for (int j = 0; j < _cs.enemy_cache_count; j++) {
            if (_distance_xz(_cs.enemy_positions[i], _cs.enemy_positions[j]) <= 15.0f) {
                sum += _cs.enemy_positions[j];
                count++;
            }
        }
        if (count > best_count) {
            best_count = count;
            best_center = sum / (float)count;
        }
    }
    return best_center;
}

// ── Debug ───────────────────────────────────────────────────────────────────

Dictionary ColonyAICPP::get_debug_info() const {
    Dictionary d;
    d["team"] = _team;
    d["squad_count"] = _squad_count;
    d["last_plan_ms"] = _last_plan_ms;
    d["friendly_alive"] = _cs.friendly_alive;
    d["enemy_alive"] = _cs.enemy_alive;
    d["pois_owned"] = _cs.pois_owned;
    d["capturable"] = _cs.capturable_count;
    d["front_line_x"] = _cs.front_line_x;

    // Theater multipliers
    TheaterCommander *tc = TheaterCommander::get_instance(_team);
    if (tc) {
        Dictionary axes;
        for (int a = 0; a < (int)TheaterCommander::AXIS_COUNT; a++) {
            axes[a] = tc->get_axis(a);
        }
        d["theater_axes"] = axes;
    }

    // Last auction assignments (for KPI tracking)
    d["assignments"] = _last_assignments;

    return d;
}

Dictionary ColonyAICPP::get_score_matrix() const {
    Dictionary result;
    for (int sq = 0; sq < _squad_count; sq++) {
        Dictionary row;
        for (int g = 0; g < GOAL_COUNT; g++) {
            row[String(GOAL_NAMES[g])] = _score_matrix[sq][g];
        }
        result[sq] = row;
    }
    return result;
}

// ── LLM Directive Override ───────────────────────────────────────────────────

void ColonyAICPP::_apply_llm_directives() {
    int64_t now_ms = Time::get_singleton()->get_ticks_msec();
    for (int sq = 0; sq < _squad_count; sq++) {
        auto &d = _llm_directives[sq];
        if (d.sector_col < 0) continue;  // no directive

        float age_s = (float)(now_ms - d.issued_ms) / 1000.0f;
        if (age_s > _tune_llm_age_max) {
            d.sector_col = -1;  // expired — clear
            continue;
        }

        // Soft linear decay after DECAY_START
        float decay = 1.0f;
        if (age_s > _tune_llm_decay_start) {
            decay = 1.0f - (age_s - _tune_llm_decay_start)
                         / (_tune_llm_age_max - _tune_llm_decay_start);
        }
        float effective_conf = d.confidence * decay;

        int goal = _intent_to_goal(d.intent);
        if (goal < 0 || goal >= GOAL_COUNT) continue;

        // Score floor: raise directed goal to at least FLOOR * confidence
        float floor_val = _tune_llm_floor * effective_conf;
        if (_score_matrix[sq][goal] < floor_val) {
            _score_matrix[sq][goal] = floor_val;
        }

        // Directive gravity: dampen competing goals when high confidence
        // This makes the LLM's choice harder to override without completely preventing it
        if (effective_conf > 0.7f) {
            float gravity = effective_conf * 0.3f; // 0.7→0.21 (0.79x), 1.0→0.30 (0.70x)
            for (int g = 0; g < GOAL_COUNT; g++) {
                if (g != goal)
                    _score_matrix[sq][g] *= (1.0f - gravity);
            }
        }
    }
}

int ColonyAICPP::_intent_to_goal(int intent) {
    // Intent enum matches the LLM output vocabulary:
    // 0=ATTACK, 1=DEFEND, 2=FLANK, 3=CAPTURE, 4=RECON, 5=HOLD, 6=FIRE_MISSION, 7=WITHDRAW
    switch (intent) {
        case 0: return GOAL_ASSAULT_ENEMY;
        case 1: return GOAL_DEFEND_POI;
        case 2: return GOAL_FLANK_ENEMY;
        case 3: return GOAL_CAPTURE_POI;
        case 4: return GOAL_RECONNAISSANCE;
        case 5: return GOAL_HOLD_POSITION;
        case 6: return GOAL_FIRE_MISSION;
        case 7: return GOAL_DEFEND_BASE;   // WITHDRAW → retreat to base
        default: return -1;
    }
}

void ColonyAICPP::set_llm_directive(int squad_idx, int sector_col, int sector_row,
                                     int intent, float confidence) {
    if (squad_idx < 0 || squad_idx >= _squad_count) return;
    auto &d = _llm_directives[squad_idx];
    d.sector_col = (int8_t)sector_col;
    d.sector_row = (int8_t)sector_row;
    d.intent     = (uint8_t)intent;
    d.confidence = _clampf(confidence, 0.0f, 1.0f);
    d.issued_ms  = Time::get_singleton()->get_ticks_msec();
}

void ColonyAICPP::clear_llm_directive(int squad_idx) {
    if (squad_idx < 0 || squad_idx >= MAX_COLONY_SQUADS) return;
    _llm_directives[squad_idx].sector_col = -1;
}

void ColonyAICPP::clear_all_llm_directives() {
    for (int i = 0; i < MAX_COLONY_SQUADS; i++)
        _llm_directives[i].sector_col = -1;
}

Dictionary ColonyAICPP::get_llm_directive_debug() const {
    Dictionary result;
    for (int sq = 0; sq < _squad_count; sq++) {
        auto &d = _llm_directives[sq];
        if (d.sector_col < 0) continue;
        Dictionary entry;
        entry["sector_col"] = (int)d.sector_col;
        entry["sector_row"] = (int)d.sector_row;
        entry["intent"]     = (int)d.intent;
        entry["confidence"] = d.confidence;
        entry["age_ms"]     = (int64_t)(Time::get_singleton()->get_ticks_msec() - d.issued_ms);
        result[sq] = entry;
    }
    return result;
}

// ── Tuning API ──────────────────────────────────────────────────────────────

Dictionary ColonyAICPP::get_tuning_params() const {
    Dictionary d;
    d["llm_floor"] = _tune_llm_floor;
    d["llm_age_max"] = _tune_llm_age_max;
    d["llm_decay_start"] = _tune_llm_decay_start;
    d["coord_damping"] = _tune_coord_damping;
    return d;
}

void ColonyAICPP::set_tuning_param(const String &name, float value) {
    if (name == "llm_floor") _tune_llm_floor = value;
    else if (name == "llm_age_max") _tune_llm_age_max = value;
    else if (name == "llm_decay_start") _tune_llm_decay_start = value;
    else if (name == "coord_damping") _tune_coord_damping = value;
}

void ColonyAICPP::reset_tuning_params() {
    _tune_llm_floor = LLM_DIRECTIVE_FLOOR;
    _tune_llm_age_max = LLM_DIRECTIVE_AGE_MAX;
    _tune_llm_decay_start = LLM_DIRECTIVE_DECAY_START;
    _tune_coord_damping = LLM_COORD_DAMPING;
}

// ── Bindings ────────────────────────────────────────────────────────────────

void ColonyAICPP::_bind_methods() {
    // Setup
    ClassDB::bind_method(D_METHOD("setup", "team", "map_w", "map_h", "squad_count"),
                         &ColonyAICPP::setup);
    ClassDB::bind_method(D_METHOD("set_influence_map", "map"),
                         &ColonyAICPP::set_influence_map);
    ClassDB::bind_method(D_METHOD("set_squad_role", "squad_idx", "role_str"),
                         &ColonyAICPP::set_squad_role);
    ClassDB::bind_method(D_METHOD("set_squad_sim_id", "squad_idx", "sim_squad_id"),
                         &ColonyAICPP::set_squad_sim_id);
    ClassDB::bind_method(D_METHOD("set_push_direction", "dir"),
                         &ColonyAICPP::set_push_direction);
    ClassDB::bind_method(D_METHOD("set_base_x", "x"),
                         &ColonyAICPP::set_base_x);

    // LLM Directive Interface
    ClassDB::bind_method(D_METHOD("set_llm_directive", "squad_idx", "sector_col", "sector_row", "intent", "confidence"),
                         &ColonyAICPP::set_llm_directive);
    ClassDB::bind_method(D_METHOD("clear_llm_directive", "squad_idx"),
                         &ColonyAICPP::clear_llm_directive);
    ClassDB::bind_method(D_METHOD("clear_all_llm_directives"),
                         &ColonyAICPP::clear_all_llm_directives);
    ClassDB::bind_method(D_METHOD("get_llm_directive_debug"),
                         &ColonyAICPP::get_llm_directive_debug);

    // Main
    ClassDB::bind_method(D_METHOD("plan_intents"),
                         &ColonyAICPP::plan_intents);

    // Debug
    ClassDB::bind_method(D_METHOD("get_debug_info"),
                         &ColonyAICPP::get_debug_info);
    ClassDB::bind_method(D_METHOD("get_score_matrix"),
                         &ColonyAICPP::get_score_matrix);
    ClassDB::bind_method(D_METHOD("get_last_plan_ms"),
                         &ColonyAICPP::get_last_plan_ms);

    // Tuning API
    ClassDB::bind_method(D_METHOD("get_tuning_params"), &ColonyAICPP::get_tuning_params);
    ClassDB::bind_method(D_METHOD("set_tuning_param", "name", "value"),
                         &ColonyAICPP::set_tuning_param);
    ClassDB::bind_method(D_METHOD("reset_tuning_params"), &ColonyAICPP::reset_tuning_params);

    // Enum constants
    BIND_ENUM_CONSTANT(GOAL_CAPTURE_POI);
    BIND_ENUM_CONSTANT(GOAL_DEFEND_POI);
    BIND_ENUM_CONSTANT(GOAL_ASSAULT_ENEMY);
    BIND_ENUM_CONSTANT(GOAL_DEFEND_BASE);
    BIND_ENUM_CONSTANT(GOAL_FIRE_MISSION);
    BIND_ENUM_CONSTANT(GOAL_FLANK_ENEMY);
    BIND_ENUM_CONSTANT(GOAL_HOLD_POSITION);
    BIND_ENUM_CONSTANT(GOAL_RECONNAISSANCE);
    BIND_ENUM_CONSTANT(GOAL_COUNT);

    BIND_ENUM_CONSTANT(SQUAD_ASSAULT);
    BIND_ENUM_CONSTANT(SQUAD_DEFEND);
    BIND_ENUM_CONSTANT(SQUAD_FLANK);
    BIND_ENUM_CONSTANT(SQUAD_SNIPER);
    BIND_ENUM_CONSTANT(SQUAD_RECON);
    BIND_ENUM_CONSTANT(SQUAD_MORTAR);
    BIND_ENUM_CONSTANT(SQUAD_ROLE_COUNT);
}
