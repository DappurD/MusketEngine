#include "structural_integrity.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <cstring>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════════
//  GDScript Binding
// ═══════════════════════════════════════════════════════════════════════

void StructuralIntegrity::_bind_methods() {
    ClassDB::bind_method(D_METHOD("detect_islands", "world", "destruction_center", "search_radius"),
                         &StructuralIntegrity::detect_islands);
    ClassDB::bind_method(D_METHOD("detect_weakened_voxels", "world", "center", "search_radius"),
                         &StructuralIntegrity::detect_weakened_voxels);
}

// ═══════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════

Array StructuralIntegrity::detect_islands(VoxelWorld *world, const Vector3 &destruction_center, float search_radius) {
    Array result;
    if (!world || !world->is_initialized()) return result;

    float vscale = world->get_voxel_scale();
    float inv_scale = 1.0f / vscale;
    Vector3i vc = world->world_to_voxel(destruction_center);
    int vr = (int)std::ceil(search_radius * inv_scale) + (int)std::ceil(24.0f * inv_scale); // expand ~24m for reliable ground detection

    // Compute chunk search range
    int cx_min = std::max(0, (vc.x - vr) >> 5);
    int cx_max = std::min(world->get_chunks_x() - 1, (vc.x + vr) >> 5);
    int cy_min = 0; // always include ground
    int cy_max = std::min(world->get_chunks_y() - 1, (vc.y + vr) >> 5);
    int cz_min = std::max(0, (vc.z - vr) >> 5);
    int cz_max = std::min(world->get_chunks_z() - 1, (vc.z + vr) >> 5);

    // Phase A: Find ungrounded chunks
    auto ungrounded = _find_ungrounded_chunks(world, cx_min, cx_max, cy_min, cy_max, cz_min, cz_max);
    if (ungrounded.empty()) return result;

    // Phase B: Extract islands via voxel-level flood-fill
    auto islands = _extract_islands(world, ungrounded,
                                     world->get_chunks_x(), world->get_chunks_y(), world->get_chunks_z());

    // Phase C: For each island, mesh it and erase from world
    // Island size limits scale with voxel resolution:
    // At 0.25m: min=4, max=2000 (~31 m³). At 0.1m: min=62, max=31250.
    float voxel_vol = vscale * vscale * vscale;
    int min_island = std::max(4, (int)(0.0625f / voxel_vol));   // ~0.06 m³ minimum
    int max_island = (int)(31.25f / voxel_vol);                  // ~31 m³ maximum
    for (auto &island : islands) {
        if (island.voxel_count < min_island) continue; // too small, skip
        if (island.voxel_count > max_island) continue; // too large = likely false positive

        // Erase island voxels from world
        for (int i = 0; i < (int)island.voxel_positions.size(); i++) {
            auto &p = island.voxel_positions[i];
            world->set_voxel(p.x, p.y, p.z, 0); // MAT_AIR
        }

        // Mesh the island
        Array mesh_arrays = _mesh_island(island);

        // Pack voxel data for GDScript re-solidification
        PackedVector3Array voxel_pos_packed;
        voxel_pos_packed.resize(island.voxel_count);
        PackedByteArray voxel_mat_packed;
        voxel_mat_packed.resize(island.voxel_count);
        for (int i = 0; i < island.voxel_count; i++) {
            auto &p = island.voxel_positions[i];
            voxel_pos_packed.set(i, Vector3(p.x, p.y, p.z));
            voxel_mat_packed.set(i, island.voxel_materials[i]);
        }

        Dictionary d;
        d["center"] = island.center_of_mass;
        d["mass"] = island.total_mass;
        d["voxel_count"] = island.voxel_count;
        d["mesh_arrays"] = mesh_arrays;
        d["bounds_min"] = island.bounds_min;
        d["bounds_max"] = island.bounds_max;
        d["voxel_positions"] = voxel_pos_packed;
        d["voxel_materials"] = voxel_mat_packed;
        result.push_back(d);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════
//  Phase A: Chunk-Level Connectivity
// ═══════════════════════════════════════════════════════════════════════

bool StructuralIntegrity::_chunks_connected(VoxelWorld *world, int cx1, int cy1, int cz1,
                                             int cx2, int cy2, int cz2) {
    const VoxelChunk *c1 = world->get_chunk(cx1, cy1, cz1);
    const VoxelChunk *c2 = world->get_chunk(cx2, cy2, cz2);
    if (!c1 || !c2) return false;

    // Determine shared face and check for solid voxel pairs
    int dx = cx2 - cx1, dy = cy2 - cy1, dz = cz2 - cz1;
    constexpr int CS = 32;

    // Check every voxel on the shared face — thin supports (1-3 voxels) must not be missed.
    // 32x32 = 1024 checks per face, still <10μs per pair.
    if (dx == 1) {
        for (int z = 0; z < CS; z++) {
            for (int y = 0; y < CS; y++) {
                if (is_material_solid(c1->get(31, y, z)) && is_material_solid(c2->get(0, y, z)))
                    return true;
            }
        }
    } else if (dx == -1) {
        for (int z = 0; z < CS; z++) {
            for (int y = 0; y < CS; y++) {
                if (is_material_solid(c1->get(0, y, z)) && is_material_solid(c2->get(31, y, z)))
                    return true;
            }
        }
    } else if (dy == 1) {
        for (int z = 0; z < CS; z++) {
            for (int x = 0; x < CS; x++) {
                if (is_material_solid(c1->get(x, 31, z)) && is_material_solid(c2->get(x, 0, z)))
                    return true;
            }
        }
    } else if (dy == -1) {
        for (int z = 0; z < CS; z++) {
            for (int x = 0; x < CS; x++) {
                if (is_material_solid(c1->get(x, 0, z)) && is_material_solid(c2->get(x, 31, z)))
                    return true;
            }
        }
    } else if (dz == 1) {
        for (int x = 0; x < CS; x++) {
            for (int y = 0; y < CS; y++) {
                if (is_material_solid(c1->get(x, y, 31)) && is_material_solid(c2->get(x, y, 0)))
                    return true;
            }
        }
    } else if (dz == -1) {
        for (int x = 0; x < CS; x++) {
            for (int y = 0; y < CS; y++) {
                if (is_material_solid(c1->get(x, y, 0)) && is_material_solid(c2->get(x, y, 31)))
                    return true;
            }
        }
    }
    return false;
}

std::vector<int> StructuralIntegrity::_find_ungrounded_chunks(
    VoxelWorld *world, int cx_min, int cx_max, int cy_min, int cy_max, int cz_min, int cz_max)
{
    int chunks_x = world->get_chunks_x();
    int chunks_y = world->get_chunks_y();
    int chunks_z = world->get_chunks_z();

    // Flat index for chunks in search range
    auto range_idx = [&](int cx, int cy, int cz) -> int {
        return cz * (chunks_x * chunks_y) + cx * chunks_y + cy;
    };

    int total = chunks_x * chunks_y * chunks_z;
    std::vector<bool> has_solid(total, false);
    std::vector<bool> grounded(total, false);

    // First pass: mark chunks that contain solid voxels
    for (int cz = cz_min; cz <= cz_max; cz++) {
        for (int cx = cx_min; cx <= cx_max; cx++) {
            for (int cy = cy_min; cy <= cy_max; cy++) {
                const VoxelChunk *chunk = world->get_chunk(cx, cy, cz);
                if (!chunk) continue;
                // Quick check: scan for any solid voxel
                bool found = false;
                for (int z = 0; z < 32 && !found; z += 8) {
                    for (int x = 0; x < 32 && !found; x += 8) {
                        for (int y = 0; y < 32 && !found; y += 8) {
                            if (is_material_solid(chunk->get(x, y, z))) found = true;
                        }
                    }
                }
                // If sparse check found nothing, do full scan
                if (!found) {
                    for (int z = 0; z < 32 && !found; z++) {
                        for (int x = 0; x < 32 && !found; x++) {
                            for (int y = 0; y < 32 && !found; y++) {
                                if (is_material_solid(chunk->get(x, y, z))) found = true;
                            }
                        }
                    }
                }
                has_solid[range_idx(cx, cy, cz)] = found;
            }
        }
    }

    // BFS from all ground-level chunks (cy=0) that have solid voxels
    std::queue<int> bfs;
    for (int cz = cz_min; cz <= cz_max; cz++) {
        for (int cx = cx_min; cx <= cx_max; cx++) {
            int idx = range_idx(cx, 0, cz);
            if (has_solid[idx]) {
                grounded[idx] = true;
                bfs.push(idx);
            }
        }
    }

    // 6-connected BFS through chunk graph
    while (!bfs.empty()) {
        int idx = bfs.front(); bfs.pop();

        // Recover cx,cy,cz from flat index
        int cz = idx / (chunks_x * chunks_y);
        int rem = idx % (chunks_x * chunks_y);
        int cx = rem / chunks_y;
        int cy = rem % chunks_y;

        // Check 6 neighbors
        static const int d6[][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (auto &d : d6) {
            int nx = cx + d[0], ny = cy + d[1], nz = cz + d[2];
            if (nx < cx_min || nx > cx_max || ny < cy_min || ny > cy_max || nz < cz_min || nz > cz_max)
                continue;
            int ni = range_idx(nx, ny, nz);
            if (grounded[ni] || !has_solid[ni]) continue;

            if (_chunks_connected(world, cx, cy, cz, nx, ny, nz)) {
                grounded[ni] = true;
                bfs.push(ni);
            }
        }
    }

    // Collect ungrounded chunks
    std::vector<int> ungrounded;
    for (int cz = cz_min; cz <= cz_max; cz++) {
        for (int cx = cx_min; cx <= cx_max; cx++) {
            for (int cy = cy_min; cy <= cy_max; cy++) {
                int idx = range_idx(cx, cy, cz);
                if (has_solid[idx] && !grounded[idx]) {
                    ungrounded.push_back(idx);
                }
            }
        }
    }
    return ungrounded;
}

// ═══════════════════════════════════════════════════════════════════════
//  Phase B: Voxel-Level Island Extraction
// ═══════════════════════════════════════════════════════════════════════

std::vector<StructuralIntegrity::IslandData> StructuralIntegrity::_extract_islands(
    VoxelWorld *world, const std::vector<int> &ungrounded_chunk_indices,
    int chunks_x, int chunks_y, int chunks_z)
{
    std::vector<IslandData> islands;

    // Collect all voxel positions in ungrounded chunks
    std::unordered_set<uint64_t> unvisited;
    auto pack_key = [](int x, int y, int z) -> uint64_t {
        return ((uint64_t)(uint16_t)x << 32) | ((uint64_t)(uint16_t)y << 16) | (uint16_t)z;
    };

    for (int idx : ungrounded_chunk_indices) {
        int cz = idx / (chunks_x * chunks_y);
        int rem = idx % (chunks_x * chunks_y);
        int cx = rem / chunks_y;
        int cy = rem % chunks_y;

        int base_x = cx * 32, base_y = cy * 32, base_z = cz * 32;
        const VoxelChunk *chunk = world->get_chunk(cx, cy, cz);
        if (!chunk) continue;

        for (int z = 0; z < 32; z++) {
            for (int x = 0; x < 32; x++) {
                for (int y = 0; y < 32; y++) {
                    uint8_t mat = chunk->get(x, y, z);
                    if (is_material_solid(mat)) {
                        unvisited.insert(pack_key(base_x + x, base_y + y, base_z + z));
                    }
                }
            }
        }
    }

    // Flood-fill to group into connected islands
    static const int d6[][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    float vscale = world->get_voxel_scale();

    while (!unvisited.empty()) {
        uint64_t start_key = *unvisited.begin();
        int sx = (int)(int16_t)(uint16_t)(start_key >> 32);
        int sy = (int)(int16_t)(uint16_t)(start_key >> 16);
        int sz = (int)(int16_t)(uint16_t)(start_key);

        IslandData island;
        island.bounds_min = Vector3i(sx, sy, sz);
        island.bounds_max = Vector3i(sx, sy, sz);

        std::queue<uint64_t> q;
        q.push(start_key);
        unvisited.erase(start_key);

        float com_x = 0, com_y = 0, com_z = 0;

        while (!q.empty()) {
            uint64_t key = q.front(); q.pop();
            int vx = (int)(int16_t)(uint16_t)(key >> 32);
            int vy = (int)(int16_t)(uint16_t)(key >> 16);
            int vz = (int)(int16_t)(uint16_t)(key);

            uint8_t mat = world->get_voxel_fast(vx, vy, vz);
            float density = get_material_density(mat);

            island.voxel_positions.push_back(Vector3i(vx, vy, vz));
            island.voxel_materials.push_back(mat);
            island.voxel_count++;
            island.total_mass += density;

            com_x += vx * density;
            com_y += vy * density;
            com_z += vz * density;

            island.bounds_min.x = std::min(island.bounds_min.x, vx);
            island.bounds_min.y = std::min(island.bounds_min.y, vy);
            island.bounds_min.z = std::min(island.bounds_min.z, vz);
            island.bounds_max.x = std::max(island.bounds_max.x, vx);
            island.bounds_max.y = std::max(island.bounds_max.y, vy);
            island.bounds_max.z = std::max(island.bounds_max.z, vz);

            // Expand to 6 neighbors
            for (auto &d : d6) {
                int nx = vx + d[0], ny = vy + d[1], nz = vz + d[2];
                uint64_t nkey = pack_key(nx, ny, nz);
                auto it = unvisited.find(nkey);
                if (it != unvisited.end()) {
                    unvisited.erase(it);
                    q.push(nkey);
                } else if (world->is_solid(nx, ny, nz)) {
                    // Neighbor is solid but wasn't in ungrounded set —
                    // this island actually connects to grounded voxels.
                    // Abort: this is NOT a floating island.
                    // Drain the queue and discard.
                    while (!q.empty()) q.pop();
                    island.voxel_count = 0; // mark invalid
                    break;
                }
            }
            if (island.voxel_count == 0) break;
        }

        if (island.voxel_count > 0 && island.total_mass > 0.0f) {
            island.center_of_mass = Vector3(
                (com_x / island.total_mass) * vscale,
                (com_y / island.total_mass) * vscale,
                (com_z / island.total_mass) * vscale
            );
            islands.push_back(std::move(island));
        }
    }

    return islands;
}

// ═══════════════════════════════════════════════════════════════════════
//  Phase C: Island Meshing
// ═══════════════════════════════════════════════════════════════════════

Array StructuralIntegrity::_mesh_island(const IslandData &island) {
    // Create a padded voxel array for the island bounding box
    int sx = island.bounds_max.x - island.bounds_min.x + 1;
    int sy = island.bounds_max.y - island.bounds_min.y + 1;
    int sz = island.bounds_max.z - island.bounds_min.z + 1;

    // Padded dimensions
    int px = sx + 2, py = sy + 2, pz = sz + 2;
    int padded_size = pz * px * py;

    std::vector<uint8_t> padded(padded_size, 0); // all air

    // Fill in island voxels (offset by +1 for padding)
    for (int i = 0; i < (int)island.voxel_positions.size(); i++) {
        auto &pos = island.voxel_positions[i];
        int lx = pos.x - island.bounds_min.x + 1;
        int ly = pos.y - island.bounds_min.y + 1;
        int lz = pos.z - island.bounds_min.z + 1;
        // ZXY order like the mesher expects
        padded[lz * (px * py) + lx * py + ly] = island.voxel_materials[i];
    }

    // If island fits in one chunk (32x32x32), use the existing mesher directly
    if (sx <= 32 && sy <= 32 && sz <= 32) {
        // Repack into CS_P format (34x34x34)
        constexpr int CS_P = VoxelMesherBlocky::CS_P;
        constexpr int CS_P2 = VoxelMesherBlocky::CS_P2;
        constexpr int CS_P3 = VoxelMesherBlocky::CS_P3;

        uint8_t mesher_padded[CS_P3];
        std::memset(mesher_padded, 0, CS_P3);

        for (int z = 0; z < pz && z < CS_P; z++) {
            for (int x = 0; x < px && x < CS_P; x++) {
                for (int y = 0; y < py && y < CS_P; y++) {
                    mesher_padded[z * CS_P2 + x * CS_P + y] = padded[z * (px * py) + x * py + y];
                }
            }
        }

        auto mesh = VoxelMesherBlocky::mesh_chunk(mesher_padded);
        if (!mesh.empty) {
            return VoxelMesherBlocky::to_godot_arrays(mesh);
        }
    } else {
        // Large island: mesh in sub-chunks
        // For now, mesh the first 32x32x32 portion (covers most cases)
        // TODO: multi-chunk island meshing for very large islands
        constexpr int CS_P = VoxelMesherBlocky::CS_P;
        constexpr int CS_P2 = VoxelMesherBlocky::CS_P2;
        constexpr int CS_P3 = VoxelMesherBlocky::CS_P3;

        uint8_t mesher_padded[CS_P3];
        std::memset(mesher_padded, 0, CS_P3);

        int copy_x = std::min(px, (int)CS_P);
        int copy_y = std::min(py, (int)CS_P);
        int copy_z = std::min(pz, (int)CS_P);

        for (int z = 0; z < copy_z; z++) {
            for (int x = 0; x < copy_x; x++) {
                for (int y = 0; y < copy_y; y++) {
                    mesher_padded[z * CS_P2 + x * CS_P + y] = padded[z * (px * py) + x * py + y];
                }
            }
        }

        auto mesh = VoxelMesherBlocky::mesh_chunk(mesher_padded);
        if (!mesh.empty) {
            return VoxelMesherBlocky::to_godot_arrays(mesh);
        }
    }

    return Array();
}

// ═══════════════════════════════════════════════════════════════════════
//  Support Propagation: BFS Distance from Ground
// ═══════════════════════════════════════════════════════════════════════

Array StructuralIntegrity::detect_weakened_voxels(VoxelWorld *world, const Vector3 &center, float search_radius) {
    Array result;
    if (!world || !world->is_initialized()) return result;

    float inv_scale = 1.0f / world->get_voxel_scale();
    Vector3i vc = world->world_to_voxel(center);
    int vr_base = (int)std::ceil(search_radius * inv_scale);
    // XZ: only expand slightly beyond blast for wall connectivity
    // Y: extend to ground (min_y=0) and well above for upper floors
    int vr_xz = vr_base + std::max(8, (int)std::ceil(8.0f * inv_scale));

    // Search bounds — always include ground (min_y=0)
    int world_sx = world->get_world_size_x();
    int world_sy = world->get_world_size_y();
    int world_sz = world->get_world_size_z();

    int min_x = std::max(0, vc.x - vr_xz);
    int max_x = std::min(world_sx - 1, vc.x + vr_xz);
    int min_y = 0; // always include ground for proper seeding
    int max_y = std::min(world_sy - 1, vc.y + vr_xz + 32); // extend upward generously
    int min_z = std::max(0, vc.z - vr_xz);
    int max_z = std::min(world_sz - 1, vc.z + vr_xz);

    int range_x = max_x - min_x + 1;
    int range_y = max_y - min_y + 1;
    int range_z = max_z - min_z + 1;
    int total = range_x * range_y * range_z;

    // Safety cap: don't process absurdly large volumes
    if (total > 2000000) return result;

    // Distance array: -1 = unvisited solid, -2 = not solid, >=0 = BFS distance
    std::vector<int> dist(total, -1);

    auto local_idx = [&](int x, int y, int z) -> int {
        return (z - min_z) * (range_x * range_y) + (x - min_x) * range_y + (y - min_y);
    };

    // First pass: mark non-solid voxels and seed ground-level solids
    std::queue<int> bfs;
    for (int z = min_z; z <= max_z; z++) {
        for (int x = min_x; x <= max_x; x++) {
            for (int y = min_y; y <= max_y; y++) {
                int idx = local_idx(x, y, z);
                if (!world->is_solid(x, y, z)) {
                    dist[idx] = -2; // not solid
                    continue;
                }
                // Seed: voxel at y=0 (absolute ground)
                if (y == 0) {
                    dist[idx] = 0;
                    bfs.push(idx);
                }
            }
        }
    }

    // BFS: propagate distance from ground seeds through solid voxels
    static const int d6[][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    while (!bfs.empty()) {
        int idx = bfs.front(); bfs.pop();
        int cur_dist = dist[idx];

        // Recover x,y,z from flat index
        int rz = idx / (range_x * range_y);
        int rem = idx % (range_x * range_y);
        int rx = rem / range_y;
        int ry = rem % range_y;
        int vx = rx + min_x, vy = ry + min_y, vz = rz + min_z;

        for (auto &d : d6) {
            int nx = vx + d[0], ny = vy + d[1], nz = vz + d[2];
            if (nx < min_x || nx > max_x || ny < min_y || ny > max_y || nz < min_z || nz > max_z)
                continue;
            int ni = local_idx(nx, ny, nz);
            if (dist[ni] != -1) continue; // already visited or not solid
            dist[ni] = cur_dist + 1;
            bfs.push(ni);
        }
    }

    // Collect weakened voxels: solid + distance exceeds material support limit
    // Only collect within the original blast radius on XZ (don't destroy neighboring buildings)
    // but allow full Y range (upper floors of the damaged building should collapse)
    int collect_r2 = (vr_base + 4) * (vr_base + 4);  // slightly beyond blast on XZ
    for (int z = min_z; z <= max_z; z++) {
        for (int x = min_x; x <= max_x; x++) {
            // Skip voxel columns too far from blast center on XZ
            int dx = x - vc.x, dz = z - vc.z;
            if (dx * dx + dz * dz > collect_r2) continue;

            for (int y = min_y; y <= max_y; y++) {
                int idx = local_idx(x, y, z);
                int d = dist[idx];
                if (d == -2) continue; // not solid
                if (d == -1) continue; // disconnected — detect_islands handles these

                if (d > 0) {
                    uint8_t mat = world->get_voxel_fast(x, y, z);
                    int max_dist = get_material_support_distance(mat, world->get_voxel_scale());
                    if (d > max_dist) {
                        Dictionary voxel;
                        voxel["position"] = Vector3i(x, y, z);
                        voxel["material"] = (int)mat;
                        voxel["distance"] = d;
                        result.push_back(voxel);
                    }
                }
            }
        }
    }

    return result;
}
