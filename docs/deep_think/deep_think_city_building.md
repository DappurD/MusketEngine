# Deep Think Prompt #5: City Building & Urbanism — M15-M18

> **PREREQUISITE**: Read Prompt #0 first. Requires M9 (Citizens), M11-12 (Economy), M13-14 (Voxels).

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §7.1 | Smart Buildings, Dumb Agents: buildings post to `LogisticsJob` array, 1Hz matchmaker |
| GDD | §7.2 | Deep Production Chains: Arsenal, Fabric, Burgage |
| GDD | §7.4 | Procedural Urbanism: Voronoi/Straight-Skeleton, Vauban solver, Haussmannization |
| GDD | §15.4 | Architect Controls: spline drawing, magnetic snapping, polygon zoning |
| CORE_MATH | §5 | Vauban Magistral Line vertex math |
| CORE_MATH | §6 | Workplace/LogisticsJob structs |

## § What Exists In GDD

### Building Placement (GDD §15.4)
- **Spline Drawing**: Roads & walls via `Curve3D` bezier
- **Magnetic Snapping**: 2m sphere-cast. Snap to entrances, roads, Vauban vertices
- **Polygon Zoning**: Burgage Plots via Voronoi subdivision + mouse wheel density

### Procedural Generation (GDD §7.4)
- **Burgage Plots**: Curved spline → Voronoi/Straight-Skeleton → dynamic fence-lined lots
- **Vauban Star Forts**: Macro-polygon → magistral line solver (CORE_MATH §5)
- **Haussmannization**: Late-game: demolish star forts → boulevards → 2× agent speed

### Art Pipeline (GDD §9.1)
- **Pillar 2**: Historical Village Kit → C++ snaps segments to Voronoi plots
- **Alternative**: Phone → Gaussian Splats → drop into Godot

## § Design Questions

### Organic Placement
1. Gridless building placement: how does the C++ engine validate placement? Terrain flatness check? Collision with existing buildings via spatial hash?
2. Road network: curved splines become pathable surfaces. How does the flow field integrate with curved roads?
3. Burgage Plot upgrades: Tier 1 vegetable garden → Tier 2 backyard forge → Tier 3 stone house. Is this a component state change on the same entity? Or destroy+rebuild?

### Construction System
4. Buildings under construction: visible construction phase with scaffolding? Workers assigned via LogisticsJob?
5. Building destruction (fire, artillery): triggers voxel destruction? Or just entity removal?

### Research Expansions
6. Road progression: dirt path → cobblestone → macadam. Each tier = different speed multiplier in flow field?
7. Lighting progression: oil lamps → gas street lights. Affects night LOS radius?
8. Housing progression: cottage → townhouse → tenement. Population density per plot?
9. Sensory landscape at street zoom: market noise, forge hammering, church bells — what drives these? Building type proximity?

## Deliverables
1. Building placement validation pipeline (C++ side)
2. Road network → flow field integration
3. Burgage Plot upgrade system
4. Construction phase specification
5. Building destruction pipeline
6. ⚠️ Traps section
