#include "radiance_cascades.h"
#include "rc_shaders.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

namespace godot {

// ═══════════════════════════════════════════════════════════════════════
//  Push constant structs — must match GLSL layouts exactly
// ═══════════════════════════════════════════════════════════════════════

struct TracePushConstants {
    int32_t screen_w, screen_h;
    int32_t probes_x, probes_y;
    int32_t probe_spacing;
    int32_t interval_start;
    int32_t interval_length;
    int32_t step_size;
    float   sky_r, sky_g, sky_b, sky_a;  // vec4 at offset 32 (16-byte aligned)
    float   depth_threshold;
    int32_t total_probes;
    int32_t pad0, pad1;
};  // 64 bytes
static_assert(sizeof(TracePushConstants) == 64, "TracePushConstants must be 64 bytes");

struct MergePushConstants {
    int32_t fine_probes_x, fine_probes_y;
    int32_t coarse_probes_x, coarse_probes_y;
    int32_t fine_spacing;
    int32_t coarse_spacing;
    int32_t total_fine_probes;
    int32_t pad0;
};  // 32 bytes
static_assert(sizeof(MergePushConstants) == 32, "MergePushConstants must be 32 bytes");

struct ApplyPushConstants {
    int32_t screen_w, screen_h;
    int32_t probes_x, probes_y;
    int32_t probe_spacing;
    float   gi_intensity;
    int32_t pad0, pad1;
};  // 32 bytes
static_assert(sizeof(ApplyPushConstants) == 32, "ApplyPushConstants must be 32 bytes");

static const int TRACE_LOCAL_SIZE = 64;
static const int APPLY_LOCAL_SIZE = 8;

// ═══════════════════════════════════════════════════════════════════════
//  Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════

RadianceCascadesEffect::RadianceCascadesEffect() {
    // Configure CompositorEffect for post-transparent callback
    set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
    set_access_resolved_color(true);
    set_access_resolved_depth(true);
    set_needs_normal_roughness(false);
    set_needs_separate_specular(false);
    set_needs_motion_vectors(false);
}

RadianceCascadesEffect::~RadianceCascadesEffect() {
    _cleanup_cascade_textures();
    _cleanup_shaders();
}

// ═══════════════════════════════════════════════════════════════════════
//  Shader compilation
// ═══════════════════════════════════════════════════════════════════════

static RID _compile_shader(RenderingDevice *rd, const char *glsl, const char *name) {
    Ref<RDShaderSource> src;
    src.instantiate();
    src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(glsl));
    src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);

    Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(src);
    if (spirv.is_null()) {
        UtilityFunctions::push_error("[RadianceCascades] ", name, " SPIR-V is null");
        return RID();
    }
    String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!err.is_empty()) {
        UtilityFunctions::push_error("[RadianceCascades] ", name, " error: ", err);
        return RID();
    }

    RID shader = rd->shader_create_from_spirv(spirv, String(name));
    if (!shader.is_valid()) {
        UtilityFunctions::push_error("[RadianceCascades] ", name, " shader_create failed");
    }
    return shader;
}

bool RadianceCascadesEffect::_ensure_shaders() {
    if (_shaders_ready) return true;

    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) return false;
    _rd = rs->get_rendering_device();
    if (!_rd) return false;

    // Compile 3 shaders
    _trace_shader = _compile_shader(_rd, RC_TRACE_GLSL, "RC_Trace");
    if (!_trace_shader.is_valid()) return false;

    _merge_shader = _compile_shader(_rd, RC_MERGE_GLSL, "RC_Merge");
    if (!_merge_shader.is_valid()) { _cleanup_shaders(); return false; }

    _apply_shader = _compile_shader(_rd, RC_APPLY_GLSL, "RC_Apply");
    if (!_apply_shader.is_valid()) { _cleanup_shaders(); return false; }

    // Create pipelines
    _trace_pipeline = _rd->compute_pipeline_create(_trace_shader);
    _merge_pipeline = _rd->compute_pipeline_create(_merge_shader);
    _apply_pipeline = _rd->compute_pipeline_create(_apply_shader);

    // Create nearest sampler
    Ref<RDSamplerState> ss;
    ss.instantiate();
    ss->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    ss->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    ss->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    ss->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    _nearest_sampler = _rd->sampler_create(ss);

    _shaders_ready = true;
    UtilityFunctions::print("[RadianceCascades] Shaders compiled, ", _cascade_count, " cascades");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Cascade texture management
// ═══════════════════════════════════════════════════════════════════════

