# Colony AI — Stigmergic Economy & Base Building System

**Architecture:** Pheromone-based swarm intelligence for resource gathering and voxel base building

---

## Overview

The Colony AI uses **stigmergy** (indirect coordination through environment modification) to manage worker units. Workers deposit pheromone trails as they move, creating an emergent optimization system inspired by ant colony optimization (ACO).

The system now includes **stigmergic base building** where workers autonomously construct voxel structures using gathered resources, with emergent behaviors like adaptive wall thickness (threat-responsive) and rubble fortification (destruction → defensive structures).

## Core Systems

### 1. **VoxelResourceScanner** ([integration/voxel_resource_scanner.gd](integration/voxel_resource_scanner.gd))
- Scans VoxelWorld for resource materials (metal ore, crystals, energy cores)
- Spatial clustering (8m radius, min 3 voxels)
- Cached results with invalidation on voxel destruction
- ACO scoring: `score = density / (distance + 1.0)`

### 2. **ColonyTaskAllocator** ([colony_task_allocator.gd](colony_task_allocator.gd))
- Stigmergic worker assignment
- ACO decision-making: exploitation (follow trails) vs exploration (ignore trails)
- Congestion avoidance via load balancing
- Priority-based task scheduling

### 3. **EconomyConstants** ([economy_constants.gd](economy_constants.gd))
- Unified 15-channel pheromone layout
  - **Combat channels (0-7)**: SimulationServer deposits
  - **Economy channels (8-14)**: ColonyAI deposits
- Per-channel evaporation/diffusion rates
- ACO parameters (α=1.5, β=2.0)

### 4. **WorkerPheromoneDemo** ([worker_pheromone_demo.gd](worker_pheromone_demo.gd))
- Example integration showing trail deposition
- Gradient following
- Cross-domain awareness (economy reads combat danger)

### 5. **ColonyAI** ([colony_ai.gd](colony_ai.gd))
- High-level colony controller and coordinator
- Worker unit spawning and lifecycle management
- Resource stockpiling (metal, crystal, energy)
- SimulationServer integration (spawn_unit, get_position, set_order)
- Periodic resource scanning and task creation
- State machine for mining → delivery → **building** → idle cycle
- **NEW: Strategic base building** with 5 building types
- **NEW: Adaptive turret construction** (threat-responsive walls)
- **NEW: Rubble fortification** (destruction → defensive structures)

### 6. **BuildPlanner** ([core/build_planner.gd](core/build_planner.gd))
- Multi-axis site selection (5 scoring axes)
- Pareto-optimal placement (base proximity, resources, threat, terrain, strategic value)
- Construction queue management
- Multi-worker coordination (up to 4 workers per site)

### 7. **EconomyState** ([core/economy_state.gd](core/economy_state.gd))
- Per-team resource stockpiles (metal, crystal, energy)
- Affordability checks (`can_afford()`)
- Resource consumption gating (`consume_resources()`)
- Production rate tracking

---

## Building Types

| Type | Cost (Metal/Crystal) | Purpose | Voxel Structure |
|------|---------------------|---------|-----------------|
| **OUTPOST** | 200 / 50 | Forward base, expansion | 12×8×12 wood building |
| **REFINERY** | 150 / 75 | Resource processing | 16×12×16 steel industrial |
| **BARRACKS** | 300 / 100 | Unit production | 24×10×20 brick barracks |
| **TURRET** | 100 / 25 | Defensive fortification | **Adaptive wall** (threat-responsive) |
| **SUPPLY_DEPOT** | 250 / 50 | Stockpile capacity | 14×8×14 wood storage |

### Building Priority (RimWorld-style heuristics)
1. **Refinery** (if none exist — resource processing critical)
2. **Barracks** (if < 2 — unit production)
3. **Supply Depot** (if stockpile > 800 — near capacity)
4. **Outpost** (default — expansion)

---

## Emergent Base Building Behaviors

### 1. **Adaptive Turret Construction**
Turret walls adapt to threat levels via CH_DANGER pheromone sampling:

**Threat Level** → **Wall Properties**
- **Low (danger < 0.3)**: 2 voxel thickness, 4 voxel height, MAT_SANDBAG
- **Medium (0.3-0.6)**: 3-4 voxel thickness, 5-6 voxel height, MAT_CONCRETE
- **High (> 0.6)**: 5-6 voxel thickness, 7-8 voxel height, MAT_STEEL

