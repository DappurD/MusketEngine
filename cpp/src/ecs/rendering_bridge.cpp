#include "rendering_bridge.h"
#include "musket_components.h"
#include <cmath>

namespace musket {

// ═══════════════════════════════════════════════════════════════
// BUFFER FORMAT CONTRACT (SHARED BY LEGACY AND BATTALION PATHS)
//
// 16 floats per instance, row-major 3×4 + 4 custom:
//
//   [0]  right.x   [1]  up.x   [2]  fwd.x   [3]  origin.x
//   [4]  right.y   [5]  up.y   [6]  fwd.y   [7]  origin.y
//   [8]  right.z   [9]  up.z   [10] fwd.z   [11] origin.z
//   [12] custom.r  [13] custom.g [14] custom.b [15] custom.a
//
// Alive:  [12]=speed [13]=team [14]=0 [15]=0
// Dead:   [12]=cause [13]=death_time [14]=impulse_x [15]=impulse_z
//
// multimesh_set_buffer() consumes this in one Vulkan upload.
// ═══════════════════════════════════════════════════════════════

// ── Battalion Registry (LAZY INIT) ────────────────────────────
// Cannot use static BattalionShadowBuffer[] — PackedFloat32Array
// constructors fire before Godot runtime is initialized, crashing
// the DLL on load. Use a pointer allocated on first access.
static BattalionShadowBuffer *g_battalions = nullptr;

static void ensure_battalions() {
  if (g_battalions == nullptr) {
    g_battalions = new BattalionShadowBuffer[MAX_BATTALIONS];
  }
}

BattalionShadowBuffer &get_battalion(uint32_t battalion_id) {
  ensure_battalions();
  return g_battalions[battalion_id % MAX_BATTALIONS];
}

int get_battalion_count() {
  ensure_battalions();
  int count = 0;
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    if (g_battalions[i].active)
      count++;
  }
  return count;
}

godot::PackedInt32Array get_active_battalion_ids() {
  ensure_battalions();
  godot::PackedInt32Array ids;
  for (int i = 0; i < MAX_BATTALIONS; i++) {
    if (g_battalions[i].active) {
      ids.push_back(i);
    }
  }
  return ids;
}

// ── Helper: write transform into a dest buffer at offset ──────
static void write_transform(float *dest, int offset, const Position &p,
                            const Velocity &v, float custom_r, float custom_g,
                            float custom_b, float custom_a) {
  float speed_sq = (v.vx * v.vx) + (v.vz * v.vz);

  float fwd_x = 0.0f;
  float fwd_z = 1.0f;
  float speed = 0.0f;

  if (speed_sq > 0.0001f) {
    speed = std::sqrt(speed_sq);
    float inv_speed = 1.0f / speed;
    fwd_x = v.vx * inv_speed;
    fwd_z = v.vz * inv_speed;
  }

  float right_x = -fwd_z;
  float right_z = fwd_x;

  // Row 0: basis_col0.x, basis_col1.x, basis_col2.x, origin.x
  dest[offset + 0] = right_x;
  dest[offset + 1] = 0.0f;
  dest[offset + 2] = fwd_x;
  dest[offset + 3] = p.x;

  // Row 1: basis_col0.y, basis_col1.y, basis_col2.y, origin.y
  dest[offset + 4] = 0.0f;
  dest[offset + 5] = 1.0f;
  dest[offset + 6] = 0.0f;
  dest[offset + 7] = 0.0f; // Ground plane Y

  // Row 2: basis_col0.z, basis_col1.z, basis_col2.z, origin.z
  dest[offset + 8] = right_z;
  dest[offset + 9] = 0.0f;
  dest[offset + 10] = fwd_z;
  dest[offset + 11] = p.z;

  // Custom data
  dest[offset + 12] = custom_r;
  dest[offset + 13] = custom_g;
  dest[offset + 14] = custom_b;
  dest[offset + 15] = custom_a;
}

