#include "tactical_cover_map.h"
#include "voxel_world.h"

#include <godot_cpp/core/class_db.hpp>
#include <cmath>
#include <algorithm>

using namespace godot;

TacticalCoverMap *TacticalCoverMap::_singleton = nullptr;

// ═══════════════════════════════════════════════════════════════════════
//  Binding
// ═══════════════════════════════════════════════════════════════════════

void TacticalCoverMap::_bind_methods() {
    ClassDB::bind_method(D_METHOD("setup", "world_size_x", "world_size_z", "voxel_scale"), &TacticalCoverMap::setup);
    ClassDB::bind_method(D_METHOD("update_cover", "threat_positions"), &TacticalCoverMap::update_cover);
    ClassDB::bind_method(D_METHOD("get_cover_value", "position", "threat_direction"), &TacticalCoverMap::get_cover_value);
    ClassDB::bind_method(D_METHOD("get_best_cover_at", "position"), &TacticalCoverMap::get_best_cover_at);
    ClassDB::bind_method(D_METHOD("find_covered_position", "from", "threat_pos", "radius"), &TacticalCoverMap::find_covered_position);
    ClassDB::bind_method(D_METHOD("is_covered_from", "position", "threat_pos"), &TacticalCoverMap::is_covered_from);
    ClassDB::bind_method(D_METHOD("get_cells_x"), &TacticalCoverMap::get_cells_x);
    ClassDB::bind_method(D_METHOD("get_cells_z"), &TacticalCoverMap::get_cells_z);
    ClassDB::bind_method(D_METHOD("get_cell_cover", "cx", "cz"), &TacticalCoverMap::get_cell_cover);
}

// ═══════════════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════════════

TacticalCoverMap::TacticalCoverMap() {
    if (_singleton == nullptr) {
        _singleton = this;
    }
}

