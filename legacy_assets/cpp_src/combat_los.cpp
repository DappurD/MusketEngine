#include "combat_los.h"
#include "voxel_world.h"

#include <algorithm>
#include <cmath>

using namespace godot;

void CombatLOS::_bind_methods() {
    ClassDB::bind_static_method("CombatLOS", D_METHOD("check_visibility", "from_pos", "to_pos", "vision_angle_deg", "vision_range", "peripheral_range", "darkness", "flashlight_on", "facing_direction", "space_state"), &CombatLOS::check_visibility);
    ClassDB::bind_static_method("CombatLOS", D_METHOD("check_friendly_fire", "from_pos", "to_pos", "ally_positions"), &CombatLOS::check_friendly_fire);
    ClassDB::bind_static_method("CombatLOS", D_METHOD("batch_check_visibility", "from_positions", "to_positions", "vision_angle_deg", "vision_range", "peripheral_range", "darkness", "flashlight_flags", "facing_directions", "space_state"), &CombatLOS::batch_check_visibility);
}

Dictionary CombatLOS::check_visibility(
        const Vector3 &from_pos,
        const Vector3 &to_pos,
        float vision_angle_deg,
        float vision_range,
        float peripheral_range,
        float darkness,
        bool flashlight_on,
        const Vector3 &facing_direction,
        PhysicsDirectSpaceState3D *space_state) {

    Dictionary result;
    result["can_see"] = false;
    result["clear_line_of_fire"] = false;
    result["hit_position"] = Vector3();

    Vector3 eye_pos = from_pos + Vector3(0, EYE_HEIGHT, 0);
    Vector3 target_pos = to_pos + Vector3(0, TARGET_CENTER_MASS, 0);
    Vector3 to_target = target_pos - eye_pos;

    // Flat distance and direction for angle checks
    Vector3 to_target_flat(to_target.x, 0, to_target.z);
    float dist = to_target_flat.length();

    if (dist < 1e-4f) {
        // Basically on top of target
        result["can_see"] = true;
        result["clear_line_of_fire"] = true;
        return result;
    }

    Vector3 to_dir = to_target_flat / dist;
    Vector3 facing_flat(facing_direction.x, 0, facing_direction.z);
    float facing_len = facing_flat.length();
    if (facing_len < 1e-4f) facing_flat = Vector3(0, 0, 1);
    else facing_flat /= facing_len;

    // ── Vision cone check ────────────────────────────────────────
    bool in_vision_cone = false;

    if (darkness > 0.3f && flashlight_on) {
        // Flashlight mode: 20m range, 30 degree cone
        if (dist <= 20.0f) {
            float angle_rad = std::acos(std::clamp(facing_flat.dot(to_dir), -1.0f, 1.0f));
            if (angle_rad <= 0.5236f) {  // ~30 degrees
                in_vision_cone = true;
            }
        }
    } else {
        // Standard vision
        float effective_vision = vision_range * (1.0f - darkness * 0.6f);
        float effective_peripheral = peripheral_range * (1.0f - darkness * 0.5f);

        if (dist <= effective_vision) {
            in_vision_cone = true;  // Within full vision range
        } else if (dist <= effective_peripheral) {
            // Check angle — must be within vision angle
            float angle_rad = std::acos(std::clamp(facing_flat.dot(to_dir), -1.0f, 1.0f));
            float half_angle_rad = vision_angle_deg * 0.5f * 0.01745329f;  // deg to rad
            if (angle_rad <= half_angle_rad) {
                in_vision_cone = true;
            }
        }
    }

    // ── Single raycast for both visibility and line-of-fire ──────
    // Only raycast if there's a chance the target is visible
    // The raycast result serves both purposes:
    //   - If clear (no hit): can_see = in_cone, clear_line_of_fire = true
    //   - If blocked: can_see = false, clear_line_of_fire = false

    // ── Voxel-primary LOS check with physics fallback ──────────
    VoxelWorld *vw = VoxelWorld::get_singleton();
    if (vw != nullptr && vw->is_initialized()) {
        // Use voxel DDA raycast for terrain/building LOS
        bool clear = vw->check_los(eye_pos, target_pos);
        if (clear) {
            result["can_see"] = in_vision_cone;
            result["clear_line_of_fire"] = true;
        } else {
            result["can_see"] = false;
            result["clear_line_of_fire"] = false;
            // Get exact hit position via full raycast
            VoxelHit hit;
            Vector3 dir = (target_pos - eye_pos).normalized();
            float max_d = eye_pos.distance_to(target_pos);
            if (vw->raycast(eye_pos, dir, max_d, hit)) {
                result["hit_position"] = hit.world_pos;
            }
        }
    } else if (space_state != nullptr) {
        // Fallback: physics raycast for non-voxel scenes
        Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(
            eye_pos, target_pos, VISION_MASK
        );
        query->set_hit_from_inside(false);

        Dictionary ray_result = space_state->intersect_ray(query);

        if (ray_result.is_empty()) {
            result["can_see"] = in_vision_cone;
            result["clear_line_of_fire"] = true;
        } else {
            result["can_see"] = false;
            result["clear_line_of_fire"] = false;
            result["hit_position"] = ray_result["position"];
        }
    }

    return result;
}

bool CombatLOS::check_friendly_fire(
        const Vector3 &from_pos,
        const Vector3 &to_pos,
        const PackedVector3Array &ally_positions) {

    Vector3 aim_vec = to_pos - from_pos;
    aim_vec.y = 0;
    float aim_dist = aim_vec.length();
    if (aim_dist < 1e-4f) return false;

    Vector3 aim_dir = aim_vec / aim_dist;

    for (int i = 0; i < ally_positions.size(); i++) {
        Vector3 to_ally = ally_positions[i] - from_pos;
        to_ally.y = 0;

        // Project ally onto aim line
        float proj_dist = to_ally.dot(aim_dir);

        // Skip if ally is behind us or way past target
        if (proj_dist < FRIENDLY_FIRE_MIN_D || proj_dist > aim_dist + 1.0f) continue;

        // Perpendicular distance from aim line
        Vector3 proj_point = aim_dir * proj_dist;
        float perp_dist = (to_ally - proj_point).length();

        // Corridor width increases with distance (spread factor)
        float spread_factor = proj_dist / aim_dist;
        float safe_width = FRIENDLY_FIRE_BASE_W + (spread_factor * 0.4f) + FRIENDLY_FIRE_UNIT_R;

        if (perp_dist < safe_width) return true;
    }

    return false;
}

Array CombatLOS::batch_check_visibility(
        const PackedVector3Array &from_positions,
        const PackedVector3Array &to_positions,
        float vision_angle_deg,
        float vision_range,
        float peripheral_range,
        float darkness,
        const PackedInt32Array &flashlight_flags,
        const PackedVector3Array &facing_directions,
        PhysicsDirectSpaceState3D *space_state) {

    Array results;
    int count = from_positions.size();
    if (count != to_positions.size()) return results;

    for (int i = 0; i < count; i++) {
        bool flashlight = (i < flashlight_flags.size()) ? (flashlight_flags[i] != 0) : false;
        Vector3 facing = (i < facing_directions.size()) ? facing_directions[i] : Vector3(0, 0, 1);

        Dictionary r = check_visibility(
            from_positions[i], to_positions[i],
            vision_angle_deg, vision_range, peripheral_range,
            darkness, flashlight, facing, space_state
        );
        results.append(r);
    }

    return results;
}
