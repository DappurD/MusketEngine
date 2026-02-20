# CORE_MATH.md
## The Musket Engine: Algorithmic & ECS Reference

> **AI DIRECTIVE**: Use these exact patterns when implementing the engine's core C++ systems. Do not reinvent the math. Translate these proofs directly into the Flecs `ecs.system` pipelines.

---

## §1. The Muscle: Spring-Damper Formation Physics

**Concept**: Soldiers do not use A* to stay in formation. They are physics particles attached to a moving macro-slot via a mathematical spring.

```cpp
// 16-byte Entity Components
struct Position { float x, z; };   // 8 bytes
struct Velocity { float vx, vz; }; // 8 bytes

struct SoldierFormationTarget {
    float target_x, target_z;       // The slot they are pulled toward
    float base_stiffness;           // Modified by Morale/Uniforms
    float damping_multiplier;       // Usually ~2.0 for critical damping
}; // 16 bytes

// The O(1) 60Hz Flecs System
ecs.system<Position, Velocity, const SoldierFormationTarget>("MusketSpringDamperPhysics")
    .iter([](flecs::iter &it, Position *p, Velocity *v,
             const SoldierFormationTarget *target) {
        float dt = it.delta_time();
        for (int i : it) {
            float dx = target[i].target_x - p[i].x;
            float dz = target[i].target_z - p[i].z;

            // Panic Grid dynamically lowers this stiffness before this system runs!
            float stiffness = target[i].base_stiffness;
            float damping = target[i].damping_multiplier * std::sqrt(stiffness);

            float force_x = (stiffness * dx) - (damping * v[i].vx);
            float force_z = (stiffness * dz) - (damping * v[i].vz);

            v[i].vx += force_x * dt;
            v[i].vz += force_z * dt;

            // Supersonic Rubber-Banding Clamp (Max Sprint Speed)
            const float MAX_SPEED = 4.0f; // 12.0f for Cavalry Charge
            float speed_sq = (v[i].vx * v[i].vx) + (v[i].vz * v[i].vz);
            if (speed_sq > (MAX_SPEED * MAX_SPEED)) {
                float speed = std::sqrt(speed_sq);
                v[i].vx = (v[i].vx / speed) * MAX_SPEED;
                v[i].vz = (v[i].vz / speed) * MAX_SPEED;
            }
            p[i].x += v[i].vx * dt;
            p[i].z += v[i].vz * dt;
        }
    });
```

**Edge Cases**: `dt = 0` → harmless. `displacement = 0` → soldier decelerates to rest. `stiffness = 0` → soldier drifts freely (ROUTING state).

---

## §2. The Sword: Volley Fire (The Inverse Sieve)

**Concept**: Calculates musket hits by physically raycasting through the enemy formation's grid. Front ranks act as meat-shields.

```cpp
// 1. Broad Phase: Battalion vs Battalion distance check
if (distance(attacker_center, defender_center) > max_musket_range) return;

// 2. The Sieve Iteration (Defender's Grid)
// Iterate front-to-back relative to the firing angle
for (int row = 0; row < defender_depth; ++row) {
    for (int col = 0; col < defender_width; ++col) {

        int slot_index = (row * defender_width) + col;
        if (!defender_slots[slot_index].is_alive) continue;

        // 3. Hit probability based on range and weather
        float dist = distance(attacker_pos, defender_slots[slot_index].pos);
        float hit_chance = base_accuracy * (1.0f - (dist / max_musket_range));
        hit_chance *= (1.0f - global_humidity_penalty); // Wet powder misfire

        // 4. Roll the dice
        if (random_float(0.0f, 1.0f) <= hit_chance) {
            defender_slots[slot_index].is_alive = false;
            ecs.entity(defender_slots[slot_index].entity_id).remove<IsAlive>();

            // 5. The Meat Shield:
            // Musket ball stops here. Cannot penetrate to row behind.
            break; // Move to next attacker's shot
        }
    }
}
```

**Edge Cases**: All front row dead → shots reach second row. `humidity_penalty = 1.0` → 0% hit chance (bayonet brawl). Empty defender grid → no effect.

---

## §3. The Sword: DDA Artillery & Ricochet Kinematics

**Concept**: True 3D ballistics mapped to integer grids for microsecond collision detection.

