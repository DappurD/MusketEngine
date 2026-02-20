extends RefCounted
class_name ColonyTaskAllocator

## ColonyTaskAllocator — Stigmergic worker assignment
##
## Allocates workers to resource nodes using:
## - ACO scoring (density/distance)
## - Congestion avoidance (load balancing)
## - Pheromone trail bias (exploitation vs exploration)
##
## Inspired by ant colony optimization and swarm intelligence

# Task types
enum TaskType {
	IDLE,
	MINE_METAL,
	MINE_CRYSTAL,
	MINE_ENERGY,
	MINE_RESOURCE,
	BUILD,
	BUILD_STRUCTURE,
	DELIVER_RESOURCE,
	EXPLORE,
}

# Task priority levels
enum Priority {
	LOW = 0,
	NORMAL = 1,
	HIGH = 2,
	CRITICAL = 3,
}

# Active tasks
class Task:
	var id: int
	var type: TaskType
	var priority: Priority
	var target_pos: Vector3
	var resource_node: Dictionary  # {type, pos, density} from scanner
	var building_site: RefCounted = null  # ConstructionSite from BuildPlanner
	var assigned_workers: Array[int] = []  # Worker IDs
	var congestion_score: float = 0.0
	var created_time: float = 0.0

# Task registry
var _tasks: Array[Task] = []
var _next_task_id = 0
var _time_alive = 0.0

# System references
var voxel_world: VoxelWorld = null
var resource_scanner: RefCounted = null  # VoxelResourceScanner
var pheromone_map: PheromoneMapCPP = null

# Constants from economy_constants.gd
const EconomyChannel = preload("res://ai/colony/economy_constants.gd").EconomyChannel
const ACO_ALPHA := 1.5  # Pheromone importance
const ACO_BETA := 2.0   # Greedy bias (distance heuristic)
const CONGESTION_THRESHOLD := 0.4  # Load balancing threshold
const EXPLORATION_CHANCE := 0.1  # 10% chance to ignore trails and explore

func _init(p_voxel_world, p_resource_scanner, p_pheromone_map):
	voxel_world = p_voxel_world
	resource_scanner = p_resource_scanner
	pheromone_map = p_pheromone_map

## Tick task allocation (called from ColonyAI main loop)
func tick(delta: float):
	_time_alive += delta
	_update_task_congestion()
	_prune_completed_tasks()

## Allocate worker to best available task
## Returns: Task or null if no suitable task
func allocate_worker(worker_id: int, worker_pos: Vector3, team: int) -> Task:
	# Scan for resource nodes if none cached
	if resource_scanner:
		resource_scanner.scan_resources()

	# Score all available tasks for this worker
	var scored_tasks = _score_tasks_for_worker(worker_pos, team)

	# ACO decision: exploitation vs exploration
	if randf() < EXPLORATION_CHANCE:
		# Exploration: Ignore pheromones, pick distant/unknown task
		return _pick_exploration_task(scored_tasks, worker_pos)
	else:
		# Exploitation: Follow pheromone trails to high-value tasks
		return _pick_best_task(scored_tasks, worker_id)

## Create mining task for a resource node
func create_mining_task(node: Dictionary, priority: Priority = Priority.NORMAL) -> Task:
	var task = Task.new()
	task.id = _next_task_id
	_next_task_id += 1
	task.type = _resource_type_to_task_type(node.type)
	task.priority = priority
	task.target_pos = node.pos
	task.resource_node = node
	task.created_time = _time_alive
	_tasks.append(task)

	print("  [Colony] Created %s task #%d at %s (density=%d, priority=%s)" % [
		_task_type_name(task.type),
		task.id,
		task.target_pos,
		node.density,
		_priority_name(priority)
	])

	return task

## Create building task for a construction site
func create_build_task(site, priority: Priority = Priority.NORMAL) -> Task:
	var task = Task.new()
	task.id = _next_task_id
	_next_task_id += 1
	task.type = TaskType.BUILD
	task.priority = priority
	task.target_pos = site.position
	task.building_site = site
	task.created_time = _time_alive
	_tasks.append(task)

	print("  [Colony] Created BUILD task #%d at %s (priority=%s)" % [
		task.id,
		task.target_pos,
		_priority_name(priority)
	])

	return task

## Create a generic task (mining, delivery, etc.)
func create_task(type: TaskType, target_pos: Vector3, priority: Priority, resource_node: Dictionary = {}) -> Task:
	var task = Task.new()
	task.id = _next_task_id
	_next_task_id += 1
	task.type = type
	task.priority = priority
	task.target_pos = target_pos
	task.resource_node = resource_node
	task.created_time = _time_alive
	_tasks.append(task)
	return task

## Get all active tasks
func get_tasks() -> Array[Task]:
	return _tasks

## Complete (remove) a task by ID
func complete_task(task_id: int) -> void:
	for i in range(_tasks.size()):
		if _tasks[i].id == task_id:
			_tasks.remove_at(i)
			return

