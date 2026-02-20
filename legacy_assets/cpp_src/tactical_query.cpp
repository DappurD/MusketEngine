#include "tactical_query.h"
#include "voxel_world.h"
#include "tactical_cover_map.h"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_map>

using namespace godot;

// ── Static member initialization ─────────────────────────────────────
std::unordered_map<TacticalQueryCPP::CacheKey, TacticalQueryCPP::CacheEntry, TacticalQueryCPP::CacheKeyHash> TacticalQueryCPP::_cache;
int TacticalQueryCPP::_cache_hits = 0;
int TacticalQueryCPP::_cache_misses = 0;

// ── Binding ──────────────────────────────────────────────────────────
void TacticalQueryCPP::_bind_methods() {
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("find_best_combat_pos", "seeker_pos", "radius", "enemy_pos", "space_state", "prefer_flank", "excluded_positions", "min_separation", "navigation_map", "source_tag"), &TacticalQueryCPP::find_best_combat_pos, DEFVAL(false), DEFVAL(PackedVector3Array()), DEFVAL(2.5f), DEFVAL(RID()), DEFVAL(""));
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("find_best_defense_pos", "defender_pos", "defend_point", "threat_direction", "radius", "space_state", "excluded_positions", "min_separation", "navigation_map", "source_tag"), &TacticalQueryCPP::find_best_defense_pos, DEFVAL(PackedVector3Array()), DEFVAL(2.0f), DEFVAL(RID()), DEFVAL(""));
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("find_flank_position", "seeker_pos", "enemy_pos", "ally_pos", "radius", "space_state", "source_tag"), &TacticalQueryCPP::find_flank_position, DEFVAL(""));
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("score_positions", "positions", "enemy_pos", "space_state"), &TacticalQueryCPP::score_positions);
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("has_line_of_sight", "from_pos", "to_pos", "space_state"), &TacticalQueryCPP::has_line_of_sight);
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("is_in_cover_from", "pos", "threat_pos", "space_state"), &TacticalQueryCPP::is_in_cover_from);
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("clear_cache"), &TacticalQueryCPP::clear_cache);
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("get_cache_size"), &TacticalQueryCPP::get_cache_size);
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("get_cache_hits"), &TacticalQueryCPP::get_cache_hits);
    ClassDB::bind_static_method("TacticalQueryCPP", D_METHOD("get_cache_misses"), &TacticalQueryCPP::get_cache_misses);
}

// ── Helpers ──────────────────────────────────────────────────────────

Vector3 TacticalQueryCPP::_safe_normalized(const Vector3 &v, const Vector3 &fallback) {
    float len_sq = v.length_squared();
    if (len_sq < 1e-8f) return fallback;
    return v / std::sqrt(len_sq);
}

// ── Cache ────────────────────────────────────────────────────────────

TacticalQueryCPP::CacheKey TacticalQueryCPP::_make_cache_key(const Vector3 &seeker, const Vector3 &enemy, bool flank) {
    // Quantize to 3m cells (same as grid step) for cache key — Y included for elevation awareness
    CacheKey key;
    key.seeker_cell_x = static_cast<int>(std::floor(seeker.x / QUERY_GRID_STEP));
    key.seeker_cell_y = static_cast<int>(std::floor(seeker.y / QUERY_GRID_STEP));
    key.seeker_cell_z = static_cast<int>(std::floor(seeker.z / QUERY_GRID_STEP));
    key.enemy_cell_x = static_cast<int>(std::floor(enemy.x / QUERY_GRID_STEP));
    key.enemy_cell_y = static_cast<int>(std::floor(enemy.y / QUERY_GRID_STEP));
    key.enemy_cell_z = static_cast<int>(std::floor(enemy.z / QUERY_GRID_STEP));
    key.prefer_flank = flank;
    return key;
}

bool TacticalQueryCPP::_cache_lookup(const CacheKey &key, Vector3 &out_result) {
    auto it = _cache.find(key);
    if (it == _cache.end()) {
        _cache_misses++;
        return false;
    }
    uint64_t now = Time::get_singleton()->get_ticks_msec();
    uint64_t age_ms = now - it->second.timestamp_ms;
    if (age_ms > static_cast<uint64_t>(CACHE_TTL_SEC * 1000.0f)) {
        _cache.erase(it);
        _cache_misses++;
        return false;
    }
    _cache_hits++;
    out_result = it->second.result;
    return true;
}

