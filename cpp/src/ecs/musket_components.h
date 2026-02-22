#ifndef MUSKET_COMPONENTS_H
#define MUSKET_COMPONENTS_H

#include <cstdint>

/**
 * The Musket Engine — ECS Component Definitions
 *
 * AI DIRECTIVE: These are the POD structs that define all entity data.
 * Keep them under 32 bytes. No pointers, no std::vector, no virtual functions.
 * See CORE_MATH.md for the systems that iterate over these.
 */

// ─── Spatial ───────────────────────────────────────────────
struct Position {
  float x, z;
}; // 8 bytes
struct Velocity {
  float vx, vz;
}; // 8 bytes
struct Height {
  float y;
}; // 4 bytes

// ─── Combat: Formation ────────────────────────────────────
enum FormationShape : uint8_t {
  SHAPE_LINE = 0,
  SHAPE_COLUMN = 1,
  SHAPE_SQUARE = 2
};

enum FireDiscipline : uint8_t {
  DISCIPLINE_HOLD = 0,       // Reload but do NOT fire
  DISCIPLINE_AT_WILL = 1,    // Fire when ready (default)
  DISCIPLINE_BY_RANK = 2,    // Rolling rank fire (3s cycle)
  DISCIPLINE_MASS_VOLLEY = 3 // All fire in 0.5s window → HOLD
};

struct alignas(64) SoldierFormationTarget {
  double target_x, target_z;    // Slot position (double: Trap 10 precision)
  float base_stiffness;         // Modified by morale/uniforms
  float damping_multiplier;     // ~2.0 for critical damping
  float face_dir_x, face_dir_z; // Per-soldier facing vector (§12.8)
  bool can_shoot;               // Enforces Column/Square fire limits
  uint8_t rank_index;           // 0=Front, 1=Mid, 2=Rear
  uint8_t pad[6];               // Explicit padding to 64 bytes
}; // 64 bytes — 1 soldier = 1 L1 cache line (Trap 25)

// ─── Stats ────────────────────────────────────────────────
struct MovementStats {
  float base_speed;
  float charge_speed;
}; // 8 bytes

// ─── Combat: State ────────────────────────────────────────
struct TeamId {
  uint8_t team;
}; // 1 byte — 0=red, 1=blue
struct BattalionId {
  uint32_t id;
}; // 4 bytes
struct IsAlive {}; // Tag (0 bytes)
struct Routing {}; // Tag — soldier is fleeing (GDD §5.3)

struct MusketState {
  float reload_timer;     // Seconds remaining
  uint8_t ammo_count;     // Rounds left
  uint8_t misfire_chance; // 0-255 scaled (humidity)
}; // 6 bytes

// ─── Combat: Orders ───────────────────────────────────────
struct MovementOrder {
  float target_x, target_z;
  bool arrived;
}; // 12 bytes
struct HaltOrder {}; // Tag
struct FireOrder {
  float target_x, target_z;
}; // 8 bytes

// ─── Combat: Artillery ────────────────────────────────────
enum ArtilleryAmmoType : uint8_t { AMMO_ROUNDSHOT = 0, AMMO_CANISTER = 1 };

struct ArtilleryShot {
  float x, y, z;          // World position
  float vx, vy, vz;       // Velocity
  float kinetic_energy;   // -1.0 per man penetrated
  ArtilleryAmmoType ammo; // ROUNDSHOT or CANISTER
  bool active;
}; // 32 bytes

struct ArtilleryBattery {
  int num_guns;
  float reload_timer;
  float traverse_angle;
  int ammo_roundshot;
  int ammo_canister;
  bool is_limbered;
  float unlimber_timer; // 60s countdown when deploying
}; // 28 bytes

