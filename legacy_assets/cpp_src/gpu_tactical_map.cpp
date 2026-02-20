#include "gpu_tactical_map.h"
#include "gpu_shaders.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstring>
#include <cmath>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════

GpuTacticalMap::GpuTacticalMap() {}

GpuTacticalMap::~GpuTacticalMap() {
    _cleanup();
}

// ═══════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::setup(VoxelWorld *world, float map_w, float map_h) {
    _cleanup();

    _world = world;
    _map_w = map_w;
    _map_h = map_h;
    _voxel_scale = world ? world->get_voxel_scale() : 0.25f;

    // Calculate grid sizes
    _pressure_w = std::max(1, (int)std::ceil(map_w / PRESSURE_CELL_M));
    _pressure_h = std::max(1, (int)std::ceil(map_h / PRESSURE_CELL_M));
    _cover_w = std::max(1, (int)std::ceil(map_w / COVER_CELL_M));
    _cover_h = std::max(1, (int)std::ceil(map_h / COVER_CELL_M));

    UtilityFunctions::print("[GpuTacticalMap] Pressure grid: ", _pressure_w, "x", _pressure_h,
                            " (", PRESSURE_CELL_M, "m/cell)");
    UtilityFunctions::print("[GpuTacticalMap] Cover grid: ", _cover_w, "x", _cover_h,
                            " (", COVER_CELL_M, "m/cell)");

    // Try to get a RenderingDevice: local first, then global fallback
    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) {
        UtilityFunctions::push_error("[GpuTacticalMap] RenderingServer not available");
        return;
    }

    // Try local device (Vulkan) — we own it and can freely submit/sync
    _rd = rs->create_local_rendering_device();
    if (_rd) {
        _owns_rd = true;
        UtilityFunctions::print("[GpuTacticalMap] Using local RenderingDevice");
    } else {
        // Fallback: use the global rendering device (D3D12, etc.)
        _rd = rs->get_rendering_device();
        _owns_rd = false;
        if (_rd) {
            UtilityFunctions::print("[GpuTacticalMap] Using global RenderingDevice (D3D12 fallback)");
        } else {
            UtilityFunctions::push_warning("[GpuTacticalMap] No RenderingDevice available — GPU compute disabled");
            return;
        }
    }

    // Compile shaders
    if (!_create_shaders()) {
        UtilityFunctions::push_error("[GpuTacticalMap] Shader compilation failed");
        _cleanup();
        return;
    }

    // Allocate buffers
    _create_buffers();

    // Create uniform sets
    _create_uniform_sets();

    // Create gas diffusion shader + pipeline
    _create_gas_shader();

    // Build initial height map
    _height_map_data.resize(_cover_w * _cover_h, 0);
    _pressure_cache.resize(_pressure_w * _pressure_h * 4, 0.0f);
    _cover_cache.resize(_cover_w * _cover_h, 0.0f);

    if (_world && _world->is_initialized()) {
        rebuild_height_map();
    }

    _gpu_available = true;
    UtilityFunctions::print("[GpuTacticalMap] GPU compute initialized");
}

// ═══════════════════════════════════════════════════════════════════════
//  Shader compilation
// ═══════════════════════════════════════════════════════════════════════