TacticalCoverMap::~TacticalCoverMap() {
    if (_singleton == this) {
        _singleton = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════

void TacticalCoverMap::setup(int world_size_x, int world_size_z, float voxel_scale) {
    _voxel_scale = voxel_scale;
    _cell_voxels = std::max(1, (int)std::round(1.0f / voxel_scale));
    _cell_size_m = _cell_voxels * voxel_scale;  // ~1.0m per cell at any scale

    _cells_x = world_size_x / _cell_voxels;
    _cells_z = world_size_z / _cell_voxels;

    // World origin is centered in XZ
    _world_offset_x = (float)world_size_x * voxel_scale * 0.5f;
    _world_offset_z = (float)world_size_z * voxel_scale * 0.5f;

    int total_cells = _cells_x * _cells_z;
    _cover.assign(total_cells, 0.0f);

    for (int i = 0; i < MAX_THREATS; i++) {
        _threat_shadows[i].shadow.assign(total_cells, 0.0f);
        _threat_shadows[i].active = false;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Coordinate conversion
// ═══════════════════════════════════════════════════════════════════════

int TacticalCoverMap::_world_to_cell_x(float wx) const {
    return std::clamp(static_cast<int>((wx + _world_offset_x) / _cell_size_m), 0, _cells_x - 1);
}

int TacticalCoverMap::_world_to_cell_z(float wz) const {
    return std::clamp(static_cast<int>((wz + _world_offset_z) / _cell_size_m), 0, _cells_z - 1);
}

float TacticalCoverMap::_cell_to_world_x(int cx) const {
    return (static_cast<float>(cx) + 0.5f) * _cell_size_m - _world_offset_x;
}

float TacticalCoverMap::_cell_to_world_z(int cz) const {
    return (static_cast<float>(cz) + 0.5f) * _cell_size_m - _world_offset_z;
}

int TacticalCoverMap::_cell_index(int cx, int cz) const {
    return cz * _cells_x + cx;
}

bool TacticalCoverMap::_cell_in_bounds(int cx, int cz) const {
    return cx >= 0 && cx < _cells_x && cz >= 0 && cz < _cells_z;
}

// ═══════════════════════════════════════════════════════════════════════
//  Shadow casting for one threat
// ═══════════════════════════════════════════════════════════════════════

void TacticalCoverMap::_cast_threat_shadow(ThreatShadow &shadow) {
    VoxelWorld *vw = VoxelWorld::get_singleton();
    if (vw == nullptr || !vw->is_initialized()) return;

    int total_cells = _cells_x * _cells_z;
    std::fill(shadow.shadow.begin(), shadow.shadow.end(), 0.0f);

    float threat_wx = shadow.position.x;
    float threat_wz = shadow.position.z;

    // Voxel range for wall height checks
    int min_wall_voxels = static_cast<int>(STANDING_MIN_M / _voxel_scale);
    int max_wall_voxels = static_cast<int>(STANDING_MAX_M / _voxel_scale);

    float ray_max_cells = RAY_MAX_DIST_M / _cell_size_m;

    // Cast RAY_COUNT rays outward from the threat position
    for (int r = 0; r < RAY_COUNT; r++) {
        float angle = static_cast<float>(r) * (2.0f * 3.14159265f / RAY_COUNT);
        float dx = std::cos(angle);
        float dz = std::sin(angle);

        float shadow_remaining = 0.0f;  // Active shadow depth behind a wall

        // Walk outward in cell-sized steps
        for (float t = 1.0f; t <= ray_max_cells; t += 1.0f) {
            float wx = threat_wx + dx * t * _cell_size_m;
            float wz = threat_wz + dz * t * _cell_size_m;

            int cx = _world_to_cell_x(wx);
            int cz = _world_to_cell_z(wz);
            if (!_cell_in_bounds(cx, cz)) break;

            // Check voxel column at this cell for a wall
            // Convert cell center to voxel coordinates
            Vector3i vc = vw->world_to_voxel(Vector3(wx, 0.0f, wz));

            // Find terrain surface height at this column
            int surface_y = -1;
            for (int sy = std::min(vw->get_world_size_y() - 1, vc.y + max_wall_voxels + 16);
                 sy >= 0; sy--) {
                if (vw->is_solid(vc.x, sy, vc.z)) {
                    surface_y = sy;
                    break;
                }
            }

            if (surface_y < 0) {
                // No solid voxels in this column — shadow decays
                if (shadow_remaining > 0.0f) {
                    int idx = _cell_index(cx, cz);
                    shadow.shadow[idx] = std::max(shadow.shadow[idx], shadow_remaining);
                    shadow_remaining -= _cell_size_m / SHADOW_DEPTH_M;
                    if (shadow_remaining < 0.0f) shadow_remaining = 0.0f;
                }
                continue;
            }

            // Count wall voxels above the surface in the standing height range
            // We scan from the surface up to see how many solid voxels form a wall
            int wall_thickness = 0;
            for (int wy = surface_y; wy >= 0 && wy >= surface_y - max_wall_voxels; wy--) {
                // Check from surface downward for solid voxels that form the wall
                // Actually, the wall is solid voxels at standing height above terrain
                break;
            }
            // Better approach: find the local terrain height (lowest air above solid)
            // and check if there are solid voxels at standing height
            int terrain_y = 0;
            for (int sy = 0; sy <= surface_y; sy++) {
                if (!vw->is_solid(vc.x, sy, vc.z)) {
                    terrain_y = sy;
                    break;
                }
                terrain_y = sy + 1;
            }

            // Wall = solid voxels in the standing range above terrain
            int check_min_y = terrain_y + min_wall_voxels;
            int check_max_y = std::min(terrain_y + max_wall_voxels, vw->get_world_size_y() - 1);

            wall_thickness = 0;
            for (int sy = check_min_y; sy <= check_max_y; sy++) {
                if (vw->is_solid(vc.x, sy, vc.z)) {
                    wall_thickness++;
                }
            }

            if (wall_thickness > 0) {
                // This cell has a wall — start or extend shadow behind it
                float wall_quality = std::min(1.0f, static_cast<float>(wall_thickness) * _voxel_scale * 0.5f);
                shadow_remaining = wall_quality;
                // The wall cell itself provides some cover on the far side
            } else if (shadow_remaining > 0.0f) {
                // Behind a wall — in shadow
                int idx = _cell_index(cx, cz);
                shadow.shadow[idx] = std::max(shadow.shadow[idx], shadow_remaining);
                shadow_remaining -= _cell_size_m / SHADOW_DEPTH_M;
                if (shadow_remaining < 0.0f) shadow_remaining = 0.0f;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Aggregate all threat shadows
// ═══════════════════════════════════════════════════════════════════════

void TacticalCoverMap::_aggregate_cover() {
    int total = _cells_x * _cells_z;
    std::fill(_cover.begin(), _cover.end(), 0.0f);

    for (int t = 0; t < MAX_THREATS; t++) {
        if (!_threat_shadows[t].active) continue;
        for (int i = 0; i < total; i++) {
            _cover[i] = std::max(_cover[i], _threat_shadows[t].shadow[i]);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════════════

void TacticalCoverMap::update_cover(const PackedVector3Array &threat_positions) {
    int count = std::min(static_cast<int>(threat_positions.size()), MAX_THREATS);

    // Deactivate all threats
    for (int i = 0; i < MAX_THREATS; i++) {
        _threat_shadows[i].active = false;
    }

    // Cast shadow for each threat
    for (int i = 0; i < count; i++) {
        _threat_shadows[i].position = threat_positions[i];
        _threat_shadows[i].active = true;
        _cast_threat_shadow(_threat_shadows[i]);
    }

    _aggregate_cover();
}

float TacticalCoverMap::get_cover_value(const Vector3 &position, const Vector3 &threat_direction) const {
    if (_cells_x == 0 || _cells_z == 0) return 0.0f;

    int cx = _world_to_cell_x(position.x);
    int cz = _world_to_cell_z(position.z);
    if (!_cell_in_bounds(cx, cz)) return 0.0f;

    // Find the threat shadow that best matches this direction
    float best_cover = 0.0f;
    Vector3 threat_dir_flat(threat_direction.x, 0.0f, threat_direction.z);
    float threat_len = threat_dir_flat.length();
    if (threat_len < 1e-4f) return _cover[_cell_index(cx, cz)];
    threat_dir_flat /= threat_len;

    for (int t = 0; t < MAX_THREATS; t++) {
        if (!_threat_shadows[t].active) continue;

        // Check if this threat is roughly in the specified direction
        Vector3 to_threat = _threat_shadows[t].position - position;
        to_threat.y = 0.0f;
        float to_len = to_threat.length();
        if (to_len < 1e-4f) continue;
        to_threat /= to_len;

        float dot = threat_dir_flat.dot(to_threat);
        if (dot > 0.5f) {  // Within ~60 degrees of specified direction
            float cover = _threat_shadows[t].shadow[_cell_index(cx, cz)];
            best_cover = std::max(best_cover, cover);
        }
    }

    return best_cover;
}

float TacticalCoverMap::get_best_cover_at(const Vector3 &position) const {
    if (_cells_x == 0 || _cells_z == 0) return 0.0f;

    int cx = _world_to_cell_x(position.x);
    int cz = _world_to_cell_z(position.z);
    if (!_cell_in_bounds(cx, cz)) return 0.0f;

    return _cover[_cell_index(cx, cz)];
}

Vector3 TacticalCoverMap::find_covered_position(
        const Vector3 &from,
        const Vector3 &threat_pos,
        float radius) const {
    if (_cells_x == 0 || _cells_z == 0) return Vector3();

    // Find the threat shadow index closest to threat_pos
    int best_threat = -1;
    float best_dist = 1e18f;
    for (int t = 0; t < MAX_THREATS; t++) {
        if (!_threat_shadows[t].active) continue;
        float d = _threat_shadows[t].position.distance_to(threat_pos);
        if (d < best_dist) {
            best_dist = d;
            best_threat = t;
        }
    }

    if (best_threat < 0) return Vector3();

    const auto &shadow = _threat_shadows[best_threat].shadow;

    // Search grid around 'from' for best covered position
    int search_cells = static_cast<int>(radius / _cell_size_m);
    int from_cx = _world_to_cell_x(from.x);
    int from_cz = _world_to_cell_z(from.z);

    Vector3 best_pos;
    float best_score = -1.0f;

    for (int dz = -search_cells; dz <= search_cells; dz++) {
        for (int dx = -search_cells; dx <= search_cells; dx++) {
            int cx = from_cx + dx;
            int cz = from_cz + dz;
            if (!_cell_in_bounds(cx, cz)) continue;

            float cover = shadow[_cell_index(cx, cz)];
            if (cover <= 0.0f) continue;

            float wx = _cell_to_world_x(cx);
            float wz = _cell_to_world_z(cz);
            float dist = std::sqrt(
                (wx - from.x) * (wx - from.x) + (wz - from.z) * (wz - from.z)
            );
            if (dist > radius) continue;

            // Score: cover quality minus distance cost
            float score = cover - dist * 0.02f;
            if (score > best_score) {
                best_score = score;
                best_pos = Vector3(wx, from.y, wz);
            }
        }
    }

    return (best_score > 0.0f) ? best_pos : Vector3();
}

bool TacticalCoverMap::is_covered_from(const Vector3 &position, const Vector3 &threat_pos) const {
    Vector3 threat_dir = threat_pos - position;
    return get_cover_value(position, threat_dir) > 0.3f;
}

float TacticalCoverMap::get_cell_cover(int cx, int cz) const {
    if (!_cell_in_bounds(cx, cz)) return 0.0f;
    return _cover[_cell_index(cx, cz)];
}
