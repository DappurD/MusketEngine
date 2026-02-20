#ifndef ECS_MUSKET_COMPONENTS_H
#define ECS_MUSKET_COMPONENTS_H

#include "flecs.h"
#include <cstdint>
#include <vector>

namespace godot {
namespace ecs {

// ── Musket Era Shared Definitions ──────────────────────────────────────────

constexpr int MAX_SQUAD_MEMBERS =
    120; // Maximum size of a Napoleonic Battalion/Squad instance

// ── Macro Components (The Brain) ─────────────────────────────────────────

// Defines the overarching Bounding Box and navigation state of a Battalion
struct Battalion {
  float center_x;
  float center_z;
  float dir_x; // Normalized Forward Vector
  float dir_z;
  float right_x; // Normalized Right Vector
  float right_z;

  int files;       // Width of formation
  int ranks;       // Depth of formation
  float spacing_x; // Distance between files
  float spacing_z; // Distance between ranks

  float aim_quality; // 0.0 to 1.0 (Effects spread angle of Volumetric Volley)
};

// The Bridge between the Macro Battalion and the Micro Soldiers
struct SquadRoster {
  // Maps a 1D internal index (Rank * Files + File) directly to the soldier's
  // Flecs Entity. 0 = no entity in this slot.
  flecs::entity_t slots[MAX_SQUAD_MEMBERS];
};

// ── Micro Components (The Muscle) ────────────────────────────────────────

// Geometric target calculated by the Battalion for the individual soldier to
// march toward
struct SoldierFormationTarget {
  float target_x;
  float target_z;
  float base_stiffness;
  float damping_multiplier;
};

// ── Projectile Components (The Sword) ────────────────────────────────────

// Artillery Kinematics tracking kinetic penetration rather than 'damage'
struct ArtilleryShot {
  float x;
  float y;
  float z;
  float vx;
  float vy;
  float vz;

  float kinetic_energy; // Momentum tracking. Loss of 1.0 per man penetrated.
  bool active;
};

// ── Environmental/Psychological Components (The Mind) ──────────────────────

// Double-Buffered Cellular Automata for localized panic diffusion
struct PanicGrid {
  int width;
  int height;
  float cell_size; // e.g., 4.0 meters per voxel

  int chunk_size; // e.g., 16 cells

  // Flat contiguous arrays for L3 Cache localization
  std::vector<float> read_buffer;
  std::vector<float> write_buffer;
  std::vector<uint8_t>
      active_chunks; // 1 = Awake (Process in CA tick), 0 = Asleep
};

} // namespace ecs
} // namespace godot

#endif // ECS_MUSKET_COMPONENTS_H
