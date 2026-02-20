#include "gpu_chunk_culler.h"
#include "gpu_chunk_shaders.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

namespace godot {

// ═══════════════════════════════════════════════════════════════════════
//  Push constant layout — must match gpu_chunk_shaders.h GLSL
// ═══════════════════════════════════════════════════════════════════════

struct ChunkCullPushConstants {
    float view_projection[16];                   // mat4 — 64 bytes
    float camera_x, camera_y, camera_z;          // vec4 xyz — 12 bytes
    float vis_radius_sq;                         // vec4 w   —  4 bytes
    float lod1_sq, lod2_sq, pad_a, pad_b;        // vec4     — 16 bytes
    uint32_t chunk_count;                        //           4 bytes
    uint32_t pad0, pad1, pad2;                   // padding  12 bytes
};                                               // Total:  112 bytes

static_assert(sizeof(ChunkCullPushConstants) == 112, "Push constants must be 112 bytes");

static const int LOCAL_SIZE = 64;  // Matches shader local_size_x

// ═══════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════

GpuChunkCuller::GpuChunkCuller() {}

GpuChunkCuller::~GpuChunkCuller() {
    _cleanup();
}

void GpuChunkCuller::setup(int max_chunks) {
    _cleanup();
    _max_chunks = max_chunks;

    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) {
        UtilityFunctions::push_error("[GpuChunkCuller] No RenderingServer");
        return;
    }

    // Try local device first (Vulkan) — we own it and can freely submit/sync
    _rd = rs->create_local_rendering_device();
    if (_rd) {
        _owns_rd = true;
    } else {
        // Fallback: global rendering device (D3D12, etc.)
        _rd = rs->get_rendering_device();
        _owns_rd = false;
    }

    if (!_rd) {
        UtilityFunctions::push_warning("[GpuChunkCuller] No RenderingDevice — CPU culling fallback");
        return;
    }

    if (!_create_shader()) {
        UtilityFunctions::push_error("[GpuChunkCuller] Shader creation failed");
        _cleanup();
        return;
    }

    _create_buffers();
    _create_uniform_set();
    _gpu_available = true;

    UtilityFunctions::print("[GpuChunkCuller] GPU culling ready — max ", max_chunks, " chunks");
}

// ═══════════════════════════════════════════════════════════════════════
//  Shader compilation
// ═══════════════════════════════════════════════════════════════════════