**Emergent Result:** Safe zones get cheap, thin walls; threatened areas get fortified steel barriers.

### 2. **Rubble Fortification Pipeline**
Destruction creates defensive opportunities:
1. Grenade/combat destroys voxels → rubble piles form
2. Cellular automata settles debris into stable configurations
3. Stochastic scan (10% per tick) detects high-density rubble (> 10 voxels)
4. Auto-queue TURRET at rubble location (priority 50)
5. Workers build defensive wall on existing debris (free foundation)

**Emergent Result:** Battle zones naturally fortify themselves; destruction becomes defense.

### 3. **Organic Base Layout**
Multi-objective site scoring creates terrain-adaptive layouts:
- **Refinery**: Near ore deposits (resource proximity weight 2.0×)
- **Barracks**: Near base, safe zones (threat penalty -50×)
- **Turret**: High ground, choke points (height bonus, strategic value)
- **Supply Depot**: Central location (base proximity weight 0.5×)

**Emergent Result:** No rigid grid; base follows natural terrain contours, threat zones, resource distribution.

### 4. **Pheromone-Guided Worker Flow**
CH_BUILD_URGENCY (channel 12) guides construction:
- Site queued → deposit 15.0 strength pheromone
- Workers sample gradient → move toward strongest signal
- During construction → continuous deposition (10.0/s)
- CH_CONGESTION load balancing → workers avoid crowded sites
- Site complete → pheromone evaporates (13s half-life)

**Emergent Result:** Multi-site construction self-balances; critical builds attract more workers.

---

## Pheromone Channels (Economy)

| Channel | Name | Evap | Diff | Purpose |
|---------|------|------|------|---------|
| 8 | CH_METAL | 0.98 | 0.05 | Metal ore resource trails |
| 9 | CH_CRYSTAL | 0.98 | 0.05 | Crystal resource trails |
| 10 | CH_ENERGY | 0.98 | 0.05 | Energy resource trails |
| 11 | CH_CONGESTION | 0.85 | 0.20 | Worker traffic density (load balancing) |
| 12 | CH_BUILD_URGENCY | 0.92 | 0.30 | Construction priority |
| 13 | CH_EXPLORED | 0.99 | 0.02 | Frontier exploration memory |
| 14 | CH_SPARE | 1.00 | 0.00 | Reserved for future use |

**Evaporation rates:**
- Slow (0.98): Persistent trails (~50s half-life) for resource paths
- Fast (0.85): Real-time signals (~6.5s half-life) for congestion
- Very slow (0.99): Long-term memory (~100s half-life) for explored areas

---

## ACO Algorithm

### Task Allocation Formula
```
score = (pheromone^α × heuristic^β) × density × (1 - congestion) × priority

Where:
  α (alpha) = 1.5  — Pheromone importance (trail following strength)
  β (beta)  = 2.0  — Greedy bias (prefer closer tasks)
  pheromone = sampled trail strength at task location
  heuristic = 1 / (distance + 1)
  density   = resource node size (voxel count)
  congestion = sampled congestion pheromone (0-1)
  priority  = 1-4 multiplier (LOW to CRITICAL)
```

### Exploitation vs Exploration
- **90% exploitation**: Follow strongest pheromone trails (proven paths)
- **10% exploration**: Ignore trails, try distant/unknown tasks (prevent stagnation)

### Trail Reinforcement
- **Moving with ore**: Deposit 3.0 strength per meter traveled
- **Successful delivery**: Reinforce trail at 2× strength (positive feedback)
- **Failed attempt**: No reinforcement (trail naturally evaporates)

---

## Integration Points

### Getting Pheromone Map
```gdscript
var simulation_server = get_node("/root/SimulationServer")
var pheromone_map = simulation_server.get_pheromone_map(team)
```

### Depositing Resource Trail
```gdscript
# While worker carries ore
pheromone_map.deposit_trail(
    prev_pos,
    current_pos,
    EconomyChannel.CH_METAL,
    3.0  # METAL_TRAIL_STRENGTH
)

# On successful delivery (2x reinforcement)
pheromone_map.deposit_trail(
    prev_pos,
    base_pos,
    EconomyChannel.CH_METAL,
    6.0  # 2x strength
)
```

