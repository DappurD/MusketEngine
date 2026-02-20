#include "musket_rendering.h"
#include <cmath>
#include <iostream>

namespace godot {
namespace ecs {

// In Godot, a MultiMesh requires 12 floats for the Transform3D matrix
// and 4 floats for custom_data (which we use for vertex animation states)
constexpr int FLOATS_PER_INSTANCE = 16;

void sync_muskets_to_godot(flecs::world &ecs,
                           godot::PackedFloat32Array &buffer_out) {

  // We only query soldiers that are physically alive and moving
  auto q = ecs.query_builder<const Position, const Velocity>()
               .with<IsAlive>()
               .build();

  int active_count = q.count();
  int required_size = active_count * FLOATS_PER_INSTANCE;

  // Resize the Godot buffer instantly in C++ memory
  if (buffer_out.size() != required_size) {
    buffer_out.resize(required_size);
  }

  // Get a raw, writable C++ pointer to the Godot Array's underlying memory
  // This entirely bypasses the expensive Godot C# or GDScript APIs.
  float *dest = buffer_out.ptrw();
  int idx = 0;

  q.iter([&](flecs::iter &it, const Position *p, const Velocity *v) {
    for (int i : it) {

      // 1. Math for the Transform3D Matrix
      // Find the physical angle the soldier is facing based on their velocity
      // vector
      float speed_sq = (v[i].vx * v[i].vx) + (v[i].vz * v[i].vz);
      float speed = std::sqrt(speed_sq);

      float fwd_x = 0.0f;
      float fwd_z = 1.0f; // Default forward

      if (speed > 0.01f) {
        fwd_x = v[i].vx / speed;
        fwd_z = v[i].vz / speed;
      }

      // Right Vector (-z, x)
      float right_x = -fwd_z;
      float right_z = fwd_x;

      int offset = idx * FLOATS_PER_INSTANCE;

      // --- TRANSFORM MATRIX (Godot row-major interleaved: 3 rows of 4) ---
      // Row 0: basis_col0.x, basis_col1.x, basis_col2.x, origin.x
      dest[offset + 0] = right_x; // basis[0].x
      dest[offset + 1] = 0.0f;    // basis[1].x (up.x)
      dest[offset + 2] = fwd_x;   // basis[2].x
      dest[offset + 3] = p[i].x;  // origin.x

      // Row 1: basis_col0.y, basis_col1.y, basis_col2.y, origin.y
      dest[offset + 4] = 0.0f; // basis[0].y (right.y)
      dest[offset + 5] = 1.0f; // basis[1].y (up.y = 1, standing)
      dest[offset + 6] = 0.0f; // basis[2].y (fwd.y)
      dest[offset + 7] = 0.0f; // origin.y (ground level)

      // Row 2: basis_col0.z, basis_col1.z, basis_col2.z, origin.z
      dest[offset + 8] = right_z; // basis[0].z
      dest[offset + 9] = 0.0f;    // basis[1].z (up.z)
      dest[offset + 10] = fwd_z;  // basis[2].z
      dest[offset + 11] = p[i].z; // origin.z

      // --- CUSTOM DATA (Passed straight to Godot Vertex Shader!) ---
      // The Vertex Shader uses this speed float to blend Idle -> Walk -> Run
      // animations!
      dest[offset + 12] = speed; // State/Speed blend (custom_data.r)
      dest[offset + 13] =
          0.0f; // e.g., Animation Frame start offset (custom_data.g)
      dest[offset + 14] = 0.0f; // custom_data.b
      dest[offset + 15] = 0.0f; // custom_data.a

      idx++;
    }
  });
}

} // namespace ecs
} // namespace godot
