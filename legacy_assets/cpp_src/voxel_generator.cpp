#include "voxel_generator.h"

#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════════
//  Simple value noise (no external deps)
// ═══════════════════════════════════════════════════════════════════════

float VoxelGenerator::_hash(int x, int z) {
    int n = x * 73856093 ^ z * 19349663;
    n = (n << 13) ^ n;
    return ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / (float)0x7fffffff;
}

float VoxelGenerator::_lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float VoxelGenerator::_smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
}

float VoxelGenerator::_noise2d(float x, float z) {
    int ix = (int)std::floor(x);
    int iz = (int)std::floor(z);
    float fx = x - (float)ix;
    float fz = z - (float)iz;
    fx = _smooth(fx);
    fz = _smooth(fz);

    float a = _hash(ix, iz);
    float b = _hash(ix + 1, iz);
    float c = _hash(ix, iz + 1);
    float d = _hash(ix + 1, iz + 1);

    return _lerp(_lerp(a, b, fx), _lerp(c, d, fx), fz);
}

// ═══════════════════════════════════════════════════════════════════════
//  Terrain generation
// ═══════════════════════════════════════════════════════════════════════

void VoxelGenerator::generate_terrain(
        VoxelWorld *world,
        int base_height,
        int hill_amplitude,
        float hill_frequency) {

    int sx = world->get_world_size_x();
    int sz = world->get_world_size_z();

    for (int x = 0; x < sx; x++) {
        for (int z = 0; z < sz; z++) {
            // Multi-octave noise for natural terrain
            float nx = (float)x * hill_frequency;
            float nz = (float)z * hill_frequency;
            float h = _noise2d(nx, nz) * 0.5f
                    + _noise2d(nx * 2.0f, nz * 2.0f) * 0.25f
                    + _noise2d(nx * 4.0f, nz * 4.0f) * 0.125f;
            h = h / 0.875f;  // Normalize to [0, 1]

            int terrain_h = base_height + (int)(h * (float)hill_amplitude);

            for (int y = 0; y < terrain_h; y++) {
                uint8_t mat;
                if (y < terrain_h - 3) {
                    mat = MAT_STONE;
                } else if (y < terrain_h - 1) {
                    mat = MAT_DIRT;
                } else {
                    mat = MAT_GRASS;
                }
                world->set_voxel(x, y, z, mat);
            }
        }
    }

    UtilityFunctions::print("[VoxelGenerator] Terrain generated: base=", base_height,
                            " amplitude=", hill_amplitude);
}

// ═══════════════════════════════════════════════════════════════════════
//  Building generation
// ═══════════════════════════════════════════════════════════════════════

