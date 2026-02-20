#ifndef VOXEL_MATERIALS_H
#define VOXEL_MATERIALS_H

#include <cstdint>

namespace godot {

/// Material IDs stored per-voxel (uint8_t → 256 max).
enum VoxelMaterial : uint8_t {
    MAT_AIR      = 0,
    MAT_DIRT     = 1,
    MAT_STONE    = 2,
    MAT_WOOD     = 3,
    MAT_STEEL    = 4,
    MAT_CONCRETE = 5,
    MAT_BRICK    = 6,
    MAT_GLASS    = 7,
    MAT_SAND     = 8,
    MAT_WATER    = 9,
    MAT_GRASS    = 10,
    MAT_GRAVEL   = 11,
    MAT_SANDBAG  = 12,
    MAT_CLAY     = 13,
    MAT_METAL_PLATE = 14,
    MAT_RUST     = 15,
    // Resource materials for Economy AI
    MAT_METAL_ORE = 16,
    MAT_CRYSTAL   = 17,
    MAT_ENERGY_CORE = 18,
    MAT_COUNT    = 19   // Expand as needed up to 255
};

/// Physical properties per material for AI and destruction.
struct MaterialProperties {
    float density;       // kg/m³ — used for ballistic penetration
    float health;        // HP per voxel — 0 = indestructible terrain
    float flammability;  // 0.0 = fireproof, 1.0 = ignites easily
    uint8_t r, g, b;     // Base color for mesher (material atlas)
    float roughness;     // PBR: 0.0 = mirror, 1.0 = rough (matte)
    float metallic;      // PBR: 0.0 = dielectric, 1.0 = metal
    float emission;      // PBR: 0.0 = none, 1.0+ = glowing
    // Extended PBR (Phase 5A) — row 1 of 16x2 LUT
    float subsurface;    // Subsurface scattering strength (wood warmth, skin translucency)
    float anisotropy;    // Directional highlight elongation (brushed metal, wood grain)
    float normal_strength; // Per-material normal map intensity multiplier
    float specular_tint; // Tint specular reflections with albedo color
};

/// Lookup table indexed by VoxelMaterial.
/// AI reads density for wall penetration calculations.
/// Mesher reads r,g,b for vertex coloring + packs material ID into alpha.
/// PBR values (roughness, metallic, emission) are reference — shader reads from GDScript LUT.
/// Extended PBR (subsurface, anisotropy, normal_strength, specular_tint) stored in row 1 of 16x2 LUT.
static constexpr MaterialProperties MATERIAL_TABLE[MAT_COUNT] = {
    //                                                                                            Extended PBR (Phase 5A)
    // density    health  flamm   R     G     B     rough  metal  emit    subsrf  aniso  nrm_str  spec_tint
    {   0.0f,     0.0f,   0.0f,    0,    0,    0,   1.00f, 0.00f, 0.0f,  0.00f,  0.00f,  0.00f,  0.00f }, // AIR
    { 1500.0f,   50.0f,   0.0f,  120,   85,   55,   0.95f, 0.00f, 0.0f,  0.10f,  0.00f,  0.60f,  0.10f }, // DIRT
    { 2600.0f,  200.0f,   0.0f,  128,  128,  128,   0.85f, 0.00f, 0.0f,  0.00f,  0.00f,  0.80f,  0.05f }, // STONE
    {  600.0f,   80.0f,   0.8f,  160,  120,   70,   0.80f, 0.00f, 0.0f,  0.20f,  0.30f,  0.70f,  0.15f }, // WOOD
    { 7800.0f,  500.0f,   0.0f,  180,  180,  190,   0.35f, 0.85f, 0.0f,  0.00f,  0.50f,  0.50f,  0.30f }, // STEEL
    { 2400.0f,  300.0f,   0.0f,  200,  200,  195,   0.90f, 0.00f, 0.0f,  0.00f,  0.00f,  0.75f,  0.05f }, // CONCRETE
    { 1900.0f,  150.0f,   0.0f,  180,   80,   60,   0.85f, 0.00f, 0.0f,  0.00f,  0.00f,  0.70f,  0.10f }, // BRICK
    { 2500.0f,   20.0f,   0.0f,  200,  220,  240,   0.05f, 0.00f, 0.0f,  0.00f,  0.00f,  0.10f,  0.80f }, // GLASS
    { 1600.0f,   30.0f,   0.0f,  210,  190,  140,   0.95f, 0.00f, 0.0f,  0.05f,  0.00f,  0.50f,  0.05f }, // SAND
    { 1000.0f,    0.0f,   0.0f,   40,   80,  200,   0.10f, 0.00f, 0.0f,  0.30f,  0.00f,  0.20f,  0.60f }, // WATER
    {  800.0f,   40.0f,   0.3f,   80,  150,   50,   0.85f, 0.00f, 0.0f,  0.15f,  0.10f,  0.55f,  0.10f }, // GRASS
    { 1800.0f,   60.0f,   0.0f,  160,  155,  145,   0.90f, 0.00f, 0.0f,  0.00f,  0.00f,  0.65f,  0.05f }, // GRAVEL
    { 1200.0f,  100.0f,   0.1f,  160,  145,  110,   0.88f, 0.00f, 0.0f,  0.05f,  0.00f,  0.45f,  0.05f }, // SANDBAG
    { 1700.0f,   70.0f,   0.0f,  175,  130,   90,   0.82f, 0.00f, 0.0f,  0.08f,  0.05f,  0.55f,  0.08f }, // CLAY
    { 7500.0f,  400.0f,   0.0f,  100,  105,  110,   0.40f, 0.80f, 0.0f,  0.00f,  0.50f,  0.55f,  0.25f }, // METAL_PLATE
    { 7000.0f,  250.0f,   0.0f,  150,   80,   50,   0.70f, 0.50f, 0.0f,  0.00f,  0.20f,  0.70f,  0.20f }, // RUST
    // Resource materials (mineable)
    { 8000.0f,  300.0f,   0.0f,  220,  180,   90,   0.60f, 0.70f, 0.0f,  0.00f,  0.30f,  0.60f,  0.40f }, // METAL_ORE (gold-ish)
    { 2700.0f,  150.0f,   0.0f,  100,  200,  255,   0.25f, 0.10f, 0.3f,  0.40f,  0.00f,  0.30f,  0.70f }, // CRYSTAL (cyan glow)
    { 1500.0f,  200.0f,   0.0f,  255,  220,   50,   0.30f, 0.00f, 0.8f,  0.50f,  0.00f,  0.25f,  0.60f }, // ENERGY_CORE (yellow glow)
};

/// Inline helpers for fast queries (no branching).
inline bool is_material_solid(uint8_t mat) {
    return mat != MAT_AIR && mat != MAT_WATER;
}

inline bool is_material_opaque(uint8_t mat) {
    return mat != MAT_AIR && mat != MAT_GLASS && mat != MAT_WATER;
}

inline float get_material_density(uint8_t mat) {
    return (mat < MAT_COUNT) ? MATERIAL_TABLE[mat].density : 0.0f;
}

inline float get_material_health(uint8_t mat) {
    return (mat < MAT_COUNT) ? MATERIAL_TABLE[mat].health : 0.0f;
}

/// Maximum support distance in meters per material.
/// BFS path length from ground (in voxels) is compared against this / voxel_scale.
/// Values sized so intact multi-story buildings stay up.
inline float get_material_support_distance_m(uint8_t mat) {
    switch (mat) {
        case MAT_STEEL:      return 32.0f;  // strongest
        case MAT_METAL_PLATE:return 28.0f;
        case MAT_STONE:      return 24.0f;
        case MAT_CONCRETE:   return 24.0f;
        case MAT_BRICK:      return 16.0f;
        case MAT_RUST:       return 16.0f;
        case MAT_WOOD:       return 12.0f;
        case MAT_SANDBAG:    return 10.0f;
        case MAT_CLAY:       return  8.0f;
        case MAT_DIRT:       return  6.0f;
        case MAT_SAND:       return  6.0f;
        case MAT_GRAVEL:     return  6.0f;
        case MAT_GRASS:      return  4.0f;
        case MAT_GLASS:      return  4.0f;
        default:             return  8.0f;  // fallback
    }
}

/// Support distance in voxels for a given scale.
/// Returns the BFS distance threshold above which voxels are unsupported.
inline int get_material_support_distance(uint8_t mat, float voxel_scale = 0.25f) {
    return (int)(get_material_support_distance_m(mat) / voxel_scale);
}

} // namespace godot

#endif // VOXEL_MATERIALS_H
