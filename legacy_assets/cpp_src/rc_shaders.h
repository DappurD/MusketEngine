#ifndef RC_SHADERS_H
#define RC_SHADERS_H

namespace godot {

// ═══════════════════════════════════════════════════════════════════════
//  Pass 1 — Trace: ray-march per probe per direction within cascade interval
//  Workgroup: 64 threads, each handles one (probe, direction) pair.
//  Reads color + depth via sampler, writes cascade texture via image.
// ═══════════════════════════════════════════════════════════════════════

static const char *RC_TRACE_GLSL = R"glsl(
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D color_tex;
layout(set = 0, binding = 1) uniform sampler2D depth_tex;
layout(rgba16f, set = 0, binding = 2) writeonly uniform image2D cascade_tex;

layout(push_constant, std430) uniform Params {
    ivec2 screen_size;        // 8
    ivec2 probes_count;       // 8
    int   probe_spacing;      // 4
    int   interval_start;     // 4
    int   interval_length;    // 4
    int   step_size;          // 4
    vec4  sky_color;          // 16 (aligned to 16 at offset 32)
    float depth_threshold;    // 4
    int   total_probes;       // 4
    int   pad0, pad1;         // 8
};  // Total: 64 bytes

const float PI = 3.14159265359;
const int NUM_DIRS = 16;

void main() {
    uint gid = gl_GlobalInvocationID.x;
    int probe_idx = int(gid) / NUM_DIRS;
    int dir = int(gid) % NUM_DIRS;

    if (probe_idx >= total_probes) return;

    int px = probe_idx % probes_count.x;
    int py = probe_idx / probes_count.x;

    // Probe screen position (center of its cell)
    vec2 probe_pos = (vec2(px, py) + 0.5) * float(probe_spacing);
    probe_pos = clamp(probe_pos, vec2(0.5), vec2(screen_size) - 0.5);

    // Ray direction (16 equally-spaced angles)
    float angle = float(dir) * (2.0 * PI / float(NUM_DIRS));
    vec2 rd = vec2(cos(angle), sin(angle));

    // Probe depth (reverse-Z: 1.0 = near, 0.0 = far/sky)
    float probe_depth = texelFetch(depth_tex, ivec2(probe_pos), 0).r;
    float prev_depth = probe_depth;

    vec3 radiance = vec3(0.0);
    float transmittance = 1.0;

    int num_steps = max(interval_length / max(step_size, 1), 1);
    num_steps = min(num_steps, 64);

    for (int s = 0; s < num_steps; s++) {
        float t = float(interval_start) + float(s) * float(step_size);
        if (t < 1.0) t = 1.0;  // never sample probe's own pixel
        vec2 sp = probe_pos + rd * t;

        // Screen bounds check
        if (sp.x < 0.0 || sp.x >= float(screen_size.x) ||
            sp.y < 0.0 || sp.y >= float(screen_size.y)) {
            radiance = sky_color.rgb;
            transmittance = 0.0;
            break;
        }

        ivec2 spi = ivec2(sp);
        float d = texelFetch(depth_tex, spi, 0).r;

        // Sky pixel — ray passes through, reset tracking
        if (d < 0.001) {
            prev_depth = 0.0;
            continue;
        }

        // Transitioning from sky to surface — hit
        if (prev_depth < 0.001) {
            radiance = texelFetch(color_tex, spi, 0).rgb;
            transmittance = 0.0;
            break;
        }

        // Depth discontinuity — surface boundary
        float dd = abs(d - prev_depth);
        if (dd > depth_threshold) {
            radiance = texelFetch(color_tex, spi, 0).rgb;
            transmittance = 0.0;
            break;
        }

        prev_depth = d;
    }

    // Write to cascade texture (4x4 block per probe, 16 directions)
    ivec2 out_pos = ivec2(px * 4 + dir % 4, py * 4 + dir / 4);
    imageStore(cascade_tex, out_pos, vec4(radiance, transmittance));
}
)glsl";


// ═══════════════════════════════════════════════════════════════════════
//  Pass 2 — Merge: propagate radiance from coarse cascade to fine
//  For each fine probe/direction: merge = fine + fine_transmittance * coarse
//  Coarse cascade read via sampler (bilinear interpolation of 4 neighbors).
// ═══════════════════════════════════════════════════════════════════════

static const char *RC_MERGE_GLSL = R"glsl(
#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D coarse_tex;
layout(rgba16f, set = 0, binding = 1) uniform image2D fine_tex;

layout(push_constant, std430) uniform Params {
    ivec2 fine_probes;        // 8
    ivec2 coarse_probes;      // 8
    int   fine_spacing;       // 4
    int   coarse_spacing;     // 4
    int   total_fine_probes;  // 4
    int   pad0;               // 4
};  // Total: 32 bytes