// ─── Combat: Cavalry ──────────────────────────────────────
// Deep Think: "You cannot use an Attractor to simulate a Projectile."
// Charging cavalry bypass the spring-damper and use a locked ballistic
// vector. lock_dir_x/z is set at commitment distance (30m) and stays
// constant for the entire charge.
struct CavalryState {
  float charge_momentum; // 0.0 to 1.0 (cubic ramp for impact)
  float state_timer;     // Duration tracker for current state
  float lock_dir_x;      // Committed ballistic direction X (normalized)
  float lock_dir_z;      // Committed ballistic direction Z (normalized)
  uint32_t state_flags;  // 0=Walk, 1=Charging, 2=Disordered
  uint32_t pad;
}; // 24 bytes

struct FormationDefense {
  float defense; // 0.2=Line, 0.5=Column, 0.9=Square
}; // 4 bytes

// ─── Cavalry: MacroBattalion Centroid Cache (Deep Think #4) ──
// Pre-computed per-frame. O(1) lookups for charge targeting,
// M7 command network, M7.5 fire discipline + targeting.
struct MacroBattalion {
  // ── Transient State (zeroed every frame in centroid pass) ──
  float cx = 0.0f;
  float cz = 0.0f;
  int alive_count = 0;
  uint32_t team_id = 999;

  // ── M7 Command Network (zeroed every frame, set during scan) ──
  bool flag_alive = false;
  bool drummer_alive = false;
  bool officer_alive = false;

  // ── Persistent State (DO NOT zero in centroid pass — Trap 23) ──
  float flag_cohesion = 1.0f; // Decay: 16s to 0.2 floor when flag dies

  // ── M7.5 Fire Discipline (Persistent — Trap 28) ──
  FireDiscipline fire_discipline = DISCIPLINE_AT_WILL;
  uint8_t active_firing_rank = 0; // Cycles 0→1→2 for BY_RANK
  float volley_timer = 0.0f;      // Metronome countdown

  // ── M7.5 OBB Geometry (Persistent — set by order_formation) ──
  float dir_x = 0.0f, dir_z = -1.0f; // Battalion facing vector
  float ext_w = 0.0f;                // OBB half-width + 2m buffer
  float ext_d = 0.0f;                // OBB half-depth + 2m buffer
  int target_bat_id = -1;            // Hoisted macro targeting (Trap 26)
};

// EXTERN: declared here, defined ONCE in world_manager.cpp
constexpr int MAX_BATTALIONS = 256;
extern MacroBattalion g_macro_battalions[MAX_BATTALIONS];

struct ChargeOrder {
  uint32_t target_battalion_id;
  bool is_committed;
  uint8_t pad[3];
}; // 8 bytes — triggers charge state (Phase C: promote to POD)
struct Disordered {}; // Tag — post-charge vulnerability

// ─── Rendering: Battalion Chunking ────────────────────────
// Maps each entity to a stable slot in a battalion's shadow buffer.
// The mm_slot is permanent for the entity's lifetime (never shifts).
struct RenderSlot {
  uint32_t battalion_id;
  uint32_t mm_slot;
}; // 8 bytes

// ─── Combat: Command Network (M7 — GDD §5.4) ────────────
struct FormationAnchor {}; // Flag bearer tag — death decays cohesion
struct Drummer {};         // Drum tag — order latency + panic cleanse
struct OrderLatency {
  float delay_seconds;
}; // Drummer: 2.0 alive, 8.0 dead
struct ElevatedLOS {}; // Officer tag — death blinds targeting

// ─── M7: Order Delay Pipeline (Trap 22: global, not per-entity) ──
enum OrderType : uint8_t {
  ORDER_NONE = 0,
  ORDER_MARCH,
  ORDER_FIRE,
  ORDER_CHARGE,
  ORDER_DISCIPLINE // M7.5: Change fire doctrine
};
struct PendingOrder {
  OrderType type = ORDER_NONE;
  float target_x = 0.0f;
  float target_z = 0.0f;
  float delay = 0.0f;
  uint8_t requested_discipline = 0; // M7.5: FireDiscipline enum value
};
extern PendingOrder g_pending_orders[MAX_BATTALIONS];

