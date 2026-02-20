#include "voxel_post_effects.h"
#include "voxel_post_shaders.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

namespace godot {

// ═══════════════════════════════════════════════════════════════════════
//  Push constants — must match GLSL layout exactly
// ═══════════════════════════════════════════════════════════════════════

struct ContactShadowPC {
    int32_t screen_w;
    int32_t screen_h;
    float   light_dir_x;
    float   light_dir_y;
    float   shadow_strength;
    float   max_distance;
    int32_t frame_number;
    float   thickness;
};  // 32 bytes
static_assert(sizeof(ContactShadowPC) == 32, "ContactShadowPC must be 32 bytes");

static const int LOCAL_SIZE = 8;

// ═══════════════════════════════════════════════════════════════════════
//  Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════

VoxelPostEffect::VoxelPostEffect() {
    set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
    set_access_resolved_color(true);
    set_access_resolved_depth(true);
    set_needs_normal_roughness(false);
    set_needs_separate_specular(false);
    set_needs_motion_vectors(false);
}

VoxelPostEffect::~VoxelPostEffect() {
    _cleanup();
}

// ═══════════════════════════════════════════════════════════════════════
//  Shader compilation
// ═══════════════════════════════════════════════════════════════════════

bool VoxelPostEffect::_ensure_shader() {
    if (_shader_ready) return true;

    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) return false;
    _rd = rs->get_rendering_device();
    if (!_rd) return false;

    // Compile contact shadow shader
    Ref<RDShaderSource> src;
    src.instantiate();
    src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(CONTACT_SHADOW_GLSL));
    src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);

    Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(src);
    if (spirv.is_null()) {
        UtilityFunctions::push_error("[VoxelPostEffect] SPIR-V compilation returned null");
        return false;
    }
    String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!err.is_empty()) {
        UtilityFunctions::push_error("[VoxelPostEffect] Shader error: ", err);
        return false;
    }

    _shader = _rd->shader_create_from_spirv(spirv, "VoxelContactShadow");
    if (!_shader.is_valid()) {
        UtilityFunctions::push_error("[VoxelPostEffect] shader_create failed");
        return false;
    }

    _pipeline = _rd->compute_pipeline_create(_shader);

    // Create nearest sampler for depth reads
    Ref<RDSamplerState> ss;
    ss.instantiate();
    ss->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    ss->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
    ss->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    ss->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
    _nearest_sampler = _rd->sampler_create(ss);

    _shader_ready = true;
    UtilityFunctions::print("[VoxelPostEffect] Contact shadow shader compiled");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Render callback
// ═══════════════════════════════════════════════════════════════════════

void VoxelPostEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) {
    if (!p_render_data) return;
    if (p_effect_callback_type != EFFECT_CALLBACK_TYPE_POST_TRANSPARENT) return;
    if (_shadow_strength < 0.001f) return;  // Skip if disabled

    if (!_ensure_shader()) return;

    Ref<RenderSceneBuffers> buffers = p_render_data->get_render_scene_buffers();
    if (buffers.is_null()) return;

    RenderSceneBuffersRD *buffers_rd = Object::cast_to<RenderSceneBuffersRD>(buffers.ptr());
    if (!buffers_rd) return;

    Vector2i size = buffers_rd->get_internal_size();
    if (size.x <= 0 || size.y <= 0) return;

    RID color_tex = buffers_rd->get_color_texture(false);
    RID depth_tex = buffers_rd->get_depth_texture(false);
    if (!color_tex.is_valid() || !depth_tex.is_valid()) return;

    _frame_counter++;

    // ── Build uniform set (per-frame, freed after use) ───────────
    TypedArray<Ref<RDUniform>> uniforms;

    // Binding 0: depth_tex (sampler)
    {
        Ref<RDUniform> u;
        u.instantiate();
        u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        u->set_binding(0);
        u->add_id(_nearest_sampler);
        u->add_id(depth_tex);
        uniforms.append(u);
    }

    // Binding 1: color_tex (image)
    {
        Ref<RDUniform> u;
        u.instantiate();
        u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u->set_binding(1);
        u->add_id(color_tex);
        uniforms.append(u);
    }

    RID uniform_set = _rd->uniform_set_create(uniforms, _shader, 0);

    // ── Push constants ───────────────────────────────────────────
    Vector2 dir = _light_dir.normalized();
    ContactShadowPC pc;
    memset(&pc, 0, sizeof(pc));
    pc.screen_w = size.x;
    pc.screen_h = size.y;
    pc.light_dir_x = dir.x;
    pc.light_dir_y = dir.y;
    pc.shadow_strength = _shadow_strength;
    pc.max_distance = _max_distance;
    pc.frame_number = _frame_counter;
    pc.thickness = _thickness;

    PackedByteArray pc_bytes;
    pc_bytes.resize(sizeof(pc));
    memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

    // ── Dispatch ─────────────────────────────────────────────────
    int groups_x = (size.x + LOCAL_SIZE - 1) / LOCAL_SIZE;
    int groups_y = (size.y + LOCAL_SIZE - 1) / LOCAL_SIZE;

    int64_t cl = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(cl, _pipeline);
    _rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
    _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
    _rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
    _rd->compute_list_end();

    // Free per-frame uniform set
    if (uniform_set.is_valid()) _rd->free_rid(uniform_set);
}