const int NUM_DIRS = 16;

void main() {
    uint gid = gl_GlobalInvocationID.x;
    int probe_idx = int(gid) / NUM_DIRS;
    int dir = int(gid) % NUM_DIRS;

    if (probe_idx >= total_fine_probes) return;

    int px = probe_idx % fine_probes.x;
    int py = probe_idx / fine_probes.x;

    // Read fine cascade value
    ivec2 fine_coord = ivec2(px * 4 + dir % 4, py * 4 + dir / 4);
    vec4 fine_val = imageLoad(fine_tex, fine_coord);

    // Fine probe screen position
    vec2 fine_screen = (vec2(px, py) + 0.5) * float(fine_spacing);

    // Map to coarse probe UV (floating point for bilinear)
    vec2 coarse_uv = fine_screen / float(coarse_spacing) - 0.5;
    ivec2 c00 = ivec2(floor(coarse_uv));
    vec2 f = fract(coarse_uv);

    // Bilinear gather from 4 coarse probes
    vec4 coarse_val = vec4(0.0);
    float total_w = 0.0;

    for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
            ivec2 cp = c00 + ivec2(dx, dy);
            if (cp.x < 0 || cp.x >= coarse_probes.x ||
                cp.y < 0 || cp.y >= coarse_probes.y) continue;

            float w = (dx == 0 ? 1.0 - f.x : f.x) *
                      (dy == 0 ? 1.0 - f.y : f.y);

            ivec2 cc = ivec2(cp.x * 4 + dir % 4, cp.y * 4 + dir / 4);
            coarse_val += w * texelFetch(coarse_tex, cc, 0);
            total_w += w;
        }
    }

    if (total_w > 0.0) coarse_val /= total_w;

    // Merge: fine radiance + fine transmittance * coarse radiance
    vec3 merged_rgb = fine_val.rgb + fine_val.a * coarse_val.rgb;
    float merged_a  = fine_val.a * coarse_val.a;

    imageStore(fine_tex, fine_coord, vec4(merged_rgb, merged_a));
}
)glsl";


// ═══════════════════════════════════════════════════════════════════════
//  Pass 3 — Apply: sample cascade 0 probes, add GI to color buffer
//  For each screen pixel: bilinear from 4 nearest cascade-0 probes,
//  average over 16 directions, blend into color.
// ═══════════════════════════════════════════════════════════════════════

static const char *RC_APPLY_GLSL = R"glsl(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D cascade0_tex;
layout(rgba16f, set = 0, binding = 1) uniform image2D color_img;

layout(push_constant, std430) uniform Params {
    ivec2 screen_size;     // 8
    ivec2 probes_count;    // 8
    int   probe_spacing;   // 4
    float gi_intensity;    // 4
    int   pad0, pad1;      // 8
};  // Total: 32 bytes

const int NUM_DIRS = 16;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= screen_size.x || pixel.y >= screen_size.y) return;

    // Map pixel to cascade-0 probe UV
    vec2 probe_uv = vec2(pixel) / float(probe_spacing) - 0.5;
    ivec2 p00 = ivec2(floor(probe_uv));
    vec2 f = fract(probe_uv);

    vec3 total_gi = vec3(0.0);
    float total_w = 0.0;

    // Bilinear over 4 nearest probes
    for (int dy = 0; dy <= 1; dy++) {
        for (int dx = 0; dx <= 1; dx++) {
            ivec2 pp = p00 + ivec2(dx, dy);
            if (pp.x < 0 || pp.x >= probes_count.x ||
                pp.y < 0 || pp.y >= probes_count.y) continue;

            float w = (dx == 0 ? 1.0 - f.x : f.x) *
                      (dy == 0 ? 1.0 - f.y : f.y);

            // Sum radiance from all 16 directions
            vec3 probe_rad = vec3(0.0);
            for (int d = 0; d < NUM_DIRS; d++) {
                ivec2 tc = ivec2(pp.x * 4 + d % 4, pp.y * 4 + d / 4);
                probe_rad += texelFetch(cascade0_tex, tc, 0).rgb;
            }
            probe_rad /= float(NUM_DIRS);

            total_gi += w * probe_rad;
            total_w += w;
        }
    }

    if (total_w > 0.0) total_gi /= total_w;

    // Read current color, add GI contribution, write back
    vec4 current = imageLoad(color_img, pixel);
    current.rgb += total_gi * gi_intensity;
    imageStore(color_img, pixel, current);
}
)glsl";

} // namespace godot

#endif // RC_SHADERS_H
