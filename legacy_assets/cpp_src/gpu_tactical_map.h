#ifndef GPU_TACTICAL_MAP_H
#define GPU_TACTICAL_MAP_H

#include "voxel_world.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <vector>
#include <cstdint>

namespace godot {

/// GPU-accelerated tactical pressure map using RenderingDevice compute shaders.
///
/// Dual resolution: pressure field at 4m/cell (strategic), cover map at 1m/cell (tactical).
/// Uploads a 2D height map from VoxelWorld, dispatches Jacobi pressure diffusion and
/// cover shadow compute shaders, reads results back to CPU for AI queries.
///
/// Usage:
///   var gpu_map = GpuTacticalMap.new()
///   gpu_map.setup(voxel_world, 600.0, 400.0)  # map size in meters
///   gpu_map.tick(friendlies, enemies, threats, goals, strengths)
///   var threat = gpu_map.get_threat_at(pos)
///   var flow = gpu_map.get_flow_vector(pos)
class GpuTacticalMap : public RefCounted {
    GDCLASS(GpuTacticalMap, RefCounted)

public:
    static constexpr int PRESSURE_CELL_M  = 4;     // Meters per pressure cell
    static constexpr int COVER_CELL_M     = 1;     // Meters per cover cell
    static constexpr int MAX_UNITS        = 256;
    static constexpr int MAX_THREATS      = 16;
    static constexpr int MAX_GOALS        = 64;
    static constexpr int DIFFUSION_PASSES = 6;     // Jacobi iterations per tick
    static constexpr int LOCAL_SIZE       = 8;     // Compute workgroup dimension

    GpuTacticalMap();
    ~GpuTacticalMap();

    /// Initialize GPU pipeline. map_w/map_h in meters.
    void setup(VoxelWorld *world, float map_w, float map_h);
    /// GDScript-facing wrapper (avoids cross-DLL VoxelWorld* in bind_method).
    void setup_bind(Object *world_obj, float map_w, float map_h);

    /// Run one compute tick. Call every ~0.5s.
    void tick(
        const PackedVector3Array &friendly_positions,
        const PackedVector3Array &enemy_positions,
        const PackedVector3Array &threat_centroids,
        const PackedVector3Array &goal_positions,
        const PackedFloat32Array &goal_strengths
    );

    /// Rebuild height map from VoxelWorld (call after destruction).
    void rebuild_height_map();

    // ── Queries (read CPU cache, no GPU stall) ─────────────────────

    /// Threat pressure at world position (0-10+).
    float get_threat_at(const Vector3 &pos) const;

    /// Goal attraction at world position (0+).
    float get_goal_at(const Vector3 &pos) const;

    /// Cover value at world position (0.0 exposed, 1.0 fully covered).
    float get_cover_at(const Vector3 &pos) const;

    /// Movement bias vector from pressure gradient. Normalized XZ direction.
    Vector3 get_flow_vector(const Vector3 &pos) const;

    /// True if GPU compute is available and initialized.
    bool is_gpu_available() const { return _gpu_available; }

    // ── Debug ──────────────────────────────────────────────────────

    /// Raw pressure data (4 floats per cell: R=threat, G=goal, B=safety, A=0).
    PackedFloat32Array get_pressure_debug() const;

    /// Raw cover data (1 float per cell).
    PackedFloat32Array get_cover_debug() const;

    /// Grid dimensions.
    int get_pressure_width() const { return _pressure_w; }
    int get_pressure_height() const { return _pressure_h; }
    int get_cover_width() const { return _cover_w; }
    int get_cover_height() const { return _cover_h; }

protected:
    static void _bind_methods();

private:
    // Push constant layout for pressure shader (must be <= 128 bytes)
    struct PressurePushConstants {
        int32_t grid_w;
        int32_t grid_h;
        int32_t pass_index;
        int32_t num_friendlies;
        int32_t num_enemies;
        int32_t num_goals;
        float   decay_rate;
        float   diffusion_rate;
        int32_t standing_voxels_u;  // 1.5m / voxel_scale, for wall blocking
        int32_t pad0;
        int32_t pad1;
        int32_t pad2;
    };
    // CRITICAL: Must match gpu_shaders.h pressure push constant block
    // std430 alignment pads to 16-byte boundary: 10 fields (40 bytes) → 48 bytes
    static_assert(sizeof(PressurePushConstants) == 48, "PressurePushConstants must be exactly 48 bytes for std430 alignment");

    // Push constant layout for cover shader
    struct CoverPushConstants {
        int32_t grid_w;
        int32_t grid_h;
        int32_t num_threats;
        float   max_ray_dist;
        float   shadow_depth;
        float   standing_voxels;
        float   pad0;
        float   pad1;
    };
    // CRITICAL: Must match gpu_shaders.h cover push constant block (8 fields × 4 bytes)
    static_assert(sizeof(CoverPushConstants) == 32, "CoverPushConstants must be exactly 32 bytes (8 × int32/float)");

    // ── Setup helpers ──────────────────────────────────────────────
    bool _create_shaders();
    void _create_buffers();
    void _create_uniform_sets();
    void _cleanup();

    // ── Per-tick helpers ───────────────────────────────────────────
    void _upload_height_map();
    void _upload_units(const PackedVector3Array &friendly, const PackedVector3Array &enemy);
    void _upload_threats(const PackedVector3Array &threats);
    void _upload_goals(const PackedVector3Array &positions, const PackedFloat32Array &strengths);
    void _dispatch_pressure();
    void _dispatch_cover();
    void _readback();

