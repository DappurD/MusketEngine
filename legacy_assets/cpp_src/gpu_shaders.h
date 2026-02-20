#ifndef GPU_SHADERS_H
#define GPU_SHADERS_H

// Embedded GLSL compute shader sources for Phase 3: GPU Pressure Maps.
// Compiled to SPIR-V at runtime via RenderingDevice::shader_compile_spirv_from_source().

namespace godot {

// ═══════════════════════════════════════════════════════════════════════
//  Pressure Diffusion Shader (150x100 grid, 4m/cell)
//
//  Channels: R=Threat, G=Goal, B=Safety (cover), A=reserved
//  Pass 0: seed emitters + decay.  Passes 1-N: Jacobi diffusion.
//  Wall blocking via height map lookup.
// ═══════════════════════════════════════════════════════════════════════

static const char *PRESSURE_DIFFUSION_GLSL = R"glsl(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Binding 0: Height map (uint16 packed as uint32, 1m resolution)
layout(set = 0, binding = 0, std430) restrict readonly buffer HeightMap {
    uint data[];
} height_map;

// Binding 1: Unit data (vec4: x, y, z, team — team<0 = friendly, team>0 = enemy)
layout(set = 0, binding = 1, std430) restrict readonly buffer Units {
    vec4 units[];
} unit_data;

// Binding 2: Goal data (vec4: x, y, z, strength)
layout(set = 0, binding = 2, std430) restrict readonly buffer Goals {
    vec4 goals[];
} goal_data;

// Binding 3: Pressure input (read)
layout(set = 0, binding = 3, std430) restrict readonly buffer PressureIn {
    vec4 cells[];
} pressure_in;

// Binding 4: Pressure output (write)
layout(set = 0, binding = 4, std430) restrict writeonly buffer PressureOut {
    vec4 cells[];
} pressure_out;

layout(push_constant, std430) uniform Params {
    int grid_w;          // 150
    int grid_h;          // 100
    int pass_index;      // 0 = seed, 1+ = diffuse
    int num_friendlies;
    int num_enemies;
    int num_goals;
    float decay_rate;    // 0.15
    float diffusion_rate;// 0.25
    int standing_voxels_u; // 1.5m / voxel_scale (6 at 0.25m, 15 at 0.1m)
    int pad0;
} params;

// Extract uint16 height from packed uint32 buffer at 1m resolution
uint get_height_1m(int hx, int hz, int hmap_w, int hmap_h) {
    if (hx < 0 || hx >= hmap_w || hz < 0 || hz >= hmap_h) return 65535u;
    int idx = hz * hmap_w + hx;
    uint packed = height_map.data[idx / 2];
    return (packed >> ((idx & 1) * 16)) & 0xFFFFu;
}

// Check if diffusion should be blocked between two pressure cells
bool is_blocked(int ax, int az, int bx, int bz) {
    // Sample height map at center of each 4m pressure cell
    int hmap_w = params.grid_w * 4;
    int hmap_h = params.grid_h * 4;
    int ha_x = ax * 4 + 2;
    int ha_z = az * 4 + 2;
    int hb_x = bx * 4 + 2;
    int hb_z = bz * 4 + 2;

    uint ha = get_height_1m(ha_x, ha_z, hmap_w, hmap_h);
    uint hb = get_height_1m(hb_x, hb_z, hmap_w, hmap_h);

    // Block if the neighbor column is much taller (wall)
    uint sv = uint(params.standing_voxels_u);
    return (hb > ha + sv) || (ha > hb + sv);
}

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int z = int(gl_GlobalInvocationID.y);
    if (x >= params.grid_w || z >= params.grid_h) return;

    int idx = z * params.grid_w + x;

    float cell_size = 4.0; // meters per pressure cell
    float half_w = float(params.grid_w) * cell_size * 0.5;
    float half_h = float(params.grid_h) * cell_size * 0.5;
    float wx = (float(x) + 0.5) * cell_size - half_w;
    float wz = (float(z) + 0.5) * cell_size - half_h;

