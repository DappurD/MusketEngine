#include "voxel_mesher_blocky.h"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/mesh.hpp>

#include <cstring>
#include <cmath>

#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_BitScanForward64)
#endif

using namespace godot;

// ═══════════════════════════════════════════════════════════════════════
//  Face normals
// ═══════════════════════════════════════════════════════════════════════

const float VoxelMesherBlocky::FACE_NORMALS[6][3] = {
    {  0.0f,  1.0f,  0.0f },  // FACE_POS_Y (+Y top)
    {  0.0f, -1.0f,  0.0f },  // FACE_NEG_Y (-Y bottom)
    {  1.0f,  0.0f,  0.0f },  // FACE_POS_X (+X right)
    { -1.0f,  0.0f,  0.0f },  // FACE_NEG_X (-X left)
    {  0.0f,  0.0f,  1.0f },  // FACE_POS_Z (+Z front)
    {  0.0f,  0.0f, -1.0f },  // FACE_NEG_Z (-Z back)
};

// ═══════════════════════════════════════════════════════════════════════
//  Bit-scan helper (find lowest set bit)
// ═══════════════════════════════════════════════════════════════════════

static inline int bit_scan_forward_64(uint64_t mask) {
#ifdef _MSC_VER
    unsigned long idx;
    _BitScanForward64(&idx, mask);
    return (int)idx;
#else
    return __builtin_ctzll(mask);
#endif
}

// ═══════════════════════════════════════════════════════════════════════
//  Build padded voxel array from chunk + neighbors
// ═══════════════════════════════════════════════════════════════════════

