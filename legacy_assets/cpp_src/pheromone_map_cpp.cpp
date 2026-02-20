#include "pheromone_map_cpp.h"
#include "gpu_shaders.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

PheromoneMapCPP::PheromoneMapCPP()
	: _width(0), _height(0), _channel_count(0), _cell_size(4.0f), _world_origin(Vector3(0, 0, 0)) {
}

PheromoneMapCPP::~PheromoneMapCPP() {
	_cleanup_gpu();
}

void PheromoneMapCPP::_bind_methods() {
	// Initialization
	ClassDB::bind_method(D_METHOD("initialize", "width", "height", "channel_count", "cell_size", "world_origin"),
		&PheromoneMapCPP::initialize);

	// Channel parameters
	ClassDB::bind_method(D_METHOD("set_channel_params", "channel", "evaporation_rate", "diffusion_rate"),
		&PheromoneMapCPP::set_channel_params);
	ClassDB::bind_method(D_METHOD("get_evaporation_rate", "channel"), &PheromoneMapCPP::get_evaporation_rate);
	ClassDB::bind_method(D_METHOD("get_diffusion_rate", "channel"), &PheromoneMapCPP::get_diffusion_rate);

	// Deposition
	ClassDB::bind_method(D_METHOD("deposit", "world_pos", "channel", "strength"),
		&PheromoneMapCPP::deposit);
	ClassDB::bind_method(D_METHOD("deposit_radius", "world_pos", "channel", "strength", "radius"),
		&PheromoneMapCPP::deposit_radius);
	ClassDB::bind_method(D_METHOD("deposit_cone", "origin", "direction", "channel", "strength", "half_angle_rad", "range"),
		&PheromoneMapCPP::deposit_cone);
	ClassDB::bind_method(D_METHOD("deposit_trail", "from", "to", "channel", "strength"),
		&PheromoneMapCPP::deposit_trail);

	// Sampling
	ClassDB::bind_method(D_METHOD("sample", "world_pos", "channel"),
		&PheromoneMapCPP::sample);
	ClassDB::bind_method(D_METHOD("sample_bilinear", "world_pos", "channel"),
		&PheromoneMapCPP::sample_bilinear);
	ClassDB::bind_method(D_METHOD("gradient", "world_pos", "channel"),
		&PheromoneMapCPP::gradient);
	ClassDB::bind_method(D_METHOD("gradient_raw", "world_pos", "channel"),
		&PheromoneMapCPP::gradient_raw);

	// GPU acceleration
	ClassDB::bind_method(D_METHOD("setup_gpu"), &PheromoneMapCPP::setup_gpu);
	ClassDB::bind_method(D_METHOD("is_gpu_active"), &PheromoneMapCPP::is_gpu_active);

	// CA update
	ClassDB::bind_method(D_METHOD("tick", "delta"), &PheromoneMapCPP::tick);

	// Debug
	ClassDB::bind_method(D_METHOD("get_channel_data", "channel"), &PheromoneMapCPP::get_channel_data);
	ClassDB::bind_method(D_METHOD("get_max_value", "channel"), &PheromoneMapCPP::get_max_value);
	ClassDB::bind_method(D_METHOD("get_total_value", "channel"), &PheromoneMapCPP::get_total_value);

	// Utility
	ClassDB::bind_method(D_METHOD("clear_channel", "channel"), &PheromoneMapCPP::clear_channel);
	ClassDB::bind_method(D_METHOD("clear_all"), &PheromoneMapCPP::clear_all);
	ClassDB::bind_method(D_METHOD("get_width"), &PheromoneMapCPP::get_width);
	ClassDB::bind_method(D_METHOD("get_height"), &PheromoneMapCPP::get_height);
	ClassDB::bind_method(D_METHOD("get_channel_count"), &PheromoneMapCPP::get_channel_count);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &PheromoneMapCPP::get_cell_size);
	ClassDB::bind_method(D_METHOD("get_world_origin"), &PheromoneMapCPP::get_world_origin);
}

void PheromoneMapCPP::initialize(int width, int height, int channel_count, float cell_size, const Vector3& world_origin) {
	_width = width;
	_height = height;
	_channel_count = channel_count;
	_cell_size = cell_size;
	_world_origin = world_origin;

	// Allocate grid data
	int total_cells = _width * _height * _channel_count;
	_grid_data.resize(total_cells, 0.0f);
	_grid_data_back.resize(total_cells, 0.0f);

	// Initialize channel parameters (default: no evaporation, no diffusion)
	_evaporation_rates.resize(_channel_count, 1.0f);  // 1.0 = no decay
	_diffusion_rates.resize(_channel_count, 0.0f);    // 0.0 = no diffusion
}

