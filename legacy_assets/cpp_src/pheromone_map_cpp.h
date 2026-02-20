#ifndef PHEROMONE_MAP_CPP_H
#define PHEROMONE_MAP_CPP_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <vector>

namespace godot {

/**
 * PheromoneMapCPP - Unified stigmergic coordination map
 *
 * Implements a multi-channel 2D grid with cellular automata (evaporation + diffusion).
 * Used directly by SimulationServer (one instance per team, no subclasses).
 * Combat channels 0-7 are deposited by C++ tick logic; economy channels 8-14 are
 * deposited by GDScript ColonyAI via the exposed deposit/sample API.
 *
 * Grid: 150x100 cells @ 4m/cell (600m x 400m world coverage)
 * Channels: 15 unified (8 combat + 7 economy), see PheromoneChannel enum
 * Update: GPU-accelerated cellular automata (CPU fallback for headless)
 */
class PheromoneMapCPP : public RefCounted {
	GDCLASS(PheromoneMapCPP, RefCounted)

protected:
	static void _bind_methods();

	// Grid parameters
	int _width;              // Grid width (150 cells)
	int _height;             // Grid height (100 cells)
	int _channel_count;      // Number of pheromone channels
	float _cell_size;        // Cell size in meters (4.0m)
	Vector3 _world_origin;   // World space origin (bottom-left corner)

	// Pheromone data (multi-channel grid)
	// Layout: [channel][y][x]
	// Index: channel * (_width * _height) + y * _width + x
	std::vector<float> _grid_data;       // Current grid state
	std::vector<float> _grid_data_back;  // Back buffer for two-buffer CA update

	// Per-channel parameters
	std::vector<float> _evaporation_rates;  // Exponential decay rate per channel (0.0-1.0, lower = faster decay)
	std::vector<float> _diffusion_rates;    // Diffusion strength per channel (0.0-1.0, higher = faster spread)

	// World ↔ grid conversion
	void _world_to_grid(const Vector3& world_pos, int& out_gx, int& out_gy) const;
	Vector3 _grid_to_world(int gx, int gy) const;

	// Grid access (with bounds checking)
	float _get_cell(int gx, int gy, int channel) const;
	void _set_cell(int gx, int gy, int channel, float value);
	int _get_index(int gx, int gy, int channel) const;

	// ── GPU Acceleration (optional) ──────────────────────────────
	RenderingDevice *_rd = nullptr;
	bool _owns_rd = false;
	bool _use_gpu = false;
	bool _gpu_params_dirty = true;

	RID _pca_shader, _pca_pipeline;
	RID _grid_buf_a, _grid_buf_b, _params_buf;
	RID _pca_set_a_to_b, _pca_set_b_to_a;

	struct PheromonePushConstants {
		int32_t grid_w, grid_h, channel_count;
		float delta;
	};

	bool _setup_gpu_internal();
	void _tick_gpu(float delta);
	void _upload_grid();
	void _readback_grid();
	void _upload_channel_params();
	void _cleanup_gpu();

public:
	PheromoneMapCPP();
	~PheromoneMapCPP();

	// Initialization
	void initialize(int width, int height, int channel_count, float cell_size, const Vector3& world_origin);

	// Per-channel parameter setup
	void set_channel_params(int channel, float evaporation_rate, float diffusion_rate);
	float get_evaporation_rate(int channel) const;
	float get_diffusion_rate(int channel) const;

	// Pheromone deposition
	void deposit(const Vector3& world_pos, int channel, float strength);
	void deposit_radius(const Vector3& world_pos, int channel, float strength, float radius);
	void deposit_cone(const Vector3& origin, const Vector3& direction, int channel,
	                  float strength, float half_angle_rad, float range);
	void deposit_trail(const Vector3& from, const Vector3& to, int channel, float strength);

	// Pheromone sampling
	float sample(const Vector3& world_pos, int channel) const;
	float sample_bilinear(const Vector3& world_pos, int channel) const;  // Smooth interpolation
	Vector3 gradient(const Vector3& world_pos, int channel) const;       // Direction of strongest pheromone (normalized)
	Vector3 gradient_raw(const Vector3& world_pos, int channel) const;   // Gradient with magnitude preserved

	// GPU acceleration (optional — falls back to CPU if unavailable)
	bool setup_gpu();
	bool is_gpu_active() const { return _use_gpu; }

	// Cellular automata update (evaporation + diffusion)
	void tick(float delta);

	// Debug/visualization
	PackedFloat32Array get_channel_data(int channel) const;  // For GDScript visualization
	float get_max_value(int channel) const;                   // Peak value in channel (for HUD stats)
	float get_total_value(int channel) const;                 // Sum of all cells (for analytics)

	// Utility
	void clear_channel(int channel);
	void clear_all();
	int get_width() const { return _width; }
	int get_height() const { return _height; }
	int get_channel_count() const { return _channel_count; }
	float get_cell_size() const { return _cell_size; }
	Vector3 get_world_origin() const { return _world_origin; }
};

} // namespace godot

#endif // PHEROMONE_MAP_CPP_H