// ─── Combat: Medical ──────────────────────────────────────
struct Downed {
  float bleed_timer;
}; // Panic emitter, awaiting stretcher
struct Veteran {}; // Survived surgery tag
struct Amputee {}; // Restricted jobs tag

// ─── Combat: Panic CA Grid (CORE_MATH.md §4) ─────────────
// 64×64 double-buffered cellular automata for fear diffusion.
// PER-TEAM: read_buf[team][cell] so deaths on team X only
// panic team X soldiers. 64KB total — still fits L1 cache.
struct PanicGrid {
  static constexpr int WIDTH = 64;
  static constexpr int HEIGHT = 64;
  static constexpr int CELLS = WIDTH * HEIGHT;
  static constexpr int TEAMS = 2;
  static constexpr float CELL_SIZE = 4.0f;                  // meters per cell
  static constexpr float HALF_W = (WIDTH / 2) * CELL_SIZE;  // 128m
  static constexpr float HALF_H = (HEIGHT / 2) * CELL_SIZE; // 128m

  float read_buf[TEAMS][CELLS];
  float write_buf[TEAMS][CELLS];
  float tick_accum; // accumulates dt, fires at 5Hz

  // World → grid index (clamped)
  static int world_to_idx(float wx, float wz) {
    int cx = (int)((wx + HALF_W) / CELL_SIZE);
    int cz = (int)((wz + HALF_H) / CELL_SIZE);
    if (cx < 0)
      cx = 0;
    if (cx >= WIDTH)
      cx = WIDTH - 1;
    if (cz < 0)
      cz = 0;
    if (cz >= HEIGHT)
      cz = HEIGHT - 1;
    return cz * WIDTH + cx;
  }
};

// ─── M8: Spatial Hash Grid (Singleton) ────────────────────
// Flat-array SoA spatial hash. Rebuilt from scratch every frame.
// Head/next linked list pattern — ZERO heap allocations.
// Trap 30: std::vector-per-cell is BANNED (heap fragmentation).
constexpr float SPATIAL_CELL_SIZE =
    32.0f;                         // 32m cells (100m range = ~7x7 search)
constexpr int SPATIAL_WIDTH = 128; // 4096m x 4096m map
constexpr int SPATIAL_HEIGHT = 128;
constexpr int SPATIAL_MAX_CELLS =
    SPATIAL_WIDTH * SPATIAL_HEIGHT;          // 16,384 cells
constexpr int SPATIAL_MAX_ENTITIES = 131072; // 128K cap

struct alignas(64) SpatialHashGrid {
  // Cell → first entity index (-1 = empty)
  int32_t cell_head[SPATIAL_MAX_CELLS];

  // Entity → next entity in same cell (-1 = end of chain)
  int32_t entity_next[SPATIAL_MAX_ENTITIES];

  // SoA data: cache-coherent filtering without loading full components
  uint64_t entity_id[SPATIAL_MAX_ENTITIES];
  float pos_x[SPATIAL_MAX_ENTITIES];
  float pos_z[SPATIAL_MAX_ENTITIES];
  uint32_t bat_id[SPATIAL_MAX_ENTITIES];
  uint8_t team_id[SPATIAL_MAX_ENTITIES];

  int32_t active_count;
  uint32_t last_frame_id; // Frame-boundary detection for .each() rebuild

  // World → cell coords with +2048 offset (Trap 31: no negative truncation)
  static inline void world_to_cell(float wx, float wz, int &cx, int &cz) {
    cx = static_cast<int>((wx + 2048.0f) / SPATIAL_CELL_SIZE);
    cz = static_cast<int>((wz + 2048.0f) / SPATIAL_CELL_SIZE);
    if (cx < 0)
      cx = 0;
    else if (cx >= SPATIAL_WIDTH)
      cx = SPATIAL_WIDTH - 1;
    if (cz < 0)
      cz = 0;
    else if (cz >= SPATIAL_HEIGHT)
      cz = SPATIAL_HEIGHT - 1;
  }
}; // ~4.2 MB — fits in L3 cache