void PheromoneMapCPP::set_channel_params(int channel, float evaporation_rate, float diffusion_rate) {
	if (channel < 0 || channel >= _channel_count) return;

	_evaporation_rates[channel] = std::clamp(evaporation_rate, 0.0f, 1.0f);
	_diffusion_rates[channel] = std::clamp(diffusion_rate, 0.0f, 1.0f);
	_gpu_params_dirty = true;
}

float PheromoneMapCPP::get_evaporation_rate(int channel) const {
	if (channel < 0 || channel >= _channel_count) return 1.0f;
	return _evaporation_rates[channel];
}

float PheromoneMapCPP::get_diffusion_rate(int channel) const {
	if (channel < 0 || channel >= _channel_count) return 0.0f;
	return _diffusion_rates[channel];
}

// --- World ↔ Grid Conversion ---

void PheromoneMapCPP::_world_to_grid(const Vector3& world_pos, int& out_gx, int& out_gy) const {
	// Convert world position to grid coordinates
	Vector3 local = world_pos - _world_origin;
	out_gx = static_cast<int>(local.x / _cell_size);
	out_gy = static_cast<int>(local.z / _cell_size);  // Z is forward in Godot
}

Vector3 PheromoneMapCPP::_grid_to_world(int gx, int gy) const {
	// Convert grid coordinates to world position (cell center)
	return _world_origin + Vector3(
		(gx + 0.5f) * _cell_size,
		0.0f,
		(gy + 0.5f) * _cell_size
	);
}

// --- Grid Access ---

int PheromoneMapCPP::_get_index(int gx, int gy, int channel) const {
	// Layout: channel * (_width * _height) + y * _width + x
	return channel * (_width * _height) + gy * _width + gx;
}

float PheromoneMapCPP::_get_cell(int gx, int gy, int channel) const {
	if (gx < 0 || gx >= _width || gy < 0 || gy >= _height || channel < 0 || channel >= _channel_count) {
		return 0.0f;  // Out of bounds = zero
	}
	return _grid_data[_get_index(gx, gy, channel)];
}

void PheromoneMapCPP::_set_cell(int gx, int gy, int channel, float value) {
	if (gx < 0 || gx >= _width || gy < 0 || gy >= _height || channel < 0 || channel >= _channel_count) {
		return;  // Out of bounds = no-op
	}
	_grid_data[_get_index(gx, gy, channel)] = value;
}

// --- Pheromone Deposition ---

void PheromoneMapCPP::deposit(const Vector3& world_pos, int channel, float strength) {
	if (channel < 0 || channel >= _channel_count || strength <= 0.0f) return;

	int gx, gy;
	_world_to_grid(world_pos, gx, gy);

	if (gx >= 0 && gx < _width && gy >= 0 && gy < _height) {
		// Add to existing value (accumulation)
		int idx = _get_index(gx, gy, channel);
		_grid_data[idx] = std::min(_grid_data[idx] + strength, 100.0f);  // Cap at 100
	}
}

void PheromoneMapCPP::deposit_radius(const Vector3& world_pos, int channel, float strength, float radius) {
	if (channel < 0 || channel >= _channel_count || strength <= 0.0f || radius <= 0.0f) return;

	int center_gx, center_gy;
	_world_to_grid(world_pos, center_gx, center_gy);

	int radius_cells = static_cast<int>(std::ceil(radius / _cell_size));

	for (int dy = -radius_cells; dy <= radius_cells; ++dy) {
		for (int dx = -radius_cells; dx <= radius_cells; ++dx) {
			int gx = center_gx + dx;
			int gy = center_gy + dy;

			if (gx < 0 || gx >= _width || gy < 0 || gy >= _height) continue;

			// Distance from center
			float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy)) * _cell_size;
			if (dist > radius) continue;

			// Falloff (linear)
			float falloff = 1.0f - (dist / radius);
			float deposit_amount = strength * falloff;

			int idx = _get_index(gx, gy, channel);
			_grid_data[idx] = std::min(_grid_data[idx] + deposit_amount, 100.0f);
		}
	}
}

