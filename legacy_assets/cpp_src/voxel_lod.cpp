#include "voxel_lod.h"
#include "voxel_materials.h"

#include <cstring>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════════
//  Majority material: surface-weighted voting
//  Scans each XZ column top-down; the first solid voxel (surface) gets
//  8× weight so that visible surface materials like grass/stone beat
//  subsurface dirt at LOD 2 distance.
// ═══════════════════════════════════════════════════════════════════════

uint8_t VoxelLOD::_majority_material(const uint8_t *src_padded,
                                      int base_x, int base_y, int base_z,
                                      int group_size) {
    static constexpr int SURFACE_WEIGHT = 8;
    static constexpr int BURIED_WEIGHT  = 1;

    int weighted_counts[MAT_COUNT] = {};
    int columns_with_surface = 0;
    int total_columns = group_size * group_size;

    for (int dz = 0; dz < group_size; dz++) {
        for (int dx = 0; dx < group_size; dx++) {
            bool found_surface = false;
            // Scan top-down within this XZ column
            for (int dy = group_size - 1; dy >= 0; dy--) {
                int idx = (base_z + dz) * CS_P2 + (base_x + dx) * CS_P + (base_y + dy);
                uint8_t mat = src_padded[idx];
                if (mat != MAT_AIR && mat < MAT_COUNT) {
                    if (!found_surface) {
                        weighted_counts[mat] += SURFACE_WEIGHT;
                        found_surface = true;
                    } else {
                        weighted_counts[mat] += BURIED_WEIGHT;
                    }
                }
            }
            if (found_surface) {
                columns_with_surface++;
            }
        }
    }

    // If fewer than 25% of columns have any surface, treat as air
    if (columns_with_surface * 4 < total_columns) {
        return MAT_AIR;
    }

    // Find the material with the highest weighted count
    uint8_t best_mat = MAT_AIR;
    int best_count = 0;
    for (int m = 1; m < MAT_COUNT; m++) {
        if (weighted_counts[m] > best_count) {
            best_count = weighted_counts[m];
            best_mat = (uint8_t)m;
        }
    }

    return best_mat;
}

// ═══════════════════════════════════════════════════════════════════════
//  Downsample: merge NxNxN groups and fill a 34^3 padded output
// ═══════════════════════════════════════════════════════════════════════

void VoxelLOD::downsample_padded(const uint8_t *src_padded, uint8_t *out_padded, int lod_level) {
    int group = merge_factor(lod_level);
    if (group <= 1) {
        // LOD 0: just copy
        std::memcpy(out_padded, src_padded, CS_P3);
        return;
    }

    // Clear output to air
    std::memset(out_padded, MAT_AIR, CS_P3);

    // The inner 32^3 of the padded array starts at offset (1,1,1).
    // We process groups within the inner region of the source.
    // Each group of NxN voxels in source maps to NxN voxels in output
    // (same position, same material = larger visible quads).
    int cells = CS / group;  // 16 for LOD1, 8 for LOD2

    for (int gz = 0; gz < cells; gz++) {
        for (int gx = 0; gx < cells; gx++) {
            for (int gy = 0; gy < cells; gy++) {
                // Source position in padded coords (+1 for padding)
                int src_x = gx * group + 1;
                int src_y = gy * group + 1;
                int src_z = gz * group + 1;

                uint8_t mat = _majority_material(src_padded, src_x, src_y, src_z, group);

                // Fill the NxN region in output with this material
                // (same padded coords as source)
                for (int dz = 0; dz < group; dz++) {
                    for (int dx = 0; dx < group; dx++) {
                        for (int dy = 0; dy < group; dy++) {
                            int out_idx = (src_z + dz) * CS_P2 + (src_x + dx) * CS_P + (src_y + dy);
                            out_padded[out_idx] = mat;
                        }
                    }
                }
            }
        }
    }

    // Copy padding from source for correct border face culling.
    // Z=0 and Z=33 slices
    for (int x = 0; x < CS_P; x++) {
        for (int y = 0; y < CS_P; y++) {
            out_padded[0 * CS_P2 + x * CS_P + y] = src_padded[0 * CS_P2 + x * CS_P + y];
            out_padded[(CS_P - 1) * CS_P2 + x * CS_P + y] = src_padded[(CS_P - 1) * CS_P2 + x * CS_P + y];
        }
    }
    // X=0 and X=33 slices
    for (int z = 0; z < CS_P; z++) {
        for (int y = 0; y < CS_P; y++) {
            out_padded[z * CS_P2 + 0 * CS_P + y] = src_padded[z * CS_P2 + 0 * CS_P + y];
            out_padded[z * CS_P2 + (CS_P - 1) * CS_P + y] = src_padded[z * CS_P2 + (CS_P - 1) * CS_P + y];
        }
    }
    // Y=0 and Y=33 rows
    for (int z = 0; z < CS_P; z++) {
        for (int x = 0; x < CS_P; x++) {
            out_padded[z * CS_P2 + x * CS_P + 0] = src_padded[z * CS_P2 + x * CS_P + 0];
            out_padded[z * CS_P2 + x * CS_P + (CS_P - 1)] = src_padded[z * CS_P2 + x * CS_P + (CS_P - 1)];
        }
    }
}