void RadianceCascadesEffect::_create_cascade_textures(Vector2i screen_size) {
    _cleanup_cascade_textures();
    _current_size = screen_size;

    int base_interval = _base_probe_spacing * 4;  // ray range for cascade 0
    int cumulative_start = 0;

    for (int c = 0; c < _cascade_count; c++) {
        int spacing = _base_probe_spacing * (1 << (c * 2));  // 4, 16, 64, 256
        int probes_x = (screen_size.x + spacing - 1) / spacing;
        int probes_y = (screen_size.y + spacing - 1) / spacing;

        // Interval: cascade c covers [cumulative, cumulative + base_interval * 2^c)
        int interval_len = base_interval * (1 << c);
        int step_sz = 1 << c;  // 1, 2, 4, 8

        _cascades[c].probes_count = Vector2i(probes_x, probes_y);
        _cascades[c].spacing = spacing;
        _cascades[c].interval_start = (c == 0) ? 1 : cumulative_start;
        _cascades[c].interval_length = interval_len;
        _cascades[c].step_size = step_sz;

        cumulative_start += interval_len;

        // Create RGBA16F texture: (probes_x * 4) × (probes_y * 4)
        int tex_w = probes_x * 4;
        int tex_h = probes_y * 4;

        Ref<RDTextureFormat> fmt;
        fmt.instantiate();
        fmt->set_width(tex_w);
        fmt->set_height(tex_h);
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_usage_bits(
            RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT
        );
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);

        Ref<RDTextureView> view;
        view.instantiate();

        _cascades[c].texture = _rd->texture_create(fmt, view);
    }
}

void RadianceCascadesEffect::_cleanup_cascade_textures() {
    if (!_rd) return;
    for (int c = 0; c < MAX_CASCADES; c++) {
        if (_cascades[c].texture.is_valid()) {
            _rd->free_rid(_cascades[c].texture);
            _cascades[c].texture = RID();
        }
    }
    _current_size = Vector2i(0, 0);
}

// ═══════════════════════════════════════════════════════════════════════
//  Uniform set helpers (created per-frame, caller must free)
// ═══════════════════════════════════════════════════════════════════════

static Ref<RDUniform> _make_sampler_uniform(int binding, const RID &sampler, const RID &texture) {
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
    u->set_binding(binding);
    u->add_id(sampler);
    u->add_id(texture);
    return u;
}

static Ref<RDUniform> _make_image_uniform(int binding, const RID &texture) {
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    u->set_binding(binding);
    u->add_id(texture);
    return u;
}

RID RadianceCascadesEffect::_make_trace_set(RID color_tex, RID depth_tex, int cascade_idx) {
    TypedArray<Ref<RDUniform>> uniforms;
    uniforms.append(_make_sampler_uniform(0, _nearest_sampler, color_tex));
    uniforms.append(_make_sampler_uniform(1, _nearest_sampler, depth_tex));
    uniforms.append(_make_image_uniform(2, _cascades[cascade_idx].texture));
    return _rd->uniform_set_create(uniforms, _trace_shader, 0);
}

RID RadianceCascadesEffect::_make_merge_set(int coarse_idx, int fine_idx) {
    TypedArray<Ref<RDUniform>> uniforms;
    uniforms.append(_make_sampler_uniform(0, _nearest_sampler, _cascades[coarse_idx].texture));
    uniforms.append(_make_image_uniform(1, _cascades[fine_idx].texture));
    return _rd->uniform_set_create(uniforms, _merge_shader, 0);
}

RID RadianceCascadesEffect::_make_apply_set(RID color_tex) {
    TypedArray<Ref<RDUniform>> uniforms;
    uniforms.append(_make_sampler_uniform(0, _nearest_sampler, _cascades[0].texture));
    uniforms.append(_make_image_uniform(1, color_tex));
    return _rd->uniform_set_create(uniforms, _apply_shader, 0);
}

// ═══════════════════════════════════════════════════════════════════════
//  Render callback — main entry point per frame
// ═══════════════════════════════════════════════════════════════════════

void RadianceCascadesEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) {
    if (!p_render_data) return;
    if (p_effect_callback_type != EFFECT_CALLBACK_TYPE_POST_TRANSPARENT) return;

    // Initialize shaders on first call
    if (!_ensure_shaders()) return;

    // Get render scene buffers
    Ref<RenderSceneBuffers> buffers = p_render_data->get_render_scene_buffers();
    if (buffers.is_null()) return;

    RenderSceneBuffersRD *buffers_rd = Object::cast_to<RenderSceneBuffersRD>(buffers.ptr());
    if (!buffers_rd) return;

    // Get screen size
    Vector2i size = buffers_rd->get_internal_size();
    if (size.x <= 0 || size.y <= 0) return;

    // Get color and depth textures
    RID color_tex = buffers_rd->get_color_texture(false);
    RID depth_tex = buffers_rd->get_depth_texture(false);
    if (!color_tex.is_valid() || !depth_tex.is_valid()) return;

    // Recreate cascade textures if screen size changed
    if (size != _current_size) {
        _create_cascade_textures(size);
    }

    // ── Create per-frame uniform sets ────────────────────────────
    RID trace_sets[MAX_CASCADES];
    for (int c = 0; c < _cascade_count; c++) {
        trace_sets[c] = _make_trace_set(color_tex, depth_tex, c);
    }

    RID merge_sets[MAX_CASCADES - 1];
    for (int c = 0; c < _cascade_count - 1; c++) {
        merge_sets[c] = _make_merge_set(c + 1, c);  // merge cascade c+1 → c
    }

    RID apply_set = _make_apply_set(color_tex);

    // ── Begin compute list ───────────────────────────────────────
    int64_t cl = _rd->compute_list_begin();

    // Pass 1: Trace each cascade
    for (int c = 0; c < _cascade_count; c++) {
        const CascadeData &cd = _cascades[c];
        int total = cd.probes_count.x * cd.probes_count.y;

        TracePushConstants pc;
        memset(&pc, 0, sizeof(pc));
        pc.screen_w = size.x;
        pc.screen_h = size.y;
        pc.probes_x = cd.probes_count.x;
        pc.probes_y = cd.probes_count.y;
        pc.probe_spacing = cd.spacing;
        pc.interval_start = cd.interval_start;
        pc.interval_length = cd.interval_length;
        pc.step_size = cd.step_size;
        pc.sky_r = _sky_color.r;
        pc.sky_g = _sky_color.g;
        pc.sky_b = _sky_color.b;
        pc.sky_a = 1.0f;
        pc.depth_threshold = _depth_threshold;
        pc.total_probes = total;

        PackedByteArray pc_bytes;
        pc_bytes.resize(sizeof(pc));
        memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

        int groups = (total * NUM_DIRS + TRACE_LOCAL_SIZE - 1) / TRACE_LOCAL_SIZE;

        _rd->compute_list_bind_compute_pipeline(cl, _trace_pipeline);
        _rd->compute_list_bind_uniform_set(cl, trace_sets[c], 0);
        _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
        _rd->compute_list_dispatch(cl, groups, 1, 1);
        _rd->compute_list_add_barrier(cl);
    }

    // Pass 2: Merge from coarsest to finest
    for (int c = _cascade_count - 2; c >= 0; c--) {
        const CascadeData &fine = _cascades[c];
        const CascadeData &coarse = _cascades[c + 1];
        int total_fine = fine.probes_count.x * fine.probes_count.y;

        MergePushConstants pc;
        memset(&pc, 0, sizeof(pc));
        pc.fine_probes_x = fine.probes_count.x;
        pc.fine_probes_y = fine.probes_count.y;
        pc.coarse_probes_x = coarse.probes_count.x;
        pc.coarse_probes_y = coarse.probes_count.y;
        pc.fine_spacing = fine.spacing;
        pc.coarse_spacing = coarse.spacing;
        pc.total_fine_probes = total_fine;

        PackedByteArray pc_bytes;
        pc_bytes.resize(sizeof(pc));
        memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

        int groups = (total_fine * NUM_DIRS + TRACE_LOCAL_SIZE - 1) / TRACE_LOCAL_SIZE;

        _rd->compute_list_bind_compute_pipeline(cl, _merge_pipeline);
        _rd->compute_list_bind_uniform_set(cl, merge_sets[c], 0);
        _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
        _rd->compute_list_dispatch(cl, groups, 1, 1);
        _rd->compute_list_add_barrier(cl);
    }

    // Pass 3: Apply GI to color buffer
    {
        ApplyPushConstants pc;
        memset(&pc, 0, sizeof(pc));
        pc.screen_w = size.x;
        pc.screen_h = size.y;
        pc.probes_x = _cascades[0].probes_count.x;
        pc.probes_y = _cascades[0].probes_count.y;
        pc.probe_spacing = _cascades[0].spacing;
        pc.gi_intensity = _gi_intensity;

        PackedByteArray pc_bytes;
        pc_bytes.resize(sizeof(pc));
        memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

        int groups_x = (size.x + APPLY_LOCAL_SIZE - 1) / APPLY_LOCAL_SIZE;
        int groups_y = (size.y + APPLY_LOCAL_SIZE - 1) / APPLY_LOCAL_SIZE;

        _rd->compute_list_bind_compute_pipeline(cl, _apply_pipeline);
        _rd->compute_list_bind_uniform_set(cl, apply_set, 0);
        _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
        _rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
    }

    _rd->compute_list_end();

    // ── Free per-frame uniform sets ──────────────────────────────
    for (int c = 0; c < _cascade_count; c++) {
        if (trace_sets[c].is_valid()) _rd->free_rid(trace_sets[c]);
    }
    for (int c = 0; c < _cascade_count - 1; c++) {
        if (merge_sets[c].is_valid()) _rd->free_rid(merge_sets[c]);
    }
    if (apply_set.is_valid()) _rd->free_rid(apply_set);
}