bool GpuChunkCuller::_create_shader() {
    Ref<RDShaderSource> src;
    src.instantiate();
    src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(CHUNK_CULL_GLSL));
    src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);

    Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(src);
    if (spirv.is_null()) {
        UtilityFunctions::push_error("[GpuChunkCuller] SPIR-V compilation returned null");
        return false;
    }

    String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!err.is_empty()) {
        UtilityFunctions::push_error("[GpuChunkCuller] Shader error: ", err);
        return false;
    }

    _shader = _rd->shader_create_from_spirv(spirv, "ChunkCull");
    if (!_shader.is_valid()) {
        UtilityFunctions::push_error("[GpuChunkCuller] shader_create_from_spirv failed");
        return false;
    }

    _pipeline = _rd->compute_pipeline_create(_shader);
    if (!_pipeline.is_valid()) {
        UtilityFunctions::push_error("[GpuChunkCuller] Pipeline creation failed");
        return false;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Buffer creation
// ═══════════════════════════════════════════════════════════════════════

void GpuChunkCuller::_create_buffers() {
    // AABB buffer: 2 vec4 (8 floats = 32 bytes) per chunk
    uint32_t aabb_bytes = _max_chunks * 8 * sizeof(float);
    PackedByteArray aabb_init;
    aabb_init.resize(aabb_bytes);
    memset(aabb_init.ptrw(), 0, aabb_bytes);
    _aabb_buf = _rd->storage_buffer_create(aabb_bytes, aabb_init);

    // Result buffer: 1 uint (4 bytes) per chunk
    uint32_t result_bytes = _max_chunks * sizeof(uint32_t);
    PackedByteArray result_init;
    result_init.resize(result_bytes);
    memset(result_init.ptrw(), 0, result_bytes);
    _result_buf = _rd->storage_buffer_create(result_bytes, result_init);
}

// ═══════════════════════════════════════════════════════════════════════
//  Uniform set
// ═══════════════════════════════════════════════════════════════════════

static Ref<RDUniform> _make_storage_uniform(int binding, const RID &buffer) {
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
    u->set_binding(binding);
    u->add_id(buffer);
    return u;
}

void GpuChunkCuller::_create_uniform_set() {
    TypedArray<Ref<RDUniform>> uniforms;
    uniforms.append(_make_storage_uniform(0, _aabb_buf));
    uniforms.append(_make_storage_uniform(1, _result_buf));
    _uniform_set = _rd->uniform_set_create(uniforms, _shader, 0);
}

// ═══════════════════════════════════════════════════════════════════════
//  AABB upload
// ═══════════════════════════════════════════════════════════════════════

void GpuChunkCuller::set_chunk_aabbs(const PackedFloat32Array &aabbs) {
    if (!_gpu_available || !_rd) return;

    // Input: 6 floats per chunk (min_x, min_y, min_z, max_x, max_y, max_z)
    int count = aabbs.size() / 6;
    _chunk_count = (count <= _max_chunks) ? count : _max_chunks;
    if (_chunk_count == 0) return;

    // Convert to GPU format: 2 vec4 per chunk (8 floats, .w padding = 0)
    PackedByteArray bytes;
    bytes.resize(_chunk_count * 8 * sizeof(float));
    float *dst = (float *)bytes.ptrw();
    const float *src = aabbs.ptr();

    for (int i = 0; i < _chunk_count; i++) {
        dst[i * 8 + 0] = src[i * 6 + 0]; // min_x
        dst[i * 8 + 1] = src[i * 6 + 1]; // min_y
        dst[i * 8 + 2] = src[i * 6 + 2]; // min_z
        dst[i * 8 + 3] = 0.0f;           // pad
        dst[i * 8 + 4] = src[i * 6 + 3]; // max_x
        dst[i * 8 + 5] = src[i * 6 + 4]; // max_y
        dst[i * 8 + 6] = src[i * 6 + 5]; // max_z
        dst[i * 8 + 7] = 0.0f;           // pad
    }

    _rd->buffer_update(_aabb_buf, 0, bytes.size(), bytes);
}

// ═══════════════════════════════════════════════════════════════════════
//  Cull dispatch + readback
// ═══════════════════════════════════════════════════════════════════════

Dictionary GpuChunkCuller::cull(const Projection &view_proj, const Vector3 &camera_pos,
                                float vis_radius, float lod1_dist, float lod2_dist) {
    Dictionary result;

    if (!_gpu_available || !_rd || _chunk_count == 0) {
        result["visible"] = PackedByteArray();
        result["lod_levels"] = PackedByteArray();
        return result;
    }

    // ── Build push constants ─────────────────────────────────────
    ChunkCullPushConstants pc;
    memset(&pc, 0, sizeof(pc));

    // Copy VP matrix — Godot Projection is column-major, matches GLSL mat4
    for (int col = 0; col < 4; col++) {
        pc.view_projection[col * 4 + 0] = view_proj.columns[col].x;
        pc.view_projection[col * 4 + 1] = view_proj.columns[col].y;
        pc.view_projection[col * 4 + 2] = view_proj.columns[col].z;
        pc.view_projection[col * 4 + 3] = view_proj.columns[col].w;
    }

    pc.camera_x = camera_pos.x;
    pc.camera_y = camera_pos.y;
    pc.camera_z = camera_pos.z;
    pc.vis_radius_sq = vis_radius * vis_radius;

    pc.lod1_sq = lod1_dist * lod1_dist;
    pc.lod2_sq = lod2_dist * lod2_dist;

    pc.chunk_count = (uint32_t)_chunk_count;

    PackedByteArray pc_bytes;
    pc_bytes.resize(sizeof(pc));
    memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

    // ── Dispatch ─────────────────────────────────────────────────
    int groups = (_chunk_count + LOCAL_SIZE - 1) / LOCAL_SIZE;

    int64_t cl = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(cl, _pipeline);
    _rd->compute_list_bind_uniform_set(cl, _uniform_set, 0);
    _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
    _rd->compute_list_dispatch(cl, groups, 1, 1);
    _rd->compute_list_end();

    // ── Submit + sync ────────────────────────────────────────────
    _rd->submit();
    _rd->sync();

    // ── Readback ─────────────────────────────────────────────────
    uint32_t result_bytes_count = _chunk_count * sizeof(uint32_t);
    PackedByteArray raw = _rd->buffer_get_data(_result_buf, 0, result_bytes_count);

    if ((int)raw.size() != (int)result_bytes_count) {
        UtilityFunctions::push_error("[GpuChunkCuller] Readback size mismatch: ",
            raw.size(), " vs expected ", result_bytes_count);
        result["visible"] = PackedByteArray();
        result["lod_levels"] = PackedByteArray();
        return result;
    }

    // Unpack: bit 0 = visible, bits 1-2 = LOD level
    _visible_cache.resize(_chunk_count);
    _lod_cache.resize(_chunk_count);
    const uint32_t *packed = (const uint32_t *)raw.ptr();

    for (int i = 0; i < _chunk_count; i++) {
        _visible_cache.ptrw()[i] = (packed[i] & 1u) ? 1 : 0;
        _lod_cache.ptrw()[i] = (uint8_t)((packed[i] >> 1u) & 3u);
    }

    result["visible"] = _visible_cache;
    result["lod_levels"] = _lod_cache;
    return result;
}

bool GpuChunkCuller::is_gpu_available() const {
    return _gpu_available;
}

// ═══════════════════════════════════════════════════════════════════════
//  Cleanup
// ═══════════════════════════════════════════════════════════════════════

void GpuChunkCuller::_cleanup() {
    if (!_rd) return;

    if (_uniform_set.is_valid()) _rd->free_rid(_uniform_set);
    if (_aabb_buf.is_valid())    _rd->free_rid(_aabb_buf);
    if (_result_buf.is_valid())  _rd->free_rid(_result_buf);
    if (_pipeline.is_valid())    _rd->free_rid(_pipeline);
    if (_shader.is_valid())      _rd->free_rid(_shader);

    _uniform_set = RID();
    _aabb_buf = RID();
    _result_buf = RID();
    _pipeline = RID();
    _shader = RID();

    if (_owns_rd && _rd) {
        memdelete(_rd);
    }
    _rd = nullptr;
    _owns_rd = false;
    _gpu_available = false;
}

// ═══════════════════════════════════════════════════════════════════════
//  Binding
// ═══════════════════════════════════════════════════════════════════════

void GpuChunkCuller::_bind_methods() {
    ClassDB::bind_method(D_METHOD("setup", "max_chunks"), &GpuChunkCuller::setup);
    ClassDB::bind_method(D_METHOD("set_chunk_aabbs", "aabbs"), &GpuChunkCuller::set_chunk_aabbs);
    ClassDB::bind_method(D_METHOD("cull", "view_proj", "camera_pos",
                                  "vis_radius", "lod1_dist", "lod2_dist"),
                         &GpuChunkCuller::cull);
    ClassDB::bind_method(D_METHOD("is_gpu_available"), &GpuChunkCuller::is_gpu_available);
}

} // namespace godot
