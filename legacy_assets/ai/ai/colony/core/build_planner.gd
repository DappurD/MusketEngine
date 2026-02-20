class_name BuildPlanner
extends RefCounted
## Spatial site selection and construction planning for base building.
##
## Handles multi-axis scoring for build site placement, construction queuing,
## and build-order planning. Designed to be game-agnostic with configurable
## scoring weights.
##
## Scoring Axes:
##   - Proximity to resources (closer = better for refineries, extractors)
##   - Proximity to base (closer = safer, easier to defend)
##   - Threat level (avoid enemy influence zones)
##   - Terrain suitability (flat ground, not blocking choke points)
##
## Usage:
##   var planner = BuildPlanner.new(world_adapter)
##   var site = planner.find_build_site(BuildingType.REFINERY, team_id, base_pos)
##   planner.queue_construction(BuildingType.OUTPOST, site, priority=80)
##
## Extraction Guide:
##   Replace BuildingType enum with your game's buildings.
##   Adjust scoring weights in _score_build_site() for your game's priorities.

## Building types (game-specific - replace as needed)
enum BuildingType {
	OUTPOST,       ## Basic forward base, spawns units
	REFINERY,      ## Processes resources near nodes
	BARRACKS,      ## Spawns combat units
	TURRET,        ## Defensive structure
	SUPPLY_DEPOT,  ## Increases resource storage
}

## Construction site state
class ConstructionSite:
	var id: int  ## Unique site ID
	var building_type: BuildingType  ## What's being built
	var position: Vector3  ## World location
	var team_id: int  ## Owning team
	var priority: int  ## Build priority (0-100)
	var progress: float = 0.0  ## 0.0 to 1.0
	var assigned_workers: Array[int] = []  ## Worker IDs building this

	func _init(p_id: int, p_type: BuildingType, p_pos: Vector3, p_team: int, p_priority: int = 50):
		id = p_id
		building_type = p_type
		position = p_pos
		team_id = p_team
		priority = p_priority

	func is_complete() -> bool:
		return progress >= 1.0


## Resource node reference (for proximity scoring)
class ResourceNode:
	var position: Vector3
	var resource_type: int  # EconomyState.ResourceType

	func _init(p_pos: Vector3, p_type: int):
		position = p_pos
		resource_type = p_type


## World adapter interface (injected dependency)
var _world_adapter = null  # IWorldAdapter

## Active construction sites
var _construction_queue: Array[ConstructionSite] = []

## Next site ID
var _next_site_id = 0

## Resource node registry (updated by scanner)
var _resource_nodes: Array[ResourceNode] = []

## Influence map reference (optional, for threat scoring)
var _influence_map = null  # Optional: InfluenceMapCPP for threat awareness


func _init(world_adapter = null):
	_world_adapter = world_adapter


## Find optimal build site for a building type.
## Returns Vector3 position or Vector3.ZERO if no suitable site.
func find_build_site(building_type: BuildingType, team_id: int, base_position: Vector3, search_radius: float = 100.0) -> Vector3:
	assert(_world_adapter != null, "WorldAdapter not set")

	var best_pos = Vector3.ZERO
	var best_score = -INF

	# Grid search around base position
	var grid_step = 8.0  # meters between sample points
	var samples = int(search_radius / grid_step)

	for x_offset in range(-samples, samples + 1):
		for z_offset in range(-samples, samples + 1):
			var candidate = base_position + Vector3(x_offset * grid_step, 0, z_offset * grid_step)

			# Get terrain height
			var height = _world_adapter.get_terrain_height(Vector2(candidate.x, candidate.z))
			candidate.y = height

			# Validate site
			if not _is_site_valid(candidate, building_type):
				continue

			# Score site
			var score = _score_build_site(candidate, building_type, team_id, base_position)

			if score > best_score:
				best_score = score
				best_pos = candidate

	return best_pos


## Score a potential build site (multi-axis).
## Higher score = better site.
func _score_build_site(position: Vector3, building_type: BuildingType, team_id: int, base_position: Vector3) -> float:
	var score = 0.0

	# Axis 1: Proximity to base (closer = safer, positive weight)
	var base_dist = position.distance_to(base_position)
	var base_score = max(0.0, 100.0 - base_dist)  # 100 at base, 0 at 100m+
	score += base_score * _get_base_proximity_weight(building_type)

	# Axis 2: Proximity to resources (for resource-dependent buildings)
	if _should_be_near_resources(building_type):
		var resource_score = _score_resource_proximity(position, building_type)
		score += resource_score * 2.0  # High weight for refineries

	# Axis 3: Threat level (avoid enemy influence)
	if _influence_map:
		var threat = _influence_map.get_pressure_at(position, 1 - team_id)  # Enemy team
		score -= threat * 50.0  # Penalty for high threat

	# Axis 4: Terrain suitability (flat ground preferred)
	var flatness_score = _score_terrain_flatness(position)
	score += flatness_score * 0.5

	# Axis 5: Strategic value (choke points, high ground)
	if building_type == BuildingType.TURRET:
		var height_bonus = position.y * 2.0  # Prefer high ground for turrets
		score += height_bonus

	return score