bool GpuTacticalMap::_create_shaders() {
    // ── Pressure diffusion shader ─────────────────────────────────
    {
        Ref<RDShaderSource> src;
        src.instantiate();
        src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(PRESSURE_DIFFUSION_GLSL));
        src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);

        Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(src);
        if (spirv.is_null()) {
            UtilityFunctions::push_error("[GpuTacticalMap] Pressure shader SPIR-V is null");
            return false;
        }
        String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
        if (!err.is_empty()) {
            UtilityFunctions::push_error("[GpuTacticalMap] Pressure shader error: ", err);
            return false;
        }

        _pressure_shader = _rd->shader_create_from_spirv(spirv, "PressureDiffusion");
        if (!_pressure_shader.is_valid()) {
            UtilityFunctions::push_error("[GpuTacticalMap] Failed to create pressure shader");
            return false;
        }
        _pressure_pipeline = _rd->compute_pipeline_create(_pressure_shader);
    }

    // ── Cover shadow shader ───────────────────────────────────────
    {
        Ref<RDShaderSource> src;
        src.instantiate();
        src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(COVER_SHADOW_GLSL));
        src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);

        Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(src);
        if (spirv.is_null()) {
            UtilityFunctions::push_error("[GpuTacticalMap] Cover shader SPIR-V is null");
            return false;
        }
        String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
        if (!err.is_empty()) {
            UtilityFunctions::push_error("[GpuTacticalMap] Cover shader error: ", err);
            return false;
        }

        _cover_shader = _rd->shader_create_from_spirv(spirv, "CoverShadow");
        if (!_cover_shader.is_valid()) {
            UtilityFunctions::push_error("[GpuTacticalMap] Failed to create cover shader");
            return false;
        }
        _cover_pipeline = _rd->compute_pipeline_create(_cover_shader);
    }

    UtilityFunctions::print("[GpuTacticalMap] Shaders compiled successfully");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Buffer allocation
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::_create_buffers() {
    // Height map: uint16 packed as uint32 (2 bytes per cell, pad to multiple of 4)
    uint32_t hmap_bytes = ((_cover_w * _cover_h) * 2 + 3) / 4 * 4;
    PackedByteArray hmap_init;
    hmap_init.resize(hmap_bytes);
    memset(hmap_init.ptrw(), 0, hmap_bytes);
    _height_map_buf = _rd->storage_buffer_create(hmap_bytes, hmap_init);

    // Unit buffer: vec4 * MAX_UNITS (16 bytes each)
    uint32_t unit_bytes = MAX_UNITS * 16;
    PackedByteArray unit_init;
    unit_init.resize(unit_bytes);
    memset(unit_init.ptrw(), 0, unit_bytes);
    _unit_buf = _rd->storage_buffer_create(unit_bytes, unit_init);

    // Threat buffer: vec4 * MAX_THREATS
    uint32_t threat_bytes = MAX_THREATS * 16;
    PackedByteArray threat_init;
    threat_init.resize(threat_bytes);
    memset(threat_init.ptrw(), 0, threat_bytes);
    _threat_buf = _rd->storage_buffer_create(threat_bytes, threat_init);

    // Goal buffer: vec4 * MAX_GOALS
    uint32_t goal_bytes = MAX_GOALS * 16;
    PackedByteArray goal_init;
    goal_init.resize(goal_bytes);
    memset(goal_init.ptrw(), 0, goal_bytes);
    _goal_buf = _rd->storage_buffer_create(goal_bytes, goal_init);

    // Pressure buffers: 4 floats per cell (RGBA)
    uint32_t pressure_bytes = _pressure_w * _pressure_h * 4 * sizeof(float);
    PackedByteArray pressure_init;
    pressure_init.resize(pressure_bytes);
    memset(pressure_init.ptrw(), 0, pressure_bytes);
    _pressure_buf_a = _rd->storage_buffer_create(pressure_bytes, pressure_init);
    _pressure_buf_b = _rd->storage_buffer_create(pressure_bytes, pressure_init);

    // Cover buffer: 1 float per cell
    uint32_t cover_bytes = _cover_w * _cover_h * sizeof(float);
    PackedByteArray cover_init;
    cover_init.resize(cover_bytes);
    memset(cover_init.ptrw(), 0, cover_bytes);
    _cover_buf = _rd->storage_buffer_create(cover_bytes, cover_init);
}

// ═══════════════════════════════════════════════════════════════════════
//  Uniform set creation
// ═══════════════════════════════════════════════════════════════════════

static Ref<RDUniform> make_storage_uniform(int binding, const RID &buffer) {
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
    u->set_binding(binding);
    u->add_id(buffer);
    return u;
}