    if (params.pass_index == 0) {
        // ── Seed pass: decay + emit ─────────────────────────────
        vec4 prev = pressure_in.cells[idx];
        // Only decay threat (R) and goal (G); preserve gas density (B) and type (A)
        prev.r *= (1.0 - params.decay_rate);
        prev.g *= (1.0 - params.decay_rate);

        // Emit threat from enemy positions (stored after friendlies in unit buffer)
        float threat_emit = 0.0;
        int enemy_start = params.num_friendlies;
        int enemy_end = enemy_start + params.num_enemies;
        for (int i = enemy_start; i < enemy_end; i++) {
            vec4 u = unit_data.units[i];
            float dx = u.x - wx;
            float dz = u.z - wz;
            float d2 = dx * dx + dz * dz;
            if (d2 < cell_size * cell_size) {
                threat_emit += 5.0;
            } else if (d2 < 400.0) { // 20m radius seed
                threat_emit += 3.0 / (1.0 + d2 * 0.02);
            }
        }

        // Friendly presence reduces threat (counter-pressure)
        float friendly_counter = 0.0;
        for (int i = 0; i < params.num_friendlies; i++) {
            vec4 u = unit_data.units[i];
            float dx = u.x - wx;
            float dz = u.z - wz;
            float d2 = dx * dx + dz * dz;
            if (d2 < 400.0) {
                friendly_counter += 1.0 / (1.0 + d2 * 0.05);
            }
        }

        // Emit goal from objective positions
        float goal_emit = 0.0;
        for (int i = 0; i < params.num_goals; i++) {
            vec4 g = goal_data.goals[i];
            float dx = g.x - wx;
            float dz = g.z - wz;
            float d2 = dx * dx + dz * dz;
            if (d2 < cell_size * cell_size) {
                goal_emit += g.w; // strength
            } else if (d2 < 900.0) { // 30m radius seed
                goal_emit += g.w * 0.5 / (1.0 + d2 * 0.01);
            }
        }

        prev.r = max(prev.r, threat_emit - friendly_counter * 0.5);
        prev.r = max(prev.r, 0.0);
        prev.g = max(prev.g, goal_emit);
        // B channel (safety) written by cover shader, preserved here
        pressure_out.cells[idx] = prev;

    } else {
        // ── Diffusion pass: Jacobi 4-neighbor ───────────────────
        vec4 center = pressure_in.cells[idx];
        vec4 sum = vec4(0.0);
        float count = 0.0;

        // N, S, E, W neighbors
        int offsets_x[4] = int[4](-1, 1, 0, 0);
        int offsets_z[4] = int[4]( 0, 0,-1, 1);

        for (int n = 0; n < 4; n++) {
            int nx = x + offsets_x[n];
            int nz = z + offsets_z[n];
            if (nx >= 0 && nx < params.grid_w && nz >= 0 && nz < params.grid_h) {
                if (!is_blocked(x, z, nx, nz)) {
                    sum += pressure_in.cells[nz * params.grid_w + nx];
                    count += 1.0;
                }
            }
        }

        if (count > 0.0) {
            vec4 avg = sum / count;
            // Only diffuse R (threat) and G (goal) channels; B (safety) is set by cover shader
            vec4 result = center;
            result.r = mix(center.r, avg.r, params.diffusion_rate);
            result.g = mix(center.g, avg.g, params.diffusion_rate);
            pressure_out.cells[idx] = result;
        } else {
            pressure_out.cells[idx] = center;
        }
    }
}
)glsl";

// ═══════════════════════════════════════════════════════════════════════
//  Cover Shadow Shader (600x400 grid, 1m/cell)
//
//  Each thread ray-marches toward each threat, checking height map for walls.
//  Behind walls = shadow = cover.
// ═══════════════════════════════════════════════════════════════════════

