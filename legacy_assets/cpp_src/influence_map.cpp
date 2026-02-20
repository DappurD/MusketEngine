#include "influence_map.h"
#include "tactical_cover_map.h"

#include <algorithm>
#include <cstring>
#include <cmath>

using namespace godot;

// ═══════════════════════════════════════════════════════════════════
//  Bindings
// ═══════════════════════════════════════════════════════════════════

void InfluenceMapCPP::_bind_methods() {
    ClassDB::bind_method(D_METHOD("setup", "team", "map_w", "map_h", "sector_size"), &InfluenceMapCPP::setup, DEFVAL(DEFAULT_SECTOR_SIZE));
    ClassDB::bind_method(D_METHOD("update", "positions", "teams", "in_combat"), &InfluenceMapCPP::update);
    ClassDB::bind_method(D_METHOD("update_cover_quality"), &InfluenceMapCPP::update_cover_quality);
    ClassDB::bind_method(D_METHOD("get_threat_at", "pos"), &InfluenceMapCPP::get_threat_at);
    ClassDB::bind_method(D_METHOD("get_highest_threat_sector"), &InfluenceMapCPP::get_highest_threat_sector);
    ClassDB::bind_method(D_METHOD("get_opportunity_sectors"), &InfluenceMapCPP::get_opportunity_sectors);
    ClassDB::bind_method(D_METHOD("get_front_line_x", "fallback_front"), &InfluenceMapCPP::get_front_line_x);
    ClassDB::bind_method(D_METHOD("get_enemy_density_near", "pos"), &InfluenceMapCPP::get_enemy_density_near);
    ClassDB::bind_method(D_METHOD("get_friendly", "sx", "sz"), &InfluenceMapCPP::get_friendly);
    ClassDB::bind_method(D_METHOD("get_enemy", "sx", "sz"), &InfluenceMapCPP::get_enemy);
    ClassDB::bind_method(D_METHOD("get_threat", "sx", "sz"), &InfluenceMapCPP::get_threat);
    ClassDB::bind_method(D_METHOD("get_opportunity", "sx", "sz"), &InfluenceMapCPP::get_opportunity);
    ClassDB::bind_method(D_METHOD("get_cover_quality", "sx", "sz"), &InfluenceMapCPP::get_cover_quality);
    ClassDB::bind_method(D_METHOD("world_to_sector", "pos"), &InfluenceMapCPP::world_to_sector);
    ClassDB::bind_method(D_METHOD("sector_to_world", "sx", "sz"), &InfluenceMapCPP::sector_to_world);
    ClassDB::bind_method(D_METHOD("get_sectors_x"), &InfluenceMapCPP::get_sectors_x);
    ClassDB::bind_method(D_METHOD("get_sectors_z"), &InfluenceMapCPP::get_sectors_z);
    ClassDB::bind_method(D_METHOD("get_sector_size"), &InfluenceMapCPP::get_sector_size);
}

// ═══════════════════════════════════════════════════════════════════
//  Constructor
// ═══════════════════════════════════════════════════════════════════

InfluenceMapCPP::InfluenceMapCPP() {
}

// ═══════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════

void InfluenceMapCPP::setup(int team, float map_w, float map_h, float sector_size) {
    _team = team;
    _map_w = map_w;
    _map_h = map_h;
    _sector_size = sector_size;

    _sectors_x = std::max(1, static_cast<int>(std::ceil(map_w / sector_size)));
    _sectors_z = std::max(1, static_cast<int>(std::ceil(map_h / sector_size)));
    _grid_size = _sectors_x * _sectors_z;

    _friendly.assign(_grid_size, 0);
    _enemy.assign(_grid_size, 0);
    _threat.assign(_grid_size, 0.0f);
    _opportunity.assign(_grid_size, 0.0f);
    _combat_recency.assign(_grid_size, 0.0f);
    _cover_quality.assign(_grid_size, 0.0f);
}

// ═══════════════════════════════════════════════════════════════════
//  Coordinate conversion
// ═══════════════════════════════════════════════════════════════════

Vector2i InfluenceMapCPP::world_to_sector(const Vector3 &pos) const {
    int sx = std::clamp(static_cast<int>((pos.x + _map_w * 0.5f) / _sector_size), 0, _sectors_x - 1);
    int sz = std::clamp(static_cast<int>((pos.z + _map_h * 0.5f) / _sector_size), 0, _sectors_z - 1);
    return Vector2i(sx, sz);
}

Vector3 InfluenceMapCPP::sector_to_world(int sx, int sz) const {
    float wx = (static_cast<float>(sx) + 0.5f) * _sector_size - _map_w * 0.5f;
    float wz = (static_cast<float>(sz) + 0.5f) * _sector_size - _map_h * 0.5f;
    return Vector3(wx, 0, wz);
}

// ═══════════════════════════════════════════════════════════════════
//  Update — rebuild all layers from unit data
// ═══════════════════════════════════════════════════════════════════