// S-LOD: Off-screen agents skip 60Hz physics/targeting
struct MacroSimulated {}; // Tag — entity runs 0.1Hz abstract tick only

// ─── M9: Economy — Citizen State Machine ──────────────────
enum CitizenState : uint8_t {
  CSTATE_IDLE = 0,
  CSTATE_SLEEPING,
  CSTATE_COMMUTE_WORK,
  CSTATE_WORKING,
  CSTATE_SEEK_MARKET,
  CSTATE_LOGISTICS_TO_SRC,
  CSTATE_LOGISTICS_TO_DEST
};

// ─── M9: The Citizen (32 Bytes, alignas(32)) ──────────────
// Law 4: Full vision struct. All fields the citizen will EVER need.
// 2 citizens per 64B cache line. SIMD-friendly iteration.
struct alignas(32) Citizen {
  uint64_t home_id;        // 8B: Entity ID of Household
  uint64_t workplace_id;   // 8B: Entity ID of Forge/Mill
  uint64_t current_target; // 8B: Physical waypoint entity

  float satisfaction; // 4B: 0.0-1.0 (drives Zeitgeist)

  CitizenState state;      // 1B: Routine phase
  uint8_t social_class;    // 1B: 0=Peasant, 1=Artisan, 2=Merchant
  uint8_t carrying_item;   // 1B: From ItemType enum
  uint8_t carrying_amount; // 1B: Up to 255
}; // 32 bytes

// ─── M10-M12: Multi-Recipe Workplace (64 Bytes, alignas(64)) ──
// One L1 cache line. 3-input, 3-output recipe. Discrete batches (no floats).
// Flags: Bit 0 = BYPASS_TOOLS (Trap 50), Bit 1 = MOBILE_BAKERY
constexpr uint32_t WP_FLAG_BYPASS_TOOLS = 0x01;
constexpr uint32_t WP_FLAG_MOBILE_BAKERY = 0x02;

struct alignas(64) Workplace {
  // Inputs (e.g., Niter, Sulfur, Charcoal)
  uint8_t in_items[3];  // 3B: ItemType IDs (0 = None)
  uint8_t in_reqs[3];   // 3B: Amount consumed per batch
  uint16_t in_stock[3]; // 6B: Current stockpiled inputs (STRICTLY INT)

  // Outputs (e.g., Meat, Tallow, Hides)
  uint8_t out_items[3];  // 3B: Primary + 2 Byproducts
  uint8_t out_yields[3]; // 3B: Amount produced per batch
  uint16_t out_stock[3]; // 6B: Current stockpiled outputs

  int16_t active_workers; // 2B: Drops instantly when drafted
  int16_t max_workers;    // 2B: Capacity for efficiency calculation

  float prod_timer; // 4B: Accumulates dt, resets at base_time
  float base_time;  // 4B: Seconds per batch

  float tool_durability; // 4B: Degrades per batch. At 0, 0.25x penalty
  float spark_risk;      // 4B: Ignition radius (Richmond Ordinance)
  float pollution_out;   // 4B: Injected into M9 CivicGrid

  uint32_t throughput_rate; // 4B: SLOD production rate for off-screen
  uint32_t flags;           // 4B: WP_FLAG_BYPASS_TOOLS, WP_FLAG_MOBILE_BAKERY

  uint8_t pad[8]; // 8B: Perfect padding to 64 Bytes
}; // 64 bytes

// ─── M10: Cargo Manifest (32 Bytes, alignas(32)) ──────────
// Attached to Wagon entities (Position, Velocity, TeamId, IsAlive).
struct alignas(32) CargoManifest {
  uint64_t source_building; // 8B: Entity ID (Airgap)
  uint64_t dest_building;   // 8B: Entity ID (Airgap)

  uint32_t flow_field_id; // 4B: Async road network to follow

