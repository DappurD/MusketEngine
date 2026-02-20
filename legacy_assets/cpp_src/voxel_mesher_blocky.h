#ifndef VOXEL_MESHER_BLOCKY_H
#define VOXEL_MESHER_BLOCKY_H

#include "voxel_chunk.h"
#include "voxel_materials.h"

#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/classes/mesh.hpp>

#include <cstdint>
#include <vector>

namespace godot {

/// Binary greedy mesher for 32x32x32 voxel chunks.
///
/// Uses 64-bit bitmask operations to:
/// 1. Cull hidden faces (bitwise AND/shift on neighbor columns)
/// 2. Greedily merge coplanar same-material faces into large quads
/// 3. Compute per-vertex ambient occlusion (4 levels)
///
/// Target: <200 microseconds per chunk on modern CPU.
///
/// Reference: cgerikj/binary-greedy-meshing
class VoxelMesherBlocky {
public:
    static constexpr int CS   = 32;          // Chunk size
    static constexpr int CS_P = CS + 2;      // Padded (1 voxel neighbor on each side)
    static constexpr int CS_P2 = CS_P * CS_P;   // 1156
    static constexpr int CS_P3 = CS_P * CS_P * CS_P; // 39304

    // Face directions
    enum Face {
        FACE_POS_Y = 0,  // +Y (top)
        FACE_NEG_Y = 1,  // -Y (bottom)
        FACE_POS_X = 2,  // +X (right)
        FACE_NEG_X = 3,  // -X (left)
        FACE_POS_Z = 4,  // +Z (front)
        FACE_NEG_Z = 5,  // -Z (back)
        FACE_COUNT = 6
    };

    // Normals for each face direction
    static const float FACE_NORMALS[6][3];

    /// Result of meshing a single chunk.
    struct ChunkMesh {
        PackedVector3Array  vertices;
        PackedVector3Array  normals;
        PackedColorArray    colors;     // RGB = material_color (pure), A = material_id / 255
        PackedVector2Array  uv2;        // x = raw AO (0-1), y = reserved
        PackedInt32Array    indices;
        int quad_count = 0;
        bool empty = true;
    };

    /// Mesh a single chunk with neighbor data for seamless borders.
    ///
    /// padded_voxels: 34x34x34 array in ZXY order, with 1-voxel padding from neighbors.
    ///   Index = z * CS_P2 + x * CS_P + y
    ///   Center chunk occupies [1..32] in each dimension.
    ///
    /// If padded_voxels is null, uses the chunk data alone (seams at borders).
    static ChunkMesh mesh_chunk(const uint8_t *padded_voxels);

    /// Build padded voxel array from chunk + its 26 neighbors.
    /// neighbors[dx+1][dy+1][dz+1] where dx,dy,dz in {-1,0,1}.
    /// neighbors[1][1][1] is the chunk itself. nullptr = treat as air.
    static void build_padded_voxels(
        const VoxelChunk *neighbors[3][3][3],
        uint8_t *out_padded  // Must be CS_P3 bytes
    );

    /// Convert ChunkMesh to a Godot Array suitable for RenderingServer.
    static Array to_godot_arrays(const ChunkMesh &mesh);

    // Public static helpers for compute_face_ao (used by file-scope helper)
    static inline int padded_idx_s(int x, int y, int z) {
        return z * CS_P2 + x * CS_P + y;
    }
    static inline int vertex_ao_s(bool side1, bool side2, bool corner) {
        if (side1 && side2) return 0;
        return 3 - (int)side1 - (int)side2 - (int)corner;
    }

private:
    /// Cell data for greedy merge grid (one per face in a 2D layer).
    struct FaceCell {
        uint8_t material = 0;
        int     ao0 = 3, ao1 = 3, ao2 = 3, ao3 = 3;
        bool    visited = true;  // Default: "already visited" (skip empty cells)
    };

    // Working buffers (stack-allocated, ~50KB total)
    struct WorkBuffers {
        uint64_t opaque_mask[CS_P2];       // Occupancy bitmask per column
        uint64_t face_masks[CS * CS * 6];  // Visible face bits per column per face
    };

    static inline int padded_idx(int x, int y, int z) {
        return z * CS_P2 + x * CS_P + y;  // ZXY order
    }

    /// Build occupancy bitmask columns from padded voxels.
    static void _build_opaque_mask(const uint8_t *padded, uint64_t *opaque_mask);

    /// Generate face visibility masks using bitwise culling.
    static void _cull_faces(const uint64_t *opaque_mask, uint64_t *face_masks);

    /// Greedy merge + emit quads for one face direction.
    static void _greedy_merge_face(
        int face,
        const uint8_t *padded,
        const uint64_t *face_masks,
        ChunkMesh &out
    );

    /// Compute vertex AO level (0=darkest, 3=brightest).
    static inline int _vertex_ao(bool side1, bool side2, bool corner) {
        if (side1 && side2) return 0;
        return 3 - (int)side1 - (int)side2 - (int)corner;
    }

    /// Emit a single quad (4 vertices, 6 indices) into the mesh.
    /// flip_winding: reverse triangle index order for correct backface culling.
    static void _emit_quad(
        ChunkMesh &mesh,
        float x0, float y0, float z0,  // Corner 0 position
        float ax, float ay, float az,   // Axis A (width direction)
        float bx, float by, float bz,   // Axis B (height direction)
        float w, float h,               // Quad dimensions
        float nx, float ny, float nz,   // Normal
        uint8_t material,
        int ao0, int ao1, int ao2, int ao3,  // AO per vertex
        bool flip_winding = false
    );
};

} // namespace godot

#endif // VOXEL_MESHER_BLOCKY_H