### Following Gradient
```gdscript
var gradient = pheromone_map.gradient(worker_pos, EconomyChannel.CH_METAL)
if gradient.length() > 0.01:
    move_direction = gradient.normalized()
```

### Cross-Domain Awareness
```gdscript
# Check combat danger before mining
var danger = pheromone_map.sample(mining_pos, 0)  # CH_DANGER
if danger > 0.5:
    abort_mining()  # Too dangerous!
```

---

## Task Lifecycle

1. **Creation**: ColonyAI scans for resources, creates mining tasks
2. **Allocation**: Workers request tasks, allocator scores all available
3. **Execution**: Worker travels to site, deposits congestion pheromone
4. **Deposition**: Worker carries ore, deposits resource trail
5. **Completion**: Worker delivers ore, reinforces trail (2×)
6. **Pruning**: Stale/abandoned tasks removed after 30s

---

## Performance

- **Resource scanner**: < 100ms for 2400×128×1600 world
- **Task scoring**: O(N×M) where N=workers, M=tasks
- **Pheromone tick**: < 2ms for 15 channels @ 150×100 grid
- **Memory**: ~1.8 MB for 2 teams × 15 channels

---

## Testing

See [../test/TESTING_CHECKLIST.md](../test/TESTING_CHECKLIST.md) for comprehensive test plan.

**Quick tests:**
- **Base building**: `test/test_base_building.tscn` (F6) - **NEW: Stigmergic voxel construction**
- Resource scanner: `test/test_resource_scanner.tscn` (F6)
- Full integration: `test/test_colony_ai.tscn` (F6) - Complete economy AI demo
- Pheromone visual: `test/pheromone_visual_test.tscn` (F6)
- Worker demo: Add `worker_pheromone_demo.gd` to voxel_test scene

### Base Building Test Controls
**Scene:** `test/test_base_building.tscn`
- **R key**: Trigger rubble fortification test (destroys sphere near base)
- **T key**: Test adaptive walls (deposits DANGER pheromone)
- **Status**: Printed every 5s (workers, stockpile, construction queue)

---

## Full Integration Example

### Setup ColonyAI in Scene
```gdscript
# Add to scene tree
var colony_ai = ColonyAI.new()
colony_ai.team = 0
colony_ai.max_workers = 50
colony_ai.base_position = Vector3(16, 5, 16)
colony_ai.resource_scan_interval = 10.0
add_child(colony_ai)

# Spawn initial workers
for i in range(10):
    var spawn_pos = colony_ai.base_position + Vector3.RIGHT * i * 2.0
    colony_ai.spawn_worker(spawn_pos)
```

### Complete Worker Cycle (Resource Gathering)
1. **Idle worker** → ColonyAI allocates task via ACO
2. **Move to resource** → SimulationServer ORDER_MOVE, deposit CH_CONGESTION
3. **Mine voxel** → VoxelWorld.set_voxel(pos, 0)
4. **Auto-transition to delivery** → New task created automatically
5. **Move to base** → Deposit CH_METAL trail along path
6. **Deliver resource** → Stockpile increment, 2× trail reinforcement
7. **Return to idle** → Available for next allocation

### Complete Worker Cycle (Base Building)
1. **Stockpile threshold reached** → ColonyAI strategic planner triggers
2. **Site selection** → BuildPlanner 5-axis scoring (100m radius grid search)
3. **Affordability check** → EconomyState.can_afford(cost)
4. **Resource consumption** → EconomyState.consume_resources(cost)
5. **Task creation** → ColonyTaskAllocator.create_build_task(site)
6. **Worker allocation** → Workers follow CH_BUILD_URGENCY gradient
7. **Construction** → Progress increment (0.04/s per worker, 25s solo / 6.25s with 4 workers)
8. **Voxel placement** → VoxelWorld.generate_building() on completion
9. **Return to idle** → Workers freed for next task

### Emergent Behavior (Resource Gathering)
- **Trail convergence:** Multiple workers follow same strong trails
- **Load balancing:** CH_CONGESTION discourages clustering
- **Exploration:** 10% of workers ignore trails, find new deposits
- **Danger avoidance:** Workers read CH_DANGER, skip risky mining sites
- **Positive feedback:** Successful paths get reinforced, failed paths evaporate