void GpuTacticalMap::_create_uniform_sets() {
    // ── Pressure set A→B: reads A, writes B ──────────────────────
    {
        TypedArray<Ref<RDUniform>> uniforms;
        uniforms.append(make_storage_uniform(0, _height_map_buf));
        uniforms.append(make_storage_uniform(1, _unit_buf));
        uniforms.append(make_storage_uniform(2, _goal_buf));
        uniforms.append(make_storage_uniform(3, _pressure_buf_a));  // read
        uniforms.append(make_storage_uniform(4, _pressure_buf_b));  // write
        _pressure_set_a_to_b = _rd->uniform_set_create(uniforms, _pressure_shader, 0);
    }

    // ── Pressure set B→A: reads B, writes A ──────────────────────
    {
        TypedArray<Ref<RDUniform>> uniforms;
        uniforms.append(make_storage_uniform(0, _height_map_buf));
        uniforms.append(make_storage_uniform(1, _unit_buf));
        uniforms.append(make_storage_uniform(2, _goal_buf));
        uniforms.append(make_storage_uniform(3, _pressure_buf_b));  // read
        uniforms.append(make_storage_uniform(4, _pressure_buf_a));  // write
        _pressure_set_b_to_a = _rd->uniform_set_create(uniforms, _pressure_shader, 0);
    }

    // ── Cover set ─────────────────────────────────────────────────
    {
        TypedArray<Ref<RDUniform>> uniforms;
        uniforms.append(make_storage_uniform(0, _height_map_buf));
        uniforms.append(make_storage_uniform(1, _threat_buf));
        uniforms.append(make_storage_uniform(2, _cover_buf));
        _cover_set = _rd->uniform_set_create(uniforms, _cover_shader, 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Height map
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::rebuild_height_map() {
    if (!_world || !_world->is_initialized()) return;

    int sy = _world->get_world_size_y();
    int voxels_per_cell = std::max(1, (int)std::round(1.0f / _voxel_scale));

    _height_map_data.assign(_cover_w * _cover_h, 0);

    for (int cz = 0; cz < _cover_h; cz++) {
        for (int cx = 0; cx < _cover_w; cx++) {
            // Find highest solid voxel in this NxN column (N = voxels per 1m cell)
            int vx_base = cx * voxels_per_cell;
            int vz_base = cz * voxels_per_cell;
            int max_y = 0;

            for (int vy = sy - 1; vy >= 0; vy--) {
                bool found = false;
                for (int dx = 0; dx < voxels_per_cell && !found; dx++) {
                    for (int dz = 0; dz < voxels_per_cell && !found; dz++) {
                        int vx = vx_base + dx;
                        int vz = vz_base + dz;
                        if (vx < _world->get_world_size_x() && vz < _world->get_world_size_z()) {
                            if (_world->get_voxel(vx, vy, vz) != 0) {
                                max_y = vy;
                                found = true;
                            }
                        }
                    }
                }
                if (found) break;
            }

            _height_map_data[cz * _cover_w + cx] = (uint16_t)std::min(max_y, 65535);
        }
    }

    _height_map_dirty = true;
    UtilityFunctions::print("[GpuTacticalMap] Height map rebuilt (", _cover_w, "x", _cover_h,
                            ", voxels_per_cell=", voxels_per_cell, ")");
}

void GpuTacticalMap::update_height_map_region(int min_cx, int max_cx, int min_cz, int max_cz) {
    if (!_world || !_world->is_initialized()) return;
    if (_height_map_data.empty()) return;

    int sy = _world->get_world_size_y();
    int voxels_per_cell = std::max(1, (int)std::round(1.0f / _voxel_scale));

    min_cx = std::max(0, min_cx);
    max_cx = std::min(_cover_w - 1, max_cx);
    min_cz = std::max(0, min_cz);
    max_cz = std::min(_cover_h - 1, max_cz);

    for (int cz = min_cz; cz <= max_cz; cz++) {
        for (int cx = min_cx; cx <= max_cx; cx++) {
            int vx_base = cx * voxels_per_cell;
            int vz_base = cz * voxels_per_cell;
            int max_y = 0;

            for (int vy = sy - 1; vy >= 0; vy--) {
                bool found = false;
                for (int dx = 0; dx < voxels_per_cell && !found; dx++) {
                    for (int dz = 0; dz < voxels_per_cell && !found; dz++) {
                        int vx = vx_base + dx;
                        int vz = vz_base + dz;
                        if (vx < _world->get_world_size_x() && vz < _world->get_world_size_z()) {
                            if (_world->get_voxel(vx, vy, vz) != 0) {
                                max_y = vy;
                                found = true;
                            }
                        }
                    }
                }
                if (found) break;
            }

            _height_map_data[cz * _cover_w + cx] = (uint16_t)std::min(max_y, 65535);
        }
    }

    _height_map_dirty = true;
}

void GpuTacticalMap::_upload_height_map() {
    if (!_height_map_dirty || !_rd) return;

    // uint16 data: 2 bytes per cell
    uint32_t byte_size = (uint32_t)_height_map_data.size() * 2;
    uint32_t padded_size = (byte_size + 3) / 4 * 4;

    PackedByteArray bytes;
    bytes.resize(padded_size);
    memcpy(bytes.ptrw(), _height_map_data.data(), byte_size);
    if (padded_size > byte_size) {
        memset(bytes.ptrw() + byte_size, 0, padded_size - byte_size);
    }

    _rd->buffer_update(_height_map_buf, 0, padded_size, bytes);
    _height_map_dirty = false;
}

// ═══════════════════════════════════════════════════════════════════════
//  Data upload
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::_upload_units(const PackedVector3Array &friendly, const PackedVector3Array &enemy) {
    _num_friendlies = std::min((int)friendly.size(), MAX_UNITS / 2);
    _num_enemies = std::min((int)enemy.size(), MAX_UNITS - _num_friendlies);

    int total = _num_friendlies + _num_enemies;
    if (total == 0) return;

    // Pack as vec4 (x, y, z, team) — team: -1=friendly, +1=enemy
    PackedByteArray bytes;
    bytes.resize(total * 16);
    float *ptr = (float *)bytes.ptrw();

    for (int i = 0; i < _num_friendlies; i++) {
        const Vector3 &p = friendly[i];
        ptr[i * 4 + 0] = p.x;
        ptr[i * 4 + 1] = p.y;
        ptr[i * 4 + 2] = p.z;
        ptr[i * 4 + 3] = -1.0f;
    }
    for (int i = 0; i < _num_enemies; i++) {
        int idx = _num_friendlies + i;
        const Vector3 &p = enemy[i];
        ptr[idx * 4 + 0] = p.x;
        ptr[idx * 4 + 1] = p.y;
        ptr[idx * 4 + 2] = p.z;
        ptr[idx * 4 + 3] = 1.0f;
    }

    _rd->buffer_update(_unit_buf, 0, total * 16, bytes);
}

void GpuTacticalMap::_upload_threats(const PackedVector3Array &threats) {
    _num_threats = std::min((int)threats.size(), MAX_THREATS);
    if (_num_threats == 0) return;

    PackedByteArray bytes;
    bytes.resize(_num_threats * 16);
    float *ptr = (float *)bytes.ptrw();

    for (int i = 0; i < _num_threats; i++) {
        const Vector3 &p = threats[i];
        ptr[i * 4 + 0] = p.x;
        ptr[i * 4 + 1] = p.y;
        ptr[i * 4 + 2] = p.z;
        ptr[i * 4 + 3] = 0.0f;
    }

    _rd->buffer_update(_threat_buf, 0, _num_threats * 16, bytes);
}

void GpuTacticalMap::_upload_goals(const PackedVector3Array &positions, const PackedFloat32Array &strengths) {
    _num_goals = std::min((int)positions.size(), MAX_GOALS);
    if (_num_goals == 0) return;

    PackedByteArray bytes;
    bytes.resize(_num_goals * 16);
    float *ptr = (float *)bytes.ptrw();

    for (int i = 0; i < _num_goals; i++) {
        const Vector3 &p = positions[i];
        ptr[i * 4 + 0] = p.x;
        ptr[i * 4 + 1] = p.y;
        ptr[i * 4 + 2] = p.z;
        ptr[i * 4 + 3] = (i < (int)strengths.size()) ? strengths[i] : 1.0f;
    }

    _rd->buffer_update(_goal_buf, 0, _num_goals * 16, bytes);
}

// ═══════════════════════════════════════════════════════════════════════
//  Dispatch
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::_dispatch_pressure() {
    int groups_x = (_pressure_w + LOCAL_SIZE - 1) / LOCAL_SIZE;
    int groups_z = (_pressure_h + LOCAL_SIZE - 1) / LOCAL_SIZE;

    int64_t cl = _rd->compute_list_begin();

    for (int pass = 0; pass < DIFFUSION_PASSES; pass++) {
        // Alternate uniform sets for ping-pong
        RID set = (pass % 2 == 0) ? _pressure_set_a_to_b : _pressure_set_b_to_a;

        PressurePushConstants pc;
        pc.grid_w = _pressure_w;
        pc.grid_h = _pressure_h;
        pc.pass_index = pass;
        pc.num_friendlies = _num_friendlies;
        pc.num_enemies = _num_enemies;
        pc.num_goals = _num_goals;
        pc.decay_rate = 0.15f;
        pc.diffusion_rate = 0.25f;
        pc.standing_voxels_u = (int32_t)std::round(1.5f / _voxel_scale);
        pc.pad0 = 0;

        PackedByteArray pc_bytes;
        pc_bytes.resize(sizeof(pc));
        memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

        _rd->compute_list_bind_compute_pipeline(cl, _pressure_pipeline);
        _rd->compute_list_bind_uniform_set(cl, set, 0);
        _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
        _rd->compute_list_dispatch(cl, groups_x, groups_z, 1);

        if (pass < DIFFUSION_PASSES - 1) {
            _rd->compute_list_add_barrier(cl);
        }
    }

    _rd->compute_list_end();
}

void GpuTacticalMap::_dispatch_cover() {
    int groups_x = (_cover_w + LOCAL_SIZE - 1) / LOCAL_SIZE;
    int groups_z = (_cover_h + LOCAL_SIZE - 1) / LOCAL_SIZE;

    CoverPushConstants pc;
    pc.grid_w = _cover_w;
    pc.grid_h = _cover_h;
    pc.num_threats = _num_threats;
    pc.max_ray_dist = 60.0f;
    pc.shadow_depth = 4.0f;
    pc.standing_voxels = 1.5f / _voxel_scale;  // standing height in voxels
    pc.pad0 = 0.0f;
    pc.pad1 = 0.0f;

    PackedByteArray pc_bytes;
    pc_bytes.resize(sizeof(pc));
    memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

    int64_t cl = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(cl, _cover_pipeline);
    _rd->compute_list_bind_uniform_set(cl, _cover_set, 0);
    _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
    _rd->compute_list_dispatch(cl, groups_x, groups_z, 1);
    _rd->compute_list_end();
}

// ═══════════════════════════════════════════════════════════════════════
//  Readback
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::_readback() {
    // Read pressure buffer (final output depends on pass count parity)
    // With 6 passes: pass 0 writes B, 1 writes A, 2 writes B, 3 writes A, 4 writes B, 5 writes A
    // So final result is in A
    RID final_pressure = (DIFFUSION_PASSES % 2 == 0) ? _pressure_buf_a : _pressure_buf_b;

    uint32_t pressure_bytes = _pressure_w * _pressure_h * 4 * sizeof(float);
    PackedByteArray pressure_data = _rd->buffer_get_data(final_pressure, 0, pressure_bytes);

    if ((int)pressure_data.size() == (int)pressure_bytes) {
        _pressure_cache.resize(_pressure_w * _pressure_h * 4);
        memcpy(_pressure_cache.data(), pressure_data.ptr(), pressure_bytes);
    }

    // Read cover buffer
    uint32_t cover_bytes = _cover_w * _cover_h * sizeof(float);
    PackedByteArray cover_data = _rd->buffer_get_data(_cover_buf, 0, cover_bytes);

    if ((int)cover_data.size() == (int)cover_bytes) {
        _cover_cache.resize(_cover_w * _cover_h);
        memcpy(_cover_cache.data(), cover_data.ptr(), cover_bytes);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Main tick
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::tick(
        const PackedVector3Array &friendly_positions,
        const PackedVector3Array &enemy_positions,
        const PackedVector3Array &threat_centroids,
        const PackedVector3Array &goal_positions,
        const PackedFloat32Array &goal_strengths) {

    if (!_gpu_available || !_rd) return;

    // Upload data
    _upload_height_map();
    _upload_units(friendly_positions, enemy_positions);
    _upload_threats(threat_centroids);
    _upload_goals(goal_positions, goal_strengths);

    // If gas was spawned on CPU side, upload pressure cache to GPU buffer A
    // (Buffer A is always the "current" buffer after even-count passes)
    if (_gas_spawn_dirty) {
        uint32_t size = (uint32_t)(_pressure_cache.size() * sizeof(float));
        PackedByteArray bytes;
        bytes.resize(size);
        memcpy(bytes.ptrw(), _pressure_cache.data(), size);
        _rd->buffer_update(_pressure_buf_a, 0, size, bytes);
        _gas_spawn_dirty = false;
    }

    // Dispatch pressure diffusion + cover shadows in a single submission
    _dispatch_pressure();

    if (_owns_rd) {
        // Local device: submit + sync between dispatches is fine
        _rd->submit();
        _rd->sync();

        if (_num_threats > 0) {
            _dispatch_cover();
            _rd->submit();
            _rd->sync();
        }

        // Gas diffusion (every tick)
        _dispatch_gas(0.016f);  // Assume 60 FPS for now
        _rd->submit();
        _rd->sync();
    } else {
        // Global device: single barrier + dispatch, then sync once
        _rd->barrier(RenderingDevice::BARRIER_MASK_COMPUTE, RenderingDevice::BARRIER_MASK_COMPUTE);

        if (_num_threats > 0) {
            _dispatch_cover();
        }

        // Gas diffusion
        _dispatch_gas(0.016f);

        _rd->submit();
        _rd->sync();
    }

    // Read results back to CPU
    _readback();
}

// ═══════════════════════════════════════════════════════════════════════
//  Coordinate helpers
// ═══════════════════════════════════════════════════════════════════════

int GpuTacticalMap::_world_to_pressure_x(float wx) const {
    return std::clamp((int)((wx + _map_w * 0.5f) / PRESSURE_CELL_M), 0, _pressure_w - 1);
}

int GpuTacticalMap::_world_to_pressure_z(float wz) const {
    return std::clamp((int)((wz + _map_h * 0.5f) / PRESSURE_CELL_M), 0, _pressure_h - 1);
}

int GpuTacticalMap::_world_to_cover_x(float wx) const {
    return std::clamp((int)((wx + _map_w * 0.5f) / COVER_CELL_M), 0, _cover_w - 1);
}

int GpuTacticalMap::_world_to_cover_z(float wz) const {
    return std::clamp((int)((wz + _map_h * 0.5f) / COVER_CELL_M), 0, _cover_h - 1);
}

// ═══════════════════════════════════════════════════════════════════════
//  Queries (read from CPU cache)
// ═══════════════════════════════════════════════════════════════════════

float GpuTacticalMap::get_threat_at(const Vector3 &pos) const {
    if (_pressure_cache.empty()) return 0.0f;
    int px = _world_to_pressure_x(pos.x);
    int pz = _world_to_pressure_z(pos.z);
    int idx = (pz * _pressure_w + px) * 4;
    return _pressure_cache[idx + 0];  // R channel = threat
}

float GpuTacticalMap::get_goal_at(const Vector3 &pos) const {
    if (_pressure_cache.empty()) return 0.0f;
    int px = _world_to_pressure_x(pos.x);
    int pz = _world_to_pressure_z(pos.z);
    int idx = (pz * _pressure_w + px) * 4;
    return _pressure_cache[idx + 1];  // G channel = goal
}

float GpuTacticalMap::get_cover_at(const Vector3 &pos) const {
    if (_cover_cache.empty()) return 0.0f;
    int cx = _world_to_cover_x(pos.x);
    int cz = _world_to_cover_z(pos.z);
    return _cover_cache[cz * _cover_w + cx];
}

Vector3 GpuTacticalMap::get_flow_vector(const Vector3 &pos) const {
    if (_pressure_cache.empty()) return Vector3();

    int px = _world_to_pressure_x(pos.x);
    int pz = _world_to_pressure_z(pos.z);

    if (px < 1 || px >= _pressure_w - 1 || pz < 1 || pz >= _pressure_h - 1)
        return Vector3();

    // Compute tactical value at a cell: attracted to goals, repelled by threats
    auto val = [&](int x, int z) -> float {
        int i = (z * _pressure_w + x) * 4;
        float threat = _pressure_cache[i + 0];
        float goal   = _pressure_cache[i + 1];
        float safety = _pressure_cache[i + 2];
        return goal * 1.0f - threat * 1.5f + safety * 0.5f;
    };

    float dx = val(px + 1, pz) - val(px - 1, pz);
    float dz = val(px, pz + 1) - val(px, pz - 1);

    Vector3 flow(dx, 0.0f, dz);
    float len = flow.length();
    if (len > 0.01f) flow /= len;
    return flow;
}

// ═══════════════════════════════════════════════════════════════════════
//  Debug
// ═══════════════════════════════════════════════════════════════════════

PackedFloat32Array GpuTacticalMap::get_pressure_debug() const {
    PackedFloat32Array out;
    out.resize(_pressure_cache.size());
    if (!_pressure_cache.empty()) {
        memcpy(out.ptrw(), _pressure_cache.data(), _pressure_cache.size() * sizeof(float));
    }
    return out;
}

PackedFloat32Array GpuTacticalMap::get_cover_debug() const {
    PackedFloat32Array out;
    out.resize(_cover_cache.size());
    if (!_cover_cache.empty()) {
        memcpy(out.ptrw(), _cover_cache.data(), _cover_cache.size() * sizeof(float));
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════
//  Cleanup
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::_cleanup() {
    if (_rd) {
        // Free all RIDs
        if (_pressure_set_a_to_b.is_valid()) _rd->free_rid(_pressure_set_a_to_b);
        if (_pressure_set_b_to_a.is_valid()) _rd->free_rid(_pressure_set_b_to_a);
        if (_cover_set.is_valid()) _rd->free_rid(_cover_set);
        if (_gas_set_a_to_b.is_valid()) _rd->free_rid(_gas_set_a_to_b);
        if (_gas_set_b_to_a.is_valid()) _rd->free_rid(_gas_set_b_to_a);

        if (_pressure_pipeline.is_valid()) _rd->free_rid(_pressure_pipeline);
        if (_cover_pipeline.is_valid()) _rd->free_rid(_cover_pipeline);
        if (_gas_pipeline.is_valid()) _rd->free_rid(_gas_pipeline);

        if (_pressure_shader.is_valid()) _rd->free_rid(_pressure_shader);
        if (_cover_shader.is_valid()) _rd->free_rid(_cover_shader);
        if (_gas_shader.is_valid()) _rd->free_rid(_gas_shader);

        if (_height_map_buf.is_valid()) _rd->free_rid(_height_map_buf);
        if (_unit_buf.is_valid()) _rd->free_rid(_unit_buf);
        if (_threat_buf.is_valid()) _rd->free_rid(_threat_buf);
        if (_goal_buf.is_valid()) _rd->free_rid(_goal_buf);
        if (_pressure_buf_a.is_valid()) _rd->free_rid(_pressure_buf_a);
        if (_pressure_buf_b.is_valid()) _rd->free_rid(_pressure_buf_b);
        if (_cover_buf.is_valid()) _rd->free_rid(_cover_buf);

        if (_owns_rd) {
            memdelete(_rd);
        }
        _rd = nullptr;
        _owns_rd = false;
    }

    _gpu_available = false;
    _gas_spawn_dirty = false;
    _pressure_cache.clear();
    _cover_cache.clear();
    _height_map_data.clear();

    _pressure_set_a_to_b = RID();
    _pressure_set_b_to_a = RID();
    _cover_set = RID();
    _gas_set_a_to_b = RID();
    _gas_set_b_to_a = RID();
    _pressure_pipeline = RID();
    _cover_pipeline = RID();
    _gas_pipeline = RID();
    _pressure_shader = RID();
    _cover_shader = RID();
    _gas_shader = RID();
    _height_map_buf = RID();
    _unit_buf = RID();
    _threat_buf = RID();
    _goal_buf = RID();
    _pressure_buf_a = RID();
    _pressure_buf_b = RID();
    _cover_buf = RID();
}

// ═══════════════════════════════════════════════════════════════════════
//  Gas System (Smoke & Gas Grenades)
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::spawn_gas_cloud(const Vector3 &world_pos, float radius_m,
                                      float initial_density, uint8_t gas_type) {
    if (!_gpu_available || !_rd) return;
    if (initial_density < 0.01f) return;  // Too weak to matter

    // Write to pressure cache (operates on pressure grid: 150x100, 4m/cell)
    int px = _world_to_pressure_x(world_pos.x);
    int pz = _world_to_pressure_z(world_pos.z);
    int radius_cells = std::max(1, (int)(radius_m / (float)PRESSURE_CELL_M));

    for (int dz = -radius_cells; dz <= radius_cells; dz++) {
        for (int dx = -radius_cells; dx <= radius_cells; dx++) {
            int x = px + dx;
            int z = pz + dz;
            if (x < 0 || x >= _pressure_w || z < 0 || z >= _pressure_h) continue;

            float dist = std::sqrt((float)(dx * dx + dz * dz));
            if (dist > (float)radius_cells) continue;

            // Falloff from center
            float strength = initial_density * (1.0f - dist / (float)(radius_cells + 1));
            int idx = (z * _pressure_w + x) * 4;  // 4 floats per cell (RGBA)

            // Set B (density) and A (gas type) channels
            // Only overwrite if stronger than existing gas
            if (strength > _pressure_cache[idx + 2]) {  // B channel
                _pressure_cache[idx + 2] = strength;
                _pressure_cache[idx + 3] = (float)gas_type;  // A channel
            }
        }
    }

    // Mark dirty so tick() uploads to GPU before next dispatch
    _gas_spawn_dirty = true;
}

float GpuTacticalMap::sample_gas_density(const Vector3 &pos) const {
    int px = _world_to_pressure_x(pos.x);
    int pz = _world_to_pressure_z(pos.z);
    if (px < 0 || px >= _pressure_w || pz < 0 || pz >= _pressure_h) return 0.0f;

    int idx = (pz * _pressure_w + px) * 4;
    if (idx + 2 >= (int)_pressure_cache.size()) return 0.0f;

    return _pressure_cache[idx + 2];  // B channel
}

uint8_t GpuTacticalMap::sample_gas_type(const Vector3 &pos) const {
    int px = _world_to_pressure_x(pos.x);
    int pz = _world_to_pressure_z(pos.z);
    if (px < 0 || px >= _pressure_w || pz < 0 || pz >= _pressure_h) return 0;

    int idx = (pz * _pressure_w + px) * 4;
    if (idx + 3 >= (int)_pressure_cache.size()) return 0;

    return (uint8_t)_pressure_cache[idx + 3];  // A channel
}

float GpuTacticalMap::sample_gas_along_ray(const Vector3 &from, const Vector3 &to) const {
    Vector3 diff = to - from;
    float dist = diff.length();
    if (dist < 1e-4f) return 0.0f;

    // Sample every 2 meters
    int samples = (int)(dist / 2.0f) + 1;
    float max_density = 0.0f;
    for (int i = 0; i < samples; i++) {
        float t = (float)i / (float)samples;
        Vector3 sample_pos = from + diff * t;
        max_density = std::max(max_density, sample_gas_density(sample_pos));
    }

    return max_density;
}

void GpuTacticalMap::_create_gas_shader() {
    if (!_rd) return;  // Don't check _gpu_available — called during setup before it's set

    // Compile gas diffusion shader from gpu_shaders.h
    Ref<RDShaderSource> src;
    src.instantiate();
    src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(GAS_DIFFUSION_GLSL));
    src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);

    Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(src);
    if (spirv.is_null()) {
        UtilityFunctions::push_warning("[GpuTacticalMap] Gas shader SPIR-V is null");
        return;
    }
    String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!err.is_empty()) {
        UtilityFunctions::push_warning("[GpuTacticalMap] Gas shader compile failed: ", err);
        return;
    }

    _gas_shader = _rd->shader_create_from_spirv(spirv, "GasDiffusion");
    if (!_gas_shader.is_valid()) {
        UtilityFunctions::push_warning("[GpuTacticalMap] Failed to create gas shader");
        return;
    }
    _gas_pipeline = _rd->compute_pipeline_create(_gas_shader);

    // Create uniform sets for ping-pong (reuse pressure buffers)
    // Gas shader: binding 0 = height_map, 1 = pressure_in, 2 = pressure_out
    {
        TypedArray<Ref<RDUniform>> uniforms;
        uniforms.append(make_storage_uniform(0, _height_map_buf));
        uniforms.append(make_storage_uniform(1, _pressure_buf_a));  // Read from A
        uniforms.append(make_storage_uniform(2, _pressure_buf_b));  // Write to B
        _gas_set_a_to_b = _rd->uniform_set_create(uniforms, _gas_shader, 0);
    }

    {
        TypedArray<Ref<RDUniform>> uniforms;
        uniforms.append(make_storage_uniform(0, _height_map_buf));
        uniforms.append(make_storage_uniform(1, _pressure_buf_b));  // Read from B
        uniforms.append(make_storage_uniform(2, _pressure_buf_a));  // Write to A
        _gas_set_b_to_a = _rd->uniform_set_create(uniforms, _gas_shader, 0);
    }

    UtilityFunctions::print("[GpuTacticalMap] Gas diffusion shader compiled");
}

void GpuTacticalMap::_dispatch_gas(float delta_time) {
    if (!_rd || !_gas_pipeline.is_valid()) return;

    int groups_x = (_pressure_w + LOCAL_SIZE - 1) / LOCAL_SIZE;
    int groups_z = (_pressure_h + LOCAL_SIZE - 1) / LOCAL_SIZE;

    int64_t cl = _rd->compute_list_begin();

    // Run 2 diffusion passes per tick (ping-pong)
    for (int pass = 0; pass < 2; pass++) {
        GasPushConstants pc;
        pc.grid_w = _pressure_w;   // Gas operates on pressure grid (150x100, 4m/cell)
        pc.grid_h = _pressure_h;
        pc.delta_time = delta_time;
        pc.diffusion_rate = 0.05f;
        pc.wind_x = _gas_wind_x;
        pc.wind_z = _gas_wind_z;
        pc.evaporation = 0.02f;
        pc.wall_threshold_voxels = (int32_t)std::round(2.0f / _voxel_scale);

        PackedByteArray pc_bytes;
        pc_bytes.resize(sizeof(pc));
        memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

        RID uniform_set = (pass % 2 == 0) ? _gas_set_a_to_b : _gas_set_b_to_a;

        _rd->compute_list_bind_compute_pipeline(cl, _gas_pipeline);
        _rd->compute_list_bind_uniform_set(cl, uniform_set, 0);
        _rd->compute_list_set_push_constant(cl, pc_bytes, (uint32_t)sizeof(pc));
        _rd->compute_list_dispatch(cl, groups_x, groups_z, 1);

        if (pass < 1) {
            _rd->compute_list_add_barrier(cl);
        }
    }

    _rd->compute_list_end();
}

// ═══════════════════════════════════════════════════════════════════════
//  GDScript Binding
// ═══════════════════════════════════════════════════════════════════════

void GpuTacticalMap::setup_bind(Object *world_obj, float map_w, float map_h) {
    setup(Object::cast_to<VoxelWorld>(world_obj), map_w, map_h);
}

void GpuTacticalMap::_bind_methods() {
    // Use setup_bind (Object*) instead of setup (VoxelWorld*) to avoid
    // cross-DLL type resolution crash in GDExtension class registration.
    ClassDB::bind_method(D_METHOD("setup", "world", "map_w", "map_h"), &GpuTacticalMap::setup_bind);
    ClassDB::bind_method(D_METHOD("tick", "friendly_positions", "enemy_positions",
                                  "threat_centroids", "goal_positions", "goal_strengths"),
                         &GpuTacticalMap::tick);
    ClassDB::bind_method(D_METHOD("rebuild_height_map"), &GpuTacticalMap::rebuild_height_map);
    ClassDB::bind_method(D_METHOD("update_height_map_region", "min_cx", "max_cx", "min_cz", "max_cz"),
                         &GpuTacticalMap::update_height_map_region);
    ClassDB::bind_method(D_METHOD("get_threat_at", "pos"), &GpuTacticalMap::get_threat_at);
    ClassDB::bind_method(D_METHOD("get_goal_at", "pos"), &GpuTacticalMap::get_goal_at);
    ClassDB::bind_method(D_METHOD("get_cover_at", "pos"), &GpuTacticalMap::get_cover_at);
    ClassDB::bind_method(D_METHOD("get_flow_vector", "pos"), &GpuTacticalMap::get_flow_vector);
    ClassDB::bind_method(D_METHOD("is_gpu_available"), &GpuTacticalMap::is_gpu_available);
    ClassDB::bind_method(D_METHOD("get_pressure_debug"), &GpuTacticalMap::get_pressure_debug);
    ClassDB::bind_method(D_METHOD("get_cover_debug"), &GpuTacticalMap::get_cover_debug);
    ClassDB::bind_method(D_METHOD("get_pressure_width"), &GpuTacticalMap::get_pressure_width);
    ClassDB::bind_method(D_METHOD("get_pressure_height"), &GpuTacticalMap::get_pressure_height);
    ClassDB::bind_method(D_METHOD("get_cover_width"), &GpuTacticalMap::get_cover_width);
    ClassDB::bind_method(D_METHOD("get_cover_height"), &GpuTacticalMap::get_cover_height);
    ClassDB::bind_method(D_METHOD("get_terrain_height_m", "wx", "wz"), &GpuTacticalMap::get_terrain_height_m);

    // Gas system
    ClassDB::bind_method(D_METHOD("spawn_gas_cloud", "world_pos", "radius_m", "initial_density", "gas_type"),
                         &GpuTacticalMap::spawn_gas_cloud);
    ClassDB::bind_method(D_METHOD("sample_gas_density", "pos"), &GpuTacticalMap::sample_gas_density);
    ClassDB::bind_method(D_METHOD("sample_gas_type", "pos"), &GpuTacticalMap::sample_gas_type);
    ClassDB::bind_method(D_METHOD("sample_gas_along_ray", "from", "to"), &GpuTacticalMap::sample_gas_along_ray);
}
