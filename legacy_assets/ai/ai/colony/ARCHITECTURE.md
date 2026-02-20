# Economy AI Platform Architecture

## Design Philosophy

The Economy AI Platform is designed as a **reusable research substrate** for economic decision-making in strategy games. It solves AI problems distinct from the Combat AI testbed (ColonyAI/TheaterCommander tactical hierarchy).

### Separation of Concerns

- **Combat AI (existing):** Tactical decisions, formations, targeting, cover, morale
- **Economy AI (this system):** Resources, production, workers, construction, base expansion
- **Integration (optional):** Threat alerts, reinforcement requests, build exclusions

These systems are **independent** - you can use Economy AI without Combat AI (city builder) or Combat AI without Economy AI (pure tactical shooter).

---

## Core Concepts

### Resource Flow Model

```
Resource Nodes → Workers → Gathering → Stockpile → Construction → Buildings
     ↑                                      ↓
     └──────────────── Consumption ─────────┘
```

- **Resource Nodes:** Fixed world locations containing resources (metal ore, crystal deposits, etc.)
- **Workers:** Units assigned to economic tasks (gather, build, transport)
- **Stockpile:** Per-team resource storage
- **Construction:** Consumes stockpile to create buildings
- **Buildings:** May produce resources, spawn units, or provide bonuses

### Task Lifecycle

```
1. Task Identification → 2. Worker Allocation → 3. Execution → 4. Completion
   (gather needed)         (closest idle)         (travel→act)   (stockpile++)
```

**States:**
- `PENDING`: Task exists, no worker assigned
- `ASSIGNED`: Worker allocated, traveling to site
- `EXECUTING`: Worker performing action (gathering, building)
- `COMPLETED`: Task finished, worker freed

### Decision Layers

```
EconomyManager (High-level strategy)
    ↓
BuildPlanner (Site selection, priority queue)
    ↓
TaskAllocator (Worker assignment)
    ↓
EconomyState (Resource tracking, validation)
```

---

## Module Details

### Core (Game-Agnostic)

#### EconomyState
**Purpose:** Canonical resource tracking, production rates, consumption
**Responsibilities:**
- Maintain per-team stockpiles
- Validate resource costs
- Track production/consumption rates
- Thread-safe updates (C++ backing)

**API:**
```gdscript
func add_resource(team_id: int, resource_type: int, amount: int)
func can_afford(team_id: int, cost: Dictionary) -> bool
func get_stockpile(team_id: int) -> Dictionary
func get_production_rate(team_id: int, resource_type: int) -> float
```

**Extraction:** Replace `ResourceType` enum with your game's resources. All logic is generic.

---

#### TaskAllocator
**Purpose:** Multi-objective worker assignment
**Responsibilities:**
- Assign available workers to pending tasks
- Minimize total travel time
- Respect task priorities
- Balance workload across workers

**Algorithm:** Greedy distance-weighted assignment
1. Sort tasks by priority (descending)
2. For each task, assign closest available worker
3. Mark worker as busy, remove from pool
4. Repeat until no workers or no tasks

**API:**
```gdscript
func assign_workers(workers: Array[WorkerState], tasks: Array[EconomyTask]) -> Dictionary
# Returns { worker_id: task_id }
```

**Future:** Can upgrade to Hungarian algorithm for optimal global assignment if needed.

---

#### BuildPlanner
**Purpose:** Spatial site selection for construction
**Responsibilities:**
- Score potential build sites (multi-axis)
- Queue construction by priority
- Manage build dependencies (tech tree)
- Allocate workers to construction

**Scoring Axes:**
- **Proximity to resources:** Closer to ore/crystal = better for refineries
- **Proximity to base:** Closer to HQ = safer, easier to defend
- **Threat level:** Avoid enemy influence zones
- **Terrain suitability:** Flat ground, not blocking choke points

**API:**
```gdscript
func find_build_site(building_type: int, team_id: int) -> Vector3
func queue_construction(building_type: int, priority: int)
func get_active_constructions() -> Array[ConstructionSite]
```

