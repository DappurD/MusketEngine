#ifndef MUSKET_RENDERING_BRIDGE_H
#define MUSKET_RENDERING_BRIDGE_H

#include "../../flecs/flecs.h"
#include <cstdint>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <vector>

namespace musket {

// ═══════════════════════════════════════════════════════════════
// BATTALION SHADOW BUFFER (M6 — Zero-Copy GPU Upload)
//
// Each battalion holds a persistent PackedFloat32Array as its
// shadow buffer. C++ writes directly via .ptrw() (L1 cache speed).
// GDScript passes the buffer to RenderingServer.multimesh_set_buffer()
// for a single O(1) Vulkan memory transfer per battalion.
// ═══════════════════════════════════════════════════════════════
constexpr int FLOATS_PER_INSTANCE = 16;
constexpr int MAX_BATTALIONS = 64;

struct BattalionShadowBuffer {
  godot::PackedFloat32Array buffer; // Stable slots — zero-copy to RS
  std::vector<uint32_t> free_slots; // Recycling stack
  int max_allocated = 0;
  bool active = false;

  // Allocate a slot, returns mm_slot index
  uint32_t alloc_slot() {
    if (!free_slots.empty()) {
      uint32_t slot = free_slots.back();
      free_slots.pop_back();
      return slot;
    }
    uint32_t slot = (uint32_t)max_allocated;
    max_allocated++;
    // Grow the buffer by 16 floats
    buffer.resize(max_allocated * FLOATS_PER_INSTANCE);
    // Zero-initialize the new slot
    float *dest = buffer.ptrw();
    int offset = slot * FLOATS_PER_INSTANCE;
    for (int j = 0; j < FLOATS_PER_INSTANCE; j++) {
      dest[offset + j] = 0.0f;
    }
    return slot;
  }

  // Recycle a slot (hide it by zeroing scale)
  void free_slot(uint32_t slot) {
    float *dest = buffer.ptrw();
    int offset = slot * FLOATS_PER_INSTANCE;
    // Zero the scale columns to make GPU cull the instance
    dest[offset + 0] = 0.0f;
    dest[offset + 5] = 0.0f;
    dest[offset + 10] = 0.0f;
    free_slots.push_back(slot);
  }
};

// Global battalion registry (lives in rendering_bridge.cpp)
BattalionShadowBuffer &get_battalion(uint32_t battalion_id);
int get_battalion_count();
godot::PackedInt32Array get_active_battalion_ids();

// ── Legacy sync (kept for Strangler Fig migration) ──────────
void sync_transforms(flecs::world &ecs, godot::PackedFloat32Array &buffer_out,
                     int &visible_count_out);

// ── M6: Battalion-aware sync (stable slot writes) ───────────
void sync_battalion_transforms(flecs::world &ecs);

// ── Death: Zero out shadow buffer slot when IsAlive removed ──
void register_death_clear_observer(flecs::world &ecs);

// M5: Packs active ArtilleryShot positions into a flat float array.
void sync_projectiles(flecs::world &ecs, godot::PackedFloat32Array &buffer_out,
                      int &count_out);

} // namespace musket

#endif // MUSKET_RENDERING_BRIDGE_H