void InfluenceMapCPP::update(
        const PackedVector3Array &positions,
        const PackedInt32Array &teams,
        const PackedFloat32Array &in_combat) {

    if (_grid_size == 0) return;

    int count = positions.size();
    int enemy_team = (_team == 1) ? 2 : 1;

    // Phase 1: Decay combat recency and clear counts
    for (int i = 0; i < _grid_size; i++) {
        _combat_recency[i] = std::max(_combat_recency[i] - RECENCY_DECAY, 0.0f);
        _friendly[i] = 0;
        _enemy[i] = 0;
        _threat[i] = 0.0f;
        _opportunity[i] = 0.0f;
    }

    // Phase 2: Populate from unit data
    const Vector3 *pos_ptr = positions.ptr();
    const int32_t *team_ptr = teams.ptr();
    const float *combat_ptr = in_combat.size() >= count ? in_combat.ptr() : nullptr;

    for (int i = 0; i < count; i++) {
        Vector2i sec = world_to_sector(pos_ptr[i]);
        int idx = _idx(sec.x, sec.y);

        if (team_ptr[i] == _team) {
            _friendly[idx]++;
        } else if (team_ptr[i] == enemy_team) {
            _enemy[idx]++;
            if (combat_ptr && combat_ptr[i] > 0.5f) {
                _combat_recency[idx] = 1.0f;
            }
        }
    }

    // Phase 3: Compute threat and opportunity with neighbor bleed
    for (int x = 0; x < _sectors_x; x++) {
        for (int z = 0; z < _sectors_z; z++) {
            int idx = _idx(x, z);
            int enemy_count = _enemy[idx];
            int friendly_count = _friendly[idx];

            // Neighbor influence bleed
            for (int dx = -1; dx <= 1; dx++) {
                for (int dz = -1; dz <= 1; dz++) {
                    if (dx == 0 && dz == 0) continue;
                    int nx = x + dx;
                    int nz = z + dz;
                    if (_in_bounds(nx, nz)) {
                        int nidx = _idx(nx, nz);
                        enemy_count += _enemy[nidx] / 2;      // Half-weight neighbors
                        friendly_count += _friendly[nidx] / 3;
                    }
                }
            }

            // Threat: enemy density minus friendly density
            _threat[idx] = std::clamp(static_cast<float>(enemy_count) - static_cast<float>(friendly_count) * 0.5f, 0.0f, 10.0f);

            // Opportunity: weak enemy presence we can exploit
            if (_enemy[idx] > 0 && _enemy[idx] <= 2 && friendly_count >= _enemy[idx]) {
                _opportunity[idx] = 3.0f - static_cast<float>(_enemy[idx]);
            } else if (_enemy[idx] == 0 && _combat_recency[idx] > 0.5f) {
                _opportunity[idx] = 1.5f;  // Recently cleared, possible flanking route
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Update cover quality from TacticalCoverMap
// ═══════════════════════════════════════════════════════════════════

void InfluenceMapCPP::update_cover_quality() {
    TacticalCoverMap *tcm = TacticalCoverMap::get_singleton();
    if (tcm == nullptr || _grid_size == 0) return;

    for (int x = 0; x < _sectors_x; x++) {
        for (int z = 0; z < _sectors_z; z++) {
            Vector3 world_pos = sector_to_world(x, z);
            _cover_quality[_idx(x, z)] = tcm->get_best_cover_at(world_pos);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Queries
// ═══════════════════════════════════════════════════════════════════

float InfluenceMapCPP::get_threat_at(const Vector3 &pos) const {
    if (_grid_size == 0) return 0.0f;
    Vector2i sec = world_to_sector(pos);
    return _threat[_idx(sec.x, sec.y)];
}

Vector3 InfluenceMapCPP::get_highest_threat_sector() const {
    float best = -1.0f;
    int best_x = 0, best_z = 0;
    for (int x = 0; x < _sectors_x; x++) {
        for (int z = 0; z < _sectors_z; z++) {
            float t = _threat[_idx(x, z)];
            if (t > best) {
                best = t;
                best_x = x;
                best_z = z;
            }
        }
    }
    return sector_to_world(best_x, best_z);
}

PackedVector3Array InfluenceMapCPP::get_opportunity_sectors() const {
    PackedVector3Array result;
    for (int x = 0; x < _sectors_x; x++) {
        for (int z = 0; z < _sectors_z; z++) {
            if (_opportunity[_idx(x, z)] > 1.0f) {
                result.append(sector_to_world(x, z));
            }
        }
    }
    return result;
}

float InfluenceMapCPP::get_front_line_x(float fallback_front) const {
    float total_x = 0.0f;
    int count = 0;
    for (int x = 0; x < _sectors_x; x++) {
        for (int z = 0; z < _sectors_z; z++) {
            int idx = _idx(x, z);
            if (_friendly[idx] > 0 && _enemy[idx] > 0) {
                total_x += sector_to_world(x, z).x;
                count++;
            }
        }
    }
    return (count > 0) ? total_x / static_cast<float>(count) : fallback_front;
}

int InfluenceMapCPP::get_enemy_density_near(const Vector3 &pos) const {
    if (_grid_size == 0) return 0;
    Vector2i sec = world_to_sector(pos);
    int count = _enemy[_idx(sec.x, sec.y)];
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dz == 0) continue;
            int nx = sec.x + dx;
            int nz = sec.y + dz;
            if (_in_bounds(nx, nz)) {
                count += _enemy[_idx(nx, nz)];
            }
        }
    }
    return count;
}

// ── Raw layer access ──────────────────────────────────────────────

float InfluenceMapCPP::get_friendly(int sx, int sz) const {
    if (!_in_bounds(sx, sz)) return 0.0f;
    return static_cast<float>(_friendly[_idx(sx, sz)]);
}

float InfluenceMapCPP::get_enemy(int sx, int sz) const {
    if (!_in_bounds(sx, sz)) return 0.0f;
    return static_cast<float>(_enemy[_idx(sx, sz)]);
}

float InfluenceMapCPP::get_threat(int sx, int sz) const {
    if (!_in_bounds(sx, sz)) return 0.0f;
    return _threat[_idx(sx, sz)];
}

float InfluenceMapCPP::get_opportunity(int sx, int sz) const {
    if (!_in_bounds(sx, sz)) return 0.0f;
    return _opportunity[_idx(sx, sz)];
}

float InfluenceMapCPP::get_cover_quality(int sx, int sz) const {
    if (!_in_bounds(sx, sz)) return 0.0f;
    return _cover_quality[_idx(sx, sz)];
}