// ═══════════════════════════════════════════════════════════════
// M6: BATTALION-AWARE SYNC (Stable Slot Writes)
//
// Iterates alive entities WITH RenderSlot. Writes directly into
// each battalion's shadow buffer at the entity's permanent slot.
// Dead entities are SKIPPED (their ragdoll data was already
// written by the OnRemove observer and stays frozen).
// ═══════════════════════════════════════════════════════════════
void sync_battalion_transforms(flecs::world &ecs) {
  auto q = ecs.query_builder<const Position, const Velocity, const TeamId,
                             const RenderSlot>()
               .with<IsAlive>()
               .build();

  q.each([](const Position &p, const Velocity &v, const TeamId &team,
            const RenderSlot &rs) {
    auto &bat = g_battalions[rs.battalion_id % MAX_BATTALIONS];
    float *dest = bat.buffer.ptrw();
    int offset = rs.mm_slot * FLOATS_PER_INSTANCE;

    float speed_sq = (v.vx * v.vx) + (v.vz * v.vz);
    float speed = (speed_sq > 0.0001f) ? std::sqrt(speed_sq) : 0.0f;

    write_transform(dest, offset, p, v, speed, (float)team.team, 0.0f, 0.0f);
  });
}

// ── Death Slot Clearer ─────────────────────────────────────────
// When IsAlive is removed, zero out the entity's shadow buffer slot
// so it disappears from the MultiMesh instead of freezing in place.
void register_death_clear_observer(flecs::world &ecs) {
  ecs.observer<const RenderSlot>("DeathSlotClearer")
      .event(flecs::OnRemove)
      .with<IsAlive>()
      .each([](flecs::entity e, const RenderSlot &rs) {
        ensure_battalions();
        auto &bat = g_battalions[rs.battalion_id % MAX_BATTALIONS];
        float *dest = bat.buffer.ptrw();
        int offset = rs.mm_slot * FLOATS_PER_INSTANCE;

        // Zero the entire slot — basis=0 means scale=0 → invisible
        for (int i = 0; i < FLOATS_PER_INSTANCE; i++) {
          dest[offset + i] = 0.0f;
        }
      });
}

// ═══════════════════════════════════════════════════════════════
// LEGACY: Sequential Repack (Strangler Fig — remove after M6)
// ═══════════════════════════════════════════════════════════════
void sync_transforms(flecs::world &ecs, godot::PackedFloat32Array &buffer_out,
                     int &visible_count_out) {

  auto q = ecs.query_builder<const Position, const Velocity, const TeamId>()
               .with<IsAlive>()
               .build();

  int active_count = q.count();
  visible_count_out = active_count;

  if (active_count == 0) {
    if (buffer_out.size() != 0) {
      buffer_out.resize(0);
    }
    return;
  }

  int required_size = active_count * FLOATS_PER_INSTANCE;
  if (buffer_out.size() != required_size) {
    buffer_out.resize(required_size);
  }

  float *dest = buffer_out.ptrw();
  int idx = 0;

  q.each([&](const Position &p, const Velocity &v, const TeamId &team) {
    float speed_sq = (v.vx * v.vx) + (v.vz * v.vz);
    float speed = (speed_sq > 0.0001f) ? std::sqrt(speed_sq) : 0.0f;
    int offset = idx * FLOATS_PER_INSTANCE;
    write_transform(dest, offset, p, v, speed, (float)team.team, 0.0f, 0.0f);
    idx++;
  });
}

// ═══════════════════════════════════════════════════════════════
// M5: PROJECTILE RENDERING BUFFER
// ═══════════════════════════════════════════════════════════════
constexpr int FLOATS_PER_PROJECTILE = 4;

void sync_projectiles(flecs::world &ecs, godot::PackedFloat32Array &buffer_out,
                      int &count_out) {
  auto q = ecs.query_builder<const ArtilleryShot>().build();

  int active_count = 0;
  q.each([&](flecs::entity e, const ArtilleryShot &shot) {
    if (shot.active)
      active_count++;
  });

  count_out = active_count;

  if (active_count == 0) {
    if (buffer_out.size() != 0) {
      buffer_out.resize(0);
    }
    return;
  }

  int required_size = active_count * FLOATS_PER_PROJECTILE;
  if (buffer_out.size() != required_size) {
    buffer_out.resize(required_size);
  }

  float *dest = buffer_out.ptrw();
  int idx = 0;

  q.each([&](flecs::entity e, const ArtilleryShot &shot) {
    if (!shot.active)
      return;

    int offset = idx * FLOATS_PER_PROJECTILE;
    dest[offset + 0] = shot.x;
    dest[offset + 1] = shot.y;
    dest[offset + 2] = shot.z;
    dest[offset + 3] = (float)shot.ammo;
    idx++;
  });
}

} // namespace musket
