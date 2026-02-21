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
struct SoldierFormationTarget {
  float target_x, target_z; // Slot position
  float base_stiffness;     // Modified by morale/uniforms
  float damping_multiplier; // ~2.0 for critical damping
}; // 16 bytes

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
// Pre-computed per-frame. O(1) lookups for charge targeting
// AND M7 command network state (flag/drummer/officer alive).
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
  ORDER_CHARGE
};
struct PendingOrder {
  OrderType type = ORDER_NONE;
  float target_x = 0.0f;
  float target_z = 0.0f;
  float delay = 0.0f;
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
// ─── Economy: Civilian ────────────────────────────────────
struct Citizen {
  enum State : uint8_t { IDLE, WALKING_TO_SOURCE, WALKING_TO_DEST };
  State current_state;
  uint8_t carrying_item; // ITEM_WHEAT, ITEM_MUSKET, etc.
  uint8_t carrying_amount;
  // current_target is stored as a Flecs relationship, not inline
}; // 3 bytes (lightweight!)

// ─── Economy: Buildings ───────────────────────────────────
struct Workplace {
  uint8_t consumes_item;
  uint8_t produces_item;
  int16_t inventory_in;
  int16_t inventory_out;
  float tool_durability; // Degrades over time
}; // 10 bytes

// ─── Economy: Social ──────────────────────────────────────
struct SocialClass {
  enum Type : uint8_t { PEASANT, ARTISAN, MERCHANT, NOBLE };
  Type type;
}; // 1 byte

struct Morale {
  float value;
}; // 4 bytes (0.0 = enraged, 1.0 = loyal)

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
