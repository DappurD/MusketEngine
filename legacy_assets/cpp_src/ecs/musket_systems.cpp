#include "musket_systems.h"
#include "components.h" // Requires existing Position, Velocity
#include "musket_components.h"
#include <algorithm>
#include <cmath>


namespace godot {
namespace ecs {

void register_musket_systems(flecs::world &ecs) {

  // -------------------------------------------------------------------------
  // 1. THE MUSCLE (Micro-Physics)
  // -------------------------------------------------------------------------
  // O(1) continuous physics spring integrating Panic and Target coordinates.
  // Iterates smoothly over tens of thousands of entities per tick.
  ecs.system<Position, Velocity, const SoldierFormationTarget>(
         "MusketSpringDamperPhysics")
      .iter([](flecs::iter &it, Position *p, Velocity *v,
               const SoldierFormationTarget *target) {
        float dt = it.delta_time();

        for (int i : it) {
          float dx = target[i].target_x - p[i].x;
          float dz = target[i].target_z - p[i].z;

          float stiffness = target[i].base_stiffness;
          float damping = target[i].damping_multiplier * std::sqrt(stiffness);

          float force_x = (stiffness * dx) - (damping * v[i].vx);
          float force_z = (stiffness * dz) - (damping * v[i].vz);

          v[i].vx += force_x * dt;
          v[i].vz += force_z * dt;

          // Max speed cap to prevent supersonic wheeling or rubber-banding
          const float MAX_SPEED = 4.0f;
          float speed_sq = (v[i].vx * v[i].vx) + (v[i].vz * v[i].vz);
          if (speed_sq > (MAX_SPEED * MAX_SPEED)) {
            float speed = std::sqrt(speed_sq);
            v[i].vx = (v[i].vx / speed) * MAX_SPEED;
            v[i].vz = (v[i].vz / speed) * MAX_SPEED;
          }

          p[i].x += v[i].vx * dt;
          p[i].z += v[i].vz * dt;
        }
      });

  // -------------------------------------------------------------------------
  // 2. THE GOD OF WAR (Artillery Traversal)
  // -------------------------------------------------------------------------
  // Advances cannonball kinematics before they hit the Inverse Sieve/Grid logic
  ecs.system<ArtilleryShot>("MusketArtilleryTraversal")
      .iter([](flecs::iter &it, ArtilleryShot *shots) {
        float dt = it.delta_time();

        for (int i : it) {
          if (!shots[i].active)
            continue;

          // Simple Gravity/Kinematics Integration
          shots[i].vy -= 9.81f * dt;

          shots[i].x += shots[i].vx * dt;
          shots[i].y += shots[i].vy * dt;
          shots[i].z += shots[i].vz * dt;
        }
      });

  // -------------------------------------------------------------------------
  // 3. CHAOS & FRICTION (Panic / Slaughter Observer)
  // -------------------------------------------------------------------------
  // Listens instantly for a Death (removal of IsAlive tag) and executes
  // visual/AI triggers
  ecs.observer<Position>("OnSlaughter_InjectTerror")
      .event(flecs::OnRemove)
      .with<IsAlive>()
      .each([](flecs::iter &it, size_t i, Position &pos) {
        // A. Locate the global PanicGrid pointer (omitted here for structural
        // brevity) B. Compute integer chunk/cell index based on pos.x and pos.z
        // C. Spike the PanicGrid value: grid[idx] += 0.4f;

        // D. (Deferred) Send an event to the Godot Sync Bridge:
        // "Spawn a ragdoll at X, Z with a physics impulse!"
      });
}

} // namespace ecs
} // namespace godot
