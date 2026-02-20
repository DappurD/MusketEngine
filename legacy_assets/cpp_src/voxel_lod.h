#ifndef VOXEL_LOD_H
#define VOXEL_LOD_H

#include "voxel_chunk.h"
#include "voxel_mesher_blocky.h"

#include <cstdint>
#include <cstring>

namespace godot {

/// LOD downsampler for voxel chunks.
///
/// Generates reduced-resolution chunk data by merging NxNxN voxel groups
/// into single voxels (majority material wins). The downsampled data is
/// then fed to the existing greedy mesher.
///
/// LOD levels:
///   0: Full 32^3 (no downsampling) — existing mesher handles this
///   1: 16^3 effective — merge 2x2x2 groups
///   2: 8^3 effective — merge 4x4x4 groups
///
/// Output mesh vertices are in 0..32 range (same as LOD 0) so the same
/// chunk transform can be used. The mesher produces fewer, larger quads.
class VoxelLOD {
public:
    /// Downsample a padded voxel array to a lower LOD level.
    ///
    /// Takes a full CS_P3 padded voxel array (34^3) and produces a
    /// downsampled padded array suitable for mesh_chunk().
    ///
    /// lod_level: 1 = merge 2x2x2, 2 = merge 4x4x4
    /// out_padded: must be CS_P3 bytes (reused for the downsampled data)
    ///
    /// The downsampled data is written into a 34^3 padded format where
    /// the inner 32^3 contains the merged voxels (with each merged voxel
    /// duplicated to fill its NxN space in the 32^3 grid).
    static void downsample_padded(const uint8_t *src_padded, uint8_t *out_padded, int lod_level);

    /// Get the merge factor for a LOD level (2 for LOD 1, 4 for LOD 2).
    static inline int merge_factor(int lod_level) {
        switch (lod_level) {
            case 1: return 2;
            case 2: return 4;
            default: return 1;
        }
    }

private:
    static constexpr int CS = VoxelMesherBlocky::CS;      // 32
    static constexpr int CS_P = VoxelMesherBlocky::CS_P;  // 34
    static constexpr int CS_P2 = VoxelMesherBlocky::CS_P2;
    static constexpr int CS_P3 = VoxelMesherBlocky::CS_P3;

    /// Find the majority non-air material in a group of voxels.
    /// Returns MAT_AIR if all are air.
    static uint8_t _majority_material(const uint8_t *src_padded,
                                       int base_x, int base_y, int base_z,
                                       int group_size);
};

} // namespace godot

#endif // VOXEL_LOD_H
