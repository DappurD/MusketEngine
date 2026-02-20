#ifndef RADIANCE_CASCADES_H
#define RADIANCE_CASCADES_H

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/render_scene_buffers.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/color.hpp>

namespace godot {

/// 2D Screen-Space Radiance Cascades (Alexander Sannikov, 2023).
/// Implements multi-bounce GI via a CompositorEffect that runs
/// 3 compute shader passes: trace → merge → apply.
///
/// 4 cascade levels with doubling probe spacing / ray range.
/// Supplements Godot's built-in SSIL for long-range color bleeding.
class RadianceCascadesEffect : public CompositorEffect {
    GDCLASS(RadianceCascadesEffect, CompositorEffect)

public:
    RadianceCascadesEffect();
    ~RadianceCascadesEffect();

    // CompositorEffect virtual
    void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

    // Properties
    void set_cascade_count(int p_count);
    int get_cascade_count() const;

    void set_gi_intensity(float p_intensity);
    float get_gi_intensity() const;

    void set_sky_color(const Color &p_color);
    Color get_sky_color() const;

    void set_base_probe_spacing(int p_spacing);
    int get_base_probe_spacing() const;

    void set_depth_threshold(float p_threshold);
    float get_depth_threshold() const;

protected:
    static void _bind_methods();

private:
    static const int MAX_CASCADES = 4;
    static const int NUM_DIRS = 16;

    // ── Tuning parameters ────────────────────────────────────────
    int   _cascade_count      = 4;
    float _gi_intensity       = 0.8f;
    Color _sky_color          = Color(0.1f, 0.15f, 0.25f, 1.0f);
    int   _base_probe_spacing = 4;     // cascade 0 spacing in pixels
    float _depth_threshold    = 0.02f;

    // ── GPU resources ────────────────────────────────────────────
    RenderingDevice *_rd = nullptr;
    bool _shaders_ready = false;

    // Shaders + pipelines
    RID _trace_shader, _trace_pipeline;
    RID _merge_shader, _merge_pipeline;
    RID _apply_shader, _apply_pipeline;

    // Sampler (nearest, clamp)
    RID _nearest_sampler;

    // Per-cascade data
    struct CascadeData {
        RID texture;           // RGBA16F, (probes_x*4) × (probes_y*4)
        Vector2i probes_count; // number of probes
        int spacing;           // probe spacing in pixels
        int interval_start;    // ray march start distance
        int interval_length;   // ray march length
        int step_size;         // pixels per step
    };
    CascadeData _cascades[MAX_CASCADES];

    // Screen size tracking (recreate on change)
    Vector2i _current_size;

    // ── Methods ──────────────────────────────────────────────────
    bool _ensure_shaders();
    void _create_cascade_textures(Vector2i screen_size);
    void _cleanup_cascade_textures();
    void _cleanup_shaders();

    // Uniform set helpers (created per-frame, freed after use)
    RID _make_trace_set(RID color_tex, RID depth_tex, int cascade_idx);
    RID _make_merge_set(int coarse_idx, int fine_idx);
    RID _make_apply_set(RID color_tex);
};

} // namespace godot

#endif // RADIANCE_CASCADES_H
