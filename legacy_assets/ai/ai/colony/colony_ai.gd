extends Node
class_name ColonyEconomyAI

## ColonyAI — High-Level Stigmergic Economy Controller
##
## Manages resource gathering, base building, and worker coordination
## using pheromone-based swarm intelligence (ACO).
##
## Architecture:
## - VoxelResourceScanner: Finds ore deposits in VoxelWorld
## - ColonyTaskAllocator: Assigns workers to tasks via ACO
## - PheromoneMapCPP: Shared 15-channel map (combat + economy)
## - SimulationServer integration: Worker unit spawning/tracking

const EconomyConstants = preload("res://ai/colony/economy_constants.gd")
const EconomyChannel = EconomyConstants.EconomyChannel
const VoxelResourceScannerScript = preload("res://ai/colony/integration/voxel_resource_scanner.gd")
const ColonyTaskAllocatorScript = preload("res://ai/colony/colony_task_allocator.gd")
const BuildPlannerScript = preload("res://ai/colony/core/build_planner.gd")
const EconomyStateScript = preload("res://ai/colony/core/economy_state.gd")

## Colony configuration
@export var team: int = 0
@export var max_workers: int = 50
@export var resource_scan_interval: float = 10.0  # seconds
@export var task_allocation_interval: float = 0.5  # seconds
@export var base_position: Vector3 = Vector3(0, 5, 0)

## References
var simulation_server = null  # SimulationServer (RefCounted — set by parent scene)
var pheromone_map: PheromoneMapCPP = null
var voxel_world: VoxelWorld = null
var resource_scanner: RefCounted = null  # VoxelResourceScanner
var task_allocator: RefCounted = null  # ColonyTaskAllocator
var build_planner = null  # BuildPlanner
var economy_state = null  # EconomyState

## Colony state
var worker_ids: Array[int] = []  # Unit IDs of worker units
var worker_tasks: Dictionary = {}  # {worker_id: Task}
var stockpile: Dictionary = {
	"metal": 0,
	"crystal": 0,
	"energy": 0
}

## Building cost definitions (metal, crystal, energy)
const BUILDING_COSTS: Dictionary = {
	0: {0: 200, 1: 50},   # OUTPOST: 200 metal, 50 crystal
	1: {0: 150, 1: 75},   # REFINERY: 150 metal, 75 crystal
	2: {0: 300, 1: 100},  # BARRACKS: 300 metal, 100 crystal
	3: {0: 100, 1: 25},   # TURRET: 100 metal, 25 crystal
	4: {0: 250, 1: 50},   # SUPPLY_DEPOT: 250 metal, 50 crystal
}

## Construction work rate (progress per worker per second)
const CONSTRUCTION_RATE_PER_WORKER: float = 0.04  # 25s solo, 6.25s with 4 workers

## Timers
var _scan_timer: float = 0.0
var _allocation_timer: float = 0.0
var _build_planning_timer: float = 0.0
const BUILD_PLANNING_INTERVAL: float = 15.0

func _ready():
	# Use pre-injected reference, or fall back to autoload lookup
	if not simulation_server:
		simulation_server = get_node_or_null("/root/SimulationServer")
	if not simulation_server:
		push_error("ColonyEconomyAI: SimulationServer not found — set .simulation_server before add_child()")
		return

	# Get unified pheromone map
	pheromone_map = simulation_server.get_pheromone_map(team)
	if not pheromone_map:
		push_error("ColonyAI: Failed to get pheromone map for team %d" % team)
		return

	# Get VoxelWorld (should be in scene tree)
	voxel_world = get_tree().root.find_child("VoxelWorld", true, false)
	if not voxel_world:
		push_warning("ColonyAI: VoxelWorld not found in scene tree")
		return

	# Initialize resource scanner
	resource_scanner = VoxelResourceScannerScript.new(voxel_world)

	# Initialize task allocator
	task_allocator = ColonyTaskAllocatorScript.new(voxel_world, resource_scanner, pheromone_map)

	# Initialize build planner
	var world_adapter = WorldAdapter.new(voxel_world)
	build_planner = BuildPlannerScript.new(world_adapter)
	build_planner.set_influence_map(null)  # TODO: Wire combat influence map later

	# Initialize economy state
	economy_state = EconomyStateScript.new()
	# Starting stockpile for testing
	economy_state.add_resource(team, 0, 500)  # RES_METAL
	economy_state.add_resource(team, 1, 100)  # RES_CRYSTAL

	print("✓ ColonyAI initialized (team=%d)" % team)
	print("  Max workers: %d" % max_workers)
	print("  Base position: %s" % base_position)

	# Initial resource scan
	_scan_resources()