void TacticalQueryCPP::_cache_store(const CacheKey &key, const Vector3 &result, float score) {
    CacheEntry entry;
    entry.result = result;
    entry.score = score;
    entry.timestamp_ms = Time::get_singleton()->get_ticks_msec();
    _cache[key] = entry;
}

void TacticalQueryCPP::_cache_evict_stale() {
    uint64_t now = Time::get_singleton()->get_ticks_msec();
    uint64_t ttl_ms = static_cast<uint64_t>(CACHE_TTL_SEC * 1000.0f);
    for (auto it = _cache.begin(); it != _cache.end(); ) {
        if (now - it->second.timestamp_ms > ttl_ms) {
            it = _cache.erase(it);
        } else {
            ++it;
        }
    }
}

void TacticalQueryCPP::clear_cache() {
    _cache.clear();
    _cache_hits = 0;
    _cache_misses = 0;
}

void TacticalQueryCPP::advance_tick() {
    _cache_evict_stale();
}

int TacticalQueryCPP::get_cache_size() { return static_cast<int>(_cache.size()); }
int TacticalQueryCPP::get_cache_hits() { return _cache_hits; }
int TacticalQueryCPP::get_cache_misses() { return _cache_misses; }

// ── Raycasting ───────────────────────────────────────────────────────

bool TacticalQueryCPP::_raycast_blocked(
        const Vector3 &from,
        const Vector3 &to,
        PhysicsDirectSpaceState3D *space_state) {
    VoxelWorld *vw = VoxelWorld::get_singleton();
    if (vw != nullptr && vw->is_initialized()) {
        return !vw->check_los(from, to);
    }
    // Fallback: physics raycast for non-voxel scenes
    if (space_state == nullptr) return false;
    Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(from, to, OCCLUDER_MASK);
    query->set_hit_from_inside(false);
    Dictionary result = space_state->intersect_ray(query);
    return !result.is_empty();
}

bool TacticalQueryCPP::_is_candidate_grounded(
        const Vector3 &candidate,
        PhysicsDirectSpaceState3D *space_state) {
    VoxelWorld *vw = VoxelWorld::get_singleton();
    if (vw != nullptr && vw->is_initialized()) {
        // Check voxel below feet: convert to voxel coords and check solid below
        Vector3i vc = vw->world_to_voxel(candidate);
        // Voxel at feet or one below must be solid
        return vw->is_solid(vc.x, vc.y - 1, vc.z) || vw->is_solid(vc.x, vc.y, vc.z);
    }
    // Fallback: physics downward raycast
    if (space_state == nullptr) return false;
    Vector3 from = candidate + Vector3(0, 3.0f, 0);
    Vector3 to = candidate + Vector3(0, -6.0f, 0);
    Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(from, to, GROUND_MASK);
    query->set_hit_from_inside(false);
    Dictionary result = space_state->intersect_ray(query);
    return !result.is_empty();
}

bool TacticalQueryCPP::_is_position_excluded(
        const Vector3 &candidate,
        const PackedVector3Array &excluded_positions,
        float min_separation) {
    if (excluded_positions.size() == 0) return false;
    float min_sep_sq = min_separation * min_separation;
    for (int i = 0; i < excluded_positions.size(); i++) {
        float dx = candidate.x - excluded_positions[i].x;
        float dz = candidate.z - excluded_positions[i].z;
        if (dx * dx + dz * dz < min_sep_sq) return true;
    }
    return false;
}

Vector3 TacticalQueryCPP::_snap_candidate_to_nav(const Vector3 &candidate, const RID &navigation_map) {
    if (!navigation_map.is_valid()) return candidate;
    return NavigationServer3D::get_singleton()->map_get_closest_point(navigation_map, candidate);
}

// ── Scoring ──────────────────────────────────────────────────────────

