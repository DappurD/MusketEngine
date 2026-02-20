#ifndef VOXEL_WORLD_H
#define VOXEL_WORLD_H

#include "voxel_chunk.h"
#include "voxel_core_api.h"
#include "voxel_materials.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>

#include <cstdint>
#include <queue>
#include <vector>

namespace godot {

/// Result from a voxel raycast (DDA ray walk).
struct VoxelHit {
  bool hit = false;
  Vector3i voxel_pos;         // Voxel coordinates of the hit
  Vector3 world_pos;          // Exact world-space hit point
  Vector3 normal;             // Face normal of the hit voxel face
  uint8_t material = MAT_AIR; // Material of the hit voxel
  float distance = 0.0f;      // Distance from ray origin to hit
};

/// High-performance voxel world for the V-SAF engine.
///
/// Stores a 3D grid of uint8_t material IDs in 32x32x32 chunks.
/// AI systems (CombatLOS, TacticalQuery, InfluenceMap) access voxels
/// directly via C++ pointers — zero FFI overhead.
///
/// GDScript accesses via ClassDB-bound methods for setup, editing, and queries.
class VOXEL_CORE_API VoxelWorld : public Node3D {
  GDCLASS(VoxelWorld, Node3D)

public:
  // ── Constants ────────────────────────────────────────────────────
  static constexpr int CHUNK_SIZE = VoxelChunk::SIZE; // 32

  VoxelWorld();
  ~VoxelWorld();
  VoxelWorld(const VoxelWorld &) = delete;
  VoxelWorld(VoxelWorld &&) = delete;

  // ── Setup (GDScript API) ─────────────────────────────────────────
  /// Initialize world with dimensions in voxels and scale.
  /// size_x/y/z: world size in voxels (rounded up to chunk boundaries)
  /// voxel_scale: meters per voxel (default 0.25)
  void setup(int size_x, int size_y, int size_z, float voxel_scale = 0.25f);

  /// Check if world has been initialized.
  bool is_initialized() const { return _initialized; }

  // ── Voxel access (GDScript + C++ API) ────────────────────────────
  /// Get material ID at voxel coordinates. Returns MAT_AIR for out-of-bounds.
  int get_voxel(int x, int y, int z) const;

  /// Set material ID at voxel coordinates (does NOT mark dirty — caller
  /// manages).
  void set_voxel(int x, int y, int z, int material);

  /// Set material and mark chunk + boundary neighbors dirty. For GDScript
  /// callers.
  void set_voxel_dirty(int x, int y, int z, int material);

  /// Fast inline for C++ AI code (no bounds check variant).
  inline uint8_t get_voxel_fast(int x, int y, int z) const {
    int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
    if (ci < 0)
      return MAT_AIR;
    return _chunks[ci].get(x & 31, y & 31, z & 31);
  }

  /// Fast inline solid check for C++ AI code.
  inline bool is_solid(int x, int y, int z) const {
    return is_material_solid(get_voxel_fast(x, y, z));
  }

  /// Solid check from world-space position.
  bool is_solid_at(const Vector3 &world_pos) const;

  /// Find the Y of the highest solid voxel in a column. Returns -1 if all air.
  int get_column_top_y(int x, int z) const;

  // ── Destruction (GDScript + C++ API) ─────────────────────────────
  /// Destroy all voxels in a sphere. Returns number of voxels destroyed.
  int destroy_sphere(const Vector3 &center, float radius);

  /// Extended destroy: returns destruction data for VFX.
  /// Returns {"destroyed": int, "dominant_material": int,
  ///          "material_histogram": PackedInt32Array,
  ///          "debris": Array of {position: Vector3, material: int}}
  Dictionary destroy_sphere_ex(const Vector3 &center, float radius,
                               int max_debris = 12);

  /// Destroy all voxels in an axis-aligned box.
  int destroy_box(const Vector3 &min_corner, const Vector3 &max_corner);

  /// Get material color as a Godot Color (for VFX in GDScript).
  Color get_material_color(int material_id) const;