func _process(delta: float):
	if not simulation_server or not pheromone_map or not resource_scanner:
		return

	# Resource scanning (periodic)
	_scan_timer += delta
	if _scan_timer >= resource_scan_interval:
		_scan_timer = 0.0
		_scan_resources()

	# Task allocation (high-frequency)
	_allocation_timer += delta
	if _allocation_timer >= task_allocation_interval:
		_allocation_timer = 0.0
		_allocate_workers()

	# Strategic building planning (low-frequency)
	_build_planning_timer += delta
	if _build_planning_timer >= BUILD_PLANNING_INTERVAL:
		_build_planning_timer = 0.0
		_plan_new_buildings()

	# Rubble fortification scanning (stochastic)
	if randf() < 0.1:  # 10% chance per tick
		_scan_destruction_sites()

	# Tick task allocator (pruning, congestion decay)
	if task_allocator:
		task_allocator.tick(delta)

	# Update worker states
	_update_workers(delta)

## Scan VoxelWorld for resources and create tasks
func _scan_resources():
	if not resource_scanner:
		return

	print("[ColonyAI] Scanning resources...")
	var nodes: Array[Dictionary] = resource_scanner.scan_resources()
	print("  Found %d resource nodes" % nodes.size())

	# Debug: Print resource positions
	for i in range(min(5, nodes.size())):
		var node = nodes[i]
		print("    Resource %d: type=%d, pos=%s, density=%d" % [i, node.type, node.pos, node.density])

	# Create mining tasks for new resource nodes
	for node in nodes:
		_create_mining_task(node)

## Create a mining task for a resource node
func _create_mining_task(resource_node: Dictionary):
	if not task_allocator:
		return

	# Check if task already exists for this position
	var existing_tasks = task_allocator.get_tasks()
	for task in existing_tasks:
		if task.target_pos.distance_to(resource_node.pos) < 2.0:
			return  # Task already exists

	# Create new mining task
	var priority: int
	match resource_node.type:
		0:  # Metal ore
			priority = ColonyTaskAllocatorScript.Priority.NORMAL
		1:  # Crystal
			priority = ColonyTaskAllocatorScript.Priority.HIGH
		2:  # Energy core
			priority = ColonyTaskAllocatorScript.Priority.CRITICAL
		_:
			priority = ColonyTaskAllocatorScript.Priority.LOW

	task_allocator.create_task(
		ColonyTaskAllocatorScript.TaskType.MINE_RESOURCE,
		resource_node.pos,
		priority,
		resource_node
	)

## Allocate idle workers to tasks
func _allocate_workers():
	if not task_allocator:
		return

	# Find idle workers (not currently assigned to task)
	var idle_workers: Array[int] = []
	for worker_id in worker_ids:
		if not worker_tasks.has(worker_id):
			idle_workers.append(worker_id)

	if idle_workers.is_empty():
		return

	# Get worker positions from SimulationServer
	# TODO: Add get_unit_position() API to SimulationServer
	# For now, use placeholder positions
	for worker_id in idle_workers:
		var worker_pos = _get_worker_position(worker_id)

		# Allocate worker to best task via ACO
		var task = task_allocator.allocate_worker(worker_id, worker_pos, team)

		if task:
			worker_tasks[worker_id] = task
			print("  Worker %d at %s → Task %d (%s at %s, dist: %.1fm)" % [
				worker_id,
				worker_pos,
				task.id,
				_task_type_name(task.type),
				task.target_pos,
				worker_pos.distance_to(task.target_pos)
			])

## Update worker behaviors
func _update_workers(delta: float):
	# Process each worker's current task
	for worker_id in worker_ids:
		if not worker_tasks.has(worker_id):
			continue

		var task = worker_tasks[worker_id]
		var worker_pos = _get_worker_position(worker_id)

		# Check task completion
		match task.type:
			ColonyTaskAllocatorScript.TaskType.MINE_RESOURCE:
				_update_mining_worker(worker_id, worker_pos, task, delta)
			ColonyTaskAllocatorScript.TaskType.BUILD_STRUCTURE:
				_update_builder_worker(worker_id, worker_pos, task, delta)
			ColonyTaskAllocatorScript.TaskType.DELIVER_RESOURCE:
				_update_delivery_worker(worker_id, worker_pos, task, delta)

