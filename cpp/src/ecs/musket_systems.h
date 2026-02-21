#ifndef MUSKET_SYSTEMS_H
#define MUSKET_SYSTEMS_H

#include "../../flecs/flecs.h"

namespace musket {

// M2: Movement systems (spring-damper + march orders)
void register_movement_systems(flecs::world &ecs);

// M3: Combat systems (reload tick + volley fire)
void register_combat_systems(flecs::world &ecs);

// M4: Panic & Morale (CA diffusion, stiffness coupling, death observer)
void register_panic_systems(flecs::world &ecs);

// M5: Artillery (ballistics, ricochet, canister, limber/unlimber)
void register_artillery_systems(flecs::world &ecs);

// M6: Cavalry (charge momentum, impact, disorder)
void register_cavalry_systems(flecs::world &ecs);

// M9: Economy (citizen movement, workplace logic, matchmaker, zeitgeist)
void register_economy_systems(flecs::world &ecs);

} // namespace musket

#endif // MUSKET_SYSTEMS_H