void PheromoneMapCPP::deposit_cone(const Vector3& origin, const Vector3& direction, int channel,
                                   float strength, float half_angle_rad, float range) {
	if (channel < 0 || channel >= _channel_count || strength <= 0.0f || range <= 0.0f) return;

	// 2D direction (XZ plane)
	float dir_x = direction.x;
	float dir_z = direction.z;
	float dir_len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
	if (dir_len < 0.001f) return;
	dir_x /= dir_len;
	dir_z /= dir_len;

	float cos_half = std::cos(half_angle_rad);

	int origin_gx, origin_gy;
	_world_to_grid(origin, origin_gx, origin_gy);

	int cell_range = static_cast<int>(std::ceil(range / _cell_size));

	for (int dy = -cell_range; dy <= cell_range; ++dy) {
		for (int dx = -cell_range; dx <= cell_range; ++dx) {
			int gx = origin_gx + dx;
			int gy = origin_gy + dy;
			if (gx < 0 || gx >= _width || gy < 0 || gy >= _height) continue;

			float offset_x = static_cast<float>(dx);
			float offset_z = static_cast<float>(dy);
			float dist = std::sqrt(offset_x * offset_x + offset_z * offset_z) * _cell_size;
			if (dist > range || dist < 0.001f) continue;

			// Normalize offset to get direction to this cell
			float to_x = offset_x / (dist / _cell_size);
			float to_z = offset_z / (dist / _cell_size);

			// Dot product with cone direction
			float dot = to_x * dir_x + to_z * dir_z;
			if (dot < cos_half) continue;  // Outside cone angle

			// Falloff: distance * angle
			float dist_falloff = 1.0f - (dist / range);
			float angle_falloff = (dot - cos_half) / (1.0f - cos_half);
			float deposit_amount = strength * dist_falloff * angle_falloff;

			int idx = _get_index(gx, gy, channel);
			_grid_data[idx] = std::min(_grid_data[idx] + deposit_amount, 100.0f);
		}
	}
}

void PheromoneMapCPP::deposit_trail(const Vector3& from, const Vector3& to, int channel, float strength) {
	if (channel < 0 || channel >= _channel_count || strength <= 0.0f) return;

	int gx0, gy0, gx1, gy1;
	_world_to_grid(from, gx0, gy0);
	_world_to_grid(to, gx1, gy1);

	// Bresenham line rasterization
	int dx_abs = std::abs(gx1 - gx0);
	int dy_abs = std::abs(gy1 - gy0);
	int sx = (gx0 < gx1) ? 1 : -1;
	int sy = (gy0 < gy1) ? 1 : -1;
	int err = dx_abs - dy_abs;

	int gx = gx0, gy = gy0;
	for (int steps = 0; steps < dx_abs + dy_abs + 1; ++steps) {
		if (gx >= 0 && gx < _width && gy >= 0 && gy < _height) {
			int idx = _get_index(gx, gy, channel);
			_grid_data[idx] = std::min(_grid_data[idx] + strength, 100.0f);
		}

		if (gx == gx1 && gy == gy1) break;

		int e2 = 2 * err;
		if (e2 > -dy_abs) { err -= dy_abs; gx += sx; }
		if (e2 < dx_abs)  { err += dx_abs; gy += sy; }
	}
}

// --- Pheromone Sampling ---

float PheromoneMapCPP::sample(const Vector3& world_pos, int channel) const {
	if (channel < 0 || channel >= _channel_count) return 0.0f;

	int gx, gy;
	_world_to_grid(world_pos, gx, gy);

	return _get_cell(gx, gy, channel);
}

float PheromoneMapCPP::sample_bilinear(const Vector3& world_pos, int channel) const {
	if (channel < 0 || channel >= _channel_count) return 0.0f;

	// Convert to grid space (fractional)
	Vector3 local = world_pos - _world_origin;
	float fx = local.x / _cell_size;
	float fy = local.z / _cell_size;

	int x0 = static_cast<int>(std::floor(fx));
	int y0 = static_cast<int>(std::floor(fy));
	int x1 = x0 + 1;
	int y1 = y0 + 1;

	float tx = fx - x0;
	float ty = fy - y0;

	// Sample four corners
	float v00 = _get_cell(x0, y0, channel);
	float v10 = _get_cell(x1, y0, channel);
	float v01 = _get_cell(x0, y1, channel);
	float v11 = _get_cell(x1, y1, channel);

	// Bilinear interpolation
	float v0 = v00 * (1.0f - tx) + v10 * tx;
	float v1 = v01 * (1.0f - tx) + v11 * tx;
	return v0 * (1.0f - ty) + v1 * ty;
}