**Extraction:** Replace scoring weights with your game's priorities.

---

### Integration Layer (V-SAF Specific)

#### SimBridge
**Purpose:** Interface to SimulationServer for unit queries/orders
**Responsibilities:**
- Query available workers (`get_units_in_state(ST_IDLE)`)
- Issue economic orders (`issue_order(unit_id, ORDER_GATHER, target)`)
- Read unit positions, states

**Why separate:** Another game might use a different unit system. This adapter isolates the dependency.

---

#### VoxelResourceScanner
**Purpose:** Find resource nodes in VoxelWorld
**Responsibilities:**
- Scan voxel data for ore/crystal/resource voxels
- Convert voxel positions to world coordinates
- Register nodes with EconomyState

**Why separate:** Another game might use prefab nodes, procedural spawns, or a different world representation.

---

#### WorldAdapter
**Purpose:** Spatial/terrain queries
**Responsibilities:**
- Distance calculations (accounting for pathability)
- Terrain height queries
- Build site validation (flat ground, not blocked)

**Why separate:** Isolates VoxelWorld dependency. Another game might use NavMesh, tilemaps, or heightmaps.

---

### Diagnostics

#### EconomyKPI
**Purpose:** Performance metrics for headless testing
**Metrics:**
- **Resource throughput:** Resources gathered per minute
- **Worker utilization:** % of workers actively working vs idle
- **Task completion time:** Average time from task creation to completion
- **Construction cycle time:** Time from queue → building complete
- **Stockpile peaks:** Max resources achieved

**Output:** JSON export for regression testing

---

#### EconomyVisualizer
**Purpose:** Debug overlays
**Displays:**
- Resource node locations (colored by type)
- Worker assignments (lines to task sites)
- Active construction sites (progress bars)
- Stockpile UI (current amounts, production rates)

---

## Design Decisions

### Why Task-Oriented (Not Goal-Oriented)?

Combat AI uses GOAP-style goal planning because tactical situations are fluid and multi-objective (take cover, flank, suppress, advance).

