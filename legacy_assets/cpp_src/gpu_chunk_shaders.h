#ifndef GPU_CHUNK_SHADERS_H
#define GPU_CHUNK_SHADERS_H

namespace godot {

/// Compute shader for distance-based LOD + visibility of voxel chunks.
/// Workgroup: 64 threads, 1 chunk per thread.
/// NOTE: Frustum culling is handled by Godot's built-in RenderingServer
/// via instance_set_custom_aabb (runs every frame). This shader only does
/// distance-based visibility + LOD level selection (runs every 0.5s).
/// Input: AABB buffer (2 vec4 per chunk), push constants (camera + thresholds).
/// Output: 1 uint per chunk — bit 0 = visible, bits 1-2 = LOD level (0/1/2).
static const char *CHUNK_CULL_GLSL = R"glsl(
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std430, set = 0, binding = 0) restrict readonly buffer ChunkAABBs {
    vec4 aabb_data[];  // 2 vec4 per chunk: [min_xyz_pad, max_xyz_pad]
};

layout(std430, set = 0, binding = 1) restrict writeonly buffer Results {
    uint results[];  // 1 uint per chunk: bit 0 = visible, bits 1-2 = lod
};

layout(push_constant, std430) uniform Params {
    mat4 view_projection;   // 64 bytes (reserved, unused currently)
    vec4 camera_pos_vis;    // xyz = camera pos, w = vis_radius_sq  (16)
    vec4 lod_thresholds;    // x = lod1_sq, y = lod2_sq             (16)
    uint chunk_count;       // 4
    uint pad0, pad1, pad2;  // 12 padding
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= chunk_count) return;

    vec3 bmin = aabb_data[idx * 2].xyz;
    vec3 bmax = aabb_data[idx * 2 + 1].xyz;
    vec3 center = (bmin + bmax) * 0.5;

    // Distance check only — Godot handles frustum culling per-frame
    vec3 diff = center - camera_pos_vis.xyz;
    float dist_sq = dot(diff, diff);

    bool visible = dist_sq <= camera_pos_vis.w;

    // LOD level: 0 = full, 1 = medium, 2 = low
    uint lod = 0u;
    if (dist_sq > lod_thresholds.y) lod = 2u;
    else if (dist_sq > lod_thresholds.x) lod = 1u;

    results[idx] = (visible ? 1u : 0u) | (lod << 1u);
}
)glsl";

} // namespace godot

#endif // GPU_CHUNK_SHADERS_H
