#include "voxel_world.h"
#include "svdag/svo_builder.h"
#include "voxel_generator.h"
#include "voxel_mesher_blocky.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════════
//  Singleton
// ═══════════════════════════════════════════════════════════════════════

VoxelWorld *VoxelWorld::_singleton = nullptr;

// ═══════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════

VoxelWorld::VoxelWorld() {
  if (!_singleton) {
    _singleton = this;
  }
}

VoxelWorld::~VoxelWorld() {
  if (_singleton == this) {
    _singleton = nullptr;
  }
}

void VoxelWorld::_notification(int p_what) {
  switch (p_what) {
  case NOTIFICATION_ENTER_TREE:
    if (!_singleton)
      _singleton = this;
    break;
  case NOTIFICATION_EXIT_TREE:
    if (_singleton == this)
      _singleton = nullptr;
    break;
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════

void VoxelWorld::setup(int size_x, int size_y, int size_z, float voxel_scale) {
  // Round up to chunk boundaries
  _chunks_x = (size_x + CHUNK_SIZE - 1) / CHUNK_SIZE;
  _chunks_y = (size_y + CHUNK_SIZE - 1) / CHUNK_SIZE;
  _chunks_z = (size_z + CHUNK_SIZE - 1) / CHUNK_SIZE;
  _size_x = _chunks_x * CHUNK_SIZE;
  _size_y = _chunks_y * CHUNK_SIZE;
  _size_z = _chunks_z * CHUNK_SIZE;
  _total_chunks = _chunks_x * _chunks_y * _chunks_z;
  _voxel_scale = voxel_scale;
  _inv_scale = 1.0f / voxel_scale;

  // Allocate chunk array
  _chunks.clear();
  _chunks.resize(_total_chunks);

  // Initialize chunk positions
  for (int cz = 0; cz < _chunks_z; cz++) {
    for (int cx = 0; cx < _chunks_x; cx++) {
      for (int cy = 0; cy < _chunks_y; cy++) {
        int ci = cz * (_chunks_x * _chunks_y) + cx * _chunks_y + cy;
        _chunks[ci].cx = cx;
        _chunks[ci].cy = cy;
        _chunks[ci].cz = cz;
        _chunks[ci].fill(MAT_AIR);
        _chunks[ci].dirty = false; // Don't mesh empty air chunks
      }
    }
  }

  _initialized = true;

  UtilityFunctions::print("[VoxelWorld] Initialized: ", _size_x, "x", _size_y,
                          "x", _size_z, " voxels (", _chunks_x, "x", _chunks_y,
                          "x", _chunks_z, " chunks = ", _total_chunks,
                          "), scale=", _voxel_scale, "m");
}

// ═══════════════════════════════════════════════════════════════════════
//  Voxel Access
// ═══════════════════════════════════════════════════════════════════════

int VoxelWorld::get_voxel(int x, int y, int z) const {
  if (!_in_bounds(x, y, z))
    return MAT_AIR;
  int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
  if (ci < 0)
    return MAT_AIR;
  return _chunks[ci].get(x & 31, y & 31, z & 31);
}

void VoxelWorld::set_voxel(int x, int y, int z, int material) {
  if (!_in_bounds(x, y, z))
    return;
  int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
  if (ci < 0)
    return;
  _chunks[ci].set(x & 31, y & 31, z & 31, (uint8_t)material);
}

void VoxelWorld::set_voxel_dirty(int x, int y, int z, int material) {
  if (!_in_bounds(x, y, z))
    return;
  int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
  if (ci < 0)
    return;
  _chunks[ci].set(x & 31, y & 31, z & 31, (uint8_t)material);
  _mark_chunk_dirty(x, y, z);
  _mark_boundary_neighbors_dirty(x, y, z);
}

bool VoxelWorld::is_solid_at(const Vector3 &world_pos) const {
  Vector3i v = world_to_voxel(world_pos);
  return is_solid(v.x, v.y, v.z);
}

int VoxelWorld::get_column_top_y(int x, int z) const {
  if (!_initialized)
    return -1;
  if (x < 0 || x >= _size_x || z < 0 || z >= _size_z)
    return -1;
  for (int y = _size_y - 1; y >= 0; y--) {
    if (is_material_solid(get_voxel_fast(x, y, z))) {
      return y;
    }
  }
  return -1;
}

// ═══════════════════════════════════════════════════════════════════════
//  Coordinate Conversion
// ═══════════════════════════════════════════════════════════════════════

Vector3i VoxelWorld::world_to_voxel(const Vector3 &world_pos) const {
  // World origin is at center of the voxel grid (XZ), Y=0 is ground level
  int vx = (int)std::floor((world_pos.x + _size_x * _voxel_scale * 0.5f) *
                           _inv_scale);
  int vy = (int)std::floor(world_pos.y * _inv_scale);
  int vz = (int)std::floor((world_pos.z + _size_z * _voxel_scale * 0.5f) *
                           _inv_scale);
  return Vector3i(vx, vy, vz);
}

Vector3 VoxelWorld::voxel_to_world(int x, int y, int z) const {
  float wx = ((float)x + 0.5f) * _voxel_scale - _size_x * _voxel_scale * 0.5f;
  float wy = ((float)y + 0.5f) * _voxel_scale;
  float wz = ((float)z + 0.5f) * _voxel_scale - _size_z * _voxel_scale * 0.5f;
  return Vector3(wx, wy, wz);
}

Vector3 VoxelWorld::voxel_to_world_v(const Vector3i &vpos) const {
  return voxel_to_world(vpos.x, vpos.y, vpos.z);
}

// ═══════════════════════════════════════════════════════════════════════
//  Destruction
// ═══════════════════════════════════════════════════════════════════════

int VoxelWorld::destroy_sphere(const Vector3 &center, float radius) {
  if (!_initialized)
    return 0;

  Vector3i vc = world_to_voxel(center);
  int vr = (int)std::ceil(radius * _inv_scale);
  int vr_sq = vr * vr;
  int destroyed = 0;

  int min_x = std::max(0, vc.x - vr);
  int max_x = std::min(_size_x - 1, vc.x + vr);
  int min_y = std::max(0, vc.y - vr);
  int max_y = std::min(_size_y - 1, vc.y + vr);
  int min_z = std::max(0, vc.z - vr);
  int max_z = std::min(_size_z - 1, vc.z + vr);

  for (int z = min_z; z <= max_z; z++) {
    int dz = z - vc.z;
    for (int x = min_x; x <= max_x; x++) {
      int dx = x - vc.x;
      for (int y = min_y; y <= max_y; y++) {
        int dy = y - vc.y;
        if (dx * dx + dy * dy + dz * dz <= vr_sq) {
          int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
          if (ci >= 0) {
            uint8_t old = _chunks[ci].get(x & 31, y & 31, z & 31);
            if (old != MAT_AIR) {
              _chunks[ci].set(x & 31, y & 31, z & 31, MAT_AIR);
              destroyed++;

              // Mark neighbor chunks dirty when at chunk boundaries
              // (their mesh depends on our voxels via padding)
              int lx = x & 31, ly = y & 31, lz = z & 31;
              int ccx = x >> 5, ccy = y >> 5, ccz = z >> 5;
              if (lx == 0) {
                int ni = _chunk_index(ccx - 1, ccy, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (lx == 31) {
                int ni = _chunk_index(ccx + 1, ccy, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (ly == 0) {
                int ni = _chunk_index(ccx, ccy - 1, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (ly == 31) {
                int ni = _chunk_index(ccx, ccy + 1, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (lz == 0) {
                int ni = _chunk_index(ccx, ccy, ccz - 1);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (lz == 31) {
                int ni = _chunk_index(ccx, ccy, ccz + 1);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
            }
          }
        }
      }
    }
  }

  // Queue structural integrity checks for columns above the blast
  if (destroyed > 0) {
    queue_collapse_check_voxel(vc.x, vc.y, vc.z, vr);
  }

  return destroyed;
}

Dictionary VoxelWorld::destroy_sphere_ex(const Vector3 &center, float radius,
                                         int max_debris) {
  Dictionary result;
  result["destroyed"] = 0;
  result["dominant_material"] = 0;
  result["material_histogram"] = PackedInt32Array();
  result["debris"] = Array();

  if (!_initialized)
    return result;
  if (max_debris < 0)
    max_debris = 0;
  if (max_debris > 64)
    max_debris = 64;

  Vector3i vc = world_to_voxel(center);
  int vr = (int)std::ceil(radius * _inv_scale);
  int vr_sq = vr * vr;
  int destroyed = 0;

  // Material histogram
  int mat_counts[MAT_COUNT] = {};

  // Reservoir sampling for debris positions
  struct DebrisSample {
    int x, y, z;
    uint8_t mat;
  };
  DebrisSample samples[64];
  int sample_count = 0;
  uint32_t rng = (uint32_t)(uintptr_t)this ^ 0xDEADBEEF; // simple LCG seed

  int min_x = std::max(0, vc.x - vr);
  int max_x = std::min(_size_x - 1, vc.x + vr);
  int min_y = std::max(0, vc.y - vr);
  int max_y = std::min(_size_y - 1, vc.y + vr);
  int min_z = std::max(0, vc.z - vr);
  int max_z = std::min(_size_z - 1, vc.z + vr);

  for (int z = min_z; z <= max_z; z++) {
    int dz = z - vc.z;
    for (int x = min_x; x <= max_x; x++) {
      int dx = x - vc.x;
      for (int y = min_y; y <= max_y; y++) {
        int dy = y - vc.y;
        if (dx * dx + dy * dy + dz * dz <= vr_sq) {
          int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
          if (ci >= 0) {
            uint8_t old = _chunks[ci].get(x & 31, y & 31, z & 31);
            if (old != MAT_AIR) {
              // Record material before destroying
              mat_counts[old]++;

              // Reservoir sampling for debris
              if (sample_count < max_debris) {
                samples[sample_count] = {x, y, z, old};
                sample_count++;
              } else {
                // LCG random: replace with probability max_debris/(destroyed+1)
                rng = rng * 1664525u + 1013904223u;
                int j = (int)(rng % (uint32_t)(destroyed + 1));
                if (j < max_debris) {
                  samples[j] = {x, y, z, old};
                }
              }

              _chunks[ci].set(x & 31, y & 31, z & 31, MAT_AIR);
              destroyed++;

              // Mark neighbor chunks dirty at boundaries
              int lx = x & 31, ly = y & 31, lz = z & 31;
              int ccx = x >> 5, ccy = y >> 5, ccz = z >> 5;
              if (lx == 0) {
                int ni = _chunk_index(ccx - 1, ccy, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (lx == 31) {
                int ni = _chunk_index(ccx + 1, ccy, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (ly == 0) {
                int ni = _chunk_index(ccx, ccy - 1, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (ly == 31) {
                int ni = _chunk_index(ccx, ccy + 1, ccz);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (lz == 0) {
                int ni = _chunk_index(ccx, ccy, ccz - 1);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
              if (lz == 31) {
                int ni = _chunk_index(ccx, ccy, ccz + 1);
                if (ni >= 0)
                  _chunks[ni].dirty = true;
              }
            }
          }
        }
      }
    }
  }

  // Queue structural integrity checks + seed CA rubble system
  if (destroyed > 0) {
    queue_collapse_check_voxel(vc.x, vc.y, vc.z, vr);
    _activate_neighbors(vc.x, vc.y, vc.z, vr);
  }

  // Find dominant material (argmax of histogram, excluding AIR)
  int dominant = 0;
  int dominant_count = 0;
  for (int m = 1; m < MAT_COUNT; m++) {
    if (mat_counts[m] > dominant_count) {
      dominant_count = mat_counts[m];
      dominant = m;
    }
  }

  // Build material histogram PackedInt32Array
  PackedInt32Array histogram;
  histogram.resize(MAT_COUNT);
  for (int m = 0; m < MAT_COUNT; m++) {
    histogram[m] = mat_counts[m];
  }

  // Build debris array (convert sampled voxel coords to world positions)
  Array debris;
  int actual_samples = std::min(sample_count, max_debris);
  for (int i = 0; i < actual_samples; i++) {
    Dictionary d;
    d["position"] = voxel_to_world(samples[i].x, samples[i].y, samples[i].z);
    d["material"] = (int)samples[i].mat;
    debris.push_back(d);
  }

  result["destroyed"] = destroyed;
  result["dominant_material"] = dominant;
  result["material_histogram"] = histogram;
  result["debris"] = debris;

  return result;
}

Color VoxelWorld::get_material_color(int material_id) const {
  if (material_id < 0 || material_id >= MAT_COUNT)
    return Color(0.5f, 0.5f, 0.5f);
  auto &m = MATERIAL_TABLE[material_id];
  return Color(m.r / 255.0f, m.g / 255.0f, m.b / 255.0f);
}

int VoxelWorld::destroy_box(const Vector3 &min_corner,
                            const Vector3 &max_corner) {
  if (!_initialized)
    return 0;

  Vector3i vmin = world_to_voxel(min_corner);
  Vector3i vmax = world_to_voxel(max_corner);
  int destroyed = 0;

  int x0 = std::max(0, std::min(vmin.x, vmax.x));
  int x1 = std::min(_size_x - 1, std::max(vmin.x, vmax.x));
  int y0 = std::max(0, std::min(vmin.y, vmax.y));
  int y1 = std::min(_size_y - 1, std::max(vmin.y, vmax.y));
  int z0 = std::max(0, std::min(vmin.z, vmax.z));
  int z1 = std::min(_size_z - 1, std::max(vmin.z, vmax.z));

  for (int z = z0; z <= z1; z++) {
    for (int x = x0; x <= x1; x++) {
      for (int y = y0; y <= y1; y++) {
        int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
        if (ci >= 0) {
          uint8_t old = _chunks[ci].get(x & 31, y & 31, z & 31);
          if (old != MAT_AIR) {
            _chunks[ci].set(x & 31, y & 31, z & 31, MAT_AIR);
            destroyed++;

            int lx = x & 31, ly = y & 31, lz = z & 31;
            int ccx = x >> 5, ccy = y >> 5, ccz = z >> 5;
            if (lx == 0) {
              int ni = _chunk_index(ccx - 1, ccy, ccz);
              if (ni >= 0)
                _chunks[ni].dirty = true;
            }
            if (lx == 31) {
              int ni = _chunk_index(ccx + 1, ccy, ccz);
              if (ni >= 0)
                _chunks[ni].dirty = true;
            }
            if (ly == 0) {
              int ni = _chunk_index(ccx, ccy - 1, ccz);
              if (ni >= 0)
                _chunks[ni].dirty = true;
            }
            if (ly == 31) {
              int ni = _chunk_index(ccx, ccy + 1, ccz);
              if (ni >= 0)
                _chunks[ni].dirty = true;
            }
            if (lz == 0) {
              int ni = _chunk_index(ccx, ccy, ccz - 1);
              if (ni >= 0)
                _chunks[ni].dirty = true;
            }
            if (lz == 31) {
              int ni = _chunk_index(ccx, ccy, ccz + 1);
              if (ni >= 0)
                _chunks[ni].dirty = true;
            }
          }
        }
      }
    }
  }

  return destroyed;
}

// ═══════════════════════════════════════════════════════════════════════
//  Raycast — 3D DDA (Digital Differential Analyzer)
// ═══════════════════════════════════════════════════════════════════════

bool VoxelWorld::raycast(const Vector3 &from, const Vector3 &direction,
                         float max_dist, VoxelHit &hit) const {
  if (!_initialized)
    return false;

  // Convert world position to voxel-space (continuous)
  float half_wx = _size_x * _voxel_scale * 0.5f;
  float half_wz = _size_z * _voxel_scale * 0.5f;

  float ox = (from.x + half_wx) * _inv_scale;
  float oy = from.y * _inv_scale;
  float oz = (from.z + half_wz) * _inv_scale;

  // Normalize direction
  float dir_len =
      std::sqrt(direction.x * direction.x + direction.y * direction.y +
                direction.z * direction.z);
  if (dir_len < 1e-8f)
    return false;
  float inv_len = 1.0f / dir_len;
  float dx = direction.x * inv_len;
  float dy = direction.y * inv_len;
  float dz = direction.z * inv_len;

  // Scale max_dist to voxel-space
  float max_voxel_dist = max_dist * _inv_scale;

  // Current voxel position
  int vx = (int)std::floor(ox);
  int vy = (int)std::floor(oy);
  int vz = (int)std::floor(oz);

  // Step direction (+1 or -1 along each axis)
  int step_x = (dx >= 0.0f) ? 1 : -1;
  int step_y = (dy >= 0.0f) ? 1 : -1;
  int step_z = (dz >= 0.0f) ? 1 : -1;

  // Distance along ray to next voxel boundary (tMax)
  float t_max_x =
      (dx != 0.0f) ? ((float)(vx + (step_x > 0 ? 1 : 0)) - ox) / dx : 1e30f;
  float t_max_y =
      (dy != 0.0f) ? ((float)(vy + (step_y > 0 ? 1 : 0)) - oy) / dy : 1e30f;
  float t_max_z =
      (dz != 0.0f) ? ((float)(vz + (step_z > 0 ? 1 : 0)) - oz) / dz : 1e30f;

  // Distance along ray for one full voxel step (tDelta)
  float t_delta_x = (dx != 0.0f) ? (float)step_x / dx : 1e30f;
  float t_delta_y = (dy != 0.0f) ? (float)step_y / dy : 1e30f;
  float t_delta_z = (dz != 0.0f) ? (float)step_z / dz : 1e30f;

  float t = 0.0f;
  Vector3 normal(0, 0, 0);

  // Walk the ray — step limit scales with voxel scale for fine resolutions
  int max_steps = (int)(max_dist * _inv_scale * 1.75f) + 128;
  for (int steps = 0; steps < max_steps; steps++) {
    // Check bounds
    if (vx < 0 || vx >= _size_x || vy < 0 || vy >= _size_y || vz < 0 ||
        vz >= _size_z) {
      break; // Left the world
    }

    // Check if current voxel is solid
    int ci = _chunk_index(vx >> 5, vy >> 5, vz >> 5);
    if (ci >= 0) {
      uint8_t mat = _chunks[ci].get(vx & 31, vy & 31, vz & 31);
      if (is_material_solid(mat)) {
        hit.hit = true;
        hit.voxel_pos = Vector3i(vx, vy, vz);
        hit.material = mat;
        hit.normal = normal;
        hit.distance = t * _voxel_scale; // Convert back to world-space distance
        // Compute exact world hit point
        hit.world_pos = from + direction.normalized() * hit.distance;
        return true;
      }
    }

    // Step to next voxel boundary (Amanatides & Woo DDA)
    if (t_max_x < t_max_y) {
      if (t_max_x < t_max_z) {
        t = t_max_x;
        if (t > max_voxel_dist)
          break;
        vx += step_x;
        t_max_x += t_delta_x;
        normal = Vector3((float)(-step_x), 0, 0);
      } else {
        t = t_max_z;
        if (t > max_voxel_dist)
          break;
        vz += step_z;
        t_max_z += t_delta_z;
        normal = Vector3(0, 0, (float)(-step_z));
      }
    } else {
      if (t_max_y < t_max_z) {
        t = t_max_y;
        if (t > max_voxel_dist)
          break;
        vy += step_y;
        t_max_y += t_delta_y;
        normal = Vector3(0, (float)(-step_y), 0);
      } else {
        t = t_max_z;
        if (t > max_voxel_dist)
          break;
        vz += step_z;
        t_max_z += t_delta_z;
        normal = Vector3(0, 0, (float)(-step_z));
      }
    }
  }

  hit.hit = false;
  return false;
}

int VoxelWorld::raycast_multi(const Vector3 &from, const Vector3 &direction,
                              float max_dist, VoxelHit *hits,
                              int max_hits) const {
  if (!_initialized || max_hits <= 0)
    return 0;

  float half_wx = _size_x * _voxel_scale * 0.5f;
  float half_wz = _size_z * _voxel_scale * 0.5f;

  float ox = (from.x + half_wx) * _inv_scale;
  float oy = from.y * _inv_scale;
  float oz = (from.z + half_wz) * _inv_scale;

  float dir_len =
      std::sqrt(direction.x * direction.x + direction.y * direction.y +
                direction.z * direction.z);
  if (dir_len < 1e-8f)
    return 0;
  float inv_len = 1.0f / dir_len;
  float dx = direction.x * inv_len;
  float dy = direction.y * inv_len;
  float dz = direction.z * inv_len;

  float max_voxel_dist = max_dist * _inv_scale;

  int vx = (int)std::floor(ox);
  int vy = (int)std::floor(oy);
  int vz = (int)std::floor(oz);

  int step_x = (dx >= 0.0f) ? 1 : -1;
  int step_y = (dy >= 0.0f) ? 1 : -1;
  int step_z = (dz >= 0.0f) ? 1 : -1;

  float t_max_x =
      (dx != 0.0f) ? ((float)(vx + (step_x > 0 ? 1 : 0)) - ox) / dx : 1e30f;
  float t_max_y =
      (dy != 0.0f) ? ((float)(vy + (step_y > 0 ? 1 : 0)) - oy) / dy : 1e30f;
  float t_max_z =
      (dz != 0.0f) ? ((float)(vz + (step_z > 0 ? 1 : 0)) - oz) / dz : 1e30f;

  float t_delta_x = (dx != 0.0f) ? (float)step_x / dx : 1e30f;
  float t_delta_y = (dy != 0.0f) ? (float)step_y / dy : 1e30f;
  float t_delta_z = (dz != 0.0f) ? (float)step_z / dz : 1e30f;

  float t = 0.0f;
  Vector3 normal(0, 0, 0);
  Vector3 dir_norm = direction.normalized();
  int count = 0;

  int max_steps = (int)(max_dist * _inv_scale * 1.75f) + 128;
  for (int steps = 0; steps < max_steps; steps++) {
    if (vx < 0 || vx >= _size_x || vy < 0 || vy >= _size_y || vz < 0 ||
        vz >= _size_z) {
      break;
    }

    int ci = _chunk_index(vx >> 5, vy >> 5, vz >> 5);
    if (ci >= 0) {
      uint8_t mat = _chunks[ci].get(vx & 31, vy & 31, vz & 31);
      if (is_material_solid(mat)) {
        if (count < max_hits) {
          hits[count].hit = true;
          hits[count].voxel_pos = Vector3i(vx, vy, vz);
          hits[count].material = mat;
          hits[count].normal = normal;
          hits[count].distance = t * _voxel_scale;
          hits[count].world_pos = from + dir_norm * hits[count].distance;
        }
        count++;
        if (count >= max_hits)
          break;
      }
    }

    if (t_max_x < t_max_y) {
      if (t_max_x < t_max_z) {
        t = t_max_x;
        if (t > max_voxel_dist)
          break;
        vx += step_x;
        t_max_x += t_delta_x;
        normal = Vector3((float)(-step_x), 0, 0);
      } else {
        t = t_max_z;
        if (t > max_voxel_dist)
          break;
        vz += step_z;
        t_max_z += t_delta_z;
        normal = Vector3(0, 0, (float)(-step_z));
      }
    } else {
      if (t_max_y < t_max_z) {
        t = t_max_y;
        if (t > max_voxel_dist)
          break;
        vy += step_y;
        t_max_y += t_delta_y;
        normal = Vector3(0, (float)(-step_y), 0);
      } else {
        t = t_max_z;
        if (t > max_voxel_dist)
          break;
        vz += step_z;
        t_max_z += t_delta_z;
        normal = Vector3(0, 0, (float)(-step_z));
      }
    }
  }

  return (count < max_hits) ? count : max_hits;
}

Dictionary VoxelWorld::raycast_dict(const Vector3 &from,
                                    const Vector3 &direction,
                                    float max_dist) const {
  Dictionary result;
  VoxelHit hit;

  if (raycast(from, direction, max_dist, hit)) {
    result["hit"] = true;
    result["voxel_pos"] = hit.voxel_pos;
    result["world_pos"] = hit.world_pos;
    result["normal"] = hit.normal;
    result["material"] = (int)hit.material;
    result["distance"] = hit.distance;
  } else {
    result["hit"] = false;
  }

  return result;
}

bool VoxelWorld::check_los(const Vector3 &from, const Vector3 &to) const {
  Vector3 diff = to - from;
  float dist = diff.length();
  if (dist < 1e-4f)
    return true;

  VoxelHit hit;
  return !raycast(from, diff / dist, dist, hit);
}

// ═══════════════════════════════════════════════════════════════════════
//  Chunk Management
// ═══════════════════════════════════════════════════════════════════════

std::vector<int> VoxelWorld::consume_dirty_chunks() {
  std::vector<int> dirty;
  dirty.reserve(64);
  for (int i = 0; i < _total_chunks; i++) {
    if (_chunks[i].dirty) {
      dirty.push_back(i);
      _chunks[i].dirty = false;
    }
  }
  return dirty;
}

const VoxelChunk *VoxelWorld::get_chunk(int cx, int cy, int cz) const {
  int ci = _chunk_index(cx, cy, cz);
  if (ci < 0)
    return nullptr;
  return &_chunks[ci];
}

VoxelChunk *VoxelWorld::get_chunk_mut(int cx, int cy, int cz) {
  int ci = _chunk_index(cx, cy, cz);
  if (ci < 0)
    return nullptr;
  return &_chunks[ci];
}

void VoxelWorld::_mark_chunk_dirty(int x, int y, int z) {
  int ci = _chunk_index(x >> 5, y >> 5, z >> 5);
  if (ci >= 0)
    _chunks[ci].dirty = true;
}

void VoxelWorld::_mark_boundary_neighbors_dirty(int x, int y, int z) {
  int lx = x & 31, ly = y & 31, lz = z & 31;
  int ccx = x >> 5, ccy = y >> 5, ccz = z >> 5;

  int ci = _chunk_index(ccx, ccy, ccz);
  if (ci >= 0)
    _chunks[ci].dirty = true;

  if (lx == 0) {
    int ni = _chunk_index(ccx - 1, ccy, ccz);
    if (ni >= 0)
      _chunks[ni].dirty = true;
  }
  if (lx == 31) {
    int ni = _chunk_index(ccx + 1, ccy, ccz);
    if (ni >= 0)
      _chunks[ni].dirty = true;
  }
  if (ly == 0) {
    int ni = _chunk_index(ccx, ccy - 1, ccz);
    if (ni >= 0)
      _chunks[ni].dirty = true;
  }
  if (ly == 31) {
    int ni = _chunk_index(ccx, ccy + 1, ccz);
    if (ni >= 0)
      _chunks[ni].dirty = true;
  }
  if (lz == 0) {
    int ni = _chunk_index(ccx, ccy, ccz - 1);
    if (ni >= 0)
      _chunks[ni].dirty = true;
  }
  if (lz == 31) {
    int ni = _chunk_index(ccx, ccy, ccz + 1);
    if (ni >= 0)
      _chunks[ni].dirty = true;
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  Structural Integrity (Voxel Collapse)
// ═══════════════════════════════════════════════════════════════════════

void VoxelWorld::queue_collapse_check(const Vector3 &center, float radius) {
  if (!_initialized)
    return;
  Vector3i vc = world_to_voxel(center);
  int vr = (int)std::ceil(radius * _inv_scale);
  queue_collapse_check_voxel(vc.x, vc.y, vc.z, vr);
}

void VoxelWorld::queue_collapse_check_voxel(int vx, int vy, int vz,
                                            int voxel_radius) {
  if (!_initialized)
    return;

  int min_x = std::max(0, vx - voxel_radius);
  int max_x = std::min(_size_x - 1, vx + voxel_radius);
  int min_z = std::max(0, vz - voxel_radius);
  int max_z = std::min(_size_z - 1, vz + voxel_radius);
  int from_y = std::max(0, vy - voxel_radius);
  int to_y = std::min(_size_y - 1, vy + voxel_radius + 32);

  for (int z = min_z; z <= max_z; z++) {
    for (int x = min_x; x <= max_x; x++) {
      _collapse_queue.push({x, z, from_y, to_y});
      _pending_collapse_count++;
    }
  }
}

int VoxelWorld::process_collapses(int max_per_tick) {
  if (_collapse_queue.empty())
    return 0;

  int voxels_moved = 0;

  while (!_collapse_queue.empty() && voxels_moved < max_per_tick) {
    CollapseColumn col = _collapse_queue.front();
    _collapse_queue.pop();
    _pending_collapse_count--;

    // Scan bottom-up: for each solid voxel in column, if air below, drop it
    bool any_fell = false;
    for (int y = col.min_y; y <= col.max_y && y < _size_y; y++) {
      uint8_t mat = get_voxel_fast(col.x, y, col.z);
      if (mat == MAT_AIR || mat == MAT_WATER)
        continue;

      // Ground level is always supported
      if (y == 0)
        continue;

      // Check support below
      if (is_solid(col.x, y - 1, col.z))
        continue;

      // Unsupported! Find where it lands
      int land_y = y - 1;
      while (land_y > 0 && !is_solid(col.x, land_y - 1, col.z)) {
        land_y--;
      }

      if (land_y != y) {
        // Move voxel down
        set_voxel(col.x, y, col.z, MAT_AIR);
        set_voxel(col.x, land_y, col.z, mat);

        _mark_boundary_neighbors_dirty(col.x, y, col.z);
        _mark_boundary_neighbors_dirty(col.x, land_y, col.z);

        voxels_moved++;
        any_fell = true;
      }
    }

    // Re-queue for cascading if anything fell
    if (any_fell) {
      _collapse_queue.push(col);
      _pending_collapse_count++;
    }
  }

  return voxels_moved;
}

int VoxelWorld::get_pending_collapses() const {
  return _pending_collapse_count + (int)_active_voxels.size();
}

// ═══════════════════════════════════════════════════════════════════════
//  Cellular Automata Rubble Physics
// ═══════════════════════════════════════════════════════════════════════

float VoxelWorld::_ca_slide_chance(uint8_t mat) {
  switch (mat) {
  case MAT_SAND:
  case MAT_GRAVEL:
  case MAT_DIRT:
  case MAT_GRASS:
  case MAT_CLAY:
    return 1.0f; // Loose materials always slide
  case MAT_WOOD:
  case MAT_SANDBAG:
    return 0.5f; // Moderate slide
  case MAT_STONE:
  case MAT_BRICK:
  case MAT_CONCRETE:
  case MAT_RUST:
    return 0.25f; // Angular rubble, mostly straight down
  case MAT_GLASS:
  case MAT_STEEL:
  case MAT_METAL_PLATE:
    return 0.0f; // No slide — falls straight
  default:
    return 0.25f;
  }
}

bool VoxelWorld::_ca_can_spread(uint8_t mat) {
  // Only loose granular materials spread sideways
  return mat == MAT_SAND || mat == MAT_GRAVEL || mat == MAT_DIRT;
}

void VoxelWorld::_activate_neighbors(int vx, int vy, int vz, int radius) {
  if (!_initialized)
    return;

  int min_x = std::max(0, vx - radius);
  int max_x = std::min(_size_x - 1, vx + radius);
  int min_y = std::max(0, vy - radius);
  int max_y = std::min(_size_y - 1, vy + radius + 4); // check above too
  int min_z = std::max(0, vz - radius);
  int max_z = std::min(_size_z - 1, vz + radius);

  for (int z = min_z; z <= max_z; z++) {
    for (int x = min_x; x <= max_x; x++) {
      for (int y = min_y; y <= max_y; y++) {
        uint8_t mat = get_voxel_fast(x, y, z);
        if (mat == MAT_AIR || mat == MAT_WATER)
          continue;
        if (y == 0)
          continue; // Bedrock never moves

        // Check if this voxel has an air neighbor (it's exposed)
        bool exposed = false;
        if (y > 0 && !is_solid(x, y - 1, z))
          exposed = true;
        if (!exposed && y < _size_y - 1 && !is_solid(x, y + 1, z))
          exposed = true;
        if (!exposed && x > 0 && !is_solid(x - 1, y, z))
          exposed = true;
        if (!exposed && x < _size_x - 1 && !is_solid(x + 1, y, z))
          exposed = true;
        if (!exposed && z > 0 && !is_solid(x, y, z - 1))
          exposed = true;
        if (!exposed && z < _size_z - 1 && !is_solid(x, y, z + 1))
          exposed = true;

        if (exposed) {
          _active_voxels.push_back({(int16_t)x, (int16_t)y, (int16_t)z, 0});
        }
      }
    }
  }
}

int VoxelWorld::process_rubble_ca(int max_per_tick) {
  if (_active_voxels.empty()) {
    // Fall back to legacy collapse queue if any remain
    if (!_collapse_queue.empty()) {
      return process_collapses(max_per_tick);
    }
    return 0;
  }

  _next_active.clear();
  _next_active.reserve(_active_voxels.size());
  int voxels_moved = 0;

  for (auto &av : _active_voxels) {
    if (voxels_moved >= max_per_tick) {
      // Budget exhausted — carry remaining to next tick
      _next_active.push_back(av);
      continue;
    }

    int x = av.x, y = av.y, z = av.z;

    // Validate: is this voxel still solid? (may have been destroyed or already
    // moved)
    uint8_t mat = get_voxel_fast(x, y, z);
    if (mat == MAT_AIR || mat == MAT_WATER || y == 0)
      continue;

    bool moved = false;

    // Rule 1: FALL — air directly below?
    if (y > 0 && !is_solid(x, y - 1, z)) {
      // Find landing spot (fall through consecutive air)
      int land_y = y - 1;
      while (land_y > 0 && !is_solid(x, land_y - 1, z)) {
        land_y--;
      }
      set_voxel(x, y, z, MAT_AIR);
      set_voxel(x, land_y, z, mat);
      _mark_boundary_neighbors_dirty(x, y, z);
      _mark_boundary_neighbors_dirty(x, land_y, z);
      av.x = (int16_t)x;
      av.y = (int16_t)land_y;
      av.z = (int16_t)z;
      av.ticks_idle = 0;
      voxels_moved++;
      moved = true;
    }
    // Rule 2: SLIDE — solid below, air at diagonal-below?
    else if (y > 0 && is_solid(x, y - 1, z)) {
      float slide = _ca_slide_chance(mat);
      if (slide > 0.0f) {
        // LCG random
        _ca_rng = _ca_rng * 1664525u + 1013904223u;
        float roll = (float)(_ca_rng & 0xFFFF) / 65535.0f;

        if (roll < slide) {
          // Check 4 diagonal-below positions in random order
          static const int dx4[] = {1, -1, 0, 0};
          static const int dz4[] = {0, 0, 1, -1};
          int start = (int)(_ca_rng >> 16) & 3;

          for (int i = 0; i < 4; i++) {
            int idx = (start + i) & 3;
            int nx = x + dx4[idx];
            int nz = z + dz4[idx];
            int ny = y - 1;

            if (nx >= 0 && nx < _size_x && nz >= 0 && nz < _size_z && ny >= 0) {
              if (!is_solid(nx, ny, nz)) {
                set_voxel(x, y, z, MAT_AIR);
                set_voxel(nx, ny, nz, mat);
                _mark_boundary_neighbors_dirty(x, y, z);
                _mark_boundary_neighbors_dirty(nx, ny, nz);
                av.x = (int16_t)nx;
                av.y = (int16_t)ny;
                av.z = (int16_t)nz;
                av.ticks_idle = 0;
                voxels_moved++;
                moved = true;
                break;
              }
            }
          }
        }
      }

      // Rule 3: SPREAD — loose materials spread sideways
      if (!moved && _ca_can_spread(mat)) {
        _ca_rng = _ca_rng * 1664525u + 1013904223u;
        float spread_roll = (float)(_ca_rng & 0xFFFF) / 65535.0f;

        if (spread_roll < 0.3f) { // 30% chance to spread per tick
          static const int dx4[] = {1, -1, 0, 0};
          static const int dz4[] = {0, 0, 1, -1};
          int start = (int)(_ca_rng >> 16) & 3;

          for (int i = 0; i < 4; i++) {
            int idx = (start + i) & 3;
            int nx = x + dx4[idx];
            int nz = z + dz4[idx];

            if (nx >= 0 && nx < _size_x && nz >= 0 && nz < _size_z) {
              if (!is_solid(nx, y, nz)) {
                set_voxel(x, y, z, MAT_AIR);
                set_voxel(nx, y, nz, mat);
                _mark_boundary_neighbors_dirty(x, y, z);
                _mark_boundary_neighbors_dirty(nx, y, nz);
                av.x = (int16_t)nx;
                av.y = (int16_t)y;
                av.z = (int16_t)nz;
                av.ticks_idle = 0;
                voxels_moved++;
                moved = true;
                break;
              }
            }
          }
        }
      }
    }

    // Rule 4: SETTLE — no movement for 3 ticks → remove from active set
    if (!moved) {
      av.ticks_idle++;
      if (av.ticks_idle < 3) {
        _next_active.push_back(av);
      }
      // else: settled, don't re-add
    } else {
      _next_active.push_back(av);
    }
  }

  std::swap(_active_voxels, _next_active);
  return voxels_moved;
}

// ═══════════════════════════════════════════════════════════════════════
//  Stats
// ═══════════════════════════════════════════════════════════════════════

int VoxelWorld::get_memory_usage_bytes() const {
  int total = 0;
  for (int i = 0; i < _total_chunks; i++) {
    total += _chunks[i].memory_bytes();
  }
  return total;
}

int VoxelWorld::get_dirty_chunk_count() const {
  int count = 0;
  for (int i = 0; i < _total_chunks; i++) {
    if (_chunks[i].dirty)
      count++;
  }
  return count;
}

// ═══════════════════════════════════════════════════════════════════════
//  Serialization (simple format: header + per-chunk data)
// ═══════════════════════════════════════════════════════════════════════

PackedByteArray VoxelWorld::save_to_bytes() const {
  PackedByteArray data;
  if (!_initialized)
    return data;

  // Header: 7 ints (28 bytes)
  // [magic, size_x, size_y, size_z, chunks_x, chunks_y, chunks_z]
  int header[7] = {0x56584C57, // "VXLW" magic
                   _size_x,    _size_y,   _size_z,
                   _chunks_x,  _chunks_y, _chunks_z};

  // Calculate total size
  int header_size = sizeof(header);
  int chunk_data_size = 0;
  for (int i = 0; i < _total_chunks; i++) {
    chunk_data_size += 1; // 1 byte: uniform flag
    if (_chunks[i].is_uniform()) {
      chunk_data_size += 1; // 1 byte: uniform material
    } else {
      chunk_data_size += VoxelChunk::VOLUME; // 32KB
    }
  }

  data.resize(header_size + chunk_data_size);
  uint8_t *ptr = data.ptrw();

  // Write header
  std::memcpy(ptr, header, header_size);
  ptr += header_size;

  // Write chunks
  for (int i = 0; i < _total_chunks; i++) {
    if (_chunks[i].is_uniform()) {
      *ptr++ = 1; // Uniform flag
      *ptr++ = _chunks[i].uniform_mat;
    } else {
      *ptr++ = 0; // Not uniform
      std::memcpy(ptr, _chunks[i].voxels, VoxelChunk::VOLUME);
      ptr += VoxelChunk::VOLUME;
    }
  }

  return data;
}

void VoxelWorld::load_from_bytes(const PackedByteArray &data) {
  if (data.size() < 28) {
    UtilityFunctions::printerr("[VoxelWorld] Load failed: data too small");
    return;
  }

  const uint8_t *ptr = data.ptr();

  // Read header
  int header[7];
  std::memcpy(header, ptr, sizeof(header));
  ptr += sizeof(header);

  if (header[0] != 0x56584C57) {
    UtilityFunctions::printerr("[VoxelWorld] Load failed: bad magic");
    return;
  }

  // Reinitialize with saved dimensions
  setup(header[1], header[2], header[3], _voxel_scale);

  // Read chunks
  for (int i = 0; i < _total_chunks; i++) {
    uint8_t uniform_flag = *ptr++;
    if (uniform_flag) {
      _chunks[i].fill(*ptr++);
    } else {
      // Need to inflate and copy
      if (_chunks[i].is_uniform()) {
        _chunks[i].set(0, 0, 0, ptr[0]); // Force inflate
      }
      std::memcpy(_chunks[i].voxels, ptr, VoxelChunk::VOLUME);
      ptr += VoxelChunk::VOLUME;
    }
    _chunks[i].dirty = true;
  }

  UtilityFunctions::print("[VoxelWorld] Loaded from bytes: ",
                          get_memory_usage_bytes(), " bytes used");
}

// ═══════════════════════════════════════════════════════════════════════
//  Generation
// ═══════════════════════════════════════════════════════════════════════

void VoxelWorld::generate_test_battlefield() {
  if (!_initialized) {
    UtilityFunctions::printerr("[VoxelWorld] Cannot generate: not initialized");
    return;
  }
  VoxelGenerator::generate_test_battlefield(this);
}

void VoxelWorld::generate_terrain(int base_height, int hill_amplitude,
                                  float hill_frequency) {
  if (!_initialized)
    return;
  VoxelGenerator::generate_terrain(this, base_height, hill_amplitude,
                                   hill_frequency);
}

void VoxelWorld::generate_building(int x, int y, int z, int width, int height,
                                   int depth, int wall_mat, int floor_mat,
                                   bool has_windows, bool has_door) {
  if (!_initialized)
    return;
  VoxelGenerator::generate_building(this, x, y, z, width, height, depth,
                                    (uint8_t)wall_mat, (uint8_t)floor_mat,
                                    has_windows, has_door);
}

void VoxelWorld::generate_wall(int x, int y, int z, int length, int height,
                               int thickness, int mat, bool along_x) {
  if (!_initialized)
    return;
  VoxelGenerator::generate_wall(this, x, y, z, length, height, thickness,
                                (uint8_t)mat, along_x);
}

void VoxelWorld::generate_trench(int x, int z, int length, int depth, int width,
                                 bool along_x) {
  if (!_initialized)
    return;
  VoxelGenerator::generate_trench(this, x, z, length, depth, width, along_x);
}

// ═══════════════════════════════════════════════════════════════════════
//  Meshing (callable from GDScript)
// ═══════════════════════════════════════════════════════════════════════

Array VoxelWorld::mesh_chunk(int cx, int cy, int cz) {
  Array empty;
  if (!_initialized)
    return empty;

  // Fast path: skip if this chunk and all face-neighbors are empty
  const VoxelChunk *center = get_chunk(cx, cy, cz);
  if (center && center->is_empty()) {
    bool any_neighbor_solid = false;
    for (int dx = -1; dx <= 1 && !any_neighbor_solid; dx++)
      for (int dy = -1; dy <= 1 && !any_neighbor_solid; dy++)
        for (int dz = -1; dz <= 1 && !any_neighbor_solid; dz++) {
          if (dx == 0 && dy == 0 && dz == 0)
            continue;
          const VoxelChunk *n = get_chunk(cx + dx, cy + dy, cz + dz);
          if (n && !n->is_empty())
            any_neighbor_solid = true;
        }
    if (!any_neighbor_solid)
      return empty;
  }

  // Build padded voxel array with neighbor data
  const VoxelChunk *neighbors[3][3][3];
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      for (int dz = -1; dz <= 1; dz++) {
        neighbors[dx + 1][dy + 1][dz + 1] =
            get_chunk(cx + dx, cy + dy, cz + dz);
      }
    }
  }

  uint8_t padded[VoxelMesherBlocky::CS_P3];
  VoxelMesherBlocky::build_padded_voxels(neighbors, padded);

  // Mesh it
  VoxelMesherBlocky::ChunkMesh mesh = VoxelMesherBlocky::mesh_chunk(padded);

  if (mesh.empty)
    return empty;

  return VoxelMesherBlocky::to_godot_arrays(mesh);
}

PackedInt32Array VoxelWorld::get_dirty_chunk_coords() const {
  PackedInt32Array coords;
  for (int cz = 0; cz < _chunks_z; cz++) {
    for (int cx = 0; cx < _chunks_x; cx++) {
      for (int cy = 0; cy < _chunks_y; cy++) {
        int ci = cz * (_chunks_x * _chunks_y) + cx * _chunks_y + cy;
        if (ci >= 0 && ci < _total_chunks && _chunks[ci].dirty) {
          coords.push_back(cx);
          coords.push_back(cy);
          coords.push_back(cz);
        }
      }
    }
  }
  return coords;
}

void VoxelWorld::clear_chunk_dirty(int cx, int cy, int cz) {
  int ci = _chunk_index(cx, cy, cz);
  if (ci >= 0) {
    _chunks[ci].dirty = false;
  }
}

// ═══════════════════════════════════════════════════════════════════════
//  GPU SVDAG
// ═══════════════════════════════════════════════════════════════════════

PackedByteArray VoxelWorld::build_svo() const {
  std::vector<SVONode> nodes = SVOBuilder::build_svo(this);
  return SVOBuilder::node_vector_to_bytes(nodes);
}

// ═══════════════════════════════════════════════════════════════════════
//  GDExtension Bindings
// ═══════════════════════════════════════════════════════════════════════

void VoxelWorld::_bind_methods() {
  // Setup
  ClassDB::bind_method(
      D_METHOD("setup", "size_x", "size_y", "size_z", "voxel_scale"),
      &VoxelWorld::setup, DEFVAL(0.25f));
  ClassDB::bind_method(D_METHOD("is_initialized"), &VoxelWorld::is_initialized);

  // Voxel access
  ClassDB::bind_method(D_METHOD("get_voxel", "x", "y", "z"),
                       &VoxelWorld::get_voxel);
  ClassDB::bind_method(D_METHOD("set_voxel", "x", "y", "z", "material"),
                       &VoxelWorld::set_voxel);
  ClassDB::bind_method(D_METHOD("set_voxel_dirty", "x", "y", "z", "material"),
                       &VoxelWorld::set_voxel_dirty);
  ClassDB::bind_method(D_METHOD("is_solid_at", "world_pos"),
                       &VoxelWorld::is_solid_at);
  ClassDB::bind_method(D_METHOD("get_column_top_y", "x", "z"),
                       &VoxelWorld::get_column_top_y);

  // Destruction
  ClassDB::bind_method(D_METHOD("destroy_sphere", "center", "radius"),
                       &VoxelWorld::destroy_sphere);
  ClassDB::bind_method(
      D_METHOD("destroy_sphere_ex", "center", "radius", "max_debris"),
      &VoxelWorld::destroy_sphere_ex, DEFVAL(12));
  ClassDB::bind_method(D_METHOD("destroy_box", "min_corner", "max_corner"),
                       &VoxelWorld::destroy_box);
  ClassDB::bind_method(D_METHOD("get_material_color", "material_id"),
                       &VoxelWorld::get_material_color);

  // Structural integrity
  ClassDB::bind_method(D_METHOD("queue_collapse_check", "center", "radius"),
                       &VoxelWorld::queue_collapse_check);
  ClassDB::bind_method(D_METHOD("process_collapses", "max_per_tick"),
                       &VoxelWorld::process_collapses, DEFVAL(500));
  ClassDB::bind_method(D_METHOD("process_rubble_ca", "max_per_tick"),
                       &VoxelWorld::process_rubble_ca, DEFVAL(500));
  ClassDB::bind_method(D_METHOD("get_pending_collapses"),
                       &VoxelWorld::get_pending_collapses);
  ClassDB::bind_method(D_METHOD("get_active_rubble_count"),
                       &VoxelWorld::get_active_rubble_count);

  // Raycast
  ClassDB::bind_method(
      D_METHOD("raycast_dict", "from", "direction", "max_dist"),
      &VoxelWorld::raycast_dict);
  ClassDB::bind_method(D_METHOD("check_los", "from", "to"),
                       &VoxelWorld::check_los);

  // Coordinate conversion
  ClassDB::bind_method(D_METHOD("world_to_voxel", "world_pos"),
                       &VoxelWorld::world_to_voxel);
  ClassDB::bind_method(D_METHOD("voxel_to_world", "x", "y", "z"),
                       &VoxelWorld::voxel_to_world);
  ClassDB::bind_method(D_METHOD("voxel_to_world_v", "vpos"),
                       &VoxelWorld::voxel_to_world_v);

  // Stats
  ClassDB::bind_method(D_METHOD("get_world_size_x"),
                       &VoxelWorld::get_world_size_x);
  ClassDB::bind_method(D_METHOD("get_world_size_y"),
                       &VoxelWorld::get_world_size_y);
  ClassDB::bind_method(D_METHOD("get_world_size_z"),
                       &VoxelWorld::get_world_size_z);
  ClassDB::bind_method(D_METHOD("get_chunks_x"), &VoxelWorld::get_chunks_x);
  ClassDB::bind_method(D_METHOD("get_chunks_y"), &VoxelWorld::get_chunks_y);
  ClassDB::bind_method(D_METHOD("get_chunks_z"), &VoxelWorld::get_chunks_z);
  ClassDB::bind_method(D_METHOD("get_total_chunks"),
                       &VoxelWorld::get_total_chunks);
  ClassDB::bind_method(D_METHOD("get_voxel_scale"),
                       &VoxelWorld::get_voxel_scale);
  ClassDB::bind_method(D_METHOD("get_memory_usage_bytes"),
                       &VoxelWorld::get_memory_usage_bytes);
  ClassDB::bind_method(D_METHOD("get_dirty_chunk_count"),
                       &VoxelWorld::get_dirty_chunk_count);

  // Generation
  ClassDB::bind_method(D_METHOD("generate_test_battlefield"),
                       &VoxelWorld::generate_test_battlefield);
  ClassDB::bind_method(D_METHOD("generate_terrain", "base_height",
                                "hill_amplitude", "hill_frequency"),
                       &VoxelWorld::generate_terrain, DEFVAL(16), DEFVAL(8),
                       DEFVAL(0.02f));
  ClassDB::bind_method(D_METHOD("generate_building", "x", "y", "z", "width",
                                "height", "depth", "wall_mat", "floor_mat",
                                "has_windows", "has_door"),
                       &VoxelWorld::generate_building, DEFVAL(3), DEFVAL(4),
                       DEFVAL(true), DEFVAL(true));
  ClassDB::bind_method(D_METHOD("generate_wall", "x", "y", "z", "length",
                                "height", "thickness", "mat", "along_x"),
                       &VoxelWorld::generate_wall, DEFVAL(5), DEFVAL(true));
  ClassDB::bind_method(D_METHOD("generate_trench", "x", "z", "length", "depth",
                                "width", "along_x"),
                       &VoxelWorld::generate_trench, DEFVAL(true));

  // Meshing
  ClassDB::bind_method(D_METHOD("mesh_chunk", "cx", "cy", "cz"),
                       &VoxelWorld::mesh_chunk);
  ClassDB::bind_method(D_METHOD("get_dirty_chunk_coords"),
                       &VoxelWorld::get_dirty_chunk_coords);
  ClassDB::bind_method(D_METHOD("clear_chunk_dirty", "cx", "cy", "cz"),
                       &VoxelWorld::clear_chunk_dirty);

  // GPU SVDAG
  ClassDB::bind_method(D_METHOD("build_svo"), &VoxelWorld::build_svo);

  // Serialization
  ClassDB::bind_method(D_METHOD("save_to_bytes"), &VoxelWorld::save_to_bytes);
  ClassDB::bind_method(D_METHOD("load_from_bytes", "data"),
                       &VoxelWorld::load_from_bytes);

  // Material constants (exposed as class constants for GDScript)
  BIND_CONSTANT(MAT_AIR);
  BIND_CONSTANT(MAT_DIRT);
  BIND_CONSTANT(MAT_STONE);
  BIND_CONSTANT(MAT_WOOD);
  BIND_CONSTANT(MAT_STEEL);
  BIND_CONSTANT(MAT_CONCRETE);
  BIND_CONSTANT(MAT_BRICK);
  BIND_CONSTANT(MAT_GLASS);
  BIND_CONSTANT(MAT_SAND);
  BIND_CONSTANT(MAT_WATER);
  BIND_CONSTANT(MAT_GRASS);
  BIND_CONSTANT(MAT_GRAVEL);
  BIND_CONSTANT(MAT_SANDBAG);
  BIND_CONSTANT(MAT_CLAY);
  BIND_CONSTANT(MAT_METAL_PLATE);
  BIND_CONSTANT(MAT_RUST);
}
