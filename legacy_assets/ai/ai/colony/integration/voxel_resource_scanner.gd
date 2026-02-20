extends RefCounted
class_name VoxelResourceScanner

## VoxelResourceScanner — Detect resource nodes in VoxelWorld
##
## Scans voxel chunks for resource materials (metal ore, crystals, energy)
## and returns world positions of resource deposits.
##
## Features:
## - Cached results (only re-scan when voxels destroyed)
## - Spatial clustering (merge nearby ore into single node)
## - Prioritization (score by density and proximity)

# Resource material IDs (must match voxel_materials.h)
const MATERIAL_METAL_ORE := 16     # MAT_METAL_ORE
const MATERIAL_CRYSTAL := 17       # MAT_CRYSTAL
const MATERIAL_ENERGY_CORE := 18   # MAT_ENERGY_CORE

# Clustering parameters
const CLUSTER_RADIUS := 8.0        # meters - merge ore within this distance
const MIN_CLUSTER_SIZE := 3        # minimum voxels to count as resource node

# Cached scan results
var _resource_nodes: Array[Dictionary] = []  # [{type: int, pos: Vector3, density: int}]
var _scan_version = 0  # Incremented when voxels change
var _last_scanned_version = -1

# Reference to VoxelWorld
var _voxel_world = null  # VoxelWorld instance

func _init(voxel_world):
	_voxel_world = voxel_world

## Scan voxel world for all resource nodes
## Returns: Array of {type: int, pos: Vector3, density: int}
func scan_resources() -> Array[Dictionary]:
	if not _voxel_world:
		push_error("VoxelResourceScanner: no voxel_world assigned")
		return []

	# Check if cache is valid
	if _last_scanned_version == _scan_version:
		return _resource_nodes

	_resource_nodes.clear()

	# Get world dimensions
	var world_size = Vector3i(
		_voxel_world.get_world_size_x(),
		_voxel_world.get_world_size_y(),
		_voxel_world.get_world_size_z()
	)
	var voxel_scale: float = _voxel_world.get_voxel_scale()  # 0.25m

	# Chunk-based scanning (32^3 chunks)
	var chunk_size = 32
	var chunks_x = ceili(world_size.x / float(chunk_size))
	var chunks_y = ceili(world_size.y / float(chunk_size))
	var chunks_z = ceili(world_size.z / float(chunk_size))

	print("VoxelResourceScanner: Scanning %d chunks..." % (chunks_x * chunks_y * chunks_z))

	var found_voxels: Array[Dictionary] = []

	# Scan each chunk
	for cz in range(chunks_z):
		for cx in range(chunks_x):
			for cy in range(chunks_y):
				var chunk_origin = Vector3i(cx * chunk_size, cy * chunk_size, cz * chunk_size)
				_scan_chunk(chunk_origin, chunk_size, voxel_scale, found_voxels)

	# Cluster nearby voxels into nodes
	_cluster_into_nodes(found_voxels)

	_last_scanned_version = _scan_version
	print("VoxelResourceScanner: Found %d resource nodes" % _resource_nodes.size())
	return _resource_nodes

## Scan a single chunk for resource voxels
func _scan_chunk(chunk_origin: Vector3i, chunk_size: int, voxel_scale: float, out_voxels: Array[Dictionary]) -> void:
	var world_size = Vector3i(
		_voxel_world.get_world_size_x(),
		_voxel_world.get_world_size_y(),
		_voxel_world.get_world_size_z()
	)

	for z in range(chunk_size):
		for x in range(chunk_size):
			for y in range(chunk_size):
				var voxel_pos = chunk_origin + Vector3i(x, y, z)

				# Bounds check
				if voxel_pos.x >= world_size.x or voxel_pos.y >= world_size.y or voxel_pos.z >= world_size.z:
					continue

				# Get voxel material
				var material: int = _voxel_world.get_voxel(voxel_pos.x, voxel_pos.y, voxel_pos.z)

				# Check if it's a resource
				var resource_type = -1
				if material == MATERIAL_METAL_ORE:
					resource_type = 0
				elif material == MATERIAL_CRYSTAL:
					resource_type = 1
				elif material == MATERIAL_ENERGY_CORE:
					resource_type = 2

				if resource_type >= 0:
					# Convert to world position (voxel center)
					var world_pos = Vector3(
						voxel_pos.x * voxel_scale + voxel_scale * 0.5,
						voxel_pos.y * voxel_scale + voxel_scale * 0.5,
						voxel_pos.z * voxel_scale + voxel_scale * 0.5
					)
					out_voxels.append({"type": resource_type, "pos": world_pos})

## Cluster nearby resource voxels into nodes
func _cluster_into_nodes(voxels: Array[Dictionary]) -> void:
	if voxels.size() == 0:
		return

	# Sort by type
	voxels.sort_custom(func(a, b): return a.type < b.type)

	var used = {}

	for i in range(voxels.size()):
		if i in used:
			continue

		var seed = voxels[i]
		var cluster: Array[Dictionary] = [seed]
		used[i] = true

		# Find nearby voxels of same type
		for j in range(i + 1, voxels.size()):
			if j in used:
				continue
			if voxels[j].type != seed.type:
				break

			var dist: float = seed.pos.distance_to(voxels[j].pos)
			if dist <= CLUSTER_RADIUS:
				cluster.append(voxels[j])
				used[j] = true

		# Create node if cluster big enough
		if cluster.size() >= MIN_CLUSTER_SIZE:
			var center = _compute_cluster_center(cluster)
			_resource_nodes.append({"type": seed.type, "pos": center, "density": cluster.size()})

## Compute centroid of cluster
func _compute_cluster_center(cluster: Array[Dictionary]) -> Vector3:
	var sum = Vector3.ZERO
	for voxel in cluster:
		sum += voxel.pos
	return sum / cluster.size()

## Get resource nodes of a specific type
func get_nodes_by_type(resource_type: int) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for node in _resource_nodes:
		if node.type == resource_type:
			result.append(node)
	return result

## Get nearest resource node to a position
func get_nearest_node(world_pos: Vector3, resource_type: int = -1) -> Dictionary:
	var nearest: Dictionary = {}
	var min_dist = INF

	for node in _resource_nodes:
		if resource_type >= 0 and node.type != resource_type:
			continue

		var dist = world_pos.distance_to(node.pos)
		if dist < min_dist:
			min_dist = dist
			nearest = node

	return nearest

## Invalidate cache (call when voxels destroyed)
func invalidate_cache() -> void:
	_scan_version += 1

## Score resource nodes for task allocation
func score_nodes(worker_pos: Vector3, resource_type: int) -> Array[Dictionary]:
	var nodes: Array[Dictionary] = get_nodes_by_type(resource_type)
	var scored: Array[Dictionary] = []

	for node in nodes:
		var dist: float = worker_pos.distance_to(node.pos)
		var score: float = node.density / (dist + 1.0)
		scored.append({"node": node, "score": score})

	scored.sort_custom(func(a, b): return a.score > b.score)
	return scored