### Emergent Behavior (Base Building)
- **Adaptive fortification:** Turrets thicken walls (2-6 voxels) based on CH_DANGER
- **Rubble recycling:** Destruction creates fortification opportunities
- **Organic layout:** Buildings follow terrain contours, avoid threats, cluster near resources
- **Pheromone load balancing:** CH_BUILD_URGENCY + CH_CONGESTION optimize worker distribution
- **Economic gating:** No builds when stockpile insufficient (prevents resource starvation)

---

## Building System Integration

### Strategic Build Planning (15s interval)
```gdscript
# ColonyAI._plan_new_buildings() — RimWorld-style heuristics
if stockpile insufficient:
    return  # Wait for more resources

var desired_type = _choose_next_building()  # Priority logic
var cost = BUILDING_COSTS[desired_type]

if economy_state.can_afford(team, cost):
    var site_pos = build_planner.find_build_site(desired_type, team, base_position, 100.0)
    economy_state.consume_resources(team, cost)
    var site = build_planner.queue_construction(desired_type, site_pos, team, 80)
    task_allocator.create_build_task(site, Priority.HIGH)
    pheromone_map.deposit(site_pos, CH_BUILD_URGENCY, 15.0)
```

### Worker Construction Behavior
```gdscript
# ColonyAI._update_builder_worker()
if worker_distance_to_site < 3.0:
    site.progress += CONSTRUCTION_RATE_PER_WORKER * delta  # 0.04/s
    pheromone_map.deposit(site.position, CH_BUILD_URGENCY, 10.0 * delta)

    if site.is_complete():
        voxel_world.generate_building(...)  # Place voxel structure
        task_allocator.complete_task(task.id)
else:
    simulation_server.set_order(worker_id, ORDER_MOVE, site.position)
```

### Adaptive Turret Example
```gdscript
# Turret adapts to threat level
var danger = pheromone_map.sample(world_pos, CH_DANGER)
var thickness = int(clamp(2.0 + danger * 4.0, 2.0, 6.0))  # 2-6 voxels
var height = int(clamp(4.0 + danger * 4.0, 4.0, 8.0))     # 4-8 voxels
var wall_mat = MAT_SANDBAG if danger < 0.3 else (MAT_CONCRETE if danger < 0.6 else MAT_STEEL)

voxel_world.generate_wall(voxel_pos.x, voxel_pos.y, voxel_pos.z, 8, height, thickness, wall_mat, true)
```

### Rubble Fortification Example
```gdscript
# Stochastic scan (10% per tick)
if randf() < 0.1:
    for sample_pos in grid_around_base:
        var rubble_density = _count_rubble_at(sample_pos)  # Vertical column scan
        if rubble_density > 10:
            build_planner.queue_construction(TURRET, sample_pos, team, 50)
            return  # One per scan
```

---

## References

### State-of-the-Art Patterns Implemented
- **AlphaStar** (DeepMind): Unified observation space (15-channel pheromone map shared combat + economy)
- **OpenAI Five** (Dota 2): Emergent coordination via shared signals (stigmergic pheromones)
- **RimWorld** (Ludeon Studios): Simple hierarchical priorities beat complex optimization
- **Dwarf Fortress** (Bay 12 Games): Spatial organization, distance-ordered task execution

### Theory & Algorithms
- **Ant Colony Optimization** (Dorigo & Stützle): α=1.5, β=2.0 parameters, exploitation/exploration balance
- **Stigmergy** (Grassé 1959): Indirect coordination through environment modification
- **Multi-Objective Optimization**: Pareto-optimal site selection (5 axes)
- **Cellular Automata**: Rubble settling, structural integrity cascades

### Implementation Phases
- **Week 5-6 Track 1**: Economy AI (resource gathering, ACO pathfinding, pheromone trails)
- **Week 7 Track 1**: Base Building AI (voxel construction, adaptive structures, rubble fortification)

### Unit Authoring Documentation
- **[Unit Creation Spec](../../docs/UNIT_CREATION_SPEC.md)** - Architecture and migration strategy for data-driven unit authoring
- **[Unit Creation Technical Reference](../../docs/UNIT_CREATION_TECHNICAL.md)** - Canonical unit schema, height scaling, registry/pack contracts
- **[Unit Creation Cookbook](../../docs/UNIT_CREATION_COOKBOOK.md)** - Practical workflows for adding units and themed packs