float TacticalQueryCPP::_score_combat_position(
        const Vector3 &candidate,
        const Vector3 &seeker_pos,
        const Vector3 &enemy_pos,
        PhysicsDirectSpaceState3D *space_state,
        bool prefer_flank) {
    float score = 0.0f;
    const Vector3 eye_offset(0, QUERY_HEIGHT, 0);

    // 1. Cover check: ray from enemy → candidate BLOCKED = safe
    bool has_cover = _raycast_blocked(enemy_pos + eye_offset, candidate + eye_offset, space_state);
    if (has_cover) score += 50.0f;

    // 2. Line of fire: can we shoot from here?
    Vector3 to_enemy = _safe_normalized(enemy_pos - candidate, Vector3(0, 0, 1));
    to_enemy.y = 0.0f;
    Vector3 side_step(-to_enemy.z * 1.5f, 0, to_enemy.x * 1.5f);

    bool has_los_direct = !_raycast_blocked(candidate + eye_offset, enemy_pos + eye_offset, space_state);
    bool has_los_peek = false;
    if (!has_los_direct) {
        // Check peek left
        has_los_peek = !_raycast_blocked(candidate + side_step + eye_offset, enemy_pos + eye_offset, space_state);
        if (!has_los_peek) {
            // Check peek right
            has_los_peek = !_raycast_blocked(candidate - side_step + eye_offset, enemy_pos + eye_offset, space_state);
        }
    }

    if (has_los_direct) score += 30.0f;
    else if (has_los_peek) score += 40.0f;

    if (has_cover && (has_los_direct || has_los_peek)) score += 20.0f;

    // 3. Flanking angle
    Vector3 enemy_to_candidate = _safe_normalized(candidate - enemy_pos, Vector3(0, 0, 1));
    enemy_to_candidate.y = 0.0f;
    Vector3 enemy_forward = _safe_normalized(seeker_pos - enemy_pos, Vector3(0, 0, 1));
    enemy_forward.y = 0.0f;
    float dot = enemy_forward.dot(enemy_to_candidate);

    if (prefer_flank) {
        if (dot < 0.0f) score += 50.0f;
        else if (dot < 0.3f) score += 35.0f;
    } else {
        if (dot < 0.2f) score += 15.0f;
    }

    // 4. Distance penalties
    float dist_to_self = seeker_pos.distance_to(candidate);
    score -= dist_to_self * 0.8f;

    float dist_to_enemy = candidate.distance_to(enemy_pos);
    if (dist_to_enemy < 8.0f) score -= 25.0f;
    else if (dist_to_enemy < 15.0f) score += 10.0f;
    else if (dist_to_enemy < 25.0f) score += 20.0f;
    else if (dist_to_enemy < 35.0f) score += 5.0f;
    else score -= 10.0f;

    // 5. Height advantage scoring
    float height_delta = candidate.y - enemy_pos.y;
    if (height_delta > 0.5f) {
        // Elevated: better cover exposure + vision range
        score += std::min(height_delta * HEIGHT_ADVANTAGE_WEIGHT, 50.0f);
    } else if (height_delta < -1.0f) {
        // Below enemy: penalize (exposed, poor angle)
        score += std::max(height_delta * 2.0f, -10.0f);
    }

    // 6. Voxel cover map quality bonus (if available)
    TacticalCoverMap *tcm = TacticalCoverMap::get_singleton();
    if (tcm != nullptr) {
        Vector3 threat_dir = _safe_normalized(enemy_pos - candidate, Vector3(0, 0, 1));
        float voxel_cover = tcm->get_cover_value(candidate, threat_dir);
        score += voxel_cover * 20.0f;
    }

    return score;
}

// ── Public API ───────────────────────────────────────────────────────

