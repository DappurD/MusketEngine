class_name TaskAllocator
extends RefCounted
## Multi-objective worker assignment for economy tasks.
##
## Allocates available workers to pending tasks using distance-weighted scoring.
## Designed to be game-agnostic - only requires worker positions and task locations.
##
## Algorithm: Greedy distance-based assignment
##   1. Sort tasks by priority (descending)
##   2. For each task, assign closest available worker
##   3. Mark worker as busy, remove from available pool
##   4. Repeat until no workers or no tasks remain
##
## Usage:
##   var allocator = TaskAllocator.new()
##   var assignments = allocator.assign_workers(available_workers, pending_tasks)
##   # Returns Dictionary { worker_id: task_id }
##
## Future: Can upgrade to Hungarian algorithm for globally optimal assignment.

## Worker state (minimal data needed for allocation)
class WorkerState:
	var id: int  ## Unique worker identifier
	var position: Vector3  ## Current world position
	var team_id: int  ## Team ownership

	func _init(p_id: int, p_pos: Vector3, p_team: int):
		id = p_id
		position = p_pos
		team_id = p_team


## Economy task representation
class EconomyTask:
	var id: int  ## Unique task identifier
	var type: TaskType  ## What kind of task (gather, build, transport)
	var location: Vector3  ## World position of task
	var priority: int  ## Higher = more important (0-100)
	var required_role: int = -1  ## Optional: specific worker role needed (-1 = any)

	func _init(p_id: int, p_type: TaskType, p_loc: Vector3, p_priority: int = 50):
		id = p_id
		type = p_type
		location = p_loc
		priority = p_priority


## Task types (game-specific - extend as needed)
enum TaskType {
	TASK_GATHER,    ## Gather resources from node
	TASK_BUILD,     ## Construct building
	TASK_TRANSPORT, ## Move resources between stockpiles
	TASK_REPAIR,    ## Repair damaged building
}


## Assign workers to tasks using greedy distance-based algorithm.
## Returns Dictionary mapping worker_id -> task_id.
func assign_workers(workers: Array, tasks: Array) -> Dictionary:
	var assignments = {}
	var available = workers.duplicate()  # Don't modify input array

	# Sort tasks by priority (highest first)
	var sorted_tasks = tasks.duplicate()
	sorted_tasks.sort_custom(_sort_by_priority_desc)

	# Greedy assignment: best worker for each task
	for task in sorted_tasks:
		if available.is_empty():
			break  # No more workers

		var best_worker = _find_closest_worker(task, available)
		if best_worker == null:
			continue  # No suitable worker (e.g., wrong role)

		assignments[best_worker.id] = task.id
		available.erase(best_worker)

	return assignments


## Find closest available worker to a task.
## Returns WorkerState or null if none suitable.
func _find_closest_worker(task: EconomyTask, workers: Array) -> WorkerState:
	var best: WorkerState = null
	var best_dist = INF

	for worker in workers:
		# Role filter (if task requires specific role)
		if task.required_role >= 0 and worker.get("role_id") != task.required_role:
			continue

		# Distance scoring
		var dist = worker.position.distance_to(task.location)

		# TODO: Could add weights here:
		# - Prefer workers already carrying resources for TASK_TRANSPORT
		# - Prefer builders with construction skill for TASK_BUILD
		# For now: simple distance

		if dist < best_dist:
			best = worker
			best_dist = dist

	return best


## Sort comparator: tasks by priority descending.
func _sort_by_priority_desc(a: EconomyTask, b: EconomyTask) -> bool:
	return a.priority > b.priority


## Assign workers with role preferences.
## Preferred roles get a distance bonus (treated as closer).
## role_weights: Dictionary { TaskType: { role_id: weight_multiplier } }
func assign_workers_weighted(workers: Array, tasks: Array, role_weights: Dictionary) -> Dictionary:
	var assignments = {}
	var available = workers.duplicate()

	var sorted_tasks = tasks.duplicate()
	sorted_tasks.sort_custom(_sort_by_priority_desc)

	for task in sorted_tasks:
		if available.is_empty():
			break

		var best_worker = _find_best_worker_weighted(task, available, role_weights)
		if best_worker == null:
			continue

		assignments[best_worker.id] = task.id
		available.erase(best_worker)

	return assignments


## Find best worker using role weight multipliers.
## Lower effective distance = better match.
func _find_best_worker_weighted(task: EconomyTask, workers: Array, role_weights: Dictionary) -> WorkerState:
	var best: WorkerState = null
	var best_score = INF

	# Get role weights for this task type
	var task_weights: Dictionary = role_weights.get(task.type, {})

	for worker in workers:
		# Hard role filter
		if task.required_role >= 0 and worker.get("role_id") != task.required_role:
			continue

		var dist = worker.position.distance_to(task.location)

		# Apply role weight (multiplier on distance)
		var role_id = worker.get("role_id", 0)
		var weight = task_weights.get(role_id, 1.0)
		var effective_dist = dist * weight  # Lower weight = better match

		if effective_dist < best_score:
			best = worker
			best_score = effective_dist

	return best


## Compute total travel distance for an assignment.
## Useful for KPI tracking / comparing allocation strategies.
func compute_total_distance(workers: Array, tasks: Array, assignments: Dictionary) -> float:
	var total = 0.0

	for worker_id in assignments:
		var task_id = assignments[worker_id]

		# Find worker and task
		var worker = _find_worker_by_id(workers, worker_id)
		var task = _find_task_by_id(tasks, task_id)

		if worker and task:
			total += worker.position.distance_to(task.location)

	return total


## Helper: Find worker by ID
func _find_worker_by_id(workers: Array, worker_id: int) -> WorkerState:
	for worker in workers:
		if worker.id == worker_id:
			return worker
	return null


## Helper: Find task by ID
func _find_task_by_id(tasks: Array, task_id: int) -> EconomyTask:
	for task in tasks:
		if task.id == task_id:
			return task
	return null