  uint8_t item_type; // 1B: What it is hauling
  uint8_t amount;    // 1B: Current cargo amount
  uint16_t capacity; // 2B: Max capacity (e.g., 100)

  float volatility; // 4B: Explosion multiplier (Black Powder = 1.0)
  uint32_t pad;     // 4B: Exact padding to 32B
}; // 32 bytes

// ─── M9: The Household (16 Bytes, alignas(16)) ────────────
// Attached to residential building entities.
struct alignas(16) Household {
  int16_t food_stock;        // 2B
  int16_t fuel_stock;        // 2B
  int16_t living_population; // 2B
  uint8_t plot_level;        // 1B: 1=Tent, 2=House, 3=Artisan
  uint8_t wealth_level;      // 1B

  float accumulated_wealth; // 4B
  uint32_t pad;             // 4B
}; // 16 bytes

// ─── M9: Global Job Board (Transient) ─────────────────────
// std::vector is OK here — it's a global singleton, not per-cell (Trap 30
// exemption).
struct LogisticsJob {
  uint64_t source_building; // 8B
  uint64_t dest_building;   // 8B
  uint8_t item_type;        // 1B
  uint8_t amount;           // 1B
  uint16_t priority;        // 2B
  uint32_t flow_field_id;   // 4B
}; // 24 bytes

// ─── M9: Civic Grid (Singleton CA — same arch as PanicGrid) ──
struct CivicGrid {
  static constexpr int WIDTH = 64;
  static constexpr int HEIGHT = 64;
  static constexpr int CELLS = WIDTH * HEIGHT;
  static constexpr float CELL_SIZE = 4.0f;
  static constexpr float HALF_W = (WIDTH / 2) * CELL_SIZE;
  static constexpr float HALF_H = (HEIGHT / 2) * CELL_SIZE;

  float market_access[CELLS]; // Diffuses from food stalls
  float pollution[CELLS];     // Diffuses from Tanneries/Niter

  static int world_to_idx(float wx, float wz) {
    int cx = (int)((wx + HALF_W) / CELL_SIZE);
    int cz = (int)((wz + HALF_H) / CELL_SIZE);
    if (cx < 0)
      cx = 0;
    if (cx >= WIDTH)
      cx = WIDTH - 1;
    if (cz < 0)
      cz = 0;
    if (cz >= HEIGHT)
      cz = HEIGHT - 1;
    return cz * WIDTH + cx;
  }
};

// ─── M9: Zeitgeist Aggregation (Singleton) ────────────────
struct GlobalZeitgeist {
  int angry_peasants;
  int angry_artisans;
  int angry_merchants;
  int total_citizens;
  float avg_satisfaction;
}; // 20 bytes

// ─── Item IDs ─────────────────────────────────────────────
enum ItemType : uint8_t {
  ITEM_NONE = 0,
  ITEM_WHEAT,
  ITEM_BREAD,
  ITEM_MEAT,
  ITEM_SALT_BEEF,
  ITEM_WOOD,
  ITEM_CHARCOAL,
  ITEM_FIREWOOD,
  ITEM_IRON_ORE,
  ITEM_COAL,
  ITEM_PIG_IRON,
  ITEM_STEEL,
  ITEM_SULFUR,
  ITEM_SALTPETER,
  ITEM_BLACK_POWDER,
  ITEM_MUSKET,
  ITEM_BAYONET,
  ITEM_CANNON,
  ITEM_WOOL,
  ITEM_BROADCLOTH,
  ITEM_INDIGO,
  ITEM_BLUE_UNIFORM,
  ITEM_HIDE,
  ITEM_LEATHER,
  ITEM_BOOTS,
  ITEM_SADDLE,
  ITEM_TALLOW,
  ITEM_CANDLE,
  ITEM_TOOL,
  ITEM_BARREL,
  ITEM_BANDAGE,
  ITEM_SURGICAL_TOOL,
  ITEM_ALCOHOL,
  ITEM_COUNT
};

#endif // MUSKET_COMPONENTS_H