## Update mining worker behavior
func _update_mining_worker(worker_id: int, worker_pos: Vector3, task, delta: float):
	var resource_pos: Vector3 = task.target_pos
	var dist: float = worker_pos.distance_to(resource_pos)

	# Debug: Print worker movement every few frames
	if randf() < 0.05:  # 5% chance = ~5/sec at 100fps
		print("    [MINE] Worker %d at %s → resource at %s (dist: %.1fm)" % [worker_id, worker_pos, resource_pos, dist])
		print("           Issuing set_order(worker_id=%d, ORDER_MOVE, target=%s)" % [worker_id, resource_pos])

	if dist < 2.0:  # Reached resource
		# Mine resource (voxel destruction)
		if voxel_world:
			var voxel_pos: Vector3i = voxel_world.world_to_voxel(resource_pos)
			voxel_world.set_voxel(voxel_pos.x, voxel_pos.y, voxel_pos.z, 0)  # Air

		# Create delivery task
		var delivery_task = task_allocator.create_task(
			ColonyTaskAllocatorScript.TaskType.DELIVER_RESOURCE,
			base_position,
			ColonyTaskAllocatorScript.Priority.NORMAL,
			task.resource_node
		)

		# Auto-assign to same worker
		worker_tasks[worker_id] = delivery_task
		delivery_task.assigned_workers.append(worker_id)

		# Issue move order toward base
		simulation_server.set_order(worker_id, 1, base_position)  # ORDER_MOVE = 1
	else:
		# Deposit congestion pheromone while traveling
		pheromone_map.deposit(worker_pos, EconomyChannel.CH_CONGESTION, 5.0 * delta)

		# Issue move order toward resource
		simulation_server.set_order(worker_id, 1, resource_pos)  # ORDER_MOVE = 1

## Update builder worker behavior
func _update_builder_worker(worker_id: int, worker_pos: Vector3, task, delta: float):
	if not task.building_site:
		_complete_task(worker_id, task)
		return

	var site = task.building_site
	var dist: float = worker_pos.distance_to(site.position)

	if dist < 3.0:  # Within construction range
		# Deposit BUILD_URGENCY pheromone while working
		pheromone_map.deposit(site.position, EconomyChannel.CH_BUILD_URGENCY, 10.0 * delta)

		# Increment construction progress
		site.progress += CONSTRUCTION_RATE_PER_WORKER * delta
		site.progress = clamp(site.progress, 0.0, 1.0)

		# Check completion
		if site.is_complete():
			_finalize_building(site)
			_complete_task(worker_id, task)
			print("  ✓ Worker %d completed building at %s" % [worker_id, site.position])

		# Issue idle order (worker stays at site)
		simulation_server.set_order(worker_id, 0, Vector3.ZERO)  # ORDER_NONE
	else:
		# Deposit congestion while traveling
		pheromone_map.deposit(worker_pos, EconomyChannel.CH_CONGESTION, 5.0 * delta)

		# Issue move order toward construction site
		simulation_server.set_order(worker_id, 1, site.position)  # ORDER_MOVE

## Finalize building construction (place voxel structure)
func _finalize_building(site):
	"""Place voxel structure based on building type"""
	if not voxel_world:
		return

	var voxel_pos: Vector3i = voxel_world.world_to_voxel(site.position)

	match site.building_type:
		0:  # OUTPOST
			voxel_world.generate_building(
				voxel_pos.x, voxel_pos.y, voxel_pos.z,
				12, 8, 12, 6, 5, true, true  # wood walls, concrete floor
			)
		1:  # REFINERY
			voxel_world.generate_building(
				voxel_pos.x, voxel_pos.y, voxel_pos.z,
				16, 12, 16, 4, 5, false, true  # steel walls (industrial)
			)
		2:  # BARRACKS
			voxel_world.generate_building(
				voxel_pos.x, voxel_pos.y, voxel_pos.z,
				24, 10, 20, 6, 5, true, true  # brick walls
			)
		3:  # TURRET
			_build_adaptive_turret(voxel_pos, site.position)
		4:  # SUPPLY_DEPOT
			voxel_world.generate_building(
				voxel_pos.x, voxel_pos.y, voxel_pos.z,
				14, 8, 14, 3, 5, false, true  # wood storage
			)

	print("  🏗 Built %s at voxel (%d, %d, %d)" % [
		_building_type_name(site.building_type),
		voxel_pos.x, voxel_pos.y, voxel_pos.z
	])