Economy AI uses **discrete tasks** because economic actions are:
- Well-defined (gather this ore, build this outpost)
- Parallelizable (multiple workers on independent tasks)
- Less interdependent (one gather task doesn't block another)

**Trade-off:** Less flexible for complex economic strategies, but simpler and more performant.

---

### Why Separate from Combat AI?

1. **Different problem domains:** Tactics vs logistics
2. **Independent reusability:** City builder doesn't need combat, tactical shooter doesn't need economy
3. **Clearer architecture:** Single Responsibility Principle
4. **Performance isolation:** Budget economy tick time separately

**Integration exists** for games that need both (V-SAF), but it's **optional**.

---

### Why C++ EconomyState?

**Reasons:**
- **Tick stability:** Resource counts must be exact, no floating-point drift
- **Thread safety:** Workers may update stockpile from multiple threads
- **Performance:** Atomic operations faster than GDScript for high-frequency updates

**GDScript handles:** High-level planning, task creation, worker allocation (less frequent, more complex logic)

---

### Why Not GOAP for Economy?

Goal-Oriented Action Planning (GOAP) would allow emergent economic strategies (e.g., "satisfy resource need" → planner finds gather vs trade vs steal).

**Decision:** Start with task-oriented, upgrade to GOAP later if needed.

**Rationale:**
- Task-oriented is simpler, faster to implement
- GOAP complexity only justified if economic decisions are highly interdependent
- Can wrap task system in GOAP layer later without full rewrite

---

## Nomenclature (Avoid Combat AI Conflicts)

| Economy AI | Combat AI (Don't Use These Names) |
|------------|----------------------------------|
| EconomyManager | ColonyAI, ColonyAICPP |
| EconomyState | SimulationServer |
| TaskAllocator | Squad, SquadAI |
| BuildPlanner | TheaterCommander |
| WorkerPool | (none, OK to use) |
| EconomyTask | (none, OK to use) |
| ResourceNode | (none, OK to use) |
| EconomyKPI | (none, OK to use) |

**Critical:** Never use `ColonyAI` or `ColonyManager` - these refer to the existing **tactical coordinator** in the combat hierarchy.

---

## Extraction Guide

### To Use in Another Game

1. **Copy core modules:**
   ```
   ai/colony/core/economy_state.gd
   ai/colony/core/task_allocator.gd
   ai/colony/core/build_planner.gd
   cpp/src/economy_state.h/cpp
   ```

2. **Implement integration adapters:**
   - `IWorldQuery`: Replace VoxelWorld with your spatial representation
   - `IUnitRegistry`: Replace SimulationServer with your unit system
   - `IResourceProvider`: Replace VoxelResourceScanner with your resource spawn system

3. **Wire diagnostics:**
   - `EconomyKPI` → your analytics backend
   - `EconomyVisualizer` → your debug UI

4. **Customize:**
   - Replace `ResourceType` enum with your game's resources
   - Adjust `BuildPlanner` scoring weights
   - Modify `TaskAllocator` priorities

### Required Interfaces

```gdscript
# IWorldQuery - spatial queries
class_name IWorldQuery
func get_distance(from: Vector3, to: Vector3) -> float
func is_pathable(position: Vector3) -> bool
func get_terrain_height(position: Vector2) -> float

# IUnitRegistry - unit availability
class_name IUnitRegistry
func get_available_workers(team_id: int) -> Array[WorkerState]
func assign_task(worker_id: int, task: EconomyTask)
func is_worker_busy(worker_id: int) -> bool

# IResourceProvider - world resources
class_name IResourceProvider
func scan_resources() -> Array[ResourceNode]
func get_resource_at(position: Vector3) -> int  # ResourceType or 0
```

Implement these three interfaces, and Economy AI is 90% integrated.

---

## Performance Targets

### Tick Budget (per frame)
- **100 workers, 50 tasks:** ≤0.5ms
- **500 workers, 200 tasks:** ≤2.0ms
- **1000 workers:** Use SoA architecture (like SimulationServer)

### Memory
- **Per worker:** ~64 bytes (position, state, task_id, team)
- **Per task:** ~48 bytes (type, target, priority, assigned_worker)
- **Stockpile:** ~128 bytes per team (fixed, array of resource counts)

### Scalability Path
If worker counts exceed 500:
1. Move worker state to C++ SoA
2. Batch task allocation (N workers per frame, amortized)
3. Spatial hash for task assignment (don't check all workers for each task)

---

## Testing Strategy

### Unit Tests (when implemented)
- `test_task_allocator.gd`: Distance-based assignment correctness
- `test_economy_state.gd`: Resource add/subtract, overflow, underflow
- `test_build_planner.gd`: Site scoring, priority queue

### Integration Tests
- `economy_gather_basic.json`: 10 workers gather from 3 nodes, verify throughput
- `economy_build_simple.json`: Queue 5 outposts, verify construction completes
- `economy_scale_stress.json`: 500 workers, measure tick time

### Regression Tests
- **Critical:** Run combat scenarios after each economy change
- `micro_cover_peek`, `fow_tuning_medium` must still pass
- Tick time must not regress

---

## Future Expansion

### Increment 2: Economy Primitives
- Resource node spawning
- Worker gather loops
- Stockpile UI
- Basic telemetry

### Increment 3: Build System
- Construction site placement
- Multi-stage buildings (foundation → walls → complete)
- Worker assignment to construction

### Increment 4: High-Level Strategy
- Build-order planning (like StarCraft build orders)
- Expansion timing
- Worker population curves
- Economic strategy presets (rush, turtle, expand)

### Beyond
- Trade between teams (market, diplomacy)
- Supply lines (transport, caravans)
- Resource exhaustion (nodes deplete)
- Economic warfare (blockades, raids on gatherers)

---

## References

- Combat AI: See `ai/README.md` (tactical hierarchy)
- Voxel Engine: See `world/README.md` (spatial representation)
- Simulation: See `cpp/src/simulation_server.h` (unit state)