  // ── Structural Integrity (Voxel Collapse + Cellular Automata) ────
  /// Queue columns above a destroyed area for collapse checking.
  /// center/radius in world-space — for GDScript API.
  void queue_collapse_check(const Vector3 &center, float radius);

  /// Queue columns for collapse checking (voxel-space coords) — for C++
  /// internal use.
  void queue_collapse_check_voxel(int vx, int vy, int vz, int voxel_radius);

  /// Legacy: Process pending collapses (simple column-drop). Call once per
  /// frame. Returns number of voxels that fell this tick.
  int process_collapses(int max_per_tick = 500);

  /// Cellular automata rubble physics. Replaces process_collapses.
  /// Active voxels fall, slide diagonally, and spread based on material rules.
  /// Returns number of voxels that moved this tick.
  int process_rubble_ca(int max_per_tick = 500);

  /// Get number of pending active voxels (for debug HUD).
  int get_pending_collapses() const;

  /// Get count of active CA voxels.
  int get_active_rubble_count() const { return (int)_active_voxels.size(); }

  // ── Raycast (C++ AI API) ─────────────────────────────────────────
  /// 3D DDA ray walk through the voxel grid.
  /// Used by CombatLOS for line-of-sight, TacticalQuery for cover checks.
  /// Returns true if ray hit a solid voxel within max_dist.
  bool raycast(const Vector3 &from, const Vector3 &direction, float max_dist,
               VoxelHit &hit) const;

  /// DDA ray walk returning ALL solid voxel hits along the ray (for
  /// penetration). Returns number of hits written to `hits` array (0 = clear
  /// path).
  int raycast_multi(const Vector3 &from, const Vector3 &direction,
                    float max_dist, VoxelHit *hits, int max_hits) const;

  /// GDScript-friendly raycast returning a Dictionary.
  /// Keys: "hit", "voxel_pos", "world_pos", "normal", "material", "distance"
  Dictionary raycast_dict(const Vector3 &from, const Vector3 &direction,
                          float max_dist) const;

  /// Check LOS between two world positions (true = clear, no solid in the way).
  bool check_los(const Vector3 &from, const Vector3 &to) const;

  // ── Coordinate conversion ────────────────────────────────────────
  /// World position → voxel coordinates.
  Vector3i world_to_voxel(const Vector3 &world_pos) const;

  /// Voxel coordinates → world position (center of voxel).
  Vector3 voxel_to_world(int x, int y, int z) const;
  Vector3 voxel_to_world_v(const Vector3i &vpos) const;

  // ── Chunk management ─────────────────────────────────────────────
  /// Get list of dirty chunk indices, then clear their dirty flags.
  /// Used by mesher to know which chunks need re-meshing.
  std::vector<int> consume_dirty_chunks();

  /// Get chunk by chunk-space coordinates. Returns nullptr if out of bounds.
  const VoxelChunk *get_chunk(int cx, int cy, int cz) const;
  VoxelChunk *get_chunk_mut(int cx, int cy, int cz);

  // ── Stats (GDScript API) ─────────────────────────────────────────
  int get_world_size_x() const { return _size_x; }
  int get_world_size_y() const { return _size_y; }
  int get_world_size_z() const { return _size_z; }
  int get_chunks_x() const { return _chunks_x; }
  int get_chunks_y() const { return _chunks_y; }
  int get_chunks_z() const { return _chunks_z; }
  int get_total_chunks() const { return _total_chunks; }
  float get_voxel_scale() const { return _voxel_scale; }
  float get_inv_voxel_scale() const { return _inv_scale; }
  int get_memory_usage_bytes() const;
  int get_dirty_chunk_count() const;

  // ── Generation (GDScript API) ──────────────────────────────────
  /// Generate test battlefield (terrain + buildings + cover).
  void generate_test_battlefield();

  /// Generate flat terrain with noise hills.
  void generate_terrain(int base_height = 16, int hill_amplitude = 8,
                        float hill_frequency = 0.02f);

  /// Generate a rectangular building (voxel coordinates).
  void generate_building(int x, int y, int z, int width, int height, int depth,
                         int wall_mat = 3, int floor_mat = 4,
                         bool has_windows = true, bool has_door = true);

