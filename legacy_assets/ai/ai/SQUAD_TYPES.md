# Squad Types (Archetypes)

## Why this exists
Squad types let CommanderAI and ColonyAI treat squads as tactical tools with different strengths.

This document is the source of truth for:
- What each squad type is meant to do.
- How squad types affect strategic assignment.
- How to add new squad types without rewriting core AI logic.

## Design goals
- Increase tactical flexibility through specialization.
- Keep the system data-driven and easy to extend.
- Support future non-infantry squads (vehicles) without redesigning the model.

## Archetype schema
Each squad type should be defined with this structure:

- `id` - Unique identifier (`assault`, `mortar`, `recon`, etc.).
- `battlefield_role` - Human-readable role summary.
- `preferred_goals` - Goals this archetype should score highly for.
- `deprioritized_goals` - Goals this archetype should avoid unless forced.
- `composition_template` - Expected unit roles/loadouts for this squad type.
- `formation_profile` - Preferred formation and spacing behavior.
- `engagement_profile` - Range bands, aggression, and target priorities.
- `mobility_class` - `infantry_light`, `infantry_heavy`, `vehicle_light`, etc.
- `support_dependencies` - Optional dependencies (spotters, logistics, screening).
- `countered_by` - Known weaknesses used for commander-level risk evaluation.

## Initial archetypes

### Assault Squad
- Role: close-to-mid range pressure and objective pushes.
- Preferred goals: capture, assault, breakthrough.
- Strengths: sustained frontline presence.
- Weaknesses: vulnerable in open long-range duels.

### Defend Squad
- Role: hold objectives and protect key areas.
- Preferred goals: defend base, defend capture points.
- Strengths: positional staying power and area denial.
- Weaknesses: slower offensive tempo.

### Flank Squad
- Role: maneuver around enemy frontage and break formations.
- Preferred goals: flank, pressure exposed targets, opportunistic capture.
- Strengths: angle advantage and disruption.
- Weaknesses: can overextend without support.

### Mortar Squad
- Role: indirect fire support and area suppression from rear positions.
- Preferred goals: fire support, deny approach lanes, break entrenched defenders.
- Formation profile: spread and protected rear-line offsets.
- Engagement profile: long-range bombardment, low direct-fire priority, minimum-range safety.
- Key constraints:
  - Requires mortar-role operator (`ROLE_MORTAR` in voxel runtime).
  - Uses indirect high-arc fire with distance-based scatter.
  - Should prioritize clustered targets and avoid close-range duels.
- Weaknesses: high dependency on spotting and anti-flank protection.

### Sniper Squad
- Role: precision elimination, overwatch, and high-value target pressure.
- Preferred goals: long-range pickoffs, recon support, overwatch lanes.
- Formation profile: dispersed, concealment-aware positioning.
- Engagement profile: selective shots on priority targets.
- Weaknesses: low volume fire, weaker in close-quarters pressure.

### Recon Squad (Drone ISR)
- Role: scouting, target acquisition, and battlefield information advantage.
- Preferred goals: vision control, route scouting, target designation.
- Formation profile: mobile standoff with operator protection.
- Engagement profile: avoid direct brawls, prioritize information and positioning.
- Weaknesses: value drops if drone is destroyed or operator is suppressed.

## Recon drone subsystem contract
The recon archetype includes a deployable drone with explicit constraints.

### Deployment model
- Drone is manually deployed by one designated squad member (the operator).
- Drone is not always active. It has a deploy/recover lifecycle.

### Operator lockout (FPV mode)
- While piloting in FPV mode, the operator is task-locked.
- Task-locked means the operator cannot fire, move tactically, heal, revive, or perform other combat actions.
- Squad behavior should protect/position the operator while FPV is active.

### Vision sharing
- Airborne drone grants allied team vision in a radius around drone position.
- Shared vision should be available to CommanderAI, ColonyAI assignment logic, and eligible squad-level tactical queries.

### Limits and vulnerability
- Drone has a maximum control range from operator or owning squad anchor.
- Drone has finite endurance (battery/airtime cap).
- Drone should be hard to hit (small target/high evasion profile), but always destructible.
- Enemy units may target and destroy drone.
- Drone destruction removes granted vision until cooldown/redeploy conditions are satisfied.

## CommanderAI and ColonyAI integration contract
Squad archetypes are inputs to strategic scoring, not hardcoded outcomes.

- Goal scoring should consume archetype tags and archetype-weight multipliers.
- Assignment logic should still resolve conflicts through goal score comparisons.
- Recon-specific commander hooks should include:
  - When to deploy drone for information gain.
  - When to hold drone due to air-defense risk.
  - When to bias squad safety because operator is FPV-locked.
  - How to recover behavior after drone loss.
  - When to prioritize killing enemy recon drones.
- Mortar/sniper support coordination should benefit from recon visibility.

## Unit role and loadout mapping
Archetypes are squad-level intent. Unit roles/loadouts remain unit-level definitions.

- `composition_template` maps to unit `squad_role` assignments and loadout resources.
- Example recon template:
  - `leader`
  - `recon_operator` (drone controller)
  - `marksman`
  - `rifleman`
  - `medic` (optional by roster size)
- Example mortar template:
  - `leader`
  - `mortar_gunner`
  - `ammo_bearer` or `rifleman_support`
  - `security_rifleman`
  - optional `medic` for sustained fire missions

## Future vehicles
Vehicle squads should follow the same archetype schema with extra constraints:
- Pathing class and nav requirements.
- Survivability profile and repair dependency.
- Visibility/signature profile (easy to spot, high threat).
- Combined arms dependencies (escort infantry, recon feed, anti-armor cover).

Use IDs like `vehicle_recon`, `vehicle_assault`, or `vehicle_fire_support` to stay consistent.

## Add-a-new-archetype checklist
When adding a new squad type:

1. Define the archetype using the schema in this file.
2. Add composition template with unit roles/loadouts.
3. Add commander scoring intent (`preferred_goals`, multipliers, risk logic).
4. Define counters and support dependencies.
5. Define debug visibility requirements (what must show in AI debug overlay).
6. Document failure behavior (lost assets, suppressed role, no ammo/fuel, etc.).
7. Add scenario test notes:
   - Open terrain.
   - Dense cover.
   - Night/low visibility.
   - Objective defense and objective assault.

## Implementation note
This document defines behavior contracts and extension rules.
Code implementation can evolve, but should preserve these contracts unless this file is updated.
