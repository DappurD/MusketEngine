#ifndef MUSKET_WORLD_MANAGER_H
#define MUSKET_WORLD_MANAGER_H

#include "../../flecs/flecs.h"
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

namespace godot {

class MusketServer : public Node {
  GDCLASS(MusketServer, Node)

private:
  flecs::world ecs;

  // Legacy rendering (Strangler Fig — remove after M6 verified)
  PackedFloat32Array transform_buffer;
  int visible_count = 0;

  // M5: Projectile rendering
  PackedFloat32Array projectile_buffer;
  int projectile_count = 0;

  // M6: Battalion counter for assigning battalion IDs
  uint32_t next_battalion_id = 0;

protected:
  static void _bind_methods();

public:
  MusketServer();
  ~MusketServer();

  void _ready() override;
  void _process(double delta) override;

  void init_ecs();

  // --- GDScript API ---
  void spawn_test_battalion(int count, float center_x, float center_z,
                            int team_id);
  void order_march(float target_x, float target_z);
  void order_fire(int team_id, float target_x, float target_z);
  int get_alive_count(int team_id) const;

  // Legacy rendering (kept for dual-write migration)
  PackedFloat32Array get_transform_buffer() const;
  int get_visible_count() const;

  // --- M5: Artillery API ---
  void spawn_test_battery(int num_guns, float x, float z, int team_id);
  void order_artillery_fire(int team_id, float target_x, float target_z);
  void order_limber(int team_id);
  void order_unlimber(int team_id);
  PackedFloat32Array get_projectile_buffer() const;
  int get_projectile_count() const;

  // --- M6: Battalion Rendering API ---
  PackedInt32Array get_active_battalions() const;
  PackedFloat32Array get_battalion_buffer(int battalion_id) const;
  int get_battalion_instance_count(int battalion_id) const;

  // --- M6: Cavalry API ---
  void spawn_test_cavalry(int count, float x, float z, int team_id);
  void order_charge(int team_id, float target_x, float target_z);

  // --- M7.5: Fire Discipline + Formation API ---
  void order_fire_discipline(int battalion_id, int discipline_enum);
  void order_formation(int battalion_id, int shape_enum);
};

} // namespace godot

#endif // MUSKET_WORLD_MANAGER_H