Vector3 TacticalQueryCPP::find_best_combat_pos(
        const Vector3 &seeker_pos,
        float radius,
        const Vector3 &enemy_pos,
        PhysicsDirectSpaceState3D *space_state,
        bool prefer_flank,
        const PackedVector3Array &excluded_positions,
        float min_separation,
        const RID &navigation_map,
        const String &source_tag) {

    // Check cache first (only for non-excluded queries)
    if (excluded_positions.size() == 0) {
        CacheKey key = _make_cache_key(seeker_pos, enemy_pos, prefer_flank);
        Vector3 cached_result;
        if (_cache_lookup(key, cached_result)) {
            return cached_result;
        }
    }

    // Adaptive grid step: coarser grid for larger radii
    float grid_step = (radius > 12.0f) ? QUERY_GRID_STEP_FAR : QUERY_GRID_STEP;
    int half_r = static_cast<int>(radius / grid_step);

    Vector3 best_pos = seeker_pos;
    float best_score = -999.0f;

    VoxelWorld *vw_combat = VoxelWorld::get_singleton();
    for (int xi = -half_r; xi <= half_r; xi++) {
        for (int zi = -half_r; zi <= half_r; zi++) {
            Vector3 raw_candidate = seeker_pos + Vector3(
                static_cast<float>(xi) * grid_step, 0,
                static_cast<float>(zi) * grid_step
            );

            // Snap Y to voxel terrain surface if voxel world is available
            if (vw_combat != nullptr && vw_combat->is_initialized()) {
                Vector3i vc = vw_combat->world_to_voxel(raw_candidate);
                // Scan downward from above the candidate to find the surface
                int scan_top = std::min(vc.y + 16, vw_combat->get_world_size_y() - 1);
                bool found_surface = false;
                for (int sy = scan_top; sy >= 0; sy--) {
                    if (vw_combat->is_solid(vc.x, sy, vc.z)) {
                        // Standing position is one voxel above the solid surface
                        raw_candidate.y = vw_combat->voxel_to_world(vc.x, sy + 1, vc.z).y;
                        found_surface = true;
                        break;
                    }
                }
                if (!found_surface) continue;  // No ground here
            }

            Vector3 candidate = _snap_candidate_to_nav(raw_candidate, navigation_map);

            if (navigation_map.is_valid()) {
                if (std::abs(raw_candidate.y - candidate.y) > MAX_NAV_VERTICAL_DELTA) continue;
                if (raw_candidate.distance_to(candidate) > MAX_NAV_SNAP_DIST) continue;
            }

            if (!_is_candidate_grounded(candidate, space_state)) continue;
            if (_is_position_excluded(candidate, excluded_positions, min_separation)) continue;

            float score = _score_combat_position(candidate, seeker_pos, enemy_pos, space_state, prefer_flank);

            if (score > best_score) {
                best_score = score;
                best_pos = candidate;
            }

            // Early-out: if this is an excellent position, stop searching
            if (best_score >= EARLY_OUT_SCORE) goto done;
        }
    }

done:
    // Store in cache
    if (excluded_positions.size() == 0) {
        CacheKey key = _make_cache_key(seeker_pos, enemy_pos, prefer_flank);
        _cache_store(key, best_pos, best_score);
    }

    // Periodically evict stale entries
    if (_cache.size() > 128) {
        _cache_evict_stale();
    }

    return best_pos;
}

