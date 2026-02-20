#ifndef VOXEL_CHUNK_H
#define VOXEL_CHUNK_H

#include "voxel_materials.h"
#include <cstring>
#include <cstdint>

namespace godot {

/// A 32x32x32 voxel chunk with uniform compression.
/// If every voxel is the same material, stores only that value (1 byte).
/// Otherwise, allocates a full 32KB flat array.
///
/// Iteration order: Z-major (ZXY) for cache locality when sweeping horizontal slices.
/// Index formula: z * (SIZE_X * SIZE_Y) + x * SIZE_Y + y
///   → Iterating z in outer loop hits contiguous memory.
struct VoxelChunk {
    static constexpr int SIZE = 32;
    static constexpr int VOLUME = SIZE * SIZE * SIZE;  // 32,768

    // ── Data ─────────────────────────────────────────────────────────
    uint8_t *voxels = nullptr;     // nullptr when uniform
    uint8_t  uniform_mat = MAT_AIR; // Material when chunk is uniform
    bool     dirty = false;         // Needs re-mesh
    bool     has_mesh = false;      // Has been meshed at least once

    // Chunk position in chunk-space (not voxel-space)
    int cx = 0, cy = 0, cz = 0;

    // ── Lifecycle ────────────────────────────────────────────────────

    VoxelChunk() = default;

    ~VoxelChunk() {
        if (voxels) {
            delete[] voxels;
            voxels = nullptr;
        }
    }

    // No copy (owns heap memory)
    VoxelChunk(const VoxelChunk &) = delete;
    VoxelChunk &operator=(const VoxelChunk &) = delete;

    // Move OK
    VoxelChunk(VoxelChunk &&other) noexcept
        : voxels(other.voxels), uniform_mat(other.uniform_mat),
          dirty(other.dirty), has_mesh(other.has_mesh),
          cx(other.cx), cy(other.cy), cz(other.cz) {
        other.voxels = nullptr;
    }

    VoxelChunk &operator=(VoxelChunk &&other) noexcept {
        if (this != &other) {
            delete[] voxels;
            voxels = other.voxels;
            uniform_mat = other.uniform_mat;
            dirty = other.dirty;
            has_mesh = other.has_mesh;
            cx = other.cx;
            cy = other.cy;
            cz = other.cz;
            other.voxels = nullptr;
        }
        return *this;
    }

    // ── Index calculation (ZXY order) ────────────────────────────────
    // z is outer dimension for cache-friendly horizontal slice iteration.

    static inline int idx(int x, int y, int z) {
        return z * (SIZE * SIZE) + x * SIZE + y;
    }

    // ── Accessors ────────────────────────────────────────────────────

    inline uint8_t get(int x, int y, int z) const {
        if (!voxels) return uniform_mat;
        return voxels[idx(x, y, z)];
    }

    inline void set(int x, int y, int z, uint8_t mat) {
        if (!voxels) {
            if (mat == uniform_mat) return; // No change
            // Inflate: allocate full array and fill with current uniform value
            _inflate();
        }
        voxels[idx(x, y, z)] = mat;
        dirty = true;
    }

    inline bool is_solid(int x, int y, int z) const {
        return is_material_solid(get(x, y, z));
    }

    // ── Uniform check ────────────────────────────────────────────────

    /// Try to compress back to uniform if all voxels match.
    /// Call after bulk edits (destruction) to reclaim memory.
    void try_deflate() {
        if (!voxels) return;  // Already uniform

        uint8_t first = voxels[0];
        for (int i = 1; i < VOLUME; i++) {
            if (voxels[i] != first) return;  // Not uniform
        }

        // All same — deflate
        uniform_mat = first;
        delete[] voxels;
        voxels = nullptr;
    }

    /// Fill entire chunk with one material (resets to uniform).
    void fill(uint8_t mat) {
        if (voxels) {
            delete[] voxels;
            voxels = nullptr;
        }
        uniform_mat = mat;
        dirty = true;
    }

    /// Returns true if chunk is stored as uniform (no heap allocation).
    inline bool is_uniform() const { return voxels == nullptr; }

    /// Returns true if chunk is entirely air.
    inline bool is_empty() const { return is_uniform() && uniform_mat == MAT_AIR; }

    /// Memory usage in bytes (for debug stats).
    inline int memory_bytes() const {
        return is_uniform() ? 1 : VOLUME;
    }

private:
    void _inflate() {
        voxels = new uint8_t[VOLUME];
        std::memset(voxels, uniform_mat, VOLUME);
    }
};

} // namespace godot

#endif // VOXEL_CHUNK_H