static const char *COVER_SHADOW_GLSL = R"glsl(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Binding 0: Height map (uint16 packed as uint32, 1m resolution)
layout(set = 0, binding = 0, std430) restrict readonly buffer HeightMap {
    uint data[];
} height_map;

// Binding 1: Threat centroids (vec4: x, y, z, _unused)
layout(set = 0, binding = 1, std430) restrict readonly buffer Threats {
    vec4 threats[];
} threat_data;

// Binding 2: Cover output (float per cell)
layout(set = 0, binding = 2, std430) restrict writeonly buffer CoverOut {
    float cells[];
} cover_out;

layout(push_constant, std430) uniform Params {
    int grid_w;           // 600
    int grid_h;           // 400
    int num_threats;      // <=16
    float max_ray_dist;   // 60.0m
    float shadow_depth;   // 4.0m
    float standing_voxels;// 1.5m / voxel_scale (6.0 at 0.25m, 15.0 at 0.1m)
    float pad0;
    float pad1;
} params;

uint get_height(int hx, int hz) {
    if (hx < 0 || hx >= params.grid_w || hz < 0 || hz >= params.grid_h) return 0u;
    int idx = hz * params.grid_w + hx;
    uint packed = height_map.data[idx / 2];
    return (packed >> ((idx & 1) * 16)) & 0xFFFFu;
}

void main() {
    int cx = int(gl_GlobalInvocationID.x);
    int cz = int(gl_GlobalInvocationID.y);
    if (cx >= params.grid_w || cz >= params.grid_h) return;

    int out_idx = cz * params.grid_w + cx;

    // World position of this cell center (1m cells, centered at origin)
    float half_w = float(params.grid_w) * 0.5;
    float half_h = float(params.grid_h) * 0.5;
    float wx = float(cx) + 0.5 - half_w;
    float wz = float(cz) + 0.5 - half_h;

    uint local_h = get_height(cx, cz);
    float best_cover = 0.0;

    for (int t = 0; t < params.num_threats; t++) {
        vec4 threat = threat_data.threats[t];
        float dx = threat.x - wx;
        float dz = threat.z - wz;
        float dist = sqrt(dx * dx + dz * dz);

        if (dist < 1.0 || dist > params.max_ray_dist) continue;

        // Direction from cell toward threat
        float dir_x = dx / dist;
        float dir_z = dz / dist;

        // Ray-march from cell toward threat looking for walls
        float shadow = 0.0;
        for (float s = 1.0; s < min(dist, params.max_ray_dist); s += 1.0) {
            float rx = wx + dir_x * s;
            float rz = wz + dir_z * s;

            // Convert to height map cell
            int hx = int(rx + half_w);
            int hz = int(rz + half_h);

            uint h = get_height(hx, hz);

            if (float(h) > float(local_h) + params.standing_voxels) {
                // Wall found between cell and threat
                // Cover quality decays with distance from wall to cell
                shadow = max(shadow, clamp(1.0 - (s - 1.0) / params.shadow_depth, 0.0, 1.0));
                break; // First wall wins for this threat
            }
        }

        best_cover = max(best_cover, shadow);
    }

    cover_out.cells[out_idx] = best_cover;
}
)glsl";

// ═══════════════════════════════════════════════════════════════════════
//  Gas Diffusion Shader (600x400 grid, 1m/cell)
//
//  Simulates smoke/gas clouds via cellular automata:
//    - Diffusion: Gas spreads to neighbors based on density gradient
//    - Wind drift: Constant horizontal bias
//    - Evaporation: Density decays over time
//    - Occlusion: Walls block gas flow
//
//  Uses pressure map's Blue (density) and Alpha (gas type) channels.
//  Gas types: 0=none, 1=smoke, 2=tear gas, 3=toxic
// ═══════════════════════════════════════════════════════════════════════

static const char *GAS_DIFFUSION_GLSL = R"glsl(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Binding 0: Height map (uint16 packed as uint32, 1m resolution)
layout(set = 0, binding = 0, std430) restrict readonly buffer HeightMap {
    uint data[];
} height_map;