void VoxelGenerator::generate_building(
        VoxelWorld *world,
        int bx, int by, int bz,
        int width, int height, int depth,
        uint8_t wall_mat,
        uint8_t floor_mat,
        bool has_windows,
        bool has_door) {

    // Walls
    for (int y = by; y < by + height; y++) {
        for (int x = bx; x < bx + width; x++) {
            for (int z = bz; z < bz + depth; z++) {
                bool is_wall = (x == bx || x == bx + width - 1 ||
                               z == bz || z == bz + depth - 1);
                bool is_floor_level = (y == by);

                if (is_wall) {
                    // Window openings: 2-wide, 3-tall, starting at y+4, every 8 voxels along wall
                    if (has_windows && y >= by + 4 && y < by + 7) {
                        bool on_x_wall = (z == bz || z == bz + depth - 1);
                        bool on_z_wall = (x == bx || x == bx + width - 1);

                        int wall_pos = on_x_wall ? (x - bx) : (z - bz);
                        int wall_len = on_x_wall ? width : depth;

                        if (wall_pos > 3 && wall_pos < wall_len - 3 &&
                            ((wall_pos - 4) % 8 < 2)) {
                            continue;  // Window opening — leave as air
                        }
                    }

                    // Door opening: 2-wide, 4-tall on front wall
                    if (has_door && z == bz && y < by + 4) {
                        int door_x = bx + width / 2 - 1;
                        if (x >= door_x && x < door_x + 2) {
                            continue;  // Door opening
                        }
                    }

                    world->set_voxel(x, y, z, wall_mat);
                } else if (is_floor_level) {
                    world->set_voxel(x, y, z, floor_mat);
                }
                // Interior is air (default)
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Wall / barricade generation
// ═══════════════════════════════════════════════════════════════════════

void VoxelGenerator::generate_wall(
        VoxelWorld *world,
        int wx, int wy, int wz,
        int length, int height, int thickness,
        uint8_t mat,
        bool along_x) {

    for (int l = 0; l < length; l++) {
        for (int h = 0; h < height; h++) {
            for (int t = 0; t < thickness; t++) {
                int x = along_x ? (wx + l) : (wx + t);
                int y = wy + h;
                int z = along_x ? (wz + t) : (wz + l);
                world->set_voxel(x, y, z, mat);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Trench generation (carve into terrain)
// ═══════════════════════════════════════════════════════════════════════

void VoxelGenerator::generate_trench(
        VoxelWorld *world,
        int tx, int tz,
        int length, int depth, int width,
        bool along_x) {

    int sy = world->get_world_size_y();

    for (int l = 0; l < length; l++) {
        for (int w = 0; w < width; w++) {
            int x = along_x ? (tx + l) : (tx + w);
            int z = along_x ? (tz + w) : (tz + l);

            // Find surface height at this XZ
            int surface_y = 0;
            for (int y = sy - 1; y >= 0; y--) {
                if (world->get_voxel(x, y, z) != MAT_AIR) {
                    surface_y = y;
                    break;
                }
            }

            // Carve downward
            for (int d = 0; d < depth; d++) {
                int y = surface_y - d;
                if (y >= 0) {
                    world->set_voxel(x, y, z, MAT_AIR);
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Test battlefield
// ═══════════════════════════════════════════════════════════════════════

// Helper: find terrain surface Y at a given XZ position
static int find_surface_y(VoxelWorld *world, int x, int z) {
    int max_y = world->get_world_size_y() - 1;
    for (int y = max_y; y >= 0; y--) {
        if (world->get_voxel(x, y, z) != MAT_AIR) {
            return y + 1;
        }
    }
    return 0;
}

// Helper: raise a natural hill with smooth falloff and layered materials.
static void stamp_hill(VoxelWorld *world, int cx, int cz, int radius, int peak_height) {
    if (radius <= 1 || peak_height <= 0) return;
    int sx = world->get_world_size_x();
    int sz = world->get_world_size_z();
    int max_y = world->get_world_size_y() - 1;

    int min_x = std::max(1, cx - radius);
    int max_x = std::min(sx - 2, cx + radius);
    int min_z = std::max(1, cz - radius);
    int max_z = std::min(sz - 2, cz + radius);
    float inv_r = 1.0f / (float)radius;

    for (int x = min_x; x <= max_x; x++) {
        for (int z = min_z; z <= max_z; z++) {
            float dx = (float)(x - cx);
            float dz = (float)(z - cz);
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist > (float)radius) continue;

            float t = 1.0f - dist * inv_r;
            // Sharper summit, softer toe.
            int add_h = (int)std::round((t * t) * (float)peak_height);
            if (add_h <= 0) continue;

            int surface = find_surface_y(world, x, z) - 1;
            if (surface < 1) continue;
            int top = std::min(max_y - 1, surface + add_h);

            for (int y = surface + 1; y <= top; y++) {
                uint8_t mat = MAT_DIRT;
                if (y < top - 2) {
                    mat = MAT_STONE;
                } else if (y == top) {
                    mat = MAT_GRASS;
                }
                world->set_voxel(x, y, z, mat);
            }
        }
    }
}

// Helper: paint top surface in a rectangle for visual/tactical readability.
static void paint_surface_rect(VoxelWorld *world, int x0, int z0, int x1, int z1, uint8_t mat) {
    int sx = world->get_world_size_x();
    int sz = world->get_world_size_z();
    int min_x = std::max(1, std::min(x0, x1));
    int max_x = std::min(sx - 2, std::max(x0, x1));
    int min_z = std::max(1, std::min(z0, z1));
    int max_z = std::min(sz - 2, std::max(z0, z1));

    for (int x = min_x; x <= max_x; x++) {
        for (int z = min_z; z <= max_z; z++) {
            int y = find_surface_y(world, x, z) - 1;
            if (y >= 0) {
                world->set_voxel(x, y, z, mat);
            }
        }
    }
}

// Helper: paint a winding lane with gravel/concrete.
static void paint_winding_lane(VoxelWorld *world, int start_x, int end_x, int center_z, int half_width, float waviness) {
    if (start_x > end_x) std::swap(start_x, end_x);
    int sx = world->get_world_size_x();
    int sz = world->get_world_size_z();
    start_x = std::max(1, start_x);
    end_x = std::min(sx - 2, end_x);

    for (int x = start_x; x <= end_x; x++) {
        float tt = (float)(x - start_x) / (float)std::max(1, end_x - start_x);
        int lane_z = center_z + (int)std::round(std::sin(tt * 6.28318f) * waviness);
        int z0 = std::max(1, lane_z - half_width);
        int z1 = std::min(sz - 2, lane_z + half_width);
        for (int z = z0; z <= z1; z++) {
            int y = find_surface_y(world, x, z) - 1;
            if (y < 0) continue;
            // Slightly rough edges for natural blending.
            bool edge = (z == z0 || z == z1);
            world->set_voxel(x, y, z, edge ? MAT_GRAVEL : MAT_CONCRETE);
        }
    }
}

// Helper: paint straight urban streets (axis-aligned) with shoulders.
static void paint_street_axis(VoxelWorld *world, int x0, int z0, int x1, int z1, int half_width) {
    int sx = world->get_world_size_x();
    int sz = world->get_world_size_z();
    x0 = std::clamp(x0, 1, sx - 2);
    x1 = std::clamp(x1, 1, sx - 2);
    z0 = std::clamp(z0, 1, sz - 2);
    z1 = std::clamp(z1, 1, sz - 2);

    if (std::abs(x1 - x0) >= std::abs(z1 - z0)) {
        if (x0 > x1) std::swap(x0, x1);
        int cz = z0;
        for (int x = x0; x <= x1; x++) {
            for (int z = std::max(1, cz - half_width); z <= std::min(sz - 2, cz + half_width); z++) {
                int y = find_surface_y(world, x, z) - 1;
                if (y < 0) continue;
                bool shoulder = (z == cz - half_width || z == cz + half_width);
                world->set_voxel(x, y, z, shoulder ? MAT_GRAVEL : MAT_CONCRETE);
            }
        }
    } else {
        if (z0 > z1) std::swap(z0, z1);
        int cx = x0;
        for (int z = z0; z <= z1; z++) {
            for (int x = std::max(1, cx - half_width); x <= std::min(sx - 2, cx + half_width); x++) {
                int y = find_surface_y(world, x, z) - 1;
                if (y < 0) continue;
                bool shoulder = (x == cx - half_width || x == cx + half_width);
                world->set_voxel(x, y, z, shoulder ? MAT_GRAVEL : MAT_CONCRETE);
            }
        }
    }
}

// Helper: stamp multiple staggered cover segments across a broad lane to break sightlines.
static void stamp_lane_chicanes(
        VoxelWorld *world,
        int center_x,
        int center_z,
        int count,
        int spacing,
        int segment_len,
        int lateral_span,
        bool along_x,
        uint8_t mat,
        int height) {
    int sx = world->get_world_size_x();
    int sz = world->get_world_size_z();
    for (int i = 0; i < count; i++) {
        int offset = i - count / 2;
        int cx = center_x + (along_x ? offset * spacing : ((i % 2 == 0) ? -lateral_span : lateral_span));
        int cz = center_z + (along_x ? ((i % 2 == 0) ? -lateral_span : lateral_span) : offset * spacing);
        cx = std::clamp(cx, 8, sx - 24);
        cz = std::clamp(cz, 8, sz - 24);
        int cy = find_surface_y(world, cx, cz);
        VoxelGenerator::generate_wall(world, cx, cy, cz, segment_len, height, 2, mat, along_x);
    }
}

void VoxelGenerator::generate_test_battlefield(VoxelWorld *world) {
    int sx = world->get_world_size_x();
    int sz = world->get_world_size_z();
    int center_x = sx / 2;
    int center_z = sz / 2;

    // Step 1: Generate a dramatic terrain base with stronger macro undulation.
    generate_terrain(world, 16, 14, 0.009f);

    // Step 1.5: Sculpt tactical elevation anchors (higher hills, ridges, and saddles).
    stamp_hill(world, center_x - 340, center_z - 220, 160, 24);
    stamp_hill(world, center_x + 320, center_z + 210, 150, 22);
    stamp_hill(world, center_x - 150, center_z + 300, 120, 18);
    stamp_hill(world, center_x + 180, center_z - 320, 130, 19);
    stamp_hill(world, center_x, center_z - 260, 90, 14);
    stamp_hill(world, center_x, center_z + 250, 95, 14);
    // Mid-map ridge belt to break long lines of fire.
    for (int i = -3; i <= 3; i++) {
        stamp_hill(world, center_x + i * 88, center_z - 85, 60, 9);
        stamp_hill(world, center_x + i * 88, center_z + 105, 58, 8);
    }

    // Step 2: Central urban district — rows of large buildings with street gaps.
    for (int i = -3; i <= 3; i++) {
        int bx = center_x + i * 48 - 16;
        int bz = center_z - 12;
        int by = find_surface_y(world, bx + 16, bz + 12);
        generate_building(world, bx, by, bz, 32, 24, 24, MAT_BRICK, MAT_CONCRETE, true, true);
    }
    for (int i = -2; i <= 2; i++) {
        int bx = center_x + i * 62 - 14;
        int bz = center_z + 56;
        int by = find_surface_y(world, bx + 10, bz + 8);
        generate_building(world, bx, by, bz, 22, 18, 18, MAT_CONCRETE, MAT_CONCRETE, true, true);
    }

    // Step 3: Four village clusters at quadrant centers
    int quad_offsets[4][2] = {
        {center_x - sx / 4, center_z - sz / 4},  // NW
        {center_x + sx / 4, center_z - sz / 4},  // NE
        {center_x - sx / 4, center_z + sz / 4},  // SW
        {center_x + sx / 4, center_z + sz / 4},  // SE
    };
    uint8_t village_mats[4] = {MAT_BRICK, MAT_CONCRETE, MAT_WOOD, MAT_STONE};

    for (int q = 0; q < 4; q++) {
        int qx = quad_offsets[q][0];
        int qz = quad_offsets[q][1];

        // 3x2 grid of buildings per village
        for (int bxi = -1; bxi <= 1; bxi++) {
            for (int bzi = -1; bzi <= 0; bzi++) {
                int bx = qx + bxi * 44 - 10;
                int bz = qz + bzi * 36 - 8;
                if (bx < 4 || bx + 24 >= sx - 4 || bz < 4 || bz + 20 >= sz - 4) continue;
                int by = find_surface_y(world, bx + 10, bz + 8);
                generate_building(world, bx, by, bz, 24, 20, 20,
                                  village_mats[q], MAT_CONCRETE, true, true);
            }
        }
    }

    // Step 4: Flanking buildings along center axis
    for (int side = -1; side <= 1; side += 2) {
        for (int row = 1; row <= 3; row++) {
            int fz = center_z + side * row * 100;
            if (fz < 20 || fz + 20 >= sz - 20) continue;
            for (int i = 0; i < 5; i++) {
                int bx = center_x - 120 + i * 60;
                int by = find_surface_y(world, bx + 10, fz + 8);
                generate_building(world, bx, by, fz, 20, 16, 16,
                                  MAT_CONCRETE, MAT_CONCRETE, true, true);
            }
        }
    }

    // Step 5: Front line sandbag walls (team 1 = west, team 2 = east)
    int team1_x = center_x - 320;
    int team2_x = center_x + 320;
    for (int i = 0; i < 16; i++) {
        int wz = center_z - 180 + i * 24;
        if (wz < 4 || wz + 16 >= sz - 4) continue;

        // Team 1
        int wy1 = find_surface_y(world, team1_x, wz);
        generate_wall(world, team1_x, wy1, wz, 16, 4, 2, MAT_SANDBAG, false);

        // Team 2
        int wy2 = find_surface_y(world, team2_x, wz);
        generate_wall(world, team2_x, wy2, wz, 16, 4, 2, MAT_SANDBAG, false);
    }

    // Step 6: Secondary cover walls between front line and center
    for (int offset = -1; offset <= 1; offset += 2) {
        int wx = center_x + offset * 200;
        for (int i = 0; i < 8; i++) {
            int wz = center_z - 100 + i * 28;
            if (wz < 4 || wz + 12 >= sz - 4) continue;
            int wy = find_surface_y(world, wx, wz);
            generate_wall(world, wx, wy, wz, 12, 3, 2, MAT_SANDBAG, true);
        }
    }

    // Step 6.5: Centerline denial blocks - intentionally suppress frontal lane certainty.
    for (int i = -5; i <= 5; i++) {
        int bx = center_x + i * 54;
        int bz = center_z + ((i % 2 == 0) ? -18 : 18);
        int by = find_surface_y(world, bx, bz);
        generate_wall(world, bx, by, bz, 14, 5, 2, MAT_CONCRETE, i % 2 == 0);
    }

    // Step 7: Trench networks + low-ground approaches
    // Central trenches
    generate_trench(world, center_x - 100, center_z - 40, 80, 6, 4, false);
    generate_trench(world, center_x + 80, center_z - 40, 80, 6, 4, false);

    // Flanking trenches near front lines
    generate_trench(world, team1_x + 40, center_z - 60, 120, 5, 3, false);
    generate_trench(world, team2_x - 60, center_z - 60, 120, 5, 3, false);

    // Cross-trenches connecting positions
    generate_trench(world, center_x - 200, center_z - 2, 100, 5, 3, true);
    generate_trench(world, center_x + 100, center_z - 2, 100, 5, 3, true);
    // Wider shallow depressions for infantry movement lanes.
    for (int i = -3; i <= 3; i++) {
        generate_trench(world, center_x - 260 + i * 85, center_z + 46, 42, 3, 6, true);
        generate_trench(world, center_x - 220 + i * 85, center_z - 58, 42, 3, 6, true);
    }

    // Step 7.5: Stealth flank corridors near north/south edges (with intermittent hard cover).
    for (int side = -1; side <= 1; side += 2) {
        int edge_z = center_z + side * 300;
        // Carve a shallow but wide path.
        for (int i = -7; i <= 7; i++) {
            generate_trench(world, center_x + i * 58, edge_z, 36, 4, 8, true);
        }
        // Add staggered sandbag/wood cover pockets along the corridor.
        for (int i = -10; i <= 10; i++) {
            int wx = center_x + i * 42;
            int wz = edge_z + ((i % 2 == 0) ? -10 : 10);
            int wy = find_surface_y(world, wx, wz);
            generate_wall(world, wx, wy, wz, 9, 3, 2, (i % 3 == 0) ? MAT_WOOD : MAT_SANDBAG, i % 2 == 0);
        }
    }

    // Step 8: Steel wall compounds (industrial areas)
    for (int side = -1; side <= 1; side += 2) {
        int cx = center_x + side * 400;
        int cz = center_z;
        if (cx < 40 || cx + 60 >= sx - 40) continue;

        int wy = find_surface_y(world, cx + 30, cz);
        // Outer walls
        generate_wall(world, cx, wy, cz - 30, 60, 8, 2, MAT_STEEL, true);       // south
        generate_wall(world, cx, wy, cz + 30, 60, 8, 2, MAT_STEEL, true);       // north
        generate_wall(world, cx, wy, cz - 30, 60, 8, 2, MAT_STEEL, false);      // west
        generate_wall(world, cx + 58, wy, cz - 30, 60, 8, 2, MAT_STEEL, false); // east
    }

    // Step 9: Scattered cover in no-man's land (denser clusters + staggered occluders)
    // a) Sandbag barricade clusters across the engagement zone
    int nm_start = team1_x + 100;
    int nm_width = team2_x - team1_x - 200;
    if (nm_width > 0) {
        for (int i = 0; i < 36; i++) {
            int cx = nm_start + (i * 53) % nm_width;
            int cz = center_z - 160 + (i * 71) % 320;
            if (cx < 4 || cx + 10 >= sx - 4 || cz < 4 || cz + 10 >= sz - 4) continue;
            int cy = find_surface_y(world, cx, cz);
            generate_wall(world, cx, cy, cz, 8, 3, 2, MAT_SANDBAG, (i % 2 == 0));
        }
    }

    // b) Concrete rubble/barriers near center
    for (int i = 0; i < 28; i++) {
        int cx = center_x - 150 + (i * 37) % 300;
        int cz = center_z - 120 + (i * 47) % 240;
        if (cx < 4 || cx + 8 >= sx - 4 || cz < 4 || cz + 8 >= sz - 4) continue;
        int cy = find_surface_y(world, cx, cz);
        generate_wall(world, cx, cy, cz, 6, 4, 3, MAT_CONCRETE, (i % 3 == 0));
    }

    // c) Wood fence lines at flanks
    for (int i = 0; i < 14; i++) {
        int cx = center_x - 250 + i * 70;
        int cz = center_z + ((i % 2 == 0) ? -80 : 80);
        if (cx < 4 || cx + 16 >= sx - 4 || cz < 4 || cz + 16 >= sz - 4) continue;
        int cy = find_surface_y(world, cx, cz);
        generate_wall(world, cx, cy, cz, 14, 3, 1, MAT_WOOD, (i % 2 == 0));
    }

    // Step 10: Street network and boulevards (narrower to reduce open kill lanes).
    // Main avenues (east-west)
    paint_street_axis(world, center_x - 640, center_z - 8, center_x + 640, center_z - 8, 3);
    paint_street_axis(world, center_x - 620, center_z + 92, center_x + 620, center_z + 92, 3);
    // Vertical connectors (north-south)
    paint_street_axis(world, center_x - 220, center_z - 420, center_x - 220, center_z + 420, 2);
    paint_street_axis(world, center_x + 210, center_z - 420, center_x + 210, center_z + 420, 2);
    paint_street_axis(world, center_x + 16, center_z - 380, center_x + 16, center_z + 380, 2);
    // Inner district side streets.
    for (int j = -2; j <= 2; j++) {
        paint_street_axis(world, center_x - 180, center_z + j * 62, center_x + 180, center_z + j * 62, 1);
    }

    // Step 11: Surface readability + narrower travel lanes + dense lane occluders.
    paint_winding_lane(world, center_x - 420, center_x + 420, center_z - 12, 2, 18.0f);
    paint_winding_lane(world, center_x - 420, center_x + 420, center_z + 56, 2, 14.0f);
    // Keep objective areas readable, but smaller and less open than before.
    paint_surface_rect(world, center_x - 48, center_z - 36, center_x + 48, center_z + 36, MAT_GRAVEL);
    paint_surface_rect(world, center_x - 500, center_z - 52, center_x - 440, center_z + 52, MAT_GRAVEL);
    paint_surface_rect(world, center_x + 440, center_z - 52, center_x + 500, center_z + 52, MAT_GRAVEL);
    // Roundabout-like center landmark.
    paint_surface_rect(world, center_x - 18, center_z + 28, center_x + 18, center_z + 62, MAT_CONCRETE);
    int rb_y = find_surface_y(world, center_x, center_z + 46);
    generate_building(world, center_x - 6, rb_y, center_z + 38, 12, 12, 12, MAT_STONE, MAT_CONCRETE, false, false);

    // Step 12: Chicanes and cross-cover to intentionally break long LOS on every major lane.
    stamp_lane_chicanes(world, center_x, center_z - 8, 15, 82, 12, 16, false, MAT_CONCRETE, 4);
    stamp_lane_chicanes(world, center_x, center_z + 92, 13, 88, 10, 14, false, MAT_SANDBAG, 3);
    stamp_lane_chicanes(world, center_x - 220, center_z, 9, 84, 10, 10, true, MAT_SANDBAG, 3);
    stamp_lane_chicanes(world, center_x + 210, center_z, 9, 84, 10, 10, true, MAT_CONCRETE, 4);

    // Step 13: Additional flank corridors and connector trenches near map edges.
    for (int side = -1; side <= 1; side += 2) {
        int edge_z = center_z + side * 260;
        for (int i = -5; i <= 5; i++) {
            int x0 = center_x + i * 70;
            generate_trench(world, x0, edge_z, 34, 4, 6, true);
        }
    }

    UtilityFunctions::print("[VoxelGenerator] Test battlefield generated (",
                            sx, "x", sz, " voxels)");
}
