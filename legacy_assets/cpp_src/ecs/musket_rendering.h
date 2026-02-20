#include "components.h"
#include "flecs.h"
#include <godot_cpp/variant/packed_float32_array.hpp>


namespace godot {
namespace ecs {

// Syncs 10,000 ECS positions/velocities instantly into a raw Godot float array
// for zero-overhead MultiMeshInstance3D and Vertex Animation Shader rendering.
void sync_muskets_to_godot(flecs::world &ecs,
                           godot::PackedFloat32Array &buffer_out);

} // namespace ecs
} // namespace godot
