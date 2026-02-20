#ifndef GPU_CHUNK_CULLER_H
#define GPU_CHUNK_CULLER_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rid.hpp>

namespace godot {

/// GPU-driven frustum + distance culling for voxel chunks.
/// Uses a local RenderingDevice compute shader to test chunk AABBs against
/// the camera frustum in a single dispatch (~0.1ms for 4000 chunks).
/// Follows the same pattern as GpuTacticalMap.
class GpuChunkCuller : public RefCounted {
    GDCLASS(GpuChunkCuller, RefCounted)

public:
    GpuChunkCuller();
    ~GpuChunkCuller();

    /// Initialize the compute pipeline. Call once with the maximum number of chunks.
    void setup(int max_chunks);

    /// Upload chunk AABBs — 6 floats per chunk (min_x, min_y, min_z, max_x, max_y, max_z).
    /// Call after initial meshing and whenever chunks are added/removed.
    void set_chunk_aabbs(const PackedFloat32Array &aabbs);

    /// Run frustum + distance culling on the GPU.
    /// Returns Dictionary {"visible": PackedByteArray, "lod_levels": PackedByteArray}
    /// visible[i] = 1 if chunk i should be rendered, 0 otherwise
    /// lod_levels[i] = 0/1/2 for LOD level
    Dictionary cull(const Projection &view_proj, const Vector3 &camera_pos,
                    float vis_radius, float lod1_dist, float lod2_dist);

    bool is_gpu_available() const;

protected:
    static void _bind_methods();

private:
    bool _create_shader();
    void _create_buffers();
    void _create_uniform_set();
    void _cleanup();

    RenderingDevice *_rd = nullptr;
    bool _owns_rd = false;
    bool _gpu_available = false;

    int _max_chunks = 0;
    int _chunk_count = 0;

    // Shader & pipeline
    RID _shader;
    RID _pipeline;

    // Storage buffers
    RID _aabb_buf;    // Input: 2 vec4 per chunk (padded from 6-float AABBs)
    RID _result_buf;  // Output: 1 uint per chunk (bit 0=visible, bits 1-2=LOD)

    // Uniform set (binding 0=aabb, binding 1=results)
    RID _uniform_set;

    // CPU-side result cache
    PackedByteArray _visible_cache;
    PackedByteArray _lod_cache;
};

} // namespace godot

#endif // GPU_CHUNK_CULLER_H