## Build adaptive turret with threat-responsive wall thickness
func _build_adaptive_turret(voxel_pos: Vector3i, world_pos: Vector3):
	"""Adaptive wall thickness based on CH_DANGER pheromone"""
	var danger = pheromone_map.sample(world_pos, 0)  # CH_DANGER

	# Thickness scales with danger: 2 (safe) → 6 (high threat)
	var thickness = int(clamp(2.0 + danger * 4.0, 2.0, 6.0))
	var height = int(clamp(4.0 + danger * 4.0, 4.0, 8.0))

	# Material upgrade under threat
	var wall_mat = 12  # MAT_SANDBAG (default)
	if danger > 0.6:
		wall_mat = 4  # MAT_STEEL
	elif danger > 0.3:
		wall_mat = 5  # MAT_CONCRETE

	voxel_world.generate_wall(
		voxel_pos.x, voxel_pos.y, voxel_pos.z,
		8, height, thickness, wall_mat, true
	)

	print("  🛡 Adaptive turret: thickness=%d, height=%d, mat=%d (danger=%.2f)" %
		  [thickness, height, wall_mat, danger])

## Helper: Building type to name
func _building_type_name(type: int) -> String:
	match type:
		0: return "Outpost"
		1: return "Refinery"
		2: return "Barracks"
		3: return "Turret"
		4: return "Supply Depot"
		_: return "Unknown"

## Update delivery worker behavior
func _update_delivery_worker(worker_id: int, worker_pos: Vector3, task, delta: float):
	var dist: float = worker_pos.distance_to(base_position)

	if dist < 3.0:  # Reached base
		# Deposit resource to stockpile
		var resource_type: int = task.resource_node.type
		match resource_type:
			0: stockpile.metal += 1
			1: stockpile.crystal += 1
			2: stockpile.energy += 1

		# Reinforce trail (2x strength on success)
		var channel: int = _resource_type_to_channel(resource_type)
		pheromone_map.deposit_trail(task.resource_node.pos, base_position, channel, 6.0)

		print("  ✓ Worker %d delivered %s (stockpile: metal=%d, crystal=%d, energy=%d)" % [
			worker_id,
			_resource_type_name(resource_type),
			stockpile.metal,
			stockpile.crystal,
			stockpile.energy
		])

		_complete_task(worker_id, task)

		# Issue idle order
		simulation_server.set_order(worker_id, 0, Vector3.ZERO)  # ORDER_NONE = 0
	else:
		# Deposit resource trail while carrying ore
		var resource_type: int = task.resource_node.type
		var channel: int = _resource_type_to_channel(resource_type)

		# Deposit trail from current position to base (ACO trail laying)
		pheromone_map.deposit(worker_pos, channel, 3.0 * delta)

		# Issue move order toward base
		simulation_server.set_order(worker_id, 1, base_position)  # ORDER_MOVE = 1

## Complete a task and free the worker
func _complete_task(worker_id: int, task):
	task_allocator.complete_task(task.id)
	worker_tasks.erase(worker_id)

## Get worker position from SimulationServer
func _get_worker_position(worker_id: int) -> Vector3:
	if not simulation_server:
		return Vector3.ZERO
	return simulation_server.get_position(worker_id)

## Spawn a worker unit
func spawn_worker(spawn_pos: Vector3) -> int:
	if not simulation_server:
		push_error("ColonyAI: Cannot spawn worker - SimulationServer not available")
		return -1

	# Spawn unit as Rifleman (role 0) with no squad (-1)
	# Workers will use basic pathfinding/movement from SimulationServer
	var worker_id: int = simulation_server.spawn_unit(spawn_pos, team, 0, -1)

	if worker_id >= 0:
		worker_ids.append(worker_id)
		print("  + Spawned worker %d at %s" % [worker_id, spawn_pos])
	else:
		push_error("ColonyAI: Failed to spawn worker at %s" % spawn_pos)

	return worker_id

## Convert resource type to pheromone channel
func _resource_type_to_channel(resource_type: int) -> int:
	match resource_type:
		0: return EconomyChannel.CH_METAL
		1: return EconomyChannel.CH_CRYSTAL
		2: return EconomyChannel.CH_ENERGY
		_: return -1