void VoxelMesherBlocky::build_padded_voxels(
        const VoxelChunk *neighbors[3][3][3],
        uint8_t *out_padded) {

    std::memset(out_padded, MAT_AIR, CS_P3);

    // Fill center chunk [1..32] from neighbors[1][1][1]
    const VoxelChunk *center = neighbors[1][1][1];
    if (!center) return;

    for (int z = 0; z < CS; z++) {
        for (int x = 0; x < CS; x++) {
            for (int y = 0; y < CS; y++) {
                out_padded[padded_idx(x + 1, y + 1, z + 1)] = center->get(x, y, z);
            }
        }
    }

    // Fill 6 face neighbors (padding slices)
    // -X face (x=0 in padded)
    if (const VoxelChunk *n = neighbors[0][1][1]) {
        for (int z = 0; z < CS; z++)
            for (int y = 0; y < CS; y++)
                out_padded[padded_idx(0, y + 1, z + 1)] = n->get(CS - 1, y, z);
    }
    // +X face (x=33 in padded)
    if (const VoxelChunk *n = neighbors[2][1][1]) {
        for (int z = 0; z < CS; z++)
            for (int y = 0; y < CS; y++)
                out_padded[padded_idx(CS + 1, y + 1, z + 1)] = n->get(0, y, z);
    }
    // -Y face (y=0 in padded)
    if (const VoxelChunk *n = neighbors[1][0][1]) {
        for (int z = 0; z < CS; z++)
            for (int x = 0; x < CS; x++)
                out_padded[padded_idx(x + 1, 0, z + 1)] = n->get(x, CS - 1, z);
    }
    // +Y face (y=33 in padded)
    if (const VoxelChunk *n = neighbors[1][2][1]) {
        for (int z = 0; z < CS; z++)
            for (int x = 0; x < CS; x++)
                out_padded[padded_idx(x + 1, CS + 1, z + 1)] = n->get(x, 0, z);
    }
    // -Z face (z=0 in padded)
    if (const VoxelChunk *n = neighbors[1][1][0]) {
        for (int x = 0; x < CS; x++)
            for (int y = 0; y < CS; y++)
                out_padded[padded_idx(x + 1, y + 1, 0)] = n->get(x, y, CS - 1);
    }
    // +Z face (z=33 in padded)
    if (const VoxelChunk *n = neighbors[1][1][2]) {
        for (int x = 0; x < CS; x++)
            for (int y = 0; y < CS; y++)
                out_padded[padded_idx(x + 1, y + 1, CS + 1)] = n->get(x, y, 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Build occupancy bitmask (one uint64 per XZ column, bits = Y axis)
// ═══════════════════════════════════════════════════════════════════════

void VoxelMesherBlocky::_build_opaque_mask(const uint8_t *padded, uint64_t *opaque_mask) {
    // opaque_mask layout: [z * CS_P + x], bit y = is_solid at (x, y, z) in padded space
    // We iterate all CS_P * CS_P columns
    for (int z = 0; z < CS_P; z++) {
        for (int x = 0; x < CS_P; x++) {
            uint64_t mask = 0;
            for (int y = 0; y < CS_P; y++) {
                if (is_material_solid(padded[padded_idx(x, y, z)])) {
                    mask |= (1ull << y);
                }
            }
            opaque_mask[z * CS_P + x] = mask;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Face culling via bitwise operations
// ═══════════════════════════════════════════════════════════════════════

void VoxelMesherBlocky::_cull_faces(const uint64_t *opaque_mask, uint64_t *face_masks) {
    // P_MASK: keep only bits 1..32 (the actual chunk voxels, not padding)
    constexpr uint64_t P_MASK = ((1ull << (CS + 1)) - 1) & ~1ull;  // bits 1..32

    std::memset(face_masks, 0, sizeof(uint64_t) * CS * CS * 6);

    // For faces where the boundary is along Y (same column): +Y and -Y
    // For faces where the boundary is across columns: +X, -X, +Z, -Z

    for (int z = 0; z < CS; z++) {
        int pz = z + 1;  // Padded z coordinate
        for (int x = 0; x < CS; x++) {
            int px = x + 1;  // Padded x coordinate
            int col_idx = pz * CS_P + px;
            uint64_t col = opaque_mask[col_idx] & P_MASK;

            int face_idx = z * CS + x;

            // +Y face: solid here, air above (y+1)
            // Shift column right by 1 = the voxel "above" in Y
            face_masks[face_idx + FACE_POS_Y * CS * CS] =
                (col & ~(opaque_mask[col_idx] >> 1)) >> 1;

            // -Y face: solid here, air below (y-1)
            face_masks[face_idx + FACE_NEG_Y * CS * CS] =
                (col & ~(opaque_mask[col_idx] << 1)) >> 1;

            // +X face: solid here, air at x+1
            face_masks[face_idx + FACE_POS_X * CS * CS] =
                (col & ~opaque_mask[pz * CS_P + (px + 1)]) >> 1;

            // -X face: solid here, air at x-1
            face_masks[face_idx + FACE_NEG_X * CS * CS] =
                (col & ~opaque_mask[pz * CS_P + (px - 1)]) >> 1;

            // +Z face: solid here, air at z+1
            face_masks[face_idx + FACE_POS_Z * CS * CS] =
                (col & ~opaque_mask[(pz + 1) * CS_P + px]) >> 1;

            // -Z face: solid here, air at z-1
            face_masks[face_idx + FACE_NEG_Z * CS * CS] =
                (col & ~opaque_mask[(pz - 1) * CS_P + px]) >> 1;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Emit a quad into the mesh output
// ═══════════════════════════════════════════════════════════════════════

void VoxelMesherBlocky::_emit_quad(
        ChunkMesh &mesh,
        float x0, float y0, float z0,
        float ax, float ay, float az,
        float bx, float by, float bz,
        float w, float h,
        float nx, float ny, float nz,
        uint8_t material,
        int ao0, int ao1, int ao2, int ao3,
        bool flip_winding) {

    int base = mesh.vertices.size();

    // 4 corners of the quad
    // v0 = origin
    // v1 = origin + A*w
    // v2 = origin + A*w + B*h
    // v3 = origin + B*h
    mesh.vertices.push_back(Vector3(x0, y0, z0));
    mesh.vertices.push_back(Vector3(x0 + ax * w, y0 + ay * w, z0 + az * w));
    mesh.vertices.push_back(Vector3(x0 + ax * w + bx * h, y0 + ay * w + by * h, z0 + az * w + bz * h));
    mesh.vertices.push_back(Vector3(x0 + bx * h, y0 + by * h, z0 + bz * h));

    Vector3 n(nx, ny, nz);
    mesh.normals.push_back(n);
    mesh.normals.push_back(n);
    mesh.normals.push_back(n);
    mesh.normals.push_back(n);

    // Vertex color: pure material color in RGB, material ID in alpha
    const MaterialProperties &mp = MATERIAL_TABLE[material < MAT_COUNT ? material : 0];
    float r = (float)mp.r / 255.0f;
    float g = (float)mp.g / 255.0f;
    float b = (float)mp.b / 255.0f;
    float mat_encoded = (float)material / 255.0f;
    Color base_color(r, g, b, mat_encoded);
    mesh.colors.push_back(base_color);
    mesh.colors.push_back(base_color);
    mesh.colors.push_back(base_color);
    mesh.colors.push_back(base_color);

    // UV2: raw AO in x (0.0=fully occluded, 1.0=fully lit), y reserved
    float ao_values[4] = {
        (float)ao0 / 3.0f,
        (float)ao1 / 3.0f,
        (float)ao2 / 3.0f,
        (float)ao3 / 3.0f,
    };
    mesh.uv2.push_back(Vector2(ao_values[0], 0.0f));
    mesh.uv2.push_back(Vector2(ao_values[1], 0.0f));
    mesh.uv2.push_back(Vector2(ao_values[2], 0.0f));
    mesh.uv2.push_back(Vector2(ao_values[3], 0.0f));

    // Triangulate with AO-based flip to avoid anisotropy artifacts.
    // flip_winding reverses triangle order for faces where A×B doesn't match the normal.
    if (ao0 + ao2 > ao1 + ao3) {
        if (!flip_winding) {
            // Flipped diagonal: (0,1,3), (1,2,3)
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 3);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 3);
        } else {
            // Reversed: (0,3,1), (1,3,2)
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 3);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 3);
            mesh.indices.push_back(base + 2);
        }
    } else {
        if (!flip_winding) {
            // Normal diagonal: (0,1,2), (0,2,3)
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 3);
        } else {
            // Reversed: (0,2,1), (0,3,2)
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 3);
            mesh.indices.push_back(base + 2);
        }
    }

    mesh.quad_count++;
}

// ═══════════════════════════════════════════════════════════════════════
//  Greedy merge + emit for one face direction
// ═══════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════
//  Compute AO for a face at voxel (vx, vy, vz) in padded coords
// ═══════════════════════════════════════════════════════════════════════

static void compute_face_ao(int face, const uint8_t *padded,
                            int px, int py, int pz,
                            int &ao0, int &ao1, int &ao2, int &ao3) {
    ao0 = ao1 = ao2 = ao3 = 3;

    #define S(x,y,z) is_material_solid(padded[VoxelMesherBlocky::padded_idx_s(x,y,z)])

    switch (face) {
        case VoxelMesherBlocky::FACE_POS_Y: {
            bool sxn = S(px-1, py+1, pz), sxp = S(px+1, py+1, pz);
            bool szn = S(px, py+1, pz-1), szp = S(px, py+1, pz+1);
            bool cnn = S(px-1, py+1, pz-1), cpn = S(px+1, py+1, pz-1);
            bool cpp = S(px+1, py+1, pz+1), cnp = S(px-1, py+1, pz+1);
            ao0 = VoxelMesherBlocky::vertex_ao_s(sxn, szn, cnn);
            ao1 = VoxelMesherBlocky::vertex_ao_s(sxp, szn, cpn);
            ao2 = VoxelMesherBlocky::vertex_ao_s(sxp, szp, cpp);
            ao3 = VoxelMesherBlocky::vertex_ao_s(sxn, szp, cnp);
        } break;
        case VoxelMesherBlocky::FACE_NEG_Y: {
            bool sxn = S(px-1, py-1, pz), sxp = S(px+1, py-1, pz);
            bool szn = S(px, py-1, pz-1), szp = S(px, py-1, pz+1);
            bool cnn = S(px-1, py-1, pz-1), cpn = S(px+1, py-1, pz-1);
            bool cpp = S(px+1, py-1, pz+1), cnp = S(px-1, py-1, pz+1);
            ao0 = VoxelMesherBlocky::vertex_ao_s(sxn, szn, cnn);
            ao1 = VoxelMesherBlocky::vertex_ao_s(sxp, szn, cpn);
            ao2 = VoxelMesherBlocky::vertex_ao_s(sxp, szp, cpp);
            ao3 = VoxelMesherBlocky::vertex_ao_s(sxn, szp, cnp);
        } break;
        case VoxelMesherBlocky::FACE_POS_X: {
            bool syn = S(px+1, py-1, pz), syp = S(px+1, py+1, pz);
            bool szn = S(px+1, py, pz-1), szp = S(px+1, py, pz+1);
            bool cnn = S(px+1, py-1, pz-1), cpn = S(px+1, py+1, pz-1);
            bool cpp = S(px+1, py+1, pz+1), cnp = S(px+1, py-1, pz+1);
            ao0 = VoxelMesherBlocky::vertex_ao_s(syn, szn, cnn);
            ao1 = VoxelMesherBlocky::vertex_ao_s(syp, szn, cpn);
            ao2 = VoxelMesherBlocky::vertex_ao_s(syp, szp, cpp);
            ao3 = VoxelMesherBlocky::vertex_ao_s(syn, szp, cnp);
        } break;
        case VoxelMesherBlocky::FACE_NEG_X: {
            bool syn = S(px-1, py-1, pz), syp = S(px-1, py+1, pz);
            bool szn = S(px-1, py, pz-1), szp = S(px-1, py, pz+1);
            bool cnn = S(px-1, py-1, pz-1), cpn = S(px-1, py+1, pz-1);
            bool cpp = S(px-1, py+1, pz+1), cnp = S(px-1, py-1, pz+1);
            ao0 = VoxelMesherBlocky::vertex_ao_s(syn, szn, cnn);
            ao1 = VoxelMesherBlocky::vertex_ao_s(syp, szn, cpn);
            ao2 = VoxelMesherBlocky::vertex_ao_s(syp, szp, cpp);
            ao3 = VoxelMesherBlocky::vertex_ao_s(syn, szp, cnp);
        } break;
        case VoxelMesherBlocky::FACE_POS_Z: {
            bool sxn = S(px-1, py, pz+1), sxp = S(px+1, py, pz+1);
            bool syn = S(px, py-1, pz+1), syp = S(px, py+1, pz+1);
            bool cnn = S(px-1, py-1, pz+1), cpn = S(px+1, py-1, pz+1);
            bool cpp = S(px+1, py+1, pz+1), cnp = S(px-1, py+1, pz+1);
            ao0 = VoxelMesherBlocky::vertex_ao_s(sxn, syn, cnn);
            ao1 = VoxelMesherBlocky::vertex_ao_s(sxp, syn, cpn);
            ao2 = VoxelMesherBlocky::vertex_ao_s(sxp, syp, cpp);
            ao3 = VoxelMesherBlocky::vertex_ao_s(sxn, syp, cnp);
        } break;
        case VoxelMesherBlocky::FACE_NEG_Z: {
            bool sxn = S(px-1, py, pz-1), sxp = S(px+1, py, pz-1);
            bool syn = S(px, py-1, pz-1), syp = S(px, py+1, pz-1);
            bool cnn = S(px-1, py-1, pz-1), cpn = S(px+1, py-1, pz-1);
            bool cpp = S(px+1, py+1, pz-1), cnp = S(px-1, py+1, pz-1);
            ao0 = VoxelMesherBlocky::vertex_ao_s(sxn, syn, cnn);
            ao1 = VoxelMesherBlocky::vertex_ao_s(sxp, syn, cpn);
            ao2 = VoxelMesherBlocky::vertex_ao_s(sxp, syp, cpp);
            ao3 = VoxelMesherBlocky::vertex_ao_s(sxn, syp, cnp);
        } break;
    }
    #undef S
}

void VoxelMesherBlocky::_greedy_merge_face(
        int face,
        const uint8_t *padded,
        const uint64_t *face_masks,
        ChunkMesh &out) {

    const float *norm = FACE_NORMALS[face];
    float nx = norm[0], ny = norm[1], nz = norm[2];

    // face_masks layout: [z * CS + x + face * CS * CS], bits 0..31 = Y positions
    // For each face direction, we iterate 32 "layers" (the axis perpendicular
    // to the face plane). For each layer, we populate a 32x32 grid of FaceCells
    // and perform greedy rectangular merging.

    // For +Y/-Y faces: layer = Y value (from bits), grid axes = X (col) x Z (row)
    // For +X/-X faces: layer = X value (from col), grid axes = Y (from bits) x Z (row)
    // For +Z/-Z faces: layer = Z value (from row), grid axes = X (col) x Y (from bits)

    FaceCell grid[CS][CS];  // [row][col] in the face plane

    // For all face types, face_masks[z*CS + x + face*CS*CS] bit y = face at (x,y,z)
    // Group faces by the coordinate along the face normal:
    // +Y/-Y: group by Y → layer = y, grid[z][x]
    // +X/-X: group by X → layer = x, grid[z][y]
    // +Z/-Z: group by Z → layer = z, grid[y][x]
    for (int layer = 0; layer < CS; layer++) {
        std::memset(grid, 0, sizeof(grid));
        bool any_cell = false;

        switch (face) {
            case FACE_POS_Y: case FACE_NEG_Y: {
                // Layer = Y. Grid [row=z][col=x].
                for (int z = 0; z < CS; z++) {
                    for (int x = 0; x < CS; x++) {
                        uint64_t bits = face_masks[z * CS + x + face * CS * CS];
                        if (!(bits & (1ull << layer))) continue;
                        int px = x + 1, py = layer + 1, pz = z + 1;
                        grid[z][x].material = padded[padded_idx(px, py, pz)];
                        compute_face_ao(face, padded, px, py, pz,
                                       grid[z][x].ao0, grid[z][x].ao1,
                                       grid[z][x].ao2, grid[z][x].ao3);
                        grid[z][x].visited = false;
                        any_cell = true;
                    }
                }
            } break;
            case FACE_POS_X: case FACE_NEG_X: {
                // Layer = X. Grid [row=z][col=y].
                int x = layer;
                for (int z = 0; z < CS; z++) {
                    uint64_t bits = face_masks[z * CS + x + face * CS * CS];
                    if (bits == 0) continue;
                    while (bits) {
                        int y = bit_scan_forward_64(bits);
                        bits &= bits - 1;
                        int px = x + 1, py = y + 1, pz = z + 1;
                        grid[z][y].material = padded[padded_idx(px, py, pz)];
                        compute_face_ao(face, padded, px, py, pz,
                                       grid[z][y].ao0, grid[z][y].ao1,
                                       grid[z][y].ao2, grid[z][y].ao3);
                        grid[z][y].visited = false;
                        any_cell = true;
                    }
                }
            } break;
            case FACE_POS_Z: case FACE_NEG_Z: {
                // Layer = Z. Grid [row=y][col=x].
                int z = layer;
                for (int x = 0; x < CS; x++) {
                    uint64_t bits = face_masks[z * CS + x + face * CS * CS];
                    if (bits == 0) continue;
                    while (bits) {
                        int y = bit_scan_forward_64(bits);
                        bits &= bits - 1;
                        int px = x + 1, py = y + 1, pz = z + 1;
                        grid[y][x].material = padded[padded_idx(px, py, pz)];
                        compute_face_ao(face, padded, px, py, pz,
                                       grid[y][x].ao0, grid[y][x].ao1,
                                       grid[y][x].ao2, grid[y][x].ao3);
                        grid[y][x].visited = false;
                        any_cell = true;
                    }
                }
            } break;
        }

        if (!any_cell) continue;

        // ── Greedy merge on the 32x32 grid ──
        for (int row = 0; row < CS; row++) {
            for (int col = 0; col < CS; col++) {
                FaceCell &cell = grid[row][col];
                if (cell.material == 0 || cell.visited) continue;

                uint8_t mat = cell.material;
                int8_t a0 = cell.ao0, a1 = cell.ao1, a2 = cell.ao2, a3 = cell.ao3;

                // Extend right (col direction)
                int w = 1;
                while (col + w < CS) {
                    FaceCell &next = grid[row][col + w];
                    if (next.material != mat || next.visited ||
                        next.ao0 != a0 || next.ao1 != a1 ||
                        next.ao2 != a2 || next.ao3 != a3)
                        break;
                    w++;
                }

                // Extend down (row direction)
                int h = 1;
                while (row + h < CS) {
                    bool row_ok = true;
                    for (int c = col; c < col + w; c++) {
                        FaceCell &next = grid[row + h][c];
                        if (next.material != mat || next.visited ||
                            next.ao0 != a0 || next.ao1 != a1 ||
                            next.ao2 != a2 || next.ao3 != a3) {
                            row_ok = false;
                            break;
                        }
                    }
                    if (!row_ok) break;
                    h++;
                }

                // Mark cells as visited
                for (int r = row; r < row + h; r++)
                    for (int c = col; c < col + w; c++)
                        grid[r][c].visited = true;

                // Convert grid (row, col) back to voxel coordinates and emit quad
                float fx, fy, fz;
                float fw = (float)w, fh = (float)h;

                // Vulkan CW front-face convention: triangle is front-facing when
                // viewed from the side OPPOSITE to its A×B cross product.
                // Flip winding where A×B points the SAME direction as the face normal:
                // NEG_Y: A×B=(0,-1,0), normal=(0,-1,0) → same → flip
                // POS_X: A×B=(+1,0,0), normal=(+1,0,0) → same → flip
                // POS_Z: A×B=(0,0,+1), normal=(0,0,+1) → same → flip
                switch (face) {
                    case FACE_POS_Y:
                        // Grid [z][x], layer = y. Origin = (col, layer+1, row)
                        fx = (float)col; fy = (float)(layer + 1); fz = (float)row;
                        _emit_quad(out, fx, fy, fz,
                                   1, 0, 0,  0, 0, 1,  fw, fh,
                                   nx, ny, nz, mat, a0, a1, a2, a3);
                        break;
                    case FACE_NEG_Y:
                        fx = (float)col; fy = (float)layer; fz = (float)row;
                        _emit_quad(out, fx, fy, fz,
                                   1, 0, 0,  0, 0, 1,  fw, fh,
                                   nx, ny, nz, mat, a0, a1, a2, a3, true);
                        break;
                    case FACE_POS_X:
                        // Grid [z][y], layer = x. Origin = (layer+1, col, row)
                        fx = (float)(layer + 1); fy = (float)col; fz = (float)row;
                        _emit_quad(out, fx, fy, fz,
                                   0, 1, 0,  0, 0, 1,  fw, fh,
                                   nx, ny, nz, mat, a0, a1, a2, a3, true);
                        break;
                    case FACE_NEG_X:
                        fx = (float)layer; fy = (float)col; fz = (float)row;
                        _emit_quad(out, fx, fy, fz,
                                   0, 1, 0,  0, 0, 1,  fw, fh,
                                   nx, ny, nz, mat, a0, a1, a2, a3);
                        break;
                    case FACE_POS_Z:
                        // Grid [y][x], layer = z. Origin = (col, row, layer+1)
                        fx = (float)col; fy = (float)row; fz = (float)(layer + 1);
                        _emit_quad(out, fx, fy, fz,
                                   1, 0, 0,  0, 1, 0,  fw, fh,
                                   nx, ny, nz, mat, a0, a1, a2, a3, true);
                        break;
                    case FACE_NEG_Z:
                        fx = (float)col; fy = (float)row; fz = (float)layer;
                        _emit_quad(out, fx, fy, fz,
                                   1, 0, 0,  0, 1, 0,  fw, fh,
                                   nx, ny, nz, mat, a0, a1, a2, a3);
                        break;
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Main meshing entry point
// ═══════════════════════════════════════════════════════════════════════

VoxelMesherBlocky::ChunkMesh VoxelMesherBlocky::mesh_chunk(const uint8_t *padded_voxels) {
    ChunkMesh result;
    result.empty = true;

    if (!padded_voxels) return result;

    // Stack-allocate working buffers
    uint64_t opaque_mask[CS_P2];
    uint64_t face_masks[CS * CS * FACE_COUNT];

    // Step 1: Build occupancy bitmask
    _build_opaque_mask(padded_voxels, opaque_mask);

    // Step 2: Cull hidden faces
    _cull_faces(opaque_mask, face_masks);

    // Check if any faces exist
    bool any_faces = false;
    for (int i = 0; i < CS * CS * FACE_COUNT; i++) {
        if (face_masks[i] != 0) { any_faces = true; break; }
    }
    if (!any_faces) return result;

    // Pre-allocate output arrays based on face count estimate
    int estimated_faces = 0;
    for (int i = 0; i < CS * CS * FACE_COUNT; i++) {
#ifdef _MSC_VER
        estimated_faces += (int)__popcnt64(face_masks[i]);
#else
        estimated_faces += __builtin_popcountll(face_masks[i]);
#endif
    }
    // With greedy merging, actual quads will be much fewer than faces,
    // but reserve conservatively (merging reduces by ~50-75%)
    int est_quads = estimated_faces / 3;  // Conservative: assume 3:1 merge ratio
    if (est_quads < 64) est_quads = 64;
    result.vertices.resize(0);
    result.normals.resize(0);
    result.colors.resize(0);
    result.indices.resize(0);

    // Step 3: For each face direction, greedy merge and emit
    for (int face = 0; face < FACE_COUNT; face++) {
        _greedy_merge_face(face, padded_voxels, face_masks, result);
    }

    result.empty = (result.quad_count == 0);
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
//  Convert to Godot mesh arrays
// ═══════════════════════════════════════════════════════════════════════

Array VoxelMesherBlocky::to_godot_arrays(const ChunkMesh &mesh) {
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);

    if (mesh.empty) return arrays;

    arrays[Mesh::ARRAY_VERTEX] = mesh.vertices;
    arrays[Mesh::ARRAY_NORMAL] = mesh.normals;
    arrays[Mesh::ARRAY_COLOR]  = mesh.colors;
    arrays[Mesh::ARRAY_TEX_UV2] = mesh.uv2;
    arrays[Mesh::ARRAY_INDEX]  = mesh.indices;

    return arrays;
}