```cpp
struct ArtilleryShot {
    float x, y, z;           // World position
    float vx, vy, vz;        // Velocity vector
    float kinetic_energy;     // Drops by 1.0 per man penetrated
    bool active;
}; // ~32 bytes

// 60Hz Kinematics
shots[i].vy -= 9.81f * dt;  // Gravity
shots[i].x += shots[i].vx * dt;
shots[i].y += shots[i].vy * dt;
shots[i].z += shots[i].vz * dt;

// Ground Collision & Ricochet Math
float ground_height = TerrainCache::get_height_at(shots[i].x, shots[i].z);
float terrain_wetness = TerrainCache::get_wetness_at(shots[i].x, shots[i].z);

if (shots[i].y <= ground_height) {
    if (terrain_wetness > 0.8f) {
        // MUD: Ball sinks. Zero ricochet. (The Waterloo Effect)
        shots[i].active = false;
    } else {
        // HARD EARTH: Ricochet!
        shots[i].y = ground_height + 0.1f;
        shots[i].vy = std::abs(shots[i].vy) * 0.4f; // Lose 60% vertical
        shots[i].vx *= 0.7f; // Friction
        shots[i].vz *= 0.7f;

        if (shots[i].vy < 1.0f && std::abs(shots[i].vx) < 1.0f) {
            shots[i].active = false; // Ball stops rolling
        }
    }
}
// (Bresenham DDA traversal for killing men occurs after this)
```

**Cavalry / Artillery Structs**:

```cpp
struct CavalryState {
    float charge_momentum;     // Builds during charge, depletes on impact
    bool is_charging;
    float charge_timer;
}; // 12 bytes
// Impact: casualties = momentum × (1.0 - target_formation_defense)
// Square defense = 0.9 (bounce), Line defense = 0.2 (devastation)

struct ArtilleryBattery {
    int num_guns;
    float reload_timer;        // Per-battery (coordinated fire)
    float traverse_angle;      // Where guns are pointing
    int ammo_roundshot;
    int ammo_canister;         // Short-range shotgun effect
    bool is_limbered;          // True = moving, False = deployed
}; // 24 bytes

// Order components written directly to ECS entities
struct MovementOrder { float target_x, target_z; bool arrived; }; // 12 bytes
struct HaltOrder {};   // 0 bytes (tag)
struct FireOrder { float target_x, target_z; }; // 8 bytes
```

---

## §4. The Mind: Cellular Automata (Panic & Smoke)

**Concept**: 5Hz double-buffered grid diffuses float values (Fear or Opacity) across the map.

```cpp
// Flat arrays to fit entirely inside the CPU L3 Cache
struct VoxelGrid {
    int width, height;                   // e.g., 64×64
    float cell_size;                      // 4.0 meters per voxel
    int chunk_size;                       // 16 cells
    std::vector<float> read_buffer;       // Contiguous for cache
    std::vector<float> write_buffer;
    std::vector<uint8_t> active_chunks;   // Skips empty terrain (95% savings)
};

// The 5Hz Diffusion Kernel (Von Neumann neighborhood)
float center = read_buffer[idx];
float neighbors =
    read_buffer[idx - 1] +           // Left
    read_buffer[idx + 1] +           // Right
    read_buffer[idx - width] +       // Top
    read_buffer[idx + width];        // Bottom

// Evaporate slightly (0.95), pull 10% from neighbors
float new_val = (center * 0.95f) + (neighbors * 0.025f);
write_buffer[idx] = new_val;

// Death Observer — injects terror at kill site
ecs.observer<Position>("OnSlaughter_InjectTerror")
    .event(flecs::OnRemove)
    .with<IsAlive>()
    .each([](flecs::iter &it, size_t i, Position &pos) {
        // A. Locate PanicGrid singleton
        // B. Compute integer chunk/cell index from pos.x, pos.z
        // C. Spike: grid[idx] += 0.4f;
        // D. Wake the chunk
        // E. Queue ragdoll impulse for Godot rendering
    });
```

---

## §5. Urban Math: Vauban Magistral Line Solver

**Concept**: Calculates vertex offsets to convert a player-drawn polygon into a dead-zone-free Star Fort.

