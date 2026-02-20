#ifndef VOXEL_POST_EFFECTS_H
#define VOXEL_POST_EFFECTS_H

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/render_scene_buffers.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace godot {

/// Screen-space contact shadows for voxel worlds.
///
/// 16-step ray march toward sun in screen space with temporal jitter.
/// TAA resolves to 64+ effective shadow samples over time.
/// Adds crisp micro-shadows at every voxel edge that shadow maps cannot resolve.
///
/// Cost: ~0.3ms at 1080p.
class VoxelPostEffect : public CompositorEffect {
    GDCLASS(VoxelPostEffect, CompositorEffect)

public:
    VoxelPostEffect();
    ~VoxelPostEffect();

    // CompositorEffect virtual
    void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

    // ── Properties ───────────────────────────────────────────────
    void set_shadow_strength(float p_strength);
    float get_shadow_strength() const;

    void set_max_distance(float p_distance);
    float get_max_distance() const;

    void set_thickness(float p_thickness);
    float get_thickness() const;

    void set_light_direction(const Vector2 &p_dir);
    Vector2 get_light_direction() const;

protected:
    static void _bind_methods();

private:
    // ── Parameters ───────────────────────────────────────────────
    float   _shadow_strength = 0.5f;
    float   _max_distance    = 0.05f;   // UV-space ray length
    float   _thickness       = 0.005f;  // depth threshold
    Vector2 _light_dir       = Vector2(0.4f, -0.6f);  // screen-space light direction

    // ── GPU resources ────────────────────────────────────────────
    RenderingDevice *_rd = nullptr;
    bool _shader_ready = false;
    int  _frame_counter = 0;

    RID _shader, _pipeline;
    RID _nearest_sampler;

    // ── Methods ──────────────────────────────────────────────────
    bool _ensure_shader();
    void _cleanup();
};

} // namespace godot

#endif // VOXEL_POST_EFFECTS_H