// ═══════════════════════════════════════════════════════════════════════
//  Cleanup
// ═══════════════════════════════════════════════════════════════════════

void RadianceCascadesEffect::_cleanup_shaders() {
    if (!_rd) return;

    if (_nearest_sampler.is_valid()) _rd->free_rid(_nearest_sampler);
    if (_trace_pipeline.is_valid()) _rd->free_rid(_trace_pipeline);
    if (_merge_pipeline.is_valid()) _rd->free_rid(_merge_pipeline);
    if (_apply_pipeline.is_valid()) _rd->free_rid(_apply_pipeline);
    if (_trace_shader.is_valid()) _rd->free_rid(_trace_shader);
    if (_merge_shader.is_valid()) _rd->free_rid(_merge_shader);
    if (_apply_shader.is_valid()) _rd->free_rid(_apply_shader);

    _nearest_sampler = RID();
    _trace_pipeline = RID();
    _merge_pipeline = RID();
    _apply_pipeline = RID();
    _trace_shader = RID();
    _merge_shader = RID();
    _apply_shader = RID();

    _shaders_ready = false;
    _rd = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
//  Properties
// ═══════════════════════════════════════════════════════════════════════

void RadianceCascadesEffect::set_cascade_count(int p_count) {
    _cascade_count = (p_count < 1) ? 1 : ((p_count > MAX_CASCADES) ? MAX_CASCADES : p_count);
    _current_size = Vector2i(0, 0);  // force texture recreation
}
int RadianceCascadesEffect::get_cascade_count() const { return _cascade_count; }

void RadianceCascadesEffect::set_gi_intensity(float p_intensity) { _gi_intensity = p_intensity; }
float RadianceCascadesEffect::get_gi_intensity() const { return _gi_intensity; }

void RadianceCascadesEffect::set_sky_color(const Color &p_color) { _sky_color = p_color; }
Color RadianceCascadesEffect::get_sky_color() const { return _sky_color; }

void RadianceCascadesEffect::set_base_probe_spacing(int p_spacing) {
    _base_probe_spacing = (p_spacing < 2) ? 2 : ((p_spacing > 16) ? 16 : p_spacing);
    _current_size = Vector2i(0, 0);
}
int RadianceCascadesEffect::get_base_probe_spacing() const { return _base_probe_spacing; }

void RadianceCascadesEffect::set_depth_threshold(float p_threshold) { _depth_threshold = p_threshold; }
float RadianceCascadesEffect::get_depth_threshold() const { return _depth_threshold; }

// ═══════════════════════════════════════════════════════════════════════
//  Binding
// ═══════════════════════════════════════════════════════════════════════

void RadianceCascadesEffect::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_cascade_count", "count"), &RadianceCascadesEffect::set_cascade_count);
    ClassDB::bind_method(D_METHOD("get_cascade_count"), &RadianceCascadesEffect::get_cascade_count);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "cascade_count", PROPERTY_HINT_RANGE, "1,4,1"),
                 "set_cascade_count", "get_cascade_count");

    ClassDB::bind_method(D_METHOD("set_gi_intensity", "intensity"), &RadianceCascadesEffect::set_gi_intensity);
    ClassDB::bind_method(D_METHOD("get_gi_intensity"), &RadianceCascadesEffect::get_gi_intensity);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gi_intensity", PROPERTY_HINT_RANGE, "0.0,3.0,0.05"),
                 "set_gi_intensity", "get_gi_intensity");

    ClassDB::bind_method(D_METHOD("set_sky_color", "color"), &RadianceCascadesEffect::set_sky_color);
    ClassDB::bind_method(D_METHOD("get_sky_color"), &RadianceCascadesEffect::get_sky_color);
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "sky_color"),
                 "set_sky_color", "get_sky_color");

    ClassDB::bind_method(D_METHOD("set_base_probe_spacing", "spacing"), &RadianceCascadesEffect::set_base_probe_spacing);
    ClassDB::bind_method(D_METHOD("get_base_probe_spacing"), &RadianceCascadesEffect::get_base_probe_spacing);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "base_probe_spacing", PROPERTY_HINT_RANGE, "2,16,1"),
                 "set_base_probe_spacing", "get_base_probe_spacing");

    ClassDB::bind_method(D_METHOD("set_depth_threshold", "threshold"), &RadianceCascadesEffect::set_depth_threshold);
    ClassDB::bind_method(D_METHOD("get_depth_threshold"), &RadianceCascadesEffect::get_depth_threshold);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "depth_threshold", PROPERTY_HINT_RANGE, "0.001,0.1,0.001"),
                 "set_depth_threshold", "get_depth_threshold");
}

} // namespace godot
