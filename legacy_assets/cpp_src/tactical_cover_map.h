#ifndef TACTICAL_COVER_MAP_H
#define TACTICAL_COVER_MAP_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <vector>
#include <cstdint>

namespace godot {

/// Computes dynamic cover from voxel geometry via shadow casting.
///
/// For each enemy threat position, casts rays outward on the XZ plane.
/// Where rays hit solid voxels, cells behind the voxels are marked as
/// "in shadow" (covered). When voxels are destroyed, the cover map
/// recalculates on the next update — cover instantly vanishes.
///
/// Replaces manually-placed CoverNode3D for voxel worlds.
class TacticalCoverMap : public RefCounted {
    GDCLASS(TacticalCoverMap, RefCounted)

public:
    // Cover cell = round(1.0 / voxel_scale) voxels = 1m at any voxel scale
    static constexpr int   MAX_THREATS      = 16;
    static constexpr int   RAY_COUNT        = 36;     // Every 10 degrees
    static constexpr float RAY_MAX_DIST_M   = 60.0f;  // Max shadow cast distance in meters
    static constexpr float SHADOW_DEPTH_M   = 4.0f;   // How far cover extends behind a wall
    static constexpr float STANDING_MIN_M   = 0.5f;   // Min height for a "wall" in meters
    static constexpr float STANDING_MAX_M   = 3.0f;   // Max height checked for "wall" in meters

    TacticalCoverMap();
    ~TacticalCoverMap();

    /// Initialize the cover map grid.
    /// world_size_x/z: voxel world dimensions (in voxels).
    /// voxel_scale: meters per voxel (typically 0.25).
    void setup(int world_size_x, int world_size_z, float voxel_scale);

    /// Recompute cover shadows from current threat positions.
    /// Call once per AI tick with enemy squad centroids.
    void update_cover(const PackedVector3Array &threat_positions);

    /// Query directional cover value at a world position.
    /// Returns 0.0 (fully exposed) to 1.0 (fully covered).
    float get_cover_value(const Vector3 &position, const Vector3 &threat_direction) const;

    /// Query best cover at a position from any tracked threat.
    float get_best_cover_at(const Vector3 &position) const;

    /// Find the best covered position within radius from a threat.
    /// Returns Vector3(0,0,0) if no covered position found.
    Vector3 find_covered_position(const Vector3 &from, const Vector3 &threat_pos, float radius) const;

    /// Check if a position is covered from a specific threat.
    bool is_covered_from(const Vector3 &position, const Vector3 &threat_pos) const;

    /// Grid dimensions for debug visualization.
    int get_cells_x() const { return _cells_x; }
    int get_cells_z() const { return _cells_z; }

    /// Raw cell cover value for influence map integration.
    float get_cell_cover(int cx, int cz) const;

    /// Public coordinate helpers for external grid iteration (SimulationServer).
    int world_to_cell_x(float wx) const { return _world_to_cell_x(wx); }
    int world_to_cell_z(float wz) const { return _world_to_cell_z(wz); }
    float cell_to_world_x(int cx) const { return _cell_to_world_x(cx); }
    float cell_to_world_z(int cz) const { return _cell_to_world_z(cz); }
    bool cell_in_bounds(int cx, int cz) const { return _cell_in_bounds(cx, cz); }

    /// Singleton access for C++ AI code.
    static TacticalCoverMap *get_singleton() { return _singleton; }

protected:
    static void _bind_methods();

private:
    static TacticalCoverMap *_singleton;

    int _cells_x = 0, _cells_z = 0;
    float _voxel_scale = 0.25f;
    int _cell_voxels = 4;        // round(1.0 / voxel_scale) — voxels per 1m cover cell
    float _cell_size_m = 1.0f;   // Meters per cover cell
    float _world_offset_x = 0.0f;  // World-space offset for centering
    float _world_offset_z = 0.0f;

    // Aggregate cover (max from all threats)
    std::vector<float> _cover;   // [_cells_z * _cells_x]

    // Per-threat shadow data
    struct ThreatShadow {
        Vector3 position;
        std::vector<float> shadow;
        bool active = false;
    };
    ThreatShadow _threat_shadows[MAX_THREATS];

    // Coordinate conversion
    int _world_to_cell_x(float wx) const;
    int _world_to_cell_z(float wz) const;
    float _cell_to_world_x(int cx) const;
    float _cell_to_world_z(int cz) const;
    int _cell_index(int cx, int cz) const;
    bool _cell_in_bounds(int cx, int cz) const;

    // Shadow casting for one threat
    void _cast_threat_shadow(ThreatShadow &shadow);

    // Aggregate all threat shadows into _cover
    void _aggregate_cover();
};

} // namespace godot

#endif // TACTICAL_COVER_MAP_H
