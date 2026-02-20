#ifndef TACTICAL_QUERY_CPP_H
#define TACTICAL_QUERY_CPP_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/navigation_server3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <unordered_map>
#include <functional>

namespace godot {

/// High-performance C++ port of TacticalQuery (TEQS).
/// Drop-in replacement with result caching, adaptive grid, and early-out scoring.
class TacticalQueryCPP : public RefCounted {
    GDCLASS(TacticalQueryCPP, RefCounted)

public:
    // ── Configuration constants ──────────────────────────────────
    static constexpr float QUERY_GRID_STEP      = 3.0f;
    static constexpr float QUERY_GRID_STEP_FAR  = 5.0f;  // Adaptive: used when radius > 12m
    static constexpr float QUERY_HEIGHT          = 1.2f;
    static constexpr float MIN_COVER_DIST        = 2.0f;
    static constexpr int   OCCLUDER_MASK         = 4 | 8 | 64;  // Buildings + Cover + Trees
    static constexpr int   GROUND_MASK           = 1;
    static constexpr float MAX_NAV_SNAP_DIST     = 2.5f;
    static constexpr float MAX_NAV_VERTICAL_DELTA = 1.0f;
    static constexpr float EARLY_OUT_SCORE       = 85.0f;  // Skip remaining if candidate scores this high
    static constexpr float CACHE_TTL_SEC         = 4.0f;   // Result cache lifetime
    static constexpr float HEIGHT_ADVANTAGE_WEIGHT = 5.0f;  // Score bonus per meter above enemy

    // ── Public API (matches GDScript TacticalQuery) ──────────────
    static Vector3 find_best_combat_pos(
        const Vector3 &seeker_pos,
        float radius,
        const Vector3 &enemy_pos,
        PhysicsDirectSpaceState3D *space_state,
        bool prefer_flank = false,
        const PackedVector3Array &excluded_positions = PackedVector3Array(),
        float min_separation = 2.5f,
        const RID &navigation_map = RID(),
        const String &source_tag = ""
    );

    static Vector3 find_best_defense_pos(
        const Vector3 &defender_pos,
        const Vector3 &defend_point,
        const Vector3 &threat_direction,
        float radius,
        PhysicsDirectSpaceState3D *space_state,
        const PackedVector3Array &excluded_positions = PackedVector3Array(),
        float min_separation = 2.0f,
        const RID &navigation_map = RID(),
        const String &source_tag = ""
    );

    static Vector3 find_flank_position(
        const Vector3 &seeker_pos,
        const Vector3 &enemy_pos,
        const Vector3 &ally_pos,
        float radius,
        PhysicsDirectSpaceState3D *space_state,
        const String &source_tag = ""
    );

    static Array score_positions(
        const PackedVector3Array &positions,
        const Vector3 &enemy_pos,
        PhysicsDirectSpaceState3D *space_state
    );

    static bool has_line_of_sight(
        const Vector3 &from_pos,
        const Vector3 &to_pos,
        PhysicsDirectSpaceState3D *space_state
    );

    static bool is_in_cover_from(
        const Vector3 &pos,
        const Vector3 &threat_pos,
        PhysicsDirectSpaceState3D *space_state
    );

    // ── Cache management ─────────────────────────────────────────
    static void advance_tick();
    static void clear_cache();
    static int get_cache_size();
    static int get_cache_hits();
    static int get_cache_misses();

protected:
    static void _bind_methods();

private:
    // ── Internal scoring ─────────────────────────────────────────
    static float _score_combat_position(
        const Vector3 &candidate,
        const Vector3 &seeker_pos,
        const Vector3 &enemy_pos,
        PhysicsDirectSpaceState3D *space_state,
        bool prefer_flank
    );

    static bool _raycast_blocked(
        const Vector3 &from,
        const Vector3 &to,
        PhysicsDirectSpaceState3D *space_state
    );

    static bool _is_candidate_grounded(
        const Vector3 &candidate,
        PhysicsDirectSpaceState3D *space_state
    );

    static bool _is_position_excluded(
        const Vector3 &candidate,
        const PackedVector3Array &excluded_positions,
        float min_separation
    );

    static Vector3 _snap_candidate_to_nav(
        const Vector3 &candidate,
        const RID &navigation_map
    );

    static Vector3 _safe_normalized(const Vector3 &v, const Vector3 &fallback);

    // ── Result cache ─────────────────────────────────────────────
    struct CacheEntry {
        Vector3 result;
        uint64_t timestamp_ms;
        float score;
    };

    struct CacheKey {
        int seeker_cell_x, seeker_cell_y, seeker_cell_z;
        int enemy_cell_x, enemy_cell_y, enemy_cell_z;
        bool prefer_flank;

        bool operator==(const CacheKey &other) const {
            return seeker_cell_x == other.seeker_cell_x
                && seeker_cell_y == other.seeker_cell_y
                && seeker_cell_z == other.seeker_cell_z
                && enemy_cell_x == other.enemy_cell_x
                && enemy_cell_y == other.enemy_cell_y
                && enemy_cell_z == other.enemy_cell_z
                && prefer_flank == other.prefer_flank;
        }
    };

    struct CacheKeyHash {
        size_t operator()(const CacheKey &k) const {
            size_t h = 0;
            h ^= std::hash<int>()(k.seeker_cell_x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.seeker_cell_y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.seeker_cell_z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.enemy_cell_x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.enemy_cell_y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.enemy_cell_z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<bool>()(k.prefer_flank) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    static std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> _cache;
    static int _cache_hits;
    static int _cache_misses;

    static CacheKey _make_cache_key(const Vector3 &seeker, const Vector3 &enemy, bool flank);
    static bool _cache_lookup(const CacheKey &key, Vector3 &out_result);
    static void _cache_store(const CacheKey &key, const Vector3 &result, float score);
    static void _cache_evict_stale();
};

} // namespace godot

#endif // TACTICAL_QUERY_CPP_H