  /// Generate a wall / barricade (voxel coordinates).
  void generate_wall(int x, int y, int z, int length, int height, int thickness,
                     int mat = 5, bool along_x = true);

  /// Generate a trench carved into terrain (voxel coordinates).
  void generate_trench(int x, int z, int length, int depth, int width,
                       bool along_x = true);

  // ── Meshing (GDScript API) ──────────────────────────────────────
  /// Mesh a single chunk and return Godot Array for RenderingServer.
  /// cx, cy, cz: chunk coordinates.
  Array mesh_chunk(int cx, int cy, int cz);

  /// Get dirty chunk coordinates as flat int array [cx0,cy0,cz0, cx1,cy1,cz1,
  /// ...]. Does NOT clear dirty flags — call clear_chunk_dirty() after meshing
  /// each chunk.
  PackedInt32Array get_dirty_chunk_coords() const;

  /// Clear dirty flag for a specific chunk after it has been meshed.
  void clear_chunk_dirty(int cx, int cy, int cz);

  // ── GPU SVDAG Helpers ───────────────────────────────────────────
  /// Build and return a linear Sparse Voxel Octree (SVO) byte array
  /// representing the current world state.
  PackedByteArray build_svo() const;

  // ── Serialization ────────────────────────────────────────────────
  PackedByteArray save_to_bytes() const;
  void load_from_bytes(const PackedByteArray &data);

  // ── Singleton access for C++ AI code ─────────────────────────────
  static VoxelWorld *get_singleton() { return _singleton; }

protected:
  static void _bind_methods();
  void _notification(int p_what);

private:
  static VoxelWorld *_singleton;

  // ── World dimensions ─────────────────────────────────────────────
  int _size_x = 0, _size_y = 0, _size_z = 0;       // Voxel-space dimensions
  int _chunks_x = 0, _chunks_y = 0, _chunks_z = 0; // Chunk-space dimensions
  int _total_chunks = 0;
  float _voxel_scale = 0.25f; // Meters per voxel
  float _inv_scale = 4.0f;    // 1.0 / voxel_scale (precomputed)
  bool _initialized = false;

  // ── Chunk storage (flat array, indexed by chunk coords) ──────────
  std::vector<VoxelChunk> _chunks;

  // ── Internal helpers ─────────────────────────────────────────────
  inline int _chunk_index(int cx, int cy, int cz) const {
    if (cx < 0 || cx >= _chunks_x || cy < 0 || cy >= _chunks_y || cz < 0 ||
        cz >= _chunks_z)
      return -1;
    return cz * (_chunks_x * _chunks_y) + cx * _chunks_y + cy;
  }

  inline bool _in_bounds(int x, int y, int z) const {
    return x >= 0 && x < _size_x && y >= 0 && y < _size_y && z >= 0 &&
           z < _size_z;
  }

  void _mark_chunk_dirty(int x, int y, int z);
  void _mark_boundary_neighbors_dirty(int x, int y, int z);

  // ── Legacy collapse queue ────────────────────────────────────────
  struct CollapseColumn {
    int x, z;
    int min_y, max_y;
  };
  std::queue<CollapseColumn> _collapse_queue;
  int _pending_collapse_count = 0;

  // ── Cellular automata rubble ─────────────────────────────────────
  struct ActiveVoxel {
    int16_t x, y, z;
    uint8_t ticks_idle; // consecutive ticks with no movement
  };
  std::vector<ActiveVoxel> _active_voxels;
  std::vector<ActiveVoxel> _next_active; // double buffer for tick
  uint32_t _ca_rng = 0x12345678u;        // LCG RNG for CA decisions

  /// Activate solid neighbors of destroyed voxels for CA processing.
  void _activate_neighbors(int vx, int vy, int vz, int radius);

  /// Material-specific CA slide probability (0.0 = never slide, 1.0 = always).
  static float _ca_slide_chance(uint8_t mat);
  /// Material-specific CA spread flag (sand/gravel/dirt can spread sideways).
  static bool _ca_can_spread(uint8_t mat);
};

} // namespace godot

#endif // VOXEL_WORLD_H