Vector3 PheromoneMapCPP::gradient(const Vector3& world_pos, int channel) const {
	if (channel < 0 || channel >= _channel_count) return Vector3(0, 0, 0);

	int gx, gy;
	_world_to_grid(world_pos, gx, gy);

	// Central difference (normalized)
	float dx = _get_cell(gx + 1, gy, channel) - _get_cell(gx - 1, gy, channel);
	float dy = _get_cell(gx, gy + 1, channel) - _get_cell(gx, gy - 1, channel);

	// Return world-space gradient (X, 0, Z)
	Vector3 grad(dx, 0, dy);
	if (grad.length() > 0.0f) {
		grad = grad.normalized();
	}
	return grad;
}

Vector3 PheromoneMapCPP::gradient_raw(const Vector3& world_pos, int channel) const {
	if (channel < 0 || channel >= _channel_count) return Vector3(0, 0, 0);

	int gx, gy;
	_world_to_grid(world_pos, gx, gy);

	// Central difference — unnormalized, preserves magnitude
	float dx = _get_cell(gx + 1, gy, channel) - _get_cell(gx - 1, gy, channel);
	float dy = _get_cell(gx, gy + 1, channel) - _get_cell(gx, gy - 1, channel);

	return Vector3(dx, 0, dy);
}

// --- Cellular Automata Update ---

void PheromoneMapCPP::tick(float delta) {
	// GPU path: upload grid, dispatch compute, readback
	if (_use_gpu) {
		_tick_gpu(delta);
		return;
	}

	// Two-buffer cellular automata: evaporation + diffusion
	// Back buffer = read, front buffer = write

	for (int channel = 0; channel < _channel_count; ++channel) {
		float evap_rate = _evaporation_rates[channel];
		float diff_rate = _diffusion_rates[channel];

		// Evaporation decay factor (exponential)
		// evap_rate is per-second rate (0.0 = instant decay, 1.0 = no decay)
		// Formula: new_value = old_value * pow(evap_rate, delta)
		float decay_factor = std::pow(evap_rate, delta);

		for (int gy = 0; gy < _height; ++gy) {
			for (int gx = 0; gx < _width; ++gx) {
				int idx = _get_index(gx, gy, channel);
				float current = _grid_data[idx];

				// 1. Evaporation
				current *= decay_factor;

				// 2. Diffusion (4-neighbor average)
				if (diff_rate > 0.0f) {
					float neighbor_sum = 0.0f;
					int neighbor_count = 0;

					// Sample 4 neighbors
					if (gx > 0) {
						neighbor_sum += _grid_data[_get_index(gx - 1, gy, channel)];
						neighbor_count++;
					}
					if (gx < _width - 1) {
						neighbor_sum += _grid_data[_get_index(gx + 1, gy, channel)];
						neighbor_count++;
					}
					if (gy > 0) {
						neighbor_sum += _grid_data[_get_index(gx, gy - 1, channel)];
						neighbor_count++;
					}
					if (gy < _height - 1) {
						neighbor_sum += _grid_data[_get_index(gx, gy + 1, channel)];
						neighbor_count++;
					}

					if (neighbor_count > 0) {
						float neighbor_avg = neighbor_sum / neighbor_count;
						// Blend current with neighbor average
						current = current * (1.0f - diff_rate) + neighbor_avg * diff_rate;
					}
				}

				// Write to back buffer
				_grid_data_back[idx] = current;
			}
		}
	}

	// Swap buffers
	std::swap(_grid_data, _grid_data_back);
}

// --- Debug/Visualization ---

PackedFloat32Array PheromoneMapCPP::get_channel_data(int channel) const {
	PackedFloat32Array result;
	if (channel < 0 || channel >= _channel_count) return result;

	result.resize(_width * _height);
	for (int gy = 0; gy < _height; ++gy) {
		for (int gx = 0; gx < _width; ++gx) {
			int idx = gy * _width + gx;
			result[idx] = _get_cell(gx, gy, channel);
		}
	}
	return result;
}

float PheromoneMapCPP::get_max_value(int channel) const {
	if (channel < 0 || channel >= _channel_count) return 0.0f;

	float max_val = 0.0f;
	for (int gy = 0; gy < _height; ++gy) {
		for (int gx = 0; gx < _width; ++gx) {
			max_val = std::max(max_val, _get_cell(gx, gy, channel));
		}
	}
	return max_val;
}