Vector3 TacticalQueryCPP::find_best_defense_pos(
        const Vector3 &defender_pos,
        const Vector3 &defend_point,
        const Vector3 &threat_direction,
        float radius,
        PhysicsDirectSpaceState3D *space_state,
        const PackedVector3Array &excluded_positions,
        float min_separation,
        const RID &navigation_map,
        const String &source_tag) {

    float grid_step = (radius > 12.0f) ? QUERY_GRID_STEP_FAR : QUERY_GRID_STEP;
    int half_r = static_cast<int>(radius / grid_step);

    Vector3 threat_origin = defend_point + threat_direction * 30.0f;
    Vector3 best_pos = defend_point;
    float best_score = -999.0f;

    const Vector3 eye_offset(0, QUERY_HEIGHT, 0);
    VoxelWorld *vw_defend = VoxelWorld::get_singleton();

    for (int xi = -half_r; xi <= half_r; xi++) {
        for (int zi = -half_r; zi <= half_r; zi++) {
            Vector3 raw_candidate = defend_point + Vector3(
                static_cast<float>(xi) * grid_step, 0,
                static_cast<float>(zi) * grid_step
            );

            // Snap Y to voxel terrain surface
            if (vw_defend != nullptr && vw_defend->is_initialized()) {
                Vector3i vc = vw_defend->world_to_voxel(raw_candidate);
                int scan_top = std::min(vc.y + 16, vw_defend->get_world_size_y() - 1);
                bool found_surface = false;
                for (int sy = scan_top; sy >= 0; sy--) {
                    if (vw_defend->is_solid(vc.x, sy, vc.z)) {
                        raw_candidate.y = vw_defend->voxel_to_world(vc.x, sy + 1, vc.z).y;
                        found_surface = true;
                        break;
                    }
                }
                if (!found_surface) continue;
            }

            Vector3 candidate = _snap_candidate_to_nav(raw_candidate, navigation_map);

            if (navigation_map.is_valid()) {
                if (std::abs(raw_candidate.y - candidate.y) > MAX_NAV_VERTICAL_DELTA) continue;
                if (raw_candidate.distance_to(candidate) > MAX_NAV_SNAP_DIST) continue;
            }

            if (!_is_candidate_grounded(candidate, space_state)) continue;
            if (_is_position_excluded(candidate, excluded_positions, min_separation)) continue;

            float score = 0.0f;

            // 1. Must be close to the defense point
            float dist_to_point = candidate.distance_to(defend_point);
            if (dist_to_point > radius) continue;
            score += (radius - dist_to_point) * 1.5f;

            // 2. Cover from threat direction
            if (_raycast_blocked(threat_origin + eye_offset, candidate + eye_offset, space_state)) {
                score += 45.0f;
            }

            // 3. LOS to approach corridors (left and right of threat direction)
            Vector3 approach_left = defend_point + threat_direction.rotated(Vector3(0, 1, 0), 0.4f) * 20.0f;
            Vector3 approach_right = defend_point + threat_direction.rotated(Vector3(0, 1, 0), -0.4f) * 20.0f;

            if (!_raycast_blocked(candidate + eye_offset, approach_left + eye_offset, space_state)) {
                score += 15.0f;
            }
            if (!_raycast_blocked(candidate + eye_offset, approach_right + eye_offset, space_state)) {
                score += 15.0f;
            }

            // 4. Spread from current position
            float dist_to_self = candidate.distance_to(defender_pos);
            score -= std::abs(dist_to_self - 5.0f) * 0.5f;

            if (score > best_score) {
                best_score = score;
                best_pos = candidate;
            }

            if (best_score >= EARLY_OUT_SCORE) break;
        }
        if (best_score >= EARLY_OUT_SCORE) break;
    }

    return best_pos;
}

Vector3 TacticalQueryCPP::find_flank_position(
        const Vector3 &seeker_pos,
        const Vector3 &enemy_pos,
        const Vector3 &ally_pos,
        float radius,
        PhysicsDirectSpaceState3D *space_state,
        const String &source_tag) {

    Vector3 ally_to_enemy = _safe_normalized(enemy_pos - ally_pos, Vector3(0, 0, 1));
    ally_to_enemy.y = 0.0f;
    Vector3 perp(-ally_to_enemy.z, 0, ally_to_enemy.x);

    Vector3 left_pos = enemy_pos + perp * 15.0f;
    Vector3 right_pos = enemy_pos - perp * 15.0f;
    Vector3 flank_center = (seeker_pos.distance_to(left_pos) < seeker_pos.distance_to(right_pos))
        ? left_pos : right_pos;

    return find_best_combat_pos(flank_center, radius, enemy_pos, space_state, true, PackedVector3Array(), 2.5f, RID(), source_tag);
}

Array TacticalQueryCPP::score_positions(
        const PackedVector3Array &positions,
        const Vector3 &enemy_pos,
        PhysicsDirectSpaceState3D *space_state) {
    Array scored;
    for (int i = 0; i < positions.size(); i++) {
        float score = _score_combat_position(positions[i], positions[i], enemy_pos, space_state, false);
        Dictionary entry;
        entry["pos"] = positions[i];
        entry["score"] = score;
        scored.append(entry);
    }
    // Sort by score descending
    scored.sort_custom(Callable());  // Will need GDScript sort — return unsorted for now, sort in GDScript
    return scored;
}

bool TacticalQueryCPP::has_line_of_sight(
        const Vector3 &from_pos,
        const Vector3 &to_pos,
        PhysicsDirectSpaceState3D *space_state) {
    const Vector3 eye_offset(0, QUERY_HEIGHT, 0);
    return !_raycast_blocked(from_pos + eye_offset, to_pos + eye_offset, space_state);
}

bool TacticalQueryCPP::is_in_cover_from(
        const Vector3 &pos,
        const Vector3 &threat_pos,
        PhysicsDirectSpaceState3D *space_state) {
    const Vector3 eye_offset(0, QUERY_HEIGHT, 0);
    return _raycast_blocked(threat_pos + eye_offset, pos + eye_offset, space_state);
}
