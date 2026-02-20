class_name WorldAdapter
extends RefCounted
## Spatial and terrain queries for Economy AI.
##
## Abstracts VoxelWorld-specific queries behind a generic interface.
## Allows Economy AI to query terrain without coupling to voxel representation.
##
## For other games: Implement IWorldQuery interface with your spatial system
## (NavMesh, heightmap, tilemap, etc.).
##
## Usage:
##   var adapter = WorldAdapter.new(voxel_world)
##   var height = adapter.get_terrain_height(Vector2(100, 50))
##   if adapter.is_pathable(position):
##       # Site is valid for construction

## Reference to VoxelWorld
var _voxel_world = null  # VoxelWorld

## Cache for terrain height queries (optional optimization)
var _height_cache = {}
var _cache_grid_size = 4.0  # meters per cache cell


func _init(voxel_world):
	_voxel_world = voxel_world


## Get terrain height at 2D position.
## Returns Y coordinate of highest solid voxel, or 0.0 if none.
func get_terrain_height(xz_position: Vector2) -> float:
	if not _voxel_world:
		return 0.0

	# Check cache first
	var cache_key = _get_cache_key(xz_position)
	if _height_cache.has(cache_key):
		return _height_cache[cache_key]

	# Query VoxelWorld
	# TODO: VoxelWorld needs get_height_at(x, z) or similar
	# For now: placeholder

	var height = 0.0
	# height = _voxel_world.get_height_at(xz_position.x, xz_position.y)

	# Cache result
	_height_cache[cache_key] = height

	return height


## Check if position is pathable (not solid voxel).
func is_pathable(position: Vector3) -> bool:
	if not _voxel_world:
		return true  # Assume pathable if no world

	# TODO: Query voxel solidity at position
	# var voxel_pos = _world_to_voxel(position)
	# var material = _voxel_world.get_voxel(voxel_pos.x, voxel_pos.y, voxel_pos.z)
	# return material == 0  # 0 = air

	return true  # Placeholder


## Get distance between two points (accounting for pathability).
## For now: straight-line distance. Future: pathfinding distance.
func get_distance(from: Vector3, to: Vector3) -> float:
	return from.distance_to(to)


## Check if there's line-of-sight between two points (for build site validation).
func has_line_of_sight(from: Vector3, to: Vector3) -> bool:
	if not _voxel_world:
		return true

	# TODO: Raycast through voxel world
	# return _voxel_world.raycast(from, to).is_empty()

	return true  # Placeholder


## Get terrain slope at position (for build site flatness check).
## Returns slope in degrees (0 = flat, 90 = vertical).
func get_terrain_slope(position: Vector3, sample_radius: float = 2.0) -> float:
	if not _voxel_world:
		return 0.0

	# Sample height at 4 cardinal points around position
	var center_height = get_terrain_height(Vector2(position.x, position.z))

	var north_height = get_terrain_height(Vector2(position.x, position.z + sample_radius))
	var south_height = get_terrain_height(Vector2(position.x, position.z - sample_radius))
	var east_height = get_terrain_height(Vector2(position.x + sample_radius, position.z))
	var west_height = get_terrain_height(Vector2(position.x - sample_radius, position.z))

	# Compute max height difference
	var max_diff = max(
		abs(north_height - center_height),
		abs(south_height - center_height),
		abs(east_height - center_height),
		abs(west_height - center_height)
	)

	# Convert to slope in degrees
	var slope_radians = atan(max_diff / sample_radius)
	return rad_to_deg(slope_radians)


## Check if area is flat enough for construction.
## Returns true if slope < max_slope_degrees.
func is_flat_enough(position: Vector3, max_slope_degrees: float = 15.0) -> bool:
	var slope = get_terrain_slope(position)
	return slope <= max_slope_degrees


## Clear height cache (call after voxel destruction).
func clear_height_cache() -> void:
	_height_cache.clear()


## Get cache key for position (grid-aligned).
func _get_cache_key(xz_position: Vector2) -> Vector2i:
	return Vector2i(
		int(xz_position.x / _cache_grid_size),
		int(xz_position.y / _cache_grid_size)
	)


## Convert world position to voxel coordinates.
func _world_to_voxel(world_pos: Vector3) -> Vector3i:
	if _voxel_world:
		return _voxel_world.world_to_voxel(world_pos)
	# Fallback if no world reference
	var voxel_scale: float = 0.25
	return Vector3i(
		int(world_pos.x / voxel_scale),
		int(world_pos.y / voxel_scale),
		int(world_pos.z / voxel_scale)
	)
