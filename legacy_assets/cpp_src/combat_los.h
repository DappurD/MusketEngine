#ifndef COMBAT_LOS_H
#define COMBAT_LOS_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/array.hpp>

namespace godot {

/// Combined visibility + line-of-fire check in a single raycast.
/// Replaces separate can_see() + has_clear_line_of_fire() calls in Unit.gd.
class CombatLOS : public RefCounted {
    GDCLASS(CombatLOS, RefCounted)

public:
    // ── Configuration ────────────────────────────────────────────
    static constexpr float EYE_HEIGHT            = 1.5f;
    static constexpr float TARGET_CENTER_MASS    = 1.0f;
    static constexpr int   VISION_MASK           = 1 | 4 | 8 | 64 | 256;  // World|Buildings|Cover|Trees|Smoke
    static constexpr float FRIENDLY_FIRE_BASE_W  = 0.55f;
    static constexpr float FRIENDLY_FIRE_UNIT_R  = 0.35f;
    static constexpr float FRIENDLY_FIRE_MIN_D   = 1.5f;

    /// Combined visibility check. Returns Dictionary:
    ///   "can_see": bool — is the target visible given vision cone + darkness?
    ///   "clear_line_of_fire": bool — no world geometry blocking the shot?
    ///   "hit_position": Vector3 — where the ray hit (if blocked)
    /// Performs at most ONE raycast instead of two separate calls.
    static Dictionary check_visibility(
        const Vector3 &from_pos,
        const Vector3 &to_pos,
        float vision_angle_deg,
        float vision_range,
        float peripheral_range,
        float darkness,
        bool flashlight_on,
        const Vector3 &facing_direction,
        PhysicsDirectSpaceState3D *space_state
    );

    /// Check if friendly units block the line of fire.
    /// Uses pre-gathered neighbor positions instead of iterating all units.
    /// ally_positions: PackedVector3Array of same-team unit positions (from SpatialGrid)
    /// Returns true if an ally is blocking the shot.
    static bool check_friendly_fire(
        const Vector3 &from_pos,
        const Vector3 &to_pos,
        const PackedVector3Array &ally_positions
    );

    /// Batch check visibility for multiple from→to pairs.
    /// Returns Array of Dictionaries (same format as check_visibility).
    static Array batch_check_visibility(
        const PackedVector3Array &from_positions,
        const PackedVector3Array &to_positions,
        float vision_angle_deg,
        float vision_range,
        float peripheral_range,
        float darkness,
        const PackedInt32Array &flashlight_flags,
        const PackedVector3Array &facing_directions,
        PhysicsDirectSpaceState3D *space_state
    );

protected:
    static void _bind_methods();
};

} // namespace godot

#endif // COMBAT_LOS_H
