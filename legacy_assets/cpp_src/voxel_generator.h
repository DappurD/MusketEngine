#ifndef VOXEL_GENERATOR_H
#define VOXEL_GENERATOR_H

#include "voxel_world.h"
#include "voxel_materials.h"

#include <cstdint>
#include <cmath>

namespace godot {

/// Procedural world generator for test battlefields.
/// Generates terrain, buildings, and cover directly into a VoxelWorld.
/// All generation happens in C++ for speed.
class VoxelGenerator {
public:
    /// Generate a flat battlefield with noise hills.
    /// base_height: terrain floor in voxels from y=0
    /// hill_amplitude: max hill height in voxels
    /// hill_frequency: noise frequency (lower = smoother hills)
    static void generate_terrain(
        VoxelWorld *world,
        int base_height = 16,
        int hill_amplitude = 8,
        float hill_frequency = 0.02f
    );

    /// Generate a simple rectangular building.
    /// All coordinates in voxels.
    static void generate_building(
        VoxelWorld *world,
        int x, int y, int z,
        int width, int height, int depth,
        uint8_t wall_mat = MAT_BRICK,
        uint8_t floor_mat = MAT_CONCRETE,
        bool has_windows = true,
        bool has_door = true
    );

    /// Generate a low wall / barricade.
    static void generate_wall(
        VoxelWorld *world,
        int x, int y, int z,
        int length, int height, int thickness,
        uint8_t mat = MAT_SANDBAG,
        bool along_x = true
    );

    /// Generate a trench carved into the terrain.
    static void generate_trench(
        VoxelWorld *world,
        int x, int z,
        int length, int depth, int width,
        bool along_x = true
    );

    /// Generate a full test battlefield with buildings, walls, trenches.
    static void generate_test_battlefield(VoxelWorld *world);

private:
    /// Simple value noise (deterministic, no external deps).
    static float _noise2d(float x, float z);
    static float _hash(int x, int z);
    static float _lerp(float a, float b, float t);
    static float _smooth(float t);
};

} // namespace godot

#endif // VOXEL_GENERATOR_H