## Helper: Resource type to name
func _resource_type_name(resource_type: int) -> String:
	match resource_type:
		0: return "metal ore"
		1: return "crystal"
		2: return "energy core"
		_: return "unknown"

## Helper: Task type to name
func _task_type_name(task_type: int) -> String:
	match task_type:
		ColonyTaskAllocatorScript.TaskType.MINE_RESOURCE: return "MINE"
		ColonyTaskAllocatorScript.TaskType.BUILD_STRUCTURE: return "BUILD"
		ColonyTaskAllocatorScript.TaskType.DELIVER_RESOURCE: return "DELIVER"
		ColonyTaskAllocatorScript.TaskType.EXPLORE: return "EXPLORE"
		_: return "UNKNOWN"

## Strategic building planning (RimWorld-style simple heuristics)
func _plan_new_buildings():
	"""Periodic decision-making for what to build next"""
	if not build_planner or not economy_state:
		return

	var active_sites = build_planner.get_construction_queue()
	if active_sites.size() >= 3:  # Max 3 concurrent builds
		return

	var desired_type = _choose_next_building()
	var cost = BUILDING_COSTS.get(desired_type, {})

	if not economy_state.can_afford(team, cost):
		return

	var site_pos: Vector3 = build_planner.find_build_site(desired_type, team, base_position, 100.0)
	if site_pos == Vector3.ZERO:
		return

	if not economy_state.consume_resources(team, cost):
		return

	var site = build_planner.queue_construction(desired_type, site_pos, team, 80)
	if site:
		task_allocator.create_build_task(site, ColonyTaskAllocatorScript.Priority.HIGH)
		pheromone_map.deposit(site_pos, EconomyChannel.CH_BUILD_URGENCY, 15.0)
		print("[ColonyAI] Queued %s at %s (cost: %s)" % [
			_building_type_name(desired_type),
			site_pos,
			cost
		])

## Choose next building type based on colony needs
func _choose_next_building() -> int:
	"""Priority: Refinery → Barracks → Supply Depot → Outpost"""
	var counts = {}
	for site in build_planner.get_construction_queue():
		counts[site.building_type] = counts.get(site.building_type, 0) + 1

	# Priority 1: Always need at least one refinery (resource processing)
	if counts.get(1, 0) == 0:
		return 1

	# Priority 2: Barracks for unit production
	if counts.get(2, 0) < 2:
		return 2

	# Priority 3: Supply depots when near stockpile capacity
	if stockpile.metal + stockpile.crystal > 800:
		return 4

	# Default: Build outpost (expansion)
	return 0

## Scan for rubble and auto-queue fortifications
func _scan_destruction_sites():
	"""Detect rubble piles from destruction and convert to defensive structures"""
	if not voxel_world or not build_planner:
		return

	var grid_step = 10.0
	for x_offset in range(-5, 6):
		for z_offset in range(-5, 6):
			var sample_pos = base_position + Vector3(x_offset * grid_step, 0, z_offset * grid_step)
			var rubble_density = _count_rubble_at(sample_pos)

			if rubble_density > 10:
				var site = build_planner.queue_construction(3, sample_pos, team, 50)  # TURRET
				if site:
					print("  ♻ Rubble fortification queued at %s (density=%d)" % [sample_pos, rubble_density])
					return  # One per scan

## Count rubble voxels in vertical column
func _count_rubble_at(pos: Vector3) -> int:
	"""Count structural rubble materials in a vertical column"""
	if not voxel_world:
		return 0

	var vp: Vector3i = voxel_world.world_to_voxel(pos) if voxel_world else Vector3i.ZERO
	var voxel_x: int = vp.x
	var voxel_z: int = vp.z
	var rubble_count = 0

	for y in range(1, 20):  # Check 5m vertical column
		var material = voxel_world.get_voxel(voxel_x, y, voxel_z)
		# Structural materials: DIRT (1), STONE (2), WOOD (3), STEEL (4), CONCRETE (5), BRICK (6)
		if material >= 1 and material <= 6:
			rubble_count += 1

	return rubble_count

## Get colony statistics
func get_stats() -> Dictionary:
	return {
		"team": team,
		"workers": worker_ids.size(),
		"active_tasks": worker_tasks.size(),
		"stockpile": stockpile.duplicate(),
		"total_tasks": task_allocator.get_tasks().size() if task_allocator else 0
	}
