#ifndef VOXEL_POST_SHADERS_H
#define VOXEL_POST_SHADERS_H

// ═══════════════════════════════════════════════════════════════════════
//  Contact Shadows — screen-space ray march toward sun
//  16 steps with temporal jitter (IGN + frame index)
//  TAA resolves to 64+ effective samples over time
// ═══════════════════════════════════════════════════════════════════════

static const char *CONTACT_SHADOW_GLSL = R"glsl(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D depth_tex;
layout(set = 0, binding = 1, rgba16f) uniform image2D color_tex;

layout(push_constant) uniform PC {
    int   screen_w;
    int   screen_h;
    float light_dir_x;      // screen-space light direction (normalized)
    float light_dir_y;
    float shadow_strength;   // 0.0 - 1.0 (default 0.5)
    float max_distance;      // max ray distance in UV space (default 0.05)
    int   frame_number;      // for temporal jitter
    float thickness;         // depth threshold for occlusion (default 0.005)
} pc;

// Interleaved gradient noise (Jimenez 2014)
float ign(ivec2 pixel, int frame) {
    float x = float(pixel.x) + 5.588238 * float(frame & 63);
    float y = float(pixel.y) + 5.588238 * float(frame & 63);
    return fract(52.9829189 * fract(0.06711056 * x + 0.00583715 * y));
}

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= pc.screen_w || gid.y >= pc.screen_h) return;

    vec2 pixel_size = vec2(1.0) / vec2(float(pc.screen_w), float(pc.screen_h));
    vec2 uv = (vec2(gid) + 0.5) * pixel_size;

    // Godot reversed-Z: 1.0 = near, 0.0 = far
    float center_depth = texture(depth_tex, uv).r;

    // Skip sky (far plane)
    if (center_depth <= 0.0001) return;

    // Temporal jitter: randomize start offset per frame
    float jitter = ign(gid, pc.frame_number);

    // March toward light in screen space
    vec2 light_dir = vec2(pc.light_dir_x, pc.light_dir_y);
    float step_size = pc.max_distance / 16.0;
    float shadow = 1.0;

    for (int i = 0; i < 16; i++) {
        float t = (float(i) + jitter) * step_size;
        vec2 sample_uv = uv + light_dir * t;

        // Bounds check
        if (sample_uv.x < 0.0 || sample_uv.x > 1.0 ||
            sample_uv.y < 0.0 || sample_uv.y > 1.0) break;

        float sample_depth = texture(depth_tex, sample_uv).r;

        // Reversed-Z: closer objects = higher depth
        // Shadow if sample is closer to camera AND within thickness
        float diff = sample_depth - center_depth;
        if (diff > 0.00005 && diff < pc.thickness) {
            // Soft falloff based on distance along ray
            float falloff = 1.0 - (t / pc.max_distance);
            shadow = min(shadow, 1.0 - falloff * pc.shadow_strength);
        }
    }

    // Only modify pixels that are in shadow
    if (shadow < 0.999) {
        vec4 color = imageLoad(color_tex, gid);
        color.rgb *= shadow;
        imageStore(color_tex, gid, color);
    }
}
)glsl";

#endif // VOXEL_POST_SHADERS_H
