#ifndef ECS_MUSKET_SYSTEMS_H
#define ECS_MUSKET_SYSTEMS_H

#include "flecs.h"

namespace godot {
namespace ecs {

// Registers all pure-math Data-Oriented simulation systems for Musket-Era
// combat
void register_musket_systems(flecs::world &ecs);

} // namespace ecs
} // namespace godot

#endif // ECS_MUSKET_SYSTEMS_H