// ═══════════════════════════════════════════════════════════════════════
//  Cleanup
// ═══════════════════════════════════════════════════════════════════════

void VoxelPostEffect::_cleanup() {
    if (!_rd) return;

    if (_nearest_sampler.is_valid()) _rd->free_rid(_nearest_sampler);
    if (_pipeline.is_valid()) _rd->free_rid(_pipeline);
    if (_shader.is_valid()) _rd->free_rid(_shader);

    _nearest_sampler = RID();
    _pipeline = RID();
    _shader = RID();
    _shader_ready = false;
    _rd = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
//  Properties
// ═══════════════════════════════════════════════════════════════════════

void VoxelPostEffect::set_shadow_strength(float p_strength) {
    _shadow_strength = CLAMP(p_strength, 0.0f, 1.0f);
}
float VoxelPostEffect::get_shadow_strength() const { return _shadow_strength; }

void VoxelPostEffect::set_max_distance(float p_distance) {
    _max_distance = CLAMP(p_distance, 0.01f, 0.2f);
}
float VoxelPostEffect::get_max_distance() const { return _max_distance; }

void VoxelPostEffect::set_thickness(float p_thickness) {
    _thickness = CLAMP(p_thickness, 0.001f, 0.05f);
}
float VoxelPostEffect::get_thickness() const { return _thickness; }

void VoxelPostEffect::set_light_direction(const Vector2 &p_dir) {
    _light_dir = p_dir;
}
Vector2 VoxelPostEffect::get_light_direction() const { return _light_dir; }

// ═══════════════════════════════════════════════════════════════════════
//  Binding
// ═══════════════════════════════════════════════════════════════════════

void VoxelPostEffect::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_shadow_strength", "strength"), &VoxelPostEffect::set_shadow_strength);
    ClassDB::bind_method(D_METHOD("get_shadow_strength"), &VoxelPostEffect::get_shadow_strength);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shadow_strength", PROPERTY_HINT_RANGE, "0.0,1.0,0.05"),
                 "set_shadow_strength", "get_shadow_strength");

    ClassDB::bind_method(D_METHOD("set_max_distance", "distance"), &VoxelPostEffect::set_max_distance);
    ClassDB::bind_method(D_METHOD("get_max_distance"), &VoxelPostEffect::get_max_distance);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "0.01,0.2,0.005"),
                 "set_max_distance", "get_max_distance");

    ClassDB::bind_method(D_METHOD("set_thickness", "thickness"), &VoxelPostEffect::set_thickness);
    ClassDB::bind_method(D_METHOD("get_thickness"), &VoxelPostEffect::get_thickness);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "thickness", PROPERTY_HINT_RANGE, "0.001,0.05,0.001"),
                 "set_thickness", "get_thickness");

    ClassDB::bind_method(D_METHOD("set_light_direction", "direction"), &VoxelPostEffect::set_light_direction);
    ClassDB::bind_method(D_METHOD("get_light_direction"), &VoxelPostEffect::get_light_direction);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "light_direction"),
                 "set_light_direction", "get_light_direction");
}

} // namespace godot