```cpp
// Given a base regular polygon (e.g., hexagon with 6 sides)
// L = length of one side, n = number of sides

float edge_division_factor;
if (n == 4) edge_division_factor = 8.0f;      // Quadrangle
else if (n == 5) edge_division_factor = 7.0f;  // Pentagon
else if (n >= 6) edge_division_factor = 6.0f;  // Hexagon+

// 1. Calculate the perpendicular offset depth
float perpendicular_offset = L / edge_division_factor;

// 2. Find the midpoint of the edge
Vector2 midpoint = (vertex_A + vertex_B) / 2.0f;

// 3. Calculate the inward normal of the edge
Vector2 edge_dir = (vertex_B - vertex_A).normalized();
Vector2 inward_normal = Vector2(-edge_dir.y, edge_dir.x);

// 4. Determine the tip of the inner curtain wall
Vector2 curtain_wall_center = midpoint + (inward_normal * perpendicular_offset);

// 5. Bastion faces: vertex_A → curtain_wall_center → vertex_B
```

---

## §6. The Economy: Smart Buildings, Dumb Agents

**Concept**: Simulates 5,000 logistics agents moving physical items without 5,000 A* scripts.

```cpp
// --- THE CIVILIAN (16 Bytes) ---
struct Citizen {
    enum State { IDLE, WALKING_TO_SOURCE, WALKING_TO_DEST };
    State current_state;
    uint8_t carrying_item;    // e.g., ITEM_WHEAT, ITEM_MUSKET, ITEM_IRON_ORE
    uint8_t carrying_amount;
    flecs::entity current_target;
};

// --- THE BUILDING ---
struct Workplace {
    uint8_t consumes_item;    // What it needs (e.g., ITEM_IRON_ORE)
    uint8_t produces_item;    // What it makes (e.g., ITEM_MUSKET)
    int inventory_in;
    int inventory_out;
};

// --- THE GLOBAL JOB BOARD ---
struct LogisticsJob {
    flecs::entity source_building;
    flecs::entity dest_building;
    uint8_t item_type;
};
std::vector<LogisticsJob> g_global_job_board;

// 60Hz Execution Loop — spectacularly dumb and fast
ecs.system<Citizen, Position, Velocity>("CivilianMovement")
    .iter([](flecs::iter &it, Citizen *civ, Position *pos, Velocity *vel) {
        float dt = it.delta_time();
        for (int i : it) {
            if (civ[i].current_state == Citizen::IDLE) continue; // Zero CPU

            // Move along pre-calculated Flow Field
            pos[i].x += vel[i].vx * dt;
            pos[i].z += vel[i].vz * dt;

            // Arrived at target building?
            if (distance_squared(pos[i], civ[i].current_target) < 1.0f) {
                if (civ[i].current_state == Citizen::WALKING_TO_SOURCE) {
                    civ[i].carrying_item = ITEM_IRON_ORE;
                    civ[i].current_state = Citizen::WALKING_TO_DEST;
                    civ[i].current_target = dest_building_entity;
                }
                else if (civ[i].current_state == Citizen::WALKING_TO_DEST) {
                    civ[i].carrying_item = ITEM_NONE;
                    civ[i].current_state = Citizen::IDLE;
                }
            }
        }
    });
```

---

## §7. The Zeitgeist: O(1) SIMD Reduction

**Concept**: Summing the political alignment of 100,000 citizens in under a millisecond.

```cpp
// Runs on a 1Hz or 0.2Hz (5 second) tick
int angry_artisans = 0;
auto query = ecs.query<Citizen, SocialClass, Morale>();

query.each([&](Citizen& c, SocialClass& sc, Morale& m) {
    if (sc.type == CLASS_ARTISAN && m.value < 0.4f) {
        angry_artisans++;
    }
});
// Result is injected into the LLM Mayor's prompt YAML
```

---

## §8. The Eyes & Voice: VAT Shaders & Acoustic Delay

### Godot VAT Shader (GLSL)

```glsl
void vertex() {
    // Golden ratio hash for deterministic randomness per-soldier
    float soldier_seed = float(INSTANCE_ID) * 0.618033;

    // 1. Face/Gear Variation (0 to 4)
    float face_variant = floor(mod(soldier_seed * 5.0, 5.0));

    // 2. Mud/Fade Variation (0.0 to 0.15)
    float coat_fade = fract(soldier_seed * 7.13) * 0.15;

    // 3. March Phase Stagger (Prevents robotic unison)
    float march_phase = fract(soldier_seed * 3.77) * 0.5;

    // carrying_item drives animation blend:
    // ITEM_WHEAT  -> "Carrying Sack" anim, un-hide wheat sack mesh
    // ITEM_MUSKET -> "Shouldering Musket" anim

    // (Sample VAT Texture using TIME + march_phase)
}
```