float PheromoneMapCPP::get_total_value(int channel) const {
	if (channel < 0 || channel >= _channel_count) return 0.0f;

	float total = 0.0f;
	for (int gy = 0; gy < _height; ++gy) {
		for (int gx = 0; gx < _width; ++gx) {
			total += _get_cell(gx, gy, channel);
		}
	}
	return total;
}

// --- Utility ---

void PheromoneMapCPP::clear_channel(int channel) {
	if (channel < 0 || channel >= _channel_count) return;

	for (int gy = 0; gy < _height; ++gy) {
		for (int gx = 0; gx < _width; ++gx) {
			_set_cell(gx, gy, channel, 0.0f);
		}
	}
}

void PheromoneMapCPP::clear_all() {
	std::fill(_grid_data.begin(), _grid_data.end(), 0.0f);
	std::fill(_grid_data_back.begin(), _grid_data_back.end(), 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════
//  GPU Acceleration
// ═══════════════════════════════════════════════════════════════════════

static Ref<RDUniform> _make_storage_uniform(int binding, const RID &buffer) {
	Ref<RDUniform> u;
	u.instantiate();
	u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
	u->set_binding(binding);
	u->add_id(buffer);
	return u;
}

bool PheromoneMapCPP::setup_gpu() {
	if (_use_gpu) return true;  // Already active
	if (_width == 0 || _height == 0 || _channel_count == 0) return false;

	if (_setup_gpu_internal()) {
		_use_gpu = true;
		UtilityFunctions::print("[PheromoneMapCPP] GPU acceleration enabled (",
			_width, "x", _height, " grid, ", _channel_count, " channels)");
		return true;
	}

	UtilityFunctions::push_warning("[PheromoneMapCPP] GPU unavailable — using CPU fallback");
	return false;
}

bool PheromoneMapCPP::_setup_gpu_internal() {
	// Get RenderingDevice: local preferred, global fallback
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) return false;

	_rd = rs->create_local_rendering_device();
	if (_rd) {
		_owns_rd = true;
	} else {
		_rd = rs->get_rendering_device();
		_owns_rd = false;
		if (!_rd) return false;
	}

	// ── Compile shader ───────────────────────────────────────────
	{
		Ref<RDShaderSource> src;
		src.instantiate();
		src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(PHEROMONE_CA_GLSL));
		src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);

		Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(src);
		if (spirv.is_null()) {
			UtilityFunctions::push_error("[PheromoneMapCPP] Shader SPIR-V is null");
			_cleanup_gpu();
			return false;
		}
		String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
		if (!err.is_empty()) {
			UtilityFunctions::push_error("[PheromoneMapCPP] Shader error: ", err);
			_cleanup_gpu();
			return false;
		}

		_pca_shader = _rd->shader_create_from_spirv(spirv, "PheromoneCA");
		if (!_pca_shader.is_valid()) {
			_cleanup_gpu();
			return false;
		}
		_pca_pipeline = _rd->compute_pipeline_create(_pca_shader);
	}

	// ── Create SSBOs ─────────────────────────────────────────────
	uint32_t grid_bytes = (uint32_t)(_width * _height * _channel_count) * sizeof(float);

	PackedByteArray grid_init;
	grid_init.resize(grid_bytes);
	memset(grid_init.ptrw(), 0, grid_bytes);
	_grid_buf_a = _rd->storage_buffer_create(grid_bytes, grid_init);
	_grid_buf_b = _rd->storage_buffer_create(grid_bytes, grid_init);

	// Params buffer: vec4 per channel (x=evap, y=diff, z/w unused) — padded to vec4 for std430
	uint32_t params_bytes = (uint32_t)_channel_count * 16;  // 16 bytes per vec4
	PackedByteArray params_init;
	params_init.resize(params_bytes);
	memset(params_init.ptrw(), 0, params_bytes);
	_params_buf = _rd->storage_buffer_create(params_bytes, params_init);

	// ── Create uniform sets (ping-pong) ──────────────────────────
	{
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.append(_make_storage_uniform(0, _grid_buf_a));   // read
		uniforms.append(_make_storage_uniform(1, _grid_buf_b));   // write
		uniforms.append(_make_storage_uniform(2, _params_buf));
		_pca_set_a_to_b = _rd->uniform_set_create(uniforms, _pca_shader, 0);
	}
	{
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.append(_make_storage_uniform(0, _grid_buf_b));   // read
		uniforms.append(_make_storage_uniform(1, _grid_buf_a));   // write
		uniforms.append(_make_storage_uniform(2, _params_buf));
		_pca_set_b_to_a = _rd->uniform_set_create(uniforms, _pca_shader, 0);
	}

	_gpu_params_dirty = true;  // Force initial upload
	return true;
}

