#ifndef STRUCTURAL_INTEGRITY_H
#define STRUCTURAL_INTEGRITY_H

#include "voxel_world.h"
#include "voxel_mesher_blocky.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <vector>
#include <cstdint>

namespace godot {

/// Structural integrity analysis for voxel worlds.
///
/// Detects disconnected voxel regions after destruction using
/// hierarchical flood-fill: chunk-level BFS first (fast), then
/// voxel-level validation only for suspect chunks.
///
/// Returns island data (mesh arrays, center of mass, mass) for
/// GDScript to spawn as RigidBody3D physics objects.
class StructuralIntegrity : public RefCounted {
    GDCLASS(StructuralIntegrity, RefCounted)

public:
    StructuralIntegrity() = default;

    /// Run after destruction. Returns Array of island Dictionaries.
    /// Each: {center: Vector3, mass: float, voxel_count: int,
    ///        mesh_arrays: Array, bounds_min: Vector3i, bounds_max: Vector3i}
    /// destruction_center/radius in world-space.
    Array detect_islands(VoxelWorld *world, const Vector3 &destruction_center, float search_radius);

    /// Detect voxels whose BFS distance from ground exceeds their material's
    /// max support distance. Returns Array of Dictionaries:
    /// [{position: Vector3i, material: int, distance: int}, ...]
    /// Call after destruction, then erase returned voxels and re-run detect_islands.
    Array detect_weakened_voxels(VoxelWorld *world, const Vector3 &center, float search_radius);

protected:
    static void _bind_methods();

private:
    // ── Phase A: Chunk-level connectivity ──────────────────────────
    // Returns indices of chunks NOT connected to ground (y=0).
    std::vector<int> _find_ungrounded_chunks(
        VoxelWorld *world,
        int cx_min, int cx_max,
        int cy_min, int cy_max,
        int cz_min, int cz_max
    );

    // Check if two adjacent chunks share at least one solid voxel pair at boundary.
    bool _chunks_connected(VoxelWorld *world, int cx1, int cy1, int cz1,
                           int cx2, int cy2, int cz2);

    // ── Phase B: Voxel-level flood-fill ────────────────────────────
    struct IslandData {
        Vector3 center_of_mass;
        float total_mass = 0.0f;
        int voxel_count = 0;
        Vector3i bounds_min;
        Vector3i bounds_max;
        std::vector<Vector3i> voxel_positions;
        std::vector<uint8_t> voxel_materials;
    };

    // Flood-fill from ungrounded chunks to extract connected islands.
    std::vector<IslandData> _extract_islands(
        VoxelWorld *world,
        const std::vector<int> &ungrounded_chunk_indices,
        int chunks_x, int chunks_y, int chunks_z
    );

    // ── Phase C: Island meshing ────────────────────────────────────
    // Mesh an extracted island using the blocky mesher.
    Array _mesh_island(const IslandData &island);
};

} // namespace godot

#endif // STRUCTURAL_INTEGRITY_H