### Terrain Splatmap Shader (GLSL)

```glsl
// Terrain splatmap fragment shader
float trample = texelFetch(trample_data, ivec2(voxel_xz), 0).r;
vec3 ground = mix(grass_albedo, mud_albedo, smoothstep(0.2, 0.8, trample));
float roughness = mix(0.85, 0.35, trample); // Mud is glossy/wet
```

### Acoustic Delay (GDScript)

```gdscript
func play_cannon_sound(world_position: Vector3):
    # Speed of sound is roughly 343 meters per second
    var dist = camera.global_position.distance_to(world_position)
    var delay_seconds = dist / 343.0

    # Spawn flash particle instantly (Speed of Light)
    spawn_muzzle_flash(world_position)

    # Delay the thunderous audio (Speed of Sound)
    await get_tree().create_timer(delay_seconds).timeout
    audio_player.play_boom()
```

---

## §9. The State Compressor (Aide-de-Camp Algorithm)

```cpp
struct SectorInfo {
    Vector2i grid_pos;          // e.g., (2, 3) = "C3"
    String terrain_tag;         // "Open Field", "Sunken Road", "Dense Forest"
    AABB world_bounds;          // Converted back to world coords for pathfinding
    float avg_panic;            // From PanicGrid
    int friendly_count;
    int enemy_count_visible;
};

// Semantic Quantization thresholds
// Strength: Pristine (>80%), Degraded (40-80%), Decimated (<40%)
// Morale:   Eager (<0.3 panic), Wavering (0.3-0.7), Routing (>0.7)
// State:    Idle, Marching, Engaged, Under_Fire
// Ammo:     Full (>60%), Low (20-60%), Empty (<20%)

void generate_aides_notes(std::vector<std::string>& notes) {
    for (auto& btn : battalions) {
        if (btn.morale == WAVERING && btn.state == ENGAGED)
            notes.push_back(btn.name + " will break soon without support.");
    }
    for (auto& sector : undefended_flanks)
        notes.push_back(sector.name + " is undefended.");
    for (auto& contact : stale_intel)
        if (contact.age_seconds > 120)
            notes.push_back("Lost contact with " + contact.desc + ". Scout recommended.");
}
```

### Example SitRep Dispatch (~150 tokens)

```yaml
SITREP: 14:30 Hours
WEATHER: Clear. WIND: East (smoke clearing from Center).

FEEDBACK ON PREVIOUS ORDERS:
- "Cavalry_1 charge C3" → REJECTED. C3 is [Dense Forest]. Holding at B2.
- "3rd_Infantry advance C3" → EXECUTING. Progress: 60%.

FRIENDLY FORCES:
- 1st_Infantry (B2, Center Ridge): Engaged, Degraded, Wavering.
- 2nd_Infantry (B2, Center Ridge): Idle, Pristine, Eager.
- Cavalry_1 (A1, Left Flank): Idle, Pristine, Eager.
- Grand_Battery (B1, Rear): Deployed, firing on C3.

VISIBLE ENEMY FORCES:
- Blue_Infantry (B3, Valley): Advancing, Degraded.
- UNIDENTIFIED: Heavy dust cloud moving North through A3.

AIDE'S NOTES (CRITICAL):
- 1st_Infantry will break soon without support.
- Left Flank (A1) is undefended against the Unidentified Contact.

AWAITING ORDERS. Output JSON only.
```

### LLM Order JSON Format

```json
{
  "orders": [
    {"battalion": "3rd_infantry", "action": "ADVANCE", "target_sector": "C3"},
    {"battalion": "cavalry_reserve", "action": "HOLD",
     "condition": "SECTOR_PANIC_CRITICAL", "condition_sector": "B2",
     "then": "CHARGE", "then_sector": "B2"},
    {"battalion": "artillery_1", "action": "BOMBARD",
     "target_sector": "B2", "ammo": "ROUNDSHOT"}
  ],
  "reasoning": "Pin center with 3rd, soften B2. When panic breaks, cavalry charges."
}
```