## Score all tasks for a specific worker position
func _score_tasks_for_worker(worker_pos: Vector3, team: int) -> Array[Dictionary]:
	var scored: Array[Dictionary] = []

	for task in _tasks:
		# Skip fully assigned tasks (too congested)
		if task.congestion_score > CONGESTION_THRESHOLD and task.assigned_workers.size() >= 3:
			continue

		# Distance heuristic (ACO β parameter)
		var dist = worker_pos.distance_to(task.target_pos)
		var distance_score = 1.0 / (dist + 1.0)

		# Pheromone trail strength (ACO α parameter)
		var pheromone_score = 0.0
		if pheromone_map:
			var channel = _task_type_to_pheromone_channel(task.type)
			if channel >= 0:
				pheromone_score = pheromone_map.sample(task.target_pos, channel)

		# Congestion penalty (load balancing)
		var congestion_penalty = 1.0 - task.congestion_score

		# Resource density bonus (richer deposits = higher value)
		var density_bonus = 1.0
		if task.resource_node.has("density"):
			density_bonus = task.resource_node.density / 10.0  # Normalize

		# Priority multiplier (critical tasks get 3x weight)
		var priority_mult = 1.0 + float(task.priority)

		# ACO formula: P(task) = (pheromone^α * heuristic^β) * modifiers
		var pheromone_weight = pow(pheromone_score + 0.1, ACO_ALPHA)  # +0.1 to avoid zero
		var distance_weight = pow(distance_score, ACO_BETA)
		var base_score = pheromone_weight * distance_weight

		var final_score = base_score * density_bonus * congestion_penalty * priority_mult

		scored.append({
			"task": task,
			"score": final_score,
			"distance": dist,
			"pheromone": pheromone_score,
			"congestion": task.congestion_score
		})

	# Sort by score descending
	scored.sort_custom(func(a, b): return a.score > b.score)
	return scored

## Pick best task (exploitation)
func _pick_best_task(scored_tasks: Array[Dictionary], worker_id: int) -> Task:
	if scored_tasks.size() == 0:
		return null

	var best = scored_tasks[0]
	var task = best.task

	# Assign worker to task
	if not task.assigned_workers.has(worker_id):
		task.assigned_workers.append(worker_id)

	print("  → Worker %d assigned to %s task #%d (score=%.2f, dist=%.1fm, pheromone=%.2f)" % [
		worker_id,
		_task_type_name(task.type),
		task.id,
		best.score,
		best.distance,
		best.pheromone
	])

	return task

## Pick exploration task (ignore pheromones, try unknown areas)
func _pick_exploration_task(scored_tasks: Array[Dictionary], worker_pos: Vector3) -> Task:
	if scored_tasks.size() == 0:
		return null

	# Find farthest task with low pheromone (unexplored)
	for entry in scored_tasks:
		if entry.pheromone < 0.3:  # Low trail = unexplored
			print("  ⚡ Worker exploring task #%d (dist=%.1fm, low pheromone)" % [
				entry.task.id,
				entry.distance
			])
			return entry.task

	# Fallback: pick random task (true exploration)
	var rand_idx = randi() % scored_tasks.size()
	print("  ⚡ Worker exploring random task #%d" % scored_tasks[rand_idx].task.id)
	return scored_tasks[rand_idx].task

## Update congestion scores for all tasks
func _update_task_congestion():
	if not pheromone_map:
		return

	for task in _tasks:
		# Sample congestion pheromone at task location
		task.congestion_score = pheromone_map.sample(
			task.target_pos,
			EconomyChannel.CH_CONGESTION
		)

## Remove completed or stale tasks
func _prune_completed_tasks():
	var i = 0
	while i < _tasks.size():
		var task = _tasks[i]

		# Remove if no workers assigned for >30 seconds (abandoned)
		var task_age = _time_alive - task.created_time
		if task.assigned_workers.size() == 0 and task_age > 30.0:
			print("  [Colony] Pruned stale task #%d (%s)" % [task.id, _task_type_name(task.type)])
			_tasks.remove_at(i)
			continue

		i += 1

## Get all tasks of a specific type
func get_tasks_by_type(type: TaskType) -> Array[Task]:
	var result: Array[Task] = []
	for task in _tasks:
		if task.type == type:
			result.append(task)
	return result

## Remove worker from task (on completion or death)
func unassign_worker(worker_id: int, task: Task):
	if task and task.assigned_workers.has(worker_id):
		task.assigned_workers.erase(worker_id)

## Helper: Resource type → task type
func _resource_type_to_task_type(resource_type: int) -> TaskType:
	match resource_type:
		0: return TaskType.MINE_METAL
		1: return TaskType.MINE_CRYSTAL
		2: return TaskType.MINE_ENERGY
		_: return TaskType.IDLE

## Helper: Task type → pheromone channel
func _task_type_to_pheromone_channel(type: TaskType) -> int:
	match type:
		TaskType.MINE_METAL: return EconomyChannel.CH_METAL
		TaskType.MINE_CRYSTAL: return EconomyChannel.CH_CRYSTAL
		TaskType.MINE_ENERGY: return EconomyChannel.CH_ENERGY
		TaskType.BUILD: return EconomyChannel.CH_BUILD_URGENCY
		_: return -1

## Helper: Task type name
func _task_type_name(type: TaskType) -> String:
	match type:
		TaskType.IDLE: return "idle"
		TaskType.MINE_METAL: return "mine_metal"
		TaskType.MINE_CRYSTAL: return "mine_crystal"
		TaskType.MINE_ENERGY: return "mine_energy"
		TaskType.BUILD: return "build"
		TaskType.EXPLORE: return "explore"
		_: return "unknown"

## Helper: Priority name
func _priority_name(priority: Priority) -> String:
	match priority:
		Priority.LOW: return "low"
		Priority.NORMAL: return "normal"
		Priority.HIGH: return "high"
		Priority.CRITICAL: return "critical"
		_: return "unknown"
