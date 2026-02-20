#ifndef INFLUENCE_MAP_H
#define INFLUENCE_MAP_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <vector>

namespace godot {

/// High-performance influence map using flat contiguous arrays.
/// Replaces Dictionary-based sectors in colony_ai.gd.
/// Instance-based: each ColonyAI creates one via InfluenceMapCPP.new().
///
/// Grid resolution is configurable via sector_size parameter:
/// - Default 30.0m sectors: 10x7 grid (legacy, backward compatible)
/// - 4.0m sectors: 75x50 grid (high-res for voxel worlds)
class InfluenceMapCPP : public RefCounted {
    GDCLASS(InfluenceMapCPP, RefCounted)

public:
    static constexpr float DEFAULT_SECTOR_SIZE = 30.0f;
    static constexpr float RECENCY_DECAY = 0.15f;

    InfluenceMapCPP();
    ~InfluenceMapCPP() = default;

    // ── Setup ─────────────────────────────────────────────────────
    /// Initialize with team and map dimensions.
    /// sector_size: meters per grid cell (default 30.0 for legacy, use 4.0 for high-res).
    void setup(int team, float map_w, float map_h, float sector_size = DEFAULT_SECTOR_SIZE);

    // ── Update (call once per think tick) ─────────────────────────
    /// Rebuilds all layers from packed unit data.
    void update(
        const PackedVector3Array &positions,
        const PackedInt32Array &teams,
        const PackedFloat32Array &in_combat
    );

    /// Update cover quality layer from TacticalCoverMap.
    void update_cover_quality();

    // ── Queries ───────────────────────────────────────────────────
    float get_threat_at(const Vector3 &pos) const;
    Vector3 get_highest_threat_sector() const;
    PackedVector3Array get_opportunity_sectors() const;
    float get_front_line_x(float fallback_front) const;
    int get_enemy_density_near(const Vector3 &pos) const;

    /// Raw layer access for debug/UI.
    float get_friendly(int sx, int sz) const;
    float get_enemy(int sx, int sz) const;
    float get_threat(int sx, int sz) const;
    float get_opportunity(int sx, int sz) const;
    float get_cover_quality(int sx, int sz) const;

    /// Coordinate conversion.
    Vector2i world_to_sector(const Vector3 &pos) const;
    Vector3 sector_to_world(int sx, int sz) const;

    /// Grid info for debug.
    int get_sectors_x() const { return _sectors_x; }
    int get_sectors_z() const { return _sectors_z; }
    float get_sector_size() const { return _sector_size; }

protected:
    static void _bind_methods();

private:
    // Dynamic arrays (contiguous, cache-friendly)
    std::vector<int>   _friendly;
    std::vector<int>   _enemy;
    std::vector<float> _threat;
    std::vector<float> _opportunity;
    std::vector<float> _combat_recency;
    std::vector<float> _cover_quality;

    int   _sectors_x = 0;
    int   _sectors_z = 0;
    int   _grid_size = 0;
    float _sector_size = DEFAULT_SECTOR_SIZE;
    float _map_w = 300.0f;
    float _map_h = 200.0f;
    int   _team  = 1;

    inline int _idx(int sx, int sz) const { return sx * _sectors_z + sz; }
    inline bool _in_bounds(int sx, int sz) const {
        return sx >= 0 && sx < _sectors_x && sz >= 0 && sz < _sectors_z;
    }
};

} // namespace godot

#endif // INFLUENCE_MAP_H