// Binding 1: Pressure input (read) — RGBA: R=threat, G=goal, B=gas density, A=gas type
layout(set = 0, binding = 1, std430) restrict readonly buffer PressureIn {
    vec4 cells[];
} pressure_in;

// Binding 2: Pressure output (write)
layout(set = 0, binding = 2, std430) restrict writeonly buffer PressureOut {
    vec4 cells[];
} pressure_out;

layout(push_constant, std430) uniform Params {
    int grid_w;            // 600 (or 150 for pressure grid)
    int grid_h;            // 400 (or 100)
    float delta_time;      // Frame delta
    float diffusion_rate;  // 0.05 = moderate spread
    float wind_x;          // 0.5 = gentle breeze east
    float wind_z;          // 0.0 = no north/south wind
    float evaporation;     // 0.02 = decays 2% per second
    int wall_threshold_voxels;  // 2.0m / voxel_scale (8 at 0.25m, 20 at 0.1m)
} params;

// Height map is at 1m resolution (cover grid), but gas grid is at 4m resolution.
// Convert gas grid coords to height map coords by multiplying by 4.
uint get_height_at_gas_cell(int gx, int gz) {
    int hx = gx * 4 + 2;  // Center of 4m cell in 1m coords
    int hz = gz * 4 + 2;
    int hmap_w = params.grid_w * 4;
    int hmap_h = params.grid_h * 4;
    if (hx < 0 || hx >= hmap_w || hz < 0 || hz >= hmap_h) return 0u;
    int idx = hz * hmap_w + hx;
    uint packed = height_map.data[idx / 2];
    return (packed >> ((idx & 1) * 16)) & 0xFFFFu;
}

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int z = int(gl_GlobalInvocationID.y);
    if (x >= params.grid_w || z >= params.grid_h) return;

    int idx = z * params.grid_w + x;

    vec4 center = pressure_in.cells[idx];
    float density = center.b;      // Blue channel = gas density
    float gas_type = center.a;     // Alpha channel = gas type ID (0-3)

    // Early exit if no gas here and no neighbors to diffuse from
    if (density < 0.001) {
        // Still need to check if neighbors have gas that could diffuse here
        bool any_neighbor_gas = false;
        int offsets_x[4] = int[4](-1, 1, 0, 0);
        int offsets_z[4] = int[4]( 0, 0,-1, 1);
        for (int n = 0; n < 4; n++) {
            int nx = x + offsets_x[n];
            int nz = z + offsets_z[n];
            if (nx >= 0 && nx < params.grid_w && nz >= 0 && nz < params.grid_h) {
                if (pressure_in.cells[nz * params.grid_w + nx].b > 0.001) {
                    any_neighbor_gas = true;
                    break;
                }
            }
        }
        if (!any_neighbor_gas) {
            pressure_out.cells[idx] = center;
            return;
        }
    }

    // 1. DIFFUSION: Average with neighbors (blocked by walls)
    float sum = 0.0;
    float weight_total = 0.0;
    uint center_height = get_height_at_gas_cell(x, z);

    int offsets_x[4] = int[4](-1, 1, 0, 0);
    int offsets_z[4] = int[4]( 0, 0,-1, 1);

    for (int n = 0; n < 4; n++) {
        int nx = x + offsets_x[n];
        int nz = z + offsets_z[n];
        if (nx < 0 || nx >= params.grid_w || nz < 0 || nz >= params.grid_h) continue;

        uint neighbor_height = get_height_at_gas_cell(nx, nz);
        // Gas can't diffuse through walls (height difference > wall threshold)
        if (abs(int(neighbor_height) - int(center_height)) > params.wall_threshold_voxels) continue;

        vec4 nb = pressure_in.cells[nz * params.grid_w + nx];
        float nb_density = nb.b;
        float weight = 1.0;
        sum += nb_density * weight;
        weight_total += weight;
    }

    float avg_neighbor = (weight_total > 0.0) ? sum / weight_total : density;
    density = mix(density, avg_neighbor, params.diffusion_rate * params.delta_time);

    // 2. WIND DRIFT: Shift density based on wind direction
    // Sample upwind neighbor and bias toward it
    int wind_dx = int(sign(params.wind_x));
    int wind_dz = int(sign(params.wind_z));
    if (wind_dx != 0 || wind_dz != 0) {
        int upwind_x = x - wind_dx;
        int upwind_z = z - wind_dz;
        if (upwind_x >= 0 && upwind_x < params.grid_w &&
            upwind_z >= 0 && upwind_z < params.grid_h) {
            vec4 upwind = pressure_in.cells[upwind_z * params.grid_w + upwind_x];
            float upwind_density = upwind.b;
            float wind_strength = abs(params.wind_x) + abs(params.wind_z);
            density += (upwind_density - density) * wind_strength * 0.1 * params.delta_time;
        }
    }

    // 3. EVAPORATION: Decay over time
    density *= (1.0 - params.evaporation * params.delta_time);

    // 4. CLAMP and write back
    density = max(0.0, min(1.0, density));
    if (density < 0.001) {
        density = 0.0;
        gas_type = 0.0;  // Clear type when gas dissipates
    }
    center.b = density;
    center.a = gas_type;
    pressure_out.cells[idx] = center;
}
)glsl";

