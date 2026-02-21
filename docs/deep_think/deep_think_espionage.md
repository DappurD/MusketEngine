# Deep Think Prompt: Espionage & Intelligence System

> **QUALITY MANDATE**: Espionage in most strategy games is a menu where you click "Sabotage" and a progress bar fills. We want spies to be PHYSICAL ENTITIES that move through the world, infiltrate cities, and can be caught. The intelligence they provide should be unreliable and actionable — not omniscient.

## Context
Crown & Caliber has fog of war. You don't know the enemy's army size, supply state, or fortification layout unless you send scouts or spies. Intelligence is a RESOURCE that must be gathered, and it can be wrong.

## Engine: Flecs v4.1.4, per-citizen entities, physical map with movement.

---

## Spy Entity Lifecycle

A spy is a citizen entity with special components, not a menu abstraction:

1. **Recruit**: Assign a citizen family to espionage training at the Intelligence Office (building)
2. **Train**: 2-month training period (game time). Spy gains `SpyState` component
3. **Deploy**: Player sends spy to target city. Spy entity physically travels (wagon route or on foot)
4. **Infiltrate**: Spy arrives, enters target city as a "merchant" or "traveler" — blend in
5. **Gather**: Over time, spy accumulates intelligence on enemy strength, buildings, supply levels
6. **Act**: Player orders sabotage (powder mill explosion, supply route cut, assassination) OR extraction
7. **Extract/Capture**: Spy attempts to leave. Risk of detection increases with time spent + actions taken

**Design Questions**:
1. Is the spy visible on the map to the player who owns them? Always, or only when sending orders?
2. Detection risk: accumulates per day? Spikes after sabotage actions? Counter-intelligence buildings reduce threshold?
3. What happens when caught? Execution (spy dies, diplomatic incident)? Imprisonment (can be traded in peace deal)?
4. Double agents: can your spy be "turned" by the enemy?

## Intelligence Quality

Information from spies is NOT perfect:
- **Troop estimates**: "Between 800 and 1,200 soldiers" (range, not exact number)
- **Building intel**: reveals enemy building types but not storage levels
- **Supply state**: "Well-supplied" / "Running low" / "Critical" (qualitative, not quantitative)
- **Better spies** = narrower ranges, more categories revealed

**Design Questions**:
5. Intelligence accuracy: tied to spy skill level? Or time spent in the city?
6. Stale intelligence: does info expire? "Last updated 3 months ago" — enemy may have changed since
7. Misinformation: can the enemy feed false intel to discovered spies?

## Sabotage Events

Each sabotage has a cost (risk of detection) and an effect:

| Sabotage | Detection Risk | Effect |
|----------|---------------|--------|
| Powder mill detonation | Very High | Destroys building, kills workers, fire spread |
| Supply route disruption | Medium | Wagons on target route delayed/lost for 1 month |
| Assassination | Extreme | Target character dies, massive diplomatic penalty |
| Arson | High | Fire in target district, miasma, approval crash |
| Propaganda | Low | Reduce target city approval by 10-15% |

**Design Questions**:
8. Sabotage execution: instant? Or requires preparation time (spy plants the charge, timer ticks)?
9. Powder mill detonation: uses our voxel `destroy_sphere()`? Creates physical destruction on the map?
10. Assassination targets: limited to named characters? Or any building overseer?

## Counter-Intelligence

Defending against enemy spies:
- **Counter-Intelligence Office**: building that increases spy detection chance within your city
- **Patrols**: assign militia to patrol routes — random chance of stopping suspicious travelers
- **Foreign Registry**: policy that tracks all foreign visitors (reduces infiltration, but costs bureaucracy)

**Design Questions**:
11. Is counter-intelligence passive (building aura) or active (player assigns agents)?
12. Caught spies: interrogation mechanic? Can reveal information about enemy spy network?

## Scouts vs. Spies

| | Scout | Spy |
|---|-------|-----|
| Entity type | Light cavalry | Citizen |
| Speed | Fast | Slow |
| Info type | Military positions, terrain | City economy, buildings, supply |
| Risk | Combat death | Capture, execution |
| Duration | Real-time (hours) | Long-term (months) |
| Visibility | Visible on map | Hidden |

**Design Questions**:
13. Scouts: just cavalry entities with large `vision_radius`? Or special scouting system?
14. Fog of war: grid-based (cells revealed by vision radius)? Entity-based (see individual units)?

## Deliverables
1. Spy entity component layout and lifecycle state machine
2. Detection risk accumulation formula
3. Intelligence quality system (ranges, staleness, misinformation)
4. Sabotage event specifications with ECS integration
5. Counter-intelligence building mechanics
6. Scout vs. spy system differentiation
7. Fog of war grid architecture