    // ── Coordinate helpers ─────────────────────────────────────────
    int _world_to_pressure_x(float wx) const;
    int _world_to_pressure_z(float wz) const;
    int _world_to_cover_x(float wx) const;
    int _world_to_cover_z(float wz) const;

    // ── State ──────────────────────────────────────────────────────
    RenderingDevice *_rd = nullptr;
    bool _owns_rd = false;   // True if we created a local device (must memdelete)
    bool _gpu_available = false;
    bool _height_map_dirty = true;

    VoxelWorld *_world = nullptr;
    float _map_w = 0.0f, _map_h = 0.0f;

    // Grid dimensions
    int _pressure_w = 0, _pressure_h = 0;  // e.g., 150 x 100
    int _cover_w = 0, _cover_h = 0;        // e.g., 600 x 400

    // Shader + pipeline RIDs
    RID _pressure_shader;
    RID _pressure_pipeline;
    RID _cover_shader;
    RID _cover_pipeline;

    // Storage buffer RIDs
    RID _height_map_buf;
    RID _unit_buf;
    RID _threat_buf;
    RID _goal_buf;
    RID _pressure_buf_a;
    RID _pressure_buf_b;
    RID _cover_buf;

    // Uniform sets (pressure needs two for ping-pong)
    RID _pressure_set_a_to_b;  // reads A, writes B
    RID _pressure_set_b_to_a;  // reads B, writes A
    RID _cover_set;

    // Counts for current tick
    int _num_friendlies = 0;
    int _num_enemies = 0;
    int _num_threats = 0;
    int _num_goals = 0;

    // CPU readback cache
    std::vector<float> _pressure_cache;  // 4 floats per cell (RGBA)
    std::vector<float> _cover_cache;     // 1 float per cell

    // Height map CPU data (uint16 for 0.1m voxel support — max 65535 voxels Y)
    std::vector<uint16_t> _height_map_data;
    float _voxel_scale = 0.25f;  // Cached from VoxelWorld during setup

public:
    /// Direct access to height map for C++ tactical queries (field-of-fire).
    /// uint16 per 1m cell = max solid voxel Y. Index = cz * cover_w + cx.
    const std::vector<uint16_t> &get_height_map_data() const { return _height_map_data; }

    /// Terrain height in meters at world position (fast lookup).
    float get_terrain_height_m(float wx, float wz) const {
        int cx = _world_to_cover_x(wx);
        int cz = _world_to_cover_z(wz);
        if (cx < 0 || cx >= _cover_w || cz < 0 || cz >= _cover_h) return 0.0f;
        return (float)_height_map_data[cz * _cover_w + cx] * _voxel_scale;
    }

    /// Incremental height map update after destruction (only scan affected region).
    /// min/max_cx/cz are cover-grid cell coordinates.
    void update_height_map_region(int min_cx, int max_cx, int min_cz, int max_cz);

    /// World-to-cover-grid coordinate conversion (public for external iteration).
    int cover_to_cell_x(float wx) const { return _world_to_cover_x(wx); }
    int cover_to_cell_z(float wz) const { return _world_to_cover_z(wz); }

    /// Half-map dimensions in meters (for coordinate math).
    float get_half_map_w() const { return _map_w * 0.5f; }
    float get_half_map_h() const { return _map_h * 0.5f; }

    // ── Gas System (Phase: Smoke & Gas Grenades) ──────────────────────

    /// Spawn a gas cloud at world position. Called by grenades, mortars, etc.
    /// @param world_pos: Center of cloud in world coordinates
    /// @param radius_m: Cloud radius in meters
    /// @param initial_density: 0.0-1.0, starting concentration
    /// @param gas_type: 0=none, 1=smoke, 2=tear gas, 3=toxic
    void spawn_gas_cloud(const Vector3 &world_pos, float radius_m, float initial_density, uint8_t gas_type);

    /// Sample gas density at world position (0.0-1.0).
    float sample_gas_density(const Vector3 &pos) const;

    /// Sample gas type at world position (0-3).
    uint8_t sample_gas_type(const Vector3 &pos) const;

    /// Sample gas density along a ray (average density between two points).
    /// Used for LOS checks — if gas > 0.3, vision is blocked.
    float sample_gas_along_ray(const Vector3 &from, const Vector3 &to) const;

private:
    // ── Gas diffusion state ────────────────────────────────────────────
    RID _gas_shader;
    RID _gas_pipeline;
    RID _gas_set_a_to_b;  // reads pressure_buf_a, writes pressure_buf_b
    RID _gas_set_b_to_a;  // reads pressure_buf_b, writes pressure_buf_a

    // Push constant layout for gas shader
    struct GasPushConstants {
        int32_t grid_w;
        int32_t grid_h;
        float   delta_time;
        float   diffusion_rate;  // 0.05
        float   wind_x;          // 0.5
        float   wind_z;          // 0.0
        float   evaporation;     // 0.02
        int32_t wall_threshold_voxels;  // 2.0m / voxel_scale, for gas wall blocking
    };
    // CRITICAL: Must match gpu_shaders.h gas push constant block (8 fields × 4 bytes)
    static_assert(sizeof(GasPushConstants) == 32, "GasPushConstants must be exactly 32 bytes (8 × int32/float)");

    void _create_gas_shader();
    void _dispatch_gas(float delta_time);

    float _gas_wind_x = 0.5f;  // Constant east wind
    float _gas_wind_z = 0.0f;  // No north/south wind
    bool _gas_spawn_dirty = false;  // True when CPU-side gas spawns need GPU upload
};

} // namespace godot

#endif // GPU_TACTICAL_MAP_H