// ═══════════════════════════════════════════════════════════════════════
//  Pheromone CA Shader (multi-channel evaporation + diffusion)
//
//  Single-pass: each thread handles one (x,z) cell, loops over all channels.
//  Deposits stay on CPU; only the CA step runs on GPU.
//  Data layout: [channel * (W*H) + z * W + x] as flat float SSBO.
// ═══════════════════════════════════════════════════════════════════════

static const char *PHEROMONE_CA_GLSL = R"glsl(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Binding 0: Grid input (read) — flat float array, channel-major
layout(set = 0, binding = 0, std430) restrict readonly buffer GridIn {
    float cells[];
} grid_in;

// Binding 1: Grid output (write)
layout(set = 0, binding = 1, std430) restrict writeonly buffer GridOut {
    float cells[];
} grid_out;

// Binding 2: Per-channel parameters — vec2(evaporation_rate, diffusion_rate)
// Padded to vec4 for std430 alignment safety
layout(set = 0, binding = 2, std430) restrict readonly buffer ChParams {
    vec4 params[];  // .x = evap_rate, .y = diff_rate, .zw unused
} ch_params;

layout(push_constant, std430) uniform PC {
    int grid_w;
    int grid_h;
    int channel_count;
    float delta;
} pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int z = int(gl_GlobalInvocationID.y);
    if (x >= pc.grid_w || z >= pc.grid_h) return;

    int stride = pc.grid_w * pc.grid_h;

    for (int ch = 0; ch < pc.channel_count; ch++) {
        int idx = ch * stride + z * pc.grid_w + x;
        float val = grid_in.cells[idx];

        // Evaporation: val *= pow(evap_rate, delta)
        float evap = ch_params.params[ch].x;
        val *= pow(evap, pc.delta);

        // 4-neighbor diffusion
        float diff = ch_params.params[ch].y;
        if (diff > 0.0) {
            float sum = 0.0;
            float cnt = 0.0;
            if (x > 0)              { sum += grid_in.cells[idx - 1];          cnt += 1.0; }
            if (x < pc.grid_w - 1)  { sum += grid_in.cells[idx + 1];          cnt += 1.0; }
            if (z > 0)              { sum += grid_in.cells[idx - pc.grid_w];   cnt += 1.0; }
            if (z < pc.grid_h - 1)  { sum += grid_in.cells[idx + pc.grid_w];  cnt += 1.0; }
            if (cnt > 0.0) {
                float avg = sum / cnt;
                val = mix(val, avg, diff);
            }
        }

        grid_out.cells[idx] = val;
    }
}
)glsl";

} // namespace godot

#endif // GPU_SHADERS_H