## Check if build site is valid (not blocked, not too steep).
func _is_site_valid(position: Vector3, building_type: BuildingType) -> bool:
	if not _world_adapter:
		return true  # No validation if no adapter

	# Check terrain pathability
	if not _world_adapter.is_pathable(position):
		return false

	# Check not too steep (TODO: implement slope check in WorldAdapter)
	# For now: assume valid if pathable

	# Check not overlapping existing construction
	for site in _construction_queue:
		if site.position.distance_to(position) < 8.0:  # Min 8m spacing
			return false

	return true


## Get base proximity weight for building type.
func _get_base_proximity_weight(building_type: BuildingType) -> float:
	match building_type:
		BuildingType.OUTPOST:
			return 0.5  # Outposts can be far from base
		BuildingType.REFINERY:
			return 0.2  # Refineries prioritize resource proximity
		BuildingType.BARRACKS:
			return 1.0  # Barracks should be near base
		BuildingType.TURRET:
			return 0.3  # Turrets go on perimeter
		BuildingType.SUPPLY_DEPOT:
			return 0.8  # Supply near base
		_:
			return 0.5


## Check if building should be near resources.
func _should_be_near_resources(building_type: BuildingType) -> bool:
	return building_type == BuildingType.REFINERY


## Score proximity to nearest resource node.
func _score_resource_proximity(position: Vector3, building_type: BuildingType) -> float:
	if _resource_nodes.is_empty():
		return 0.0

	var min_dist = INF
	for node in _resource_nodes:
		var dist = position.distance_to(node.position)
		min_dist = min(min_dist, dist)

	# Closer = higher score (inverse distance)
	return max(0.0, 100.0 - min_dist)


## Score terrain flatness (TODO: implement slope detection).
func _score_terrain_flatness(position: Vector3) -> float:
	# Placeholder: assume all terrain is equally flat
	# Real implementation: sample nearby heights, compute variance
	return 50.0


## Queue a construction project.
## Returns ConstructionSite or null if site is invalid.
func queue_construction(building_type: BuildingType, position: Vector3, team_id: int, priority: int = 50) -> ConstructionSite:
	if not _is_site_valid(position, building_type):
		return null

	var site = ConstructionSite.new(_next_site_id, building_type, position, team_id, priority)
	_next_site_id += 1

	_construction_queue.append(site)
	_construction_queue.sort_custom(_sort_sites_by_priority)

	return site


## Get all active construction sites (sorted by priority).
func get_construction_queue() -> Array[ConstructionSite]:
	return _construction_queue


## Get sites ready for worker assignment (not complete, not fully staffed).
func get_sites_needing_workers(max_workers_per_site: int = 4) -> Array[ConstructionSite]:
	var sites: Array[ConstructionSite] = []
	for site in _construction_queue:
		if not site.is_complete() and site.assigned_workers.size() < max_workers_per_site:
			sites.append(site)
	return sites


## Update construction progress (called by workers).
## Returns true if construction completed.
func update_construction_progress(site_id: int, delta_progress: float) -> bool:
	var site = _find_site_by_id(site_id)
	if not site:
		return false

	site.progress += delta_progress
	site.progress = clamp(site.progress, 0.0, 1.0)

	if site.is_complete():
		_construction_queue.erase(site)
		return true  # Signal completion

	return false


## Assign worker to construction site.
func assign_worker_to_site(site_id: int, worker_id: int) -> bool:
	var site = _find_site_by_id(site_id)
	if not site:
		return false

	if not site.assigned_workers.has(worker_id):
		site.assigned_workers.append(worker_id)

	return true


## Remove worker from construction site.
func remove_worker_from_site(site_id: int, worker_id: int) -> void:
	var site = _find_site_by_id(site_id)
	if site:
		site.assigned_workers.erase(worker_id)


## Register resource nodes (called by scanner).
func register_resource_nodes(nodes: Array) -> void:
	_resource_nodes = nodes


## Set influence map reference (optional, for threat-aware building).
func set_influence_map(influence_map) -> void:
	_influence_map = influence_map


## Find construction site by ID.
func _find_site_by_id(site_id: int) -> ConstructionSite:
	for site in _construction_queue:
		if site.id == site_id:
			return site
	return null


## Sort construction sites by priority (descending).
func _sort_sites_by_priority(a: ConstructionSite, b: ConstructionSite) -> bool:
	return a.priority > b.priority


## Get construction statistics (for KPI).
func get_construction_stats() -> Dictionary:
	var total_sites = _construction_queue.size()
	var avg_progress = 0.0
	var total_workers = 0

	for site in _construction_queue:
		avg_progress += site.progress
		total_workers += site.assigned_workers.size()

	if total_sites > 0:
		avg_progress /= float(total_sites)

	return {
		"active_sites": total_sites,
		"avg_progress": avg_progress,
		"total_workers_building": total_workers,
	}