void PheromoneMapCPP::_upload_channel_params() {
	if (!_gpu_params_dirty || !_rd) return;

	// Pack as vec4 array: (evap, diff, 0, 0) per channel
	PackedByteArray buf;
	buf.resize(_channel_count * 16);
	float *ptr = (float *)buf.ptrw();
	for (int ch = 0; ch < _channel_count; ch++) {
		ptr[ch * 4 + 0] = _evaporation_rates[ch];
		ptr[ch * 4 + 1] = _diffusion_rates[ch];
		ptr[ch * 4 + 2] = 0.0f;
		ptr[ch * 4 + 3] = 0.0f;
	}
	_rd->buffer_update(_params_buf, 0, buf.size(), buf);
	_gpu_params_dirty = false;
}

void PheromoneMapCPP::_upload_grid() {
	if (!_rd) return;

	uint32_t bytes = (uint32_t)(_width * _height * _channel_count) * sizeof(float);
	PackedByteArray buf;
	buf.resize(bytes);
	memcpy(buf.ptrw(), _grid_data.data(), bytes);
	_rd->buffer_update(_grid_buf_a, 0, bytes, buf);
}

void PheromoneMapCPP::_readback_grid() {
	if (!_rd) return;

	// After A→B dispatch, result is in _grid_buf_b
	PackedByteArray buf = _rd->buffer_get_data(_grid_buf_b);
	uint32_t expected = (uint32_t)(_width * _height * _channel_count) * sizeof(float);
	if ((uint32_t)buf.size() >= expected) {
		memcpy(_grid_data.data(), buf.ptr(), expected);
	}
}

void PheromoneMapCPP::_tick_gpu(float delta) {
	if (!_rd) return;

	// Upload params if changed
	_upload_channel_params();

	// Upload CPU grid (contains deposits since last tick) to GPU buf A
	_upload_grid();

	// Dispatch compute: A → B
	PheromonePushConstants pc;
	pc.grid_w = _width;
	pc.grid_h = _height;
	pc.channel_count = _channel_count;
	pc.delta = delta;

	PackedByteArray pc_bytes;
	pc_bytes.resize(sizeof(pc));
	memcpy(pc_bytes.ptrw(), &pc, sizeof(pc));

	int groups_x = (_width + 7) / 8;
	int groups_y = (_height + 7) / 8;

	int64_t cl = _rd->compute_list_begin();
	_rd->compute_list_bind_compute_pipeline(cl, _pca_pipeline);
	_rd->compute_list_bind_uniform_set(cl, _pca_set_a_to_b, 0);
	_rd->compute_list_set_push_constant(cl, pc_bytes, sizeof(pc));
	_rd->compute_list_dispatch(cl, groups_x, groups_y, 1);
	_rd->compute_list_end();

	_rd->submit();
	_rd->sync();

	// Readback result from buf B to CPU _grid_data
	_readback_grid();
}

void PheromoneMapCPP::_cleanup_gpu() {
	if (_rd) {
		if (_pca_set_a_to_b.is_valid()) _rd->free_rid(_pca_set_a_to_b);
		if (_pca_set_b_to_a.is_valid()) _rd->free_rid(_pca_set_b_to_a);
		if (_pca_pipeline.is_valid()) _rd->free_rid(_pca_pipeline);
		if (_pca_shader.is_valid()) _rd->free_rid(_pca_shader);
		if (_grid_buf_a.is_valid()) _rd->free_rid(_grid_buf_a);
		if (_grid_buf_b.is_valid()) _rd->free_rid(_grid_buf_b);
		if (_params_buf.is_valid()) _rd->free_rid(_params_buf);

		if (_owns_rd) {
			memdelete(_rd);
		}
		_rd = nullptr;
		_owns_rd = false;
	}

	_use_gpu = false;
	_gpu_params_dirty = true;

	_pca_set_a_to_b = RID();
	_pca_set_b_to_a = RID();
	_pca_pipeline = RID();
	_pca_shader = RID();
	_grid_buf_a = RID();
	_grid_buf_b = RID();
	_params_buf = RID();
}
