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

// ─── Combat: State ────────────────────────────────────────
struct IsAlive {}; // Tag (0 bytes)

struct MusketState {
  float reload_timer;     // Seconds remaining
  uint8_t ammo_count;     // Rounds left
  uint8_t misfire_chance; // 0-255 scaled (humidity)
}; // 6 bytes

// ─── Combat: Orders ───────────────────────────────────────
struct MovementOrder {
  float target_x, target_z;
  bool arrived;
};                   // 12 bytes
struct HaltOrder {}; // Tag
struct FireOrder {
  float target_x, target_z;
}; // 8 bytes

// ─── Combat: Artillery ────────────────────────────────────
struct ArtilleryShot {
  float x, y, z;        // World position
  float vx, vy, vz;     // Velocity
  float kinetic_energy; // -1.0 per man
  bool active;
}; // 32 bytes

struct ArtilleryBattery {
  int num_guns;
  float reload_timer;
  float traverse_angle;
  int ammo_roundshot;
  int ammo_canister;
  bool is_limbered;
}; // 24 bytes

// ─── Combat: Cavalry ──────────────────────────────────────
struct CavalryState {
  float charge_momentum;
  bool is_charging;
  float charge_timer;
}; // 12 bytes

// ─── Combat: Command Network ─────────────────────────────
struct FormationAnchor {}; // Flag bearer tag
struct OrderLatency {
  float delay_seconds;
};                     // Drummer: 2.0 alive, 8.0 dead
struct ElevatedLOS {}; // Officer tag

// ─── Combat: Medical ──────────────────────────────────────
struct Downed {
  float bleed_timer;
};                 // Panic emitter, awaiting stretcher
struct Veteran {}; // Survived surgery tag
struct Amputee {}; // Restricted jobs tag

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
