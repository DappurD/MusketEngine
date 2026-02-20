extends Node3D
class_name VoxelWorldRenderer
## Renders a VoxelWorld by meshing chunks via a multi-threaded worker pool.
## Uploads completed meshes to RenderingServer on the main thread.
## Generates per-chunk collision shapes (trimesh) for physics.
##
## Phase 1F: Thread pool meshing + distance management + frustum culling.

@export var voxel_world_path: NodePath = ^".."
@export var auto_generate: bool = true
@export var generate_collisions: bool = true
@export var collision_budget_ms: float = 4.0
@export var num_mesh_threads: int = 4
@export var max_uploads_per_frame: int = 32  ## Max chunk meshes to upload per frame
@export var visibility_radius: float = 400.0  ## Hide chunks beyond this distance

## LOD distance thresholds
@export var lod1_distance: float = 100.0  ## Switch to LOD 1 beyond this distance
@export var lod2_distance: float = 200.0  ## Switch to LOD 2 beyond this distance

## Collision distance — remove collision shapes beyond this distance
@export var collision_radius: float = 100.0
const CHUNK_SIZE_VOXELS: int = 32
const GPU_CULL_CAPACITY: int = 16384

var _world: VoxelWorld
var _mesh_worker: ChunkMeshWorker

## Per-chunk RID tracking
var _chunk_instances: Dictionary = {}  ## Vector3i → RID (instance)
var _chunk_meshes: Dictionary = {}     ## Vector3i → RID (mesh) — LOD 0
var _chunk_lod1_meshes: Dictionary = {}  ## Vector3i → RID (mesh) — LOD 1
var _chunk_lod2_meshes: Dictionary = {}  ## Vector3i → RID (mesh) — LOD 2
var _chunk_current_lod: Dictionary = {}  ## Vector3i → int (active LOD level)
var _chunk_bodies: Dictionary = {}     ## Vector3i → RID (physics body)
var _chunk_shapes: Dictionary = {}     ## Vector3i → RID (collision shape)

## Rendering resources
var _material: RID
var _scenario: RID
var _physics_space: RID

## PBR material lookup texture (16x2: row0=R:roughness G:metallic B:emission, row1=R:subsurface G:anisotropy B:normal_strength A:specular_tint)
var _pbr_image: Image
var _pbr_texture: ImageTexture

## State
var _initial_mesh_done: bool = false
var _initial_queued: bool = false
var _collision_done: bool = false
var _lod_generation_queued: bool = false  ## True after LOD 1+2 are queued

## Stats
var _stats_quads: int = 0
var _stats_chunks_meshed: int = 0
var _stats_initial_total: int = 0
var _progress_timer: float = 0.0

## Deferred collision
var _pending_collision: Array[Vector3i] = []
var _chunk_collision_data: Dictionary = {}

## Distance management
var _last_camera_pos: Vector3 = Vector3.INF
var _distance_timer: float = 0.0
const DISTANCE_CHECK_INTERVAL: float = 0.5
const CAMERA_MOVE_THRESHOLD: float = 16.0

## Dedup: chunks currently queued in worker
var _queued_set: Dictionary = {}

## GPU chunk culler
var _gpu_culler: GpuChunkCuller
var _chunk_keys: Array[Vector3i] = []  ## Ordered list matching GPU AABB buffer
var _aabbs_dirty: bool = true
var _gpu_cull_mismatch_warned: bool = false
var _gpu_cull_capacity: int = 0
var _gpu_cull_invalid_entries_dropped: int = 0
var _gpu_cull_over_capacity_frames: int = 0

## Startup meshing focus (scenario battle envelope)
var _startup_focus_enabled: bool = false
var _startup_focus: Vector3 = Vector3.ZERO
var _startup_mesh_elapsed: float = 0.0
const STARTUP_THROTTLE_WINDOW_SEC: float = 60.0

## LOD 3 terrain mesh — single heightmap mesh for distant backdrop
var _lod3_mesh_rid: RID
var _lod3_instance_rid: RID
var _lod3_built: bool = false
var _lod3_rebuild_timer: float = -1.0  ## <0 = idle, >=0 = counting up
const LOD3_REBUILD_DEBOUNCE: float = 0.5  ## Seconds to wait after last destruction
@export var lod3_quad_size: float = 4.0  ## Meters per terrain quad

## Cached world params (set once in _ready)
var _scale: float = 0.25
var _half_world_x: float = 0.0
var _half_world_z: float = 0.0

## Island physics bridge (Tier 1 — disconnected voxel regions)
const MAX_ISLANDS: int = 24
const ISLAND_REST_THRESHOLD: float = 0.3  ## velocity below this for rest detection
const ISLAND_REST_TIME: float = 8.0       ## seconds at rest before re-solidify + cleanup
const ISLAND_FALL_LIMIT: float = -30.0    ## y below which islands are destroyed

var _island_bodies: Array[RID] = []
var _island_instances: Array[RID] = []
var _island_meshes: Array[RID] = []
var _island_shapes: Array[RID] = []
var _island_active: Array[bool] = []
var _island_rest_timer: Array[float] = []
var _island_initial_xforms: Array[Transform3D] = []
var _island_voxel_positions: Array = []  ## Array of PackedVector3Array
var _island_voxel_materials: Array = []  ## Array of PackedByteArray
var _islands_initialized: bool = false


func _ready() -> void:
	_world = get_node(voxel_world_path) as VoxelWorld
	if not _world:
		push_error("[VoxelWorldRenderer] No VoxelWorld found at path: %s" % voxel_world_path)
		return

	_scenario = get_world_3d().scenario
	_physics_space = get_world_3d().space

	# Create shader material for PBR voxels
	_material = RenderingServer.material_create()
	var shader_rid: RID = RenderingServer.shader_create()
	RenderingServer.shader_set_code(shader_rid, _get_shader_code())
	RenderingServer.material_set_shader(_material, shader_rid)

	# Create PBR lookup texture (16x2: row0=roughness/metallic/emission, row1=subsurface/anisotropy/normal_str/spec_tint)
	_create_pbr_lut()
	RenderingServer.material_set_param(_material, "pbr_lut", _pbr_texture.get_rid())

	if auto_generate:
		# Check for scenario-specific map config via ScenarioState autoload
		var state: Node = get_node_or_null("/root/ScenarioState")
		var scenario_map = null
		if state and state.get("is_scenario_mode") and state.get("scenario_config"):
			scenario_map = state.scenario_config.map

		if scenario_map:
			var ws: Vector3i = scenario_map.world_size
			if not _world.is_initialized():
				_world.setup(ws.x, ws.y, ws.z, scenario_map.voxel_scale)
			print("[VoxelWorldRenderer] Generating scenario map (%s, %dx%dx%d)..." % [
				scenario_map.terrain_type, ws.x, ws.y, ws.z])
			var gen_start: int = Time.get_ticks_msec()
			match scenario_map.terrain_type:
				"arena":
					_generate_arena(scenario_map)
				"ruined_city":
					_generate_ruined_city(scenario_map)
				"mountain_pass":
					_generate_mountain_pass(scenario_map)
				"river_valley":
					_generate_river_valley(scenario_map)
				"flat":
					_world.generate_terrain(scenario_map.base_height, 0, 0.0)
				"battlefield", "standard":
					_world.generate_test_battlefield()
				_:
					# Unknown terrain type falls back to the upgraded tactical battlefield.
					print("[VoxelWorldRenderer] Unknown terrain_type '%s', using tactical battlefield" % scenario_map.terrain_type)
					_world.generate_test_battlefield()
			var gen_time: int = Time.get_ticks_msec() - gen_start
			print("[VoxelWorldRenderer] Generation took %d ms" % gen_time)
		else:
			if not _world.is_initialized():
				var ws_x: int = 2400
				var ws_y: int = 128
				var ws_z: int = 1600
				var vs: float = 0.25
				if state:
					ws_x = state.get("world_size_x") if "world_size_x" in state else ws_x
					ws_y = state.get("world_size_y") if "world_size_y" in state else ws_y
					ws_z = state.get("world_size_z") if "world_size_z" in state else ws_z
					vs = state.get("world_voxel_scale") if "world_voxel_scale" in state else vs
				_world.setup(ws_x, ws_y, ws_z, vs)
			var free_play_type: String = "battlefield"
			if state and "free_play_terrain_type" in state:
				free_play_type = str(state.free_play_terrain_type)
			print("[VoxelWorldRenderer] Generating free-play map (%s)..." % free_play_type)
			var gen_start: int = Time.get_ticks_msec()
			match free_play_type:
				"arena":
					var default_map = {
						"base_height": 16,
						"hill_amplitude": 4,
						"hill_frequency": 0.02,
					}
					_generate_arena(default_map)
				"ruined_city":
					_generate_ruined_city({"base_height": 16, "hill_amplitude": 6, "hill_frequency": 0.012})
				"mountain_pass":
					_generate_mountain_pass({"base_height": 16, "hill_amplitude": 20, "hill_frequency": 0.009})
				"river_valley":
					_generate_river_valley({"base_height": 16, "hill_amplitude": 14, "hill_frequency": 0.010})
				"battlefield", "standard":
					_world.generate_test_battlefield()
				_:
					_world.generate_test_battlefield()
			var gen_time: int = Time.get_ticks_msec() - gen_start
			print("[VoxelWorldRenderer] Generation took %d ms" % gen_time)

		print("[VoxelWorldRenderer] Memory: %d bytes (%d MB)" % [
			_world.get_memory_usage_bytes(),
			_world.get_memory_usage_bytes() / (1024 * 1024)
		])

	# Cache world params
	_scale = _world.get_voxel_scale()
	_half_world_x = float(_world.get_world_size_x()) * _scale * 0.5
	_half_world_z = float(_world.get_world_size_z()) * _scale * 0.5

	# Start thread pool mesher
	_mesh_worker = ChunkMeshWorker.new()
	_mesh_worker.setup(_world, num_mesh_threads)

	# Initialize GPU chunk culler
	_gpu_culler = GpuChunkCuller.new()
	_gpu_cull_capacity = _estimate_gpu_cull_capacity()
	_gpu_culler.setup(_gpu_cull_capacity)


func _generate_arena(map_config) -> void:
	## Generate a small arena map for micro scenarios.
	## Terrain with scattered cover walls and a few small buildings.
	var sx: int = _world.get_world_size_x()
	var sz: int = _world.get_world_size_z()
	var scale: float = _world.get_voxel_scale()
	var cx: int = sx / 2
	var cz: int = sz / 2

	# Base terrain with gentle hills
	_world.generate_terrain(map_config.base_height, map_config.hill_amplitude, map_config.hill_frequency)

	# Central cover: denser staggered walls around the center (less open center lanes)
	var wall_offsets: Array[Array] = [
		[-24, -8, 12, true],   # West of center, along X
		[12, -8, 12, true],    # East of center, along X
		[-8, -24, 12, false],  # South of center, along Z
		[-8, 12, 12, false],   # North of center, along Z
		[-28, 10, 10, true],
		[14, 10, 10, true],
		[10, -28, 10, false],
		[10, 14, 10, false],
	]
	for w: Array in wall_offsets:
		var wx: int = cx + int(w[0])
		var wz: int = cz + int(w[1])
		var wlen: int = int(w[2])
		var along_x: bool = bool(w[3])
		if wx >= 4 and wx + wlen < sx - 4 and wz >= 4 and wz + wlen < sz - 4:
			var wy: int = map_config.base_height
			_world.generate_wall(wx, wy, wz, wlen, 4, 2, 5, along_x)  # 5 = MAT_SANDBAG

	# Corner + near-mid buildings for denser lane breakup
	var bld_offsets: Array[Array] = [
		[-40, -40], [-40, 24], [24, -40], [24, 24],
		[-12, -48], [-12, 32], [-52, -8], [36, -8],
	]
	for b: Array in bld_offsets:
		var bx: int = cx + int(b[0])
		var bz: int = cz + int(b[1])
		if bx >= 4 and bx + 16 < sx - 4 and bz >= 4 and bz + 16 < sz - 4:
			var by: int = map_config.base_height
			_world.generate_building(bx, by, bz, 16, 12, 16, 3, 4, true, true)  # 3=BRICK, 4=CONCRETE

	# Scattered low walls for additional cover and flank pockets
	var scatter_walls: Array[Array] = [
		[-60, 0, 8, true], [52, 0, 8, true],
		[0, -60, 8, false], [0, 44, 8, false],
		[-30, 20, 6, true], [24, -24, 6, true],
		[-20, -36, 6, false], [16, 32, 6, false],
		[-48, 30, 7, false], [42, -34, 7, false],
		[-6, -46, 9, true], [-6, 36, 9, true],
		[-66, -20, 8, false], [58, 18, 8, false],
		[-58, 40, 8, true], [52, -44, 8, true],
	]
	for sw: Array in scatter_walls:
		var wx: int = cx + int(sw[0])
		var wz: int = cz + int(sw[1])
		var wlen: int = int(sw[2])
		var along_x: bool = bool(sw[3])
		if wx >= 4 and wx + wlen < sx - 4 and wz >= 4 and wz + wlen < sz - 4:
			_world.generate_wall(wx, map_config.base_height, wz, wlen, 3, 2, 5, along_x)

	print("[VoxelWorldRenderer] Arena generated: %d buildings, %d walls" % [
		bld_offsets.size(), wall_offsets.size() + scatter_walls.size()])


func _surface_y(vx: int, vz: int, fallback_y: int) -> int:
	## Query current top solid voxel and return one voxel above it.
	var max_y: int = _world.get_world_size_y() - 1
	for vy: int in range(max_y, 0, -1):
		if _world.get_voxel(vx, vy, vz) != 0:  # 0 = MAT_AIR
			return vy + 1
	return fallback_y


func _generate_ruined_city(map_config) -> void:
	## Dense urban district with multi-lane streets, blocks, and choke points.
	var sx: int = _world.get_world_size_x()
	var sz: int = _world.get_world_size_z()
	var cx: int = sx / 2
	var cz: int = sz / 2

	_world.generate_terrain(map_config.base_height, maxi(map_config.hill_amplitude, 6), maxf(map_config.hill_frequency, 0.012))

	# Street grid: narrower lanes to avoid long open firing corridors.
	for z_off: int in [-96, -32, 32, 96]:
		var z0: int = clampi(cz + z_off, 8, sz - 9)
		_world.generate_wall(8, map_config.base_height, z0, sx - 16, 1, 5, 5, true)  # concrete street slab
	for x_off: int in [-112, -48, 16, 80]:
		var x0: int = clampi(cx + x_off, 8, sx - 9)
		_world.generate_wall(x0, map_config.base_height, 8, sz - 16, 1, 5, 5, false)
	# Additional alley channels near city edges to reward flank movement.
	for z_off: int in [-140, 140]:
		var zf: int = clampi(cz + z_off, 8, sz - 9)
		_world.generate_wall(8, map_config.base_height, zf, sx - 16, 1, 3, 5, true)

	# Building blocks with varied sizes/heights.
	var block_centers: Array[Vector2i] = []
	for ix: int in range(-2, 3):
		for iz: int in range(-2, 3):
			if abs(ix) == 0 and abs(iz) == 0:
				continue  # keep city center more open for objective fights
			block_centers.append(Vector2i(cx + ix * 64, cz + iz * 56))

	for i: int in range(block_centers.size()):
		var bc: Vector2i = block_centers[i]
		var bw: int = 16 + int(i % 3) * 6
		var bd: int = 16 + int((i + 1) % 3) * 6
		var bh: int = 10 + int(i % 4) * 3
		var bx: int = clampi(bc.x - bw / 2, 6, sx - bw - 6)
		var bz: int = clampi(bc.y - bd / 2, 6, sz - bd - 6)
		var by: int = _surface_y(bx + bw / 2, bz + bd / 2, map_config.base_height)
		_world.generate_building(bx, by, bz, bw, bh, bd, 3, 5, true, true)  # brick/concrete

	# Ruins/rubble lanes (broken barricade clusters) - denser than before.
	for i: int in range(34):
		var rx: int = clampi(cx - 140 + (i * 19) % 280, 6, sx - 18)
		var rz: int = clampi(cz - 120 + (i * 31) % 240, 6, sz - 18)
		var ry: int = _surface_y(rx, rz, map_config.base_height)
		var along_x: bool = (i % 2 == 0)
		_world.generate_wall(rx, ry, rz, 6 + (i % 5), 3 + (i % 2), 2, 5, along_x)

	# Central plaza landmark (reduced open area + denser cover ring).
	var p0x: int = clampi(cx - 20, 4, sx - 40)
	var p0z: int = clampi(cz - 20, 4, sz - 40)
	var py: int = _surface_y(cx, cz, map_config.base_height)
	_world.generate_wall(p0x, py, p0z, 40, 1, 40, 5, true)
	for off: int in [-16, 8]:
		_world.generate_wall(clampi(cx + off, 4, sx - 20), py + 1, clampi(cz - 2, 4, sz - 20), 8, 3, 2, 12, false)
		_world.generate_wall(clampi(cx - 2, 4, sx - 20), py + 1, clampi(cz + off, 4, sz - 20), 8, 3, 2, 12, true)
	# Additional center chicanes to block direct boulevard shots.
	for i: int in range(6):
		var wx: int = clampi(cx - 60 + i * 22, 8, sx - 20)
		var wz: int = clampi(cz + (-10 if i % 2 == 0 else 10), 8, sz - 20)
		_world.generate_wall(wx, py + 1, wz, 9, 3, 2, 12, i % 2 == 0)
	# Hard center denial spine to push squads toward side routes.
	for i: int in range(-4, 5):
		var sx0: int = clampi(cx + i * 18, 8, sx - 20)
		var sz0: int = clampi(cz + (12 if i % 2 == 0 else -12), 8, sz - 20)
		_world.generate_wall(sx0, py + 1, sz0, 10, 4, 2, 4, i % 2 == 0)

	# Stealth flank pockets: chained cover near north/south boundaries.
	for side: int in [-1, 1]:
		for i: int in range(12):
			var fx: int = clampi(26 + i * ((sx - 52) / 11), 8, sx - 20)
			var fz: int = clampi(cz + side * 170 + (-10 if (i % 2 == 0) else 10), 8, sz - 20)
			var fy: int = _surface_y(fx, fz, map_config.base_height)
			_world.generate_wall(fx, fy, fz, 8, 3, 2, 12, i % 2 == 0)

	print("[VoxelWorldRenderer] Ruined city generated: blocks=%d" % block_centers.size())


func _generate_mountain_pass(map_config) -> void:
	## High-relief terrain with denser ridge cover and multiple side cuts.
	var sx: int = _world.get_world_size_x()
	var sz: int = _world.get_world_size_z()
	var cx: int = sx / 2
	var cz: int = sz / 2

	# Dramatic elevation baseline.
	_world.generate_terrain(map_config.base_height, maxi(map_config.hill_amplitude, 20), maxf(map_config.hill_frequency, 0.009))

	# Carve the main pass corridor (east-west), with slight stagger.
	for seg: int in range(-6, 7):
		var px: int = cx + seg * 28
		var pz: int = cz + int(sin(float(seg) * 0.75) * 20.0)
		var tx: int = clampi(px - 10, 4, sx - 24)
		var tz: int = clampi(pz - 4, 4, sz - 24)
		_world.generate_trench(tx, tz, 20, 7, 14, true)

	# Side gullies and flanking cuts (increase count for more approach options).
	for side: int in [-1, 1]:
		for seg: int in range(0, 9):
			var gx: int = cx + side * (80 + seg * 24)
			var gz: int = cz + side * (30 + seg * 18)
			_world.generate_trench(clampi(gx, 6, sx - 24), clampi(gz, 6, sz - 24), 16, 5, 8, false)

	# Ridge bunkers/lookouts above the pass.
	for side: int in [-1, 1]:
		for i: int in range(4):
			var bx: int = clampi(cx - 120 + i * 80, 8, sx - 24)
			var bz: int = clampi(cz + side * 90, 8, sz - 24)
			var by: int = _surface_y(bx + 6, bz + 6, map_config.base_height)
			_world.generate_building(bx, by, bz, 14, 12, 14, 2, 5, true, true)  # stone/concrete
			_world.generate_wall(bx - 4, by + 1, bz + 4, 20, 4, 2, 12, true)

	# Chokepoint barricades (denser stagger to reduce pure long-range duels).
	for i: int in range(14):
		var wx: int = clampi(cx - 90 + i * 30, 8, sx - 20)
		var wz: int = clampi(cz + int(sin(float(i)) * 12.0) + (-8 if i % 2 == 0 else 8), 8, sz - 20)
		var wy: int = _surface_y(wx, wz, map_config.base_height)
		_world.generate_wall(wx, wy, wz, 10, 4, 2, 12, i % 2 == 0)
	# Extra lateral rock/sandbag pockets for off-axis movement.
	for side: int in [-1, 1]:
		for i: int in range(8):
			var wx: int = clampi(cx - 140 + i * 40, 8, sx - 20)
			var wz: int = clampi(cz + side * (48 + (i % 3) * 14), 8, sz - 20)
			var wy: int = _surface_y(wx, wz, map_config.base_height)
			_world.generate_wall(wx, wy, wz, 8, 3, 2, 2 if i % 2 == 0 else 12, i % 3 == 0)
		# Stealth shelf routes: parallel trench+cover track on both ridge sides.
		for i: int in range(7):
			var tx: int = clampi(cx - 150 + i * 48, 8, sx - 24)
			var tz: int = clampi(cz + side * 118 + (-8 if (i % 2 == 0) else 8), 8, sz - 24)
			_world.generate_trench(tx, tz, 20, 4, 7, true)
			var ty: int = _surface_y(tx, tz, map_config.base_height)
			_world.generate_wall(tx, ty, tz, 7, 3, 2, 12, i % 2 == 0)

	print("[VoxelWorldRenderer] Mountain pass generated")


func _generate_river_valley(map_config) -> void:
	## Valley map with a winding river and several bridge crossing fights.
	var sx: int = _world.get_world_size_x()
	var sz: int = _world.get_world_size_z()
	var cx: int = sx / 2
	var cz: int = sz / 2

	_world.generate_terrain(map_config.base_height, maxi(map_config.hill_amplitude, 14), maxf(map_config.hill_frequency, 0.010))

	# Carve winding river channel (north-south progression in x).
	var river_half_width: int = 10
	for x: int in range(8, sx - 8, 6):
		var t: float = float(x) / float(maxi(1, sx - 1))
		var rz: int = cz + int(sin(t * TAU * 1.35) * 70.0) + int(sin(t * TAU * 3.2) * 22.0)
		var tx: int = clampi(x - 3, 4, sx - 16)
		var tz: int = clampi(rz - river_half_width, 4, sz - 32)
		_world.generate_trench(tx, tz, 8, 10, river_half_width * 2 + 2, false)

	# Stamp shallow water surface with MAT_WATER where possible.
	for x: int in range(8, sx - 8, 4):
		var t: float = float(x) / float(maxi(1, sx - 1))
		var rz: int = cz + int(sin(t * TAU * 1.35) * 70.0) + int(sin(t * TAU * 3.2) * 22.0)
		for z: int in range(maxi(2, rz - river_half_width), mini(sz - 3, rz + river_half_width)):
			var sy: int = _surface_y(x, z, map_config.base_height) - 1
			if sy >= 1:
				_world.set_voxel(x, sy, z, 9)  # MAT_WATER

	# Build bridges at key crossing points.
	var bridge_xs: Array[int] = [sx / 5, (sx * 2) / 5, (sx * 3) / 5, (sx * 4) / 5]
	for bx: int in bridge_xs:
		var t: float = float(bx) / float(maxi(1, sx - 1))
		var rz: int = cz + int(sin(t * TAU * 1.35) * 70.0) + int(sin(t * TAU * 3.2) * 22.0)
		var by: int = _surface_y(bx, rz, map_config.base_height)
		# Deck
		_world.generate_wall(clampi(bx - 6, 4, sx - 20), by + 1, clampi(rz - 16, 4, sz - 20), 14, 1, 32, 5, false)
		# Side rails / parapets
		_world.generate_wall(clampi(bx - 6, 4, sx - 20), by + 2, clampi(rz - 16, 4, sz - 20), 14, 2, 2, 4, false)
		_world.generate_wall(clampi(bx + 6, 4, sx - 20), by + 2, clampi(rz - 16, 4, sz - 20), 14, 2, 2, 4, false)
		# Bridgehead obstacles so crossings are tactical, not open lanes.
		_world.generate_wall(clampi(bx - 18, 4, sx - 20), by + 1, clampi(rz - 8, 4, sz - 20), 8, 3, 2, 12, true)
		_world.generate_wall(clampi(bx + 10, 4, sx - 20), by + 1, clampi(rz + 4, 4, sz - 20), 8, 3, 2, 12, true)
		# Additional LOS blocker directly on approach lanes.
		_world.generate_wall(clampi(bx - 4, 4, sx - 20), by + 1, clampi(rz - 22, 4, sz - 20), 8, 4, 2, 4, false)
		_world.generate_wall(clampi(bx - 4, 4, sx - 20), by + 1, clampi(rz + 14, 4, sz - 20), 8, 4, 2, 4, false)

	# Valley settlements and embankment cover.
	for side: int in [-1, 1]:
		for i: int in range(5):
			var hx: int = clampi(60 + i * ((sx - 120) / 4), 8, sx - 28)
			var hz: int = clampi(cz + side * 120 + int(sin(float(i)) * 20.0), 8, sz - 28)
			var hy: int = _surface_y(hx + 8, hz + 8, map_config.base_height)
			_world.generate_building(hx, hy, hz, 18, 14, 18, 3, 4, true, true)
			_world.generate_wall(hx - 6, hy, hz + 4, 28, 3, 2, 12, true)
		# Dense embankment micro-cover along each side of the river.
		for i: int in range(12):
			var ex: int = clampi(40 + i * ((sx - 80) / 11), 8, sx - 20)
			var t: float = float(ex) / float(maxi(1, sx - 1))
			var rz2: int = cz + int(sin(t * TAU * 1.35) * 70.0) + int(sin(t * TAU * 3.2) * 22.0)
			var ez: int = clampi(rz2 + side * (river_half_width + 18 + (i % 3) * 4), 8, sz - 20)
			var ey: int = _surface_y(ex, ez, map_config.base_height)
			_world.generate_wall(ex, ey, ez, 7 + (i % 3), 3, 2, 5 if i % 2 == 0 else 12, i % 2 == 0)
		# Far-bank stealth lanes with chained low cover.
		for i: int in range(10):
			var lx: int = clampi(30 + i * ((sx - 60) / 9), 8, sx - 24)
			var t2: float = float(lx) / float(maxi(1, sx - 1))
			var rz3: int = cz + int(sin(t2 * TAU * 1.35) * 70.0) + int(sin(t2 * TAU * 3.2) * 22.0)
			var lz: int = clampi(rz3 + side * (river_half_width + 34 + (-6 if (i % 2 == 0) else 6)), 8, sz - 24)
			_world.generate_trench(lx, lz, 16, 3, 6, true)
			var ly: int = _surface_y(lx, lz, map_config.base_height)
			_world.generate_wall(lx, ly, lz, 7, 3, 2, 12, i % 2 == 0)

	print("[VoxelWorldRenderer] River valley generated: bridges=%d" % bridge_xs.size())


func set_startup_focus(world_pos: Vector3) -> void:
	_startup_focus = world_pos
	_startup_focus_enabled = true
	_startup_mesh_elapsed = 0.0


func _process(delta: float) -> void:
	if not _world or not _world.is_initialized():
		return

	# Phase 1: Queue dirty chunks to worker threads
	_queue_dirty_chunks()

	# Phase 2: Upload completed mesh results to RenderingServer
	_upload_mesh_results()

	# Phase 3: Check if initial mesh is complete
	if not _initial_mesh_done and _initial_queued:
		# Print progress periodically during initial load
		_progress_timer += delta
		if _progress_timer >= 0.5:
			_progress_timer = 0.0
			var remaining: int = _mesh_worker.get_pending_count() + _mesh_worker.get_completed_count()
			print("[VoxelWorldRenderer] Initial mesh progress: %d/%d chunks uploaded" % [
				_stats_chunks_meshed, _stats_initial_total])

		if _mesh_worker.is_idle() and _mesh_worker.get_completed_count() == 0:
			if not _initial_mesh_done and not _lod_generation_queued:
				_initial_mesh_done = true
				_startup_focus_enabled = false
				_startup_mesh_elapsed = 0.0
				print("[VoxelWorldRenderer] Initial mesh complete: %d quads across %d chunks" % [
					_stats_quads, _stats_chunks_meshed])
				if generate_collisions:
					_pending_collision.assign(_chunk_collision_data.keys())
					print("[VoxelWorldRenderer] Queued %d chunks for collision generation" % _pending_collision.size())
				# Queue LOD 1 and LOD 2 generation
				_queue_lod_generation()

	# Phase 4: Deferred collision generation
	if _initial_mesh_done and generate_collisions and not _collision_done:
		_process_deferred_collision()

	# Phase 5: Distance-based visibility + LOD switching
	if _initial_mesh_done:
		_startup_mesh_elapsed += delta
		_distance_timer += delta
		if _distance_timer >= DISTANCE_CHECK_INTERVAL:
			_distance_timer = 0.0
			_update_visibility_and_lod()

	# Phase 5B: LOD 3 terrain mesh (one-time build, rebuilt on destruction with debounce)
	if _initial_mesh_done and not _lod3_built:
		if _lod3_rebuild_timer < 0.0:
			# First build — no debounce needed
			_build_lod3_terrain()
		else:
			# Destruction-triggered rebuild — wait for debounce
			_lod3_rebuild_timer += delta
			if _lod3_rebuild_timer >= LOD3_REBUILD_DEBOUNCE:
				_build_lod3_terrain()
				_lod3_rebuild_timer = -1.0

	# Phase 6: Process CA rubble physics + island sync
	if _initial_mesh_done and _world:
		_world.process_rubble_ca(500)
		_update_islands(delta)


# ── Queue Dirty Chunks ─────────────────────────────────────────────────

func _queue_dirty_chunks() -> void:
	var dirty: PackedInt32Array = _world.get_dirty_chunk_coords()
	if dirty.size() == 0:
		if not _initial_queued:
			_initial_queued = true
		return

	# Get camera position for priority sorting
	var camera_pos = Vector3.ZERO
	if _startup_focus_enabled and not _initial_mesh_done:
		camera_pos = _startup_focus
	else:
		var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
		if cam:
			camera_pos = cam.global_position

	# Filter out already-queued chunks, clear dirty flags
	var to_queue = PackedInt32Array()
	var i: int = 0
	while i < dirty.size():
		var cx: int = dirty[i]
		var cy: int = dirty[i + 1]
		var cz: int = dirty[i + 2]
		var key = Vector3i(cx, cy, cz)
		i += 3

		_world.clear_chunk_dirty(cx, cy, cz)

		if not _queued_set.has(key):
			to_queue.append(cx)
			to_queue.append(cy)
			to_queue.append(cz)
			_queued_set[key] = true

	var count: int = to_queue.size() / 3
	if count > 0:
		_mesh_worker.queue_mesh_batch(to_queue, camera_pos, true)
		if not _initial_queued:
			_initial_queued = true
			_stats_initial_total = count
			print("[VoxelWorldRenderer] Queued %d chunks for threaded meshing (%d threads)" % [
				count, num_mesh_threads])
		elif _initial_mesh_done:
			# Destruction happened — start debounce timer for LOD 3 rebuild
			# Only start if not already counting (rubble CA continuously dirties chunks)
			if _lod3_rebuild_timer < 0.0:
				_lod3_built = false
				_lod3_rebuild_timer = 0.0


# ── Upload Completed Meshes ────────────────────────────────────────────

func _upload_mesh_results() -> void:
	var upload_budget: int = max_uploads_per_frame
	if _initial_mesh_done and _startup_mesh_elapsed < STARTUP_THROTTLE_WINDOW_SEC:
		upload_budget = maxi(8, int(round(float(max_uploads_per_frame) * 0.5)))
	var results: Array = _mesh_worker.poll_results(upload_budget)
	if results.size() == 0:
		return

	var start_us: int = Time.get_ticks_usec()

	for r: Dictionary in results:
		var cx: int = r["cx"]
		var cy: int = r["cy"]
		var cz: int = r["cz"]
		var lod: int = r.get("lod", 0)
		var key = Vector3i(cx, cy, cz)

		# Only clear dedup set for LOD 0 (dirty chunk tracking)
		if lod == 0:
			_queued_set.erase(key)

		if r["empty"]:
			if lod == 0:
				_remove_chunk(key)
				_chunk_collision_data.erase(key)
			continue

		var arrays: Array = r["arrays"]
		if arrays.size() == 0 or arrays[Mesh.ARRAY_VERTEX] == null:
			if lod == 0:
				_remove_chunk(key)
				_chunk_collision_data.erase(key)
			continue

		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		if verts.size() == 0:
			if lod == 0:
				_remove_chunk(key)
				_chunk_collision_data.erase(key)
			continue

		if lod == 0:
			# LOD 0: primary mesh — create instance + set as active
			_upload_chunk_mesh(key, cx, cy, cz, arrays)
			_stats_quads += verts.size() / 4
			_stats_chunks_meshed += 1

			# Collision handling (only for LOD 0)
			if generate_collisions:
				var xform: Transform3D = _chunk_transform(cx, cy, cz)
				if _initial_mesh_done:
					_update_chunk_collision(key, arrays, xform)
				else:
					_chunk_collision_data[key] = {"arrays": arrays, "xform": xform}
		else:
			# LOD 1 or 2: store mesh RID for later LOD switching
			_upload_lod_mesh(key, arrays, lod)

	if results.size() > 0 and _initial_mesh_done:
		var elapsed_ms: float = float(Time.get_ticks_usec() - start_us) / 1000.0
		if _mesh_worker.get_pending_count() > 0:
			print("[VoxelWorldRenderer] Uploaded %d results in %.1f ms (pending: %d)" % [
				results.size(), elapsed_ms, _mesh_worker.get_pending_count()])


# ── Mesh Upload ────────────────────────────────────────────────────────

func _upload_chunk_mesh(key: Vector3i, cx: int, cy: int, cz: int, arrays: Array) -> void:
	var xform: Transform3D = _chunk_transform(cx, cy, cz)

	# Create or update mesh RID
	var mesh_rid: RID
	if _chunk_meshes.has(key):
		mesh_rid = _chunk_meshes[key]
		RenderingServer.mesh_clear(mesh_rid)
	else:
		mesh_rid = RenderingServer.mesh_create()
		_chunk_meshes[key] = mesh_rid

	RenderingServer.mesh_add_surface_from_arrays(
		mesh_rid, RenderingServer.PRIMITIVE_TRIANGLES, arrays
	)
	RenderingServer.mesh_surface_set_material(mesh_rid, 0, _material)

	# Create or update instance RID
	var instance_rid: RID
	if _chunk_instances.has(key):
		instance_rid = _chunk_instances[key]
	else:
		instance_rid = RenderingServer.instance_create()
		RenderingServer.instance_set_scenario(instance_rid, _scenario)
		_chunk_instances[key] = instance_rid

	RenderingServer.instance_set_base(instance_rid, mesh_rid)
	RenderingServer.instance_set_transform(instance_rid, xform)

	# Set custom AABB for frustum culling (mesh verts are 0..32 in chunk-local space)
	RenderingServer.instance_set_custom_aabb(instance_rid,
		AABB(Vector3.ZERO, Vector3(32, 32, 32)))

	_aabbs_dirty = true


# ── Collision ──────────────────────────────────────────────────────────

func _process_deferred_collision() -> void:
	if _pending_collision.is_empty():
		if not _collision_done:
			_collision_done = true
			_chunk_collision_data.clear()
			print("[VoxelWorldRenderer] Collision generation complete: %d bodies" % _chunk_bodies.size())
		return

	var start_ms: float = Time.get_ticks_usec() / 1000.0

	while not _pending_collision.is_empty():
		var key: Vector3i = _pending_collision.pop_back()
		if _chunk_collision_data.has(key):
			var data: Dictionary = _chunk_collision_data[key]
			_update_chunk_collision(key, data["arrays"], data["xform"])
			_chunk_collision_data.erase(key)

		var elapsed: float = Time.get_ticks_usec() / 1000.0 - start_ms
		if elapsed > collision_budget_ms:
			break


func _update_chunk_collision(key: Vector3i, arrays: Array, xform: Transform3D) -> void:
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]

	if verts.size() == 0 or indices.size() == 0:
		_remove_chunk_collision(key)
		return

	# Build triangle face array from vertices + indices
	var faces = PackedVector3Array()
	faces.resize(indices.size())
	for i in range(indices.size()):
		faces[i] = verts[indices[i]]

	var shape_rid: RID
	if _chunk_shapes.has(key):
		shape_rid = _chunk_shapes[key]
	else:
		shape_rid = PhysicsServer3D.concave_polygon_shape_create()
		_chunk_shapes[key] = shape_rid

	PhysicsServer3D.shape_set_data(shape_rid, {"faces": faces, "backface_collision": false})

	var body_rid: RID
	if _chunk_bodies.has(key):
		body_rid = _chunk_bodies[key]
		PhysicsServer3D.body_clear_shapes(body_rid)
	else:
		body_rid = PhysicsServer3D.body_create()
		PhysicsServer3D.body_set_mode(body_rid, PhysicsServer3D.BODY_MODE_STATIC)
		PhysicsServer3D.body_set_space(body_rid, _physics_space)
		PhysicsServer3D.body_set_collision_layer(body_rid, 1)
		PhysicsServer3D.body_set_collision_mask(body_rid, 0)
		_chunk_bodies[key] = body_rid

	PhysicsServer3D.body_add_shape(body_rid, shape_rid)
	PhysicsServer3D.body_set_state(body_rid, PhysicsServer3D.BODY_STATE_TRANSFORM, xform)


# ── LOD Mesh Upload ────────────────────────────────────────────────────

func _upload_lod_mesh(key: Vector3i, arrays: Array, lod: int) -> void:
	var target_dict: Dictionary = _chunk_lod1_meshes if lod == 1 else _chunk_lod2_meshes

	var mesh_rid: RID
	if target_dict.has(key):
		mesh_rid = target_dict[key]
		RenderingServer.mesh_clear(mesh_rid)
	else:
		mesh_rid = RenderingServer.mesh_create()
		target_dict[key] = mesh_rid

	RenderingServer.mesh_add_surface_from_arrays(
		mesh_rid, RenderingServer.PRIMITIVE_TRIANGLES, arrays
	)
	RenderingServer.mesh_surface_set_material(mesh_rid, 0, _material)


# ── LOD Generation ─────────────────────────────────────────────────────

func _queue_lod_generation() -> void:
	_lod_generation_queued = true

	# Build coordinate list of all chunks that have LOD 0 meshes
	var coords = PackedInt32Array()
	for key: Vector3i in _chunk_meshes:
		coords.append(key.x)
		coords.append(key.y)
		coords.append(key.z)

	var count: int = coords.size() / 3
	if count == 0:
		return

	var camera_pos = Vector3.ZERO
	var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
	if cam:
		camera_pos = cam.global_position

	# Queue LOD 1 (prioritize distant chunks first — they benefit most)
	_mesh_worker.queue_mesh_batch(coords, camera_pos, true, 1)
	# Queue LOD 2
	_mesh_worker.queue_mesh_batch(coords, camera_pos, true, 2)

	print("[VoxelWorldRenderer] Queued %d chunks for LOD 1+2 generation" % count)


# ── Distance-Based Visibility + LOD Switching ─────────────────────────

func _update_visibility_and_lod() -> void:
	var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
	if not cam:
		return

	var cam_pos: Vector3 = cam.global_position
	if cam_pos.distance_squared_to(_last_camera_pos) < CAMERA_MOVE_THRESHOLD * CAMERA_MOVE_THRESHOLD:
		return
	_last_camera_pos = cam_pos

	if _gpu_culler and _gpu_culler.is_gpu_available():
		_gpu_cull(cam)
	else:
		_cpu_cull(cam_pos)


func _upload_chunk_aabbs() -> void:
	_aabbs_dirty = false
	_chunk_keys.clear()

	var aabbs = PackedFloat32Array()
	var cs_world: float = 32.0 * _scale

	for key: Vector3i in _chunk_instances:
		_chunk_keys.append(key)
		var min_x: float = float(key.x * 32) * _scale - _half_world_x
		var min_y: float = float(key.y * 32) * _scale
		var min_z: float = float(key.z * 32) * _scale - _half_world_z
		aabbs.append(min_x)
		aabbs.append(min_y)
		aabbs.append(min_z)
		aabbs.append(min_x + cs_world)
		aabbs.append(min_y + cs_world)
		aabbs.append(min_z + cs_world)

	var chunk_count: int = _chunk_keys.size()
	if chunk_count > _gpu_cull_capacity:
		var dropped: int = chunk_count - _gpu_cull_capacity
		_gpu_cull_invalid_entries_dropped += dropped

	_gpu_culler.set_chunk_aabbs(aabbs)


func _estimate_gpu_cull_capacity() -> int:
	return GPU_CULL_CAPACITY


func _gpu_cull(cam: Camera3D) -> void:
	if _aabbs_dirty:
		_upload_chunk_aabbs()

	# Build combined view-projection matrix
	var proj: Projection = cam.get_camera_projection()
	var view: Projection = Projection(cam.get_camera_transform().affine_inverse())
	var vp: Projection = proj * view
	var cam_pos: Vector3 = cam.global_position

	var cull_result: Dictionary = _gpu_culler.cull(vp, cam_pos,
		visibility_radius, lod1_distance, lod2_distance)

	var visible: PackedByteArray = cull_result.get("visible", PackedByteArray())
	var lod_levels: PackedByteArray = cull_result.get("lod_levels", PackedByteArray())
	var expected_count: int = _chunk_keys.size()
	if expected_count > _gpu_cull_capacity:
		_gpu_cull_over_capacity_frames += 1
	if visible.size() == 0 or lod_levels.size() == 0:
		_cpu_cull(cam_pos)
		return
	var safe_count: int = mini(expected_count, mini(visible.size(), lod_levels.size()))
	if safe_count <= 0:
		_cpu_cull(cam_pos)
		return
	if safe_count != expected_count:
		var dropped: int = expected_count - safe_count
		_gpu_cull_invalid_entries_dropped += maxi(dropped, 0)
		if not _gpu_cull_mismatch_warned:
			push_warning("[VoxelWorldRenderer] GPU cull result size mismatch (expected=%d, visible=%d, lod=%d, safe=%d). Clamping this frame." % [
				expected_count, visible.size(), lod_levels.size(), safe_count
			])
			_gpu_cull_mismatch_warned = true
	elif _gpu_cull_mismatch_warned:
		_gpu_cull_mismatch_warned = false

	var col_sq: float = collision_radius * collision_radius

	for i: int in range(safe_count):
		var key: Vector3i = _chunk_keys[i]
		if not _chunk_instances.has(key):
			continue

		var is_visible: bool = visible[i] == 1
		RenderingServer.instance_set_visible(_chunk_instances[key], is_visible)

		# Collision tier (CPU — cheap, different radius from visibility)
		if generate_collisions and _collision_done and _chunk_bodies.has(key):
			var center: Vector3 = _chunk_center(key.x, key.y, key.z)
			var dist_sq: float = cam_pos.distance_squared_to(center)
			var body_rid: RID = _chunk_bodies[key]
			if dist_sq <= col_sq:
				PhysicsServer3D.body_set_space(body_rid, _physics_space)
			else:
				PhysicsServer3D.body_set_space(body_rid, RID())

		if not is_visible:
			continue

		# LOD switching from GPU result — validate mesh availability
		var desired_lod: int = lod_levels[i]
		if desired_lod == 2 and not _chunk_lod2_meshes.has(key):
			desired_lod = 1 if _chunk_lod1_meshes.has(key) else 0
		elif desired_lod == 1 and not _chunk_lod1_meshes.has(key):
			desired_lod = 0

		var current_lod: int = _chunk_current_lod.get(key, 0)
		if desired_lod != current_lod:
			var mesh_rid: RID
			match desired_lod:
				0: mesh_rid = _chunk_meshes.get(key, RID())
				1: mesh_rid = _chunk_lod1_meshes.get(key, RID())
				2: mesh_rid = _chunk_lod2_meshes.get(key, RID())

			if mesh_rid.is_valid():
				RenderingServer.instance_set_base(_chunk_instances[key], mesh_rid)
				_chunk_current_lod[key] = desired_lod

	if safe_count < expected_count:
		for i: int in range(safe_count, expected_count):
			var key: Vector3i = _chunk_keys[i]
			if _chunk_instances.has(key):
				RenderingServer.instance_set_visible(_chunk_instances[key], false)
			if generate_collisions and _collision_done and _chunk_bodies.has(key):
				PhysicsServer3D.body_set_space(_chunk_bodies[key], RID())


func get_gpu_cull_debug_stats() -> Dictionary:
	return {
		"capacity": _gpu_cull_capacity,
		"tracked_chunks": _chunk_keys.size(),
		"invalid_entries_dropped": _gpu_cull_invalid_entries_dropped,
		"over_capacity_frames": _gpu_cull_over_capacity_frames
	}


func _cpu_cull(cam_pos: Vector3) -> void:
	## CPU fallback when GPU culling is unavailable.
	var vis_sq: float = visibility_radius * visibility_radius
	var lod1_sq: float = lod1_distance * lod1_distance
	var lod2_sq: float = lod2_distance * lod2_distance
	var col_sq: float = collision_radius * collision_radius

	for key: Vector3i in _chunk_instances:
		var center: Vector3 = _chunk_center(key.x, key.y, key.z)
		var dist_sq: float = cam_pos.distance_squared_to(center)

		var visible: bool = dist_sq <= vis_sq
		RenderingServer.instance_set_visible(_chunk_instances[key], visible)

		if generate_collisions and _collision_done:
			var wants_collision: bool = dist_sq <= col_sq
			if _chunk_bodies.has(key):
				var body_rid: RID = _chunk_bodies[key]
				if wants_collision:
					PhysicsServer3D.body_set_space(body_rid, _physics_space)
				else:
					PhysicsServer3D.body_set_space(body_rid, RID())

		if not visible:
			continue

		var desired_lod: int = 0
		if dist_sq > lod2_sq and _chunk_lod2_meshes.has(key):
			desired_lod = 2
		elif dist_sq > lod1_sq and _chunk_lod1_meshes.has(key):
			desired_lod = 1

		var current_lod: int = _chunk_current_lod.get(key, 0)
		if desired_lod != current_lod:
			var mesh_rid: RID
			match desired_lod:
				0: mesh_rid = _chunk_meshes.get(key, RID())
				1: mesh_rid = _chunk_lod1_meshes.get(key, RID())
				2: mesh_rid = _chunk_lod2_meshes.get(key, RID())

			if mesh_rid.is_valid():
				RenderingServer.instance_set_base(_chunk_instances[key], mesh_rid)
				_chunk_current_lod[key] = desired_lod


# ── LOD 3 Terrain Mesh ─────────────────────────────────────────────────

func _build_lod3_terrain() -> void:
	## Generate a single low-poly heightmap mesh covering the entire world.
	## Uses get_column_top_y() for fast C++ column scans and vertex colors
	## from the top surface material. No UV2 — avoids MP-4 black spot bug.
	if not _world or not _world.is_initialized():
		return

	var start_us: int = Time.get_ticks_usec()

	var sx: int = _world.get_world_size_x()
	var sz: int = _world.get_world_size_z()
	var step_voxels: int = maxi(1, int(lod3_quad_size / _scale))

	# Grid dimensions (number of vertices per axis)
	var cols: int = sx / step_voxels + 1
	var rows: int = sz / step_voxels + 1

	# Pre-allocate arrays
	var vert_count: int = cols * rows
	var quad_count: int = (cols - 1) * (rows - 1)
	var vertices := PackedVector3Array()
	var colors := PackedColorArray()
	var normals := PackedVector3Array()
	var indices := PackedInt32Array()
	vertices.resize(vert_count)
	colors.resize(vert_count)
	normals.resize(vert_count)
	indices.resize(quad_count * 6)

	# Build vertex grid: height + color per vertex
	var vi: int = 0
	var heights := PackedFloat32Array()  # raw heights for gradient clamping
	heights.resize(vert_count)
	for rz: int in rows:
		var vz: int = mini(rz * step_voxels, sz - 1)
		var wz: float = float(vz) * _scale - _half_world_z
		for rx: int in cols:
			var vx: int = mini(rx * step_voxels, sx - 1)
			var wx: float = float(vx) * _scale - _half_world_x
			var top_y: int = _world.get_column_top_y(vx, vz)
			var wy: float = float(maxi(top_y, 0)) * _scale - _scale  # Offset down 1 voxel to avoid z-fighting
			heights[vi] = wy
			vertices[vi] = Vector3(wx, wy, wz)
			normals[vi] = Vector3.UP
			# Sample top surface material for color
			if top_y >= 0:
				var mat_id: int = _world.get_voxel(vx, top_y, vz)
				colors[vi] = _world.get_material_color(mat_id)
			else:
				colors[vi] = Color(0.3, 0.4, 0.2)  # fallback green-ish
			vi += 1

	# Second pass: clamp extreme height gradients (building sides).
	# If a vertex is much higher than ALL its neighbors, snap it down to the
	# lowest neighbor's height. This prevents the coarse LOD3 mesh from
	# creating large visible triangles sloping from rooftops to ground.
	var height_threshold: float = float(8) * _scale  # 8 voxels = ~2m at 0.25 scale
	for rz2: int in rows:
		for rx2: int in cols:
			var idx: int = rz2 * cols + rx2
			var h: float = heights[idx]
			var min_neighbor: float = h
			# Check 4-connected neighbors
			if rx2 > 0:
				min_neighbor = minf(min_neighbor, heights[idx - 1])
			if rx2 < cols - 1:
				min_neighbor = minf(min_neighbor, heights[idx + 1])
			if rz2 > 0:
				min_neighbor = minf(min_neighbor, heights[idx - cols])
			if rz2 < rows - 1:
				min_neighbor = minf(min_neighbor, heights[idx + cols])
			# If this vertex towers above ALL neighbors, it's a building top —
			# snap down to prevent sloped triangle artifacts
			if h - min_neighbor > height_threshold:
				vertices[idx].y = min_neighbor
				heights[idx] = min_neighbor

	# Build index buffer (two triangles per quad)
	var ii: int = 0
	for rz: int in (rows - 1):
		for rx: int in (cols - 1):
			var tl: int = rz * cols + rx
			var tr: int = tl + 1
			var bl: int = tl + cols
			var br: int = bl + 1
			indices[ii] = tl;     ii += 1
			indices[ii] = bl;     ii += 1
			indices[ii] = tr;     ii += 1
			indices[ii] = tr;     ii += 1
			indices[ii] = bl;     ii += 1
			indices[ii] = br;     ii += 1

	# Create or update the mesh RID
	if not _lod3_mesh_rid.is_valid():
		_lod3_mesh_rid = RenderingServer.mesh_create()
	else:
		RenderingServer.mesh_clear(_lod3_mesh_rid)

	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	RenderingServer.mesh_add_surface_from_arrays(
		_lod3_mesh_rid, RenderingServer.PRIMITIVE_TRIANGLES, arrays)
	RenderingServer.mesh_surface_set_material(_lod3_mesh_rid, 0, _material)

	# Create instance if needed
	if not _lod3_instance_rid.is_valid():
		_lod3_instance_rid = RenderingServer.instance_create()
		RenderingServer.instance_set_scenario(_lod3_instance_rid, _scenario)
	RenderingServer.instance_set_base(_lod3_instance_rid, _lod3_mesh_rid)
	RenderingServer.instance_set_visible(_lod3_instance_rid, true)

	_lod3_built = true
	var elapsed_ms: float = float(Time.get_ticks_usec() - start_us) / 1000.0
	print("[LOD3] Terrain mesh: %d verts, %d quads (%.1f ms)" % [
		vert_count, quad_count, elapsed_ms])


# ── Helpers ────────────────────────────────────────────────────────────

func _chunk_transform(cx: int, cy: int, cz: int) -> Transform3D:
	var xform = Transform3D()
	xform.origin = Vector3(
		float(cx * 32) * _scale - _half_world_x,
		float(cy * 32) * _scale,
		float(cz * 32) * _scale - _half_world_z
	)
	xform = xform.scaled_local(Vector3(_scale, _scale, _scale))
	return xform


func _chunk_center(cx: int, cy: int, cz: int) -> Vector3:
	return Vector3(
		float(cx * 32 + 16) * _scale - _half_world_x,
		float(cy * 32 + 16) * _scale,
		float(cz * 32 + 16) * _scale - _half_world_z
	)


# ── Island Physics Bridge (Tier 1) ────────────────────────────────────

func _init_islands() -> void:
	if _islands_initialized:
		return
	_islands_initialized = true
	_island_bodies.resize(MAX_ISLANDS)
	_island_instances.resize(MAX_ISLANDS)
	_island_meshes.resize(MAX_ISLANDS)
	_island_shapes.resize(MAX_ISLANDS)
	_island_active.resize(MAX_ISLANDS)
	_island_rest_timer.resize(MAX_ISLANDS)
	_island_initial_xforms.resize(MAX_ISLANDS)
	_island_voxel_positions.resize(MAX_ISLANDS)
	_island_voxel_materials.resize(MAX_ISLANDS)
	for i in MAX_ISLANDS:
		_island_active[i] = false
		_island_rest_timer[i] = 0.0
		_island_bodies[i] = RID()
		_island_instances[i] = RID()
		_island_meshes[i] = RID()
		_island_shapes[i] = RID()
		_island_initial_xforms[i] = Transform3D()
		_island_voxel_positions[i] = PackedVector3Array()
		_island_voxel_materials[i] = PackedByteArray()


func _get_free_island_slot() -> int:
	for i in MAX_ISLANDS:
		if not _island_active[i]:
			return i
	return -1


## Call from external code (e.g. voxel_test_camera) when structural integrity
## detects a floating island. `island_dict` comes from StructuralIntegrity.detect_islands().
func spawn_island(island_dict: Dictionary) -> void:
	if not _islands_initialized:
		_init_islands()
	var slot = _get_free_island_slot()
	if slot < 0:
		return

	var mesh_arrays: Array = island_dict.get("mesh_arrays", Array())
	if mesh_arrays.is_empty():
		return

	var center: Vector3 = island_dict.get("center", Vector3.ZERO)
	var mass: float = island_dict.get("mass", 100.0)
	var bounds_min: Vector3i = island_dict.get("bounds_min", Vector3i.ZERO)
	var bounds_max: Vector3i = island_dict.get("bounds_max", Vector3i.ZERO)

	# Mesh
	var mesh_rid = RenderingServer.mesh_create()
	RenderingServer.mesh_add_surface_from_arrays(
		mesh_rid, RenderingServer.PRIMITIVE_TRIANGLES, mesh_arrays)
	RenderingServer.mesh_surface_set_material(mesh_rid, 0, _material)
	_island_meshes[slot] = mesh_rid

	# Instance
	var inst = RenderingServer.instance_create()
	RenderingServer.instance_set_base(inst, mesh_rid)
	RenderingServer.instance_set_scenario(inst, _scenario)
	# Transform: position at the island's voxel origin in world space
	var origin = Vector3(
		float(bounds_min.x) * _scale - _half_world_x,
		float(bounds_min.y) * _scale,
		float(bounds_min.z) * _scale - _half_world_z
	)
	var xform = Transform3D(Basis().scaled(Vector3(_scale, _scale, _scale)), origin)
	RenderingServer.instance_set_transform(inst, xform)
	_island_instances[slot] = inst

	# Physics body (dynamic)
	var body = PhysicsServer3D.body_create()
	PhysicsServer3D.body_set_mode(body, PhysicsServer3D.BODY_MODE_RIGID)
	PhysicsServer3D.body_set_space(body, _physics_space)

	# Build convex hull from mesh vertices
	var verts: PackedVector3Array = mesh_arrays[Mesh.ARRAY_VERTEX]
	if verts.size() > 0:
		var shape = PhysicsServer3D.convex_polygon_shape_create()
		# Scale vertices to world space
		var scaled_verts = PackedVector3Array()
		scaled_verts.resize(verts.size())
		for i in verts.size():
			scaled_verts[i] = verts[i] * _scale
		PhysicsServer3D.shape_set_data(shape, scaled_verts)
		PhysicsServer3D.body_add_shape(body, shape)
		_island_shapes[slot] = shape

	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM, xform)
	PhysicsServer3D.body_set_param(body, PhysicsServer3D.BODY_PARAM_MASS, clampf(mass * 0.001, 1.0, 1000.0))
	PhysicsServer3D.body_set_collision_layer(body, 2)      # Island layer
	PhysicsServer3D.body_set_collision_mask(body, 1 | 2)   # Collide with world + other islands
	_island_bodies[slot] = body

	_island_active[slot] = true
	_island_rest_timer[slot] = 0.0
	_island_initial_xforms[slot] = xform
	_island_voxel_positions[slot] = island_dict.get("voxel_positions", PackedVector3Array())
	_island_voxel_materials[slot] = island_dict.get("voxel_materials", PackedByteArray())


func _update_islands(delta: float) -> void:
	if not _islands_initialized:
		return
	for i in MAX_ISLANDS:
		if not _island_active[i]:
			continue
		if not _island_bodies[i].is_valid():
			_island_active[i] = false
			continue

		# Sync render to physics
		var xform: Transform3D = PhysicsServer3D.body_get_state(
			_island_bodies[i], PhysicsServer3D.BODY_STATE_TRANSFORM)
		RenderingServer.instance_set_transform(_island_instances[i], xform)

		# Check for fall off world — no re-solidification
		var pos: Vector3 = xform.origin
		if pos.y < ISLAND_FALL_LIMIT:
			_reclaim_island(i, false)
			continue

		var vel: Vector3 = PhysicsServer3D.body_get_state(
			_island_bodies[i], PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY)
		if vel.length() < ISLAND_REST_THRESHOLD:
			_island_rest_timer[i] += delta
			if _island_rest_timer[i] >= ISLAND_REST_TIME:
				_reclaim_island(i, true)  # re-solidify into voxel grid
		else:
			_island_rest_timer[i] = 0.0


## Re-solidify island voxels at their final resting position in the voxel grid.
## Translation-only (ignore rotation) — creates natural "shattered rubble" look.
func _resolidify_island(slot: int) -> void:
	if not _world or not _world.is_initialized():
		return
	var positions: PackedVector3Array = _island_voxel_positions[slot]
	var materials: PackedByteArray = _island_voxel_materials[slot]
	if positions.is_empty():
		return

	# Compute translation delta between initial and final physics position
	var final_xform: Transform3D = PhysicsServer3D.body_get_state(
		_island_bodies[slot], PhysicsServer3D.BODY_STATE_TRANSFORM)
	var delta_origin: Vector3 = final_xform.origin - _island_initial_xforms[slot].origin

	# Convert world-space delta to voxel-space delta
	var inv_scale: float = 1.0 / _scale
	var vdx: int = roundi(delta_origin.x * inv_scale)
	var vdy: int = roundi(delta_origin.y * inv_scale)
	var vdz: int = roundi(delta_origin.z * inv_scale)

	var world_sx: int = _world.get_world_size_x()
	var world_sy: int = _world.get_world_size_y()
	var world_sz: int = _world.get_world_size_z()

	var written: int = 0
	for i in positions.size():
		var orig: Vector3 = positions[i]
		var vx: int = int(orig.x) + vdx
		var vy: int = int(orig.y) + vdy
		var vz: int = int(orig.z) + vdz
		# Bounds check — don't write outside world
		if vx >= 0 and vx < world_sx and vy >= 0 and vy < world_sy and vz >= 0 and vz < world_sz:
			_world.set_voxel_dirty(vx, vy, vz, materials[i])
			written += 1

	if written > 0:
		print("[Islands] Re-solidified %d/%d voxels (delta: %d,%d,%d)" % [
			written, positions.size(), vdx, vdy, vdz])


func _reclaim_island(slot: int, resolidify: bool = false) -> void:
	if resolidify:
		_resolidify_island(slot)
	_island_active[slot] = false
	_island_voxel_positions[slot] = PackedVector3Array()
	_island_voxel_materials[slot] = PackedByteArray()
	if _island_bodies[slot].is_valid():
		PhysicsServer3D.free_rid(_island_bodies[slot])
		_island_bodies[slot] = RID()
	if _island_shapes[slot].is_valid():
		PhysicsServer3D.free_rid(_island_shapes[slot])
		_island_shapes[slot] = RID()
	if _island_instances[slot].is_valid():
		RenderingServer.free_rid(_island_instances[slot])
		_island_instances[slot] = RID()
	if _island_meshes[slot].is_valid():
		RenderingServer.free_rid(_island_meshes[slot])
		_island_meshes[slot] = RID()


# ── Cleanup ────────────────────────────────────────────────────────────

func _remove_chunk(key: Vector3i) -> void:
	_remove_chunk_mesh(key)
	_remove_chunk_collision(key)


func _remove_chunk_mesh(key: Vector3i) -> void:
	if _chunk_instances.has(key):
		RenderingServer.free_rid(_chunk_instances[key])
		_chunk_instances.erase(key)
		_aabbs_dirty = true
	if _chunk_meshes.has(key):
		RenderingServer.free_rid(_chunk_meshes[key])
		_chunk_meshes.erase(key)
	if _chunk_lod1_meshes.has(key):
		RenderingServer.free_rid(_chunk_lod1_meshes[key])
		_chunk_lod1_meshes.erase(key)
	if _chunk_lod2_meshes.has(key):
		RenderingServer.free_rid(_chunk_lod2_meshes[key])
		_chunk_lod2_meshes.erase(key)
	_chunk_current_lod.erase(key)


func _remove_chunk_collision(key: Vector3i) -> void:
	if _chunk_bodies.has(key):
		PhysicsServer3D.free_rid(_chunk_bodies[key])
		_chunk_bodies.erase(key)
	if _chunk_shapes.has(key):
		PhysicsServer3D.free_rid(_chunk_shapes[key])
		_chunk_shapes.erase(key)


func _exit_tree() -> void:
	# Shut down worker threads first
	if _mesh_worker:
		_mesh_worker.shutdown()

	# Clean up RenderingServer resources
	for rid: RID in _chunk_instances.values():
		RenderingServer.free_rid(rid)
	for rid: RID in _chunk_meshes.values():
		RenderingServer.free_rid(rid)
	for rid: RID in _chunk_lod1_meshes.values():
		RenderingServer.free_rid(rid)
	for rid: RID in _chunk_lod2_meshes.values():
		RenderingServer.free_rid(rid)
	_chunk_instances.clear()
	_chunk_meshes.clear()
	_chunk_lod1_meshes.clear()
	_chunk_lod2_meshes.clear()
	_chunk_current_lod.clear()

	# Clean up physics
	for rid: RID in _chunk_bodies.values():
		PhysicsServer3D.free_rid(rid)
	for rid: RID in _chunk_shapes.values():
		PhysicsServer3D.free_rid(rid)
	_chunk_bodies.clear()
	_chunk_shapes.clear()

	# Clean up LOD 3 terrain mesh
	if _lod3_instance_rid.is_valid():
		RenderingServer.free_rid(_lod3_instance_rid)
	if _lod3_mesh_rid.is_valid():
		RenderingServer.free_rid(_lod3_mesh_rid)

	# Clean up islands (no re-solidification on exit)
	if _islands_initialized:
		for i in MAX_ISLANDS:
			_reclaim_island(i, false)

	if _material.is_valid():
		RenderingServer.free_rid(_material)


func _get_shader_code() -> String:
	return """
shader_type spatial;
render_mode cull_back;

// PBR lookup texture: 16x2
// Row 0: R=roughness, G=metallic, B=emission
// Row 1: R=subsurface, G=anisotropy, B=normal_strength, A=specular_tint
uniform sampler2D pbr_lut : filter_nearest, repeat_disable;
uniform float ambient_boost : hint_range(0.0, 2.0) = 0.0;
uniform float emission_strength : hint_range(0.0, 10.0) = 3.0;
uniform float ao_strength : hint_range(0.0, 1.0) = 0.6;
uniform float weather_wetness : hint_range(0.0, 1.0) = 0.0;

void fragment() {
	// Decode material ID from vertex color alpha (packed by mesher)
	int mat_id = clamp(int(COLOR.a * 255.0 + 0.5), 0, 15);
	vec4 pbr0 = texelFetch(pbr_lut, ivec2(mat_id, 0), 0);
	vec4 pbr1 = texelFetch(pbr_lut, ivec2(mat_id, 1), 0);

	// Per-vertex AO from UV2.x (0=occluded, 1=fully lit)
	float ao = mix(1.0 - ao_strength, 1.0, UV2.x);

	ALBEDO = COLOR.rgb * ao;

	// Weather wetness: reduce roughness on upward-facing surfaces, add metallic sheen
	float up_facing = max(NORMAL.y, 0.0);  // 1.0 for horizontal surfaces
	float wet = weather_wetness * up_facing;
	ROUGHNESS = mix(pbr0.r, pbr0.r * 0.25, wet);
	METALLIC = pbr0.g + wet * 0.35;

	// Extended PBR (Phase 5A) — subsurface, specular tint
	SSS_STRENGTH = pbr1.r;
	SPECULAR = mix(0.5, 0.7, pbr1.a);  // Tinted specular boosts reflectance

	AO = UV2.x;
	AO_LIGHT_AFFECT = 0.5;

	// Per-material emission (pbr0.b) + optional ambient boost
	float emit = pbr0.b;
	EMISSION = COLOR.rgb * (emit * emission_strength + ambient_boost);
}
"""


## Weather wetness uniform (Phase 4B) — 0.0 = dry, 1.0 = fully wet.
var _weather_wetness: float = 0.0

func set_weather_wetness(w: float) -> void:
	_weather_wetness = clampf(w, 0.0, 1.0)
	if _material.is_valid():
		RenderingServer.material_set_param(_material, "weather_wetness", _weather_wetness)

func get_weather_wetness() -> float:
	return _weather_wetness


func _create_pbr_lut() -> void:
	## Per-material PBR values (mirrors MATERIAL_TABLE order):
	## 0=AIR, 1=DIRT, 2=STONE, 3=WOOD, 4=STEEL, 5=CONCRETE, 6=BRICK,
	## 7=GLASS, 8=SAND, 9=WATER, 10=GRASS, 11=GRAVEL, 12=SANDBAG,
	## 13=CLAY, 14=METAL_PLATE, 15=RUST
	## Row 0: R=roughness, G=metallic, B=emission
	var roughness: Array[float] = [
		1.00, 0.95, 0.85, 0.80, 0.35, 0.90, 0.85, 0.05,
		0.95, 0.10, 0.85, 0.90, 0.88, 0.82, 0.40, 0.70,
	]
	var metallic: Array[float] = [
		0.00, 0.00, 0.00, 0.00, 0.85, 0.00, 0.00, 0.00,
		0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.80, 0.50,
	]
	var emiss: Array[float] = [
		0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
		0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
	]
	## Row 1 (Phase 5A): R=subsurface, G=anisotropy, B=normal_strength, A=specular_tint
	var subsurface: Array[float] = [
		0.00, 0.10, 0.00, 0.20, 0.00, 0.00, 0.00, 0.00,
		0.05, 0.30, 0.15, 0.00, 0.05, 0.08, 0.00, 0.00,
	]
	var anisotropy: Array[float] = [
		0.00, 0.00, 0.00, 0.30, 0.50, 0.00, 0.00, 0.00,
		0.00, 0.00, 0.10, 0.00, 0.00, 0.05, 0.50, 0.20,
	]
	var normal_str: Array[float] = [
		0.00, 0.60, 0.80, 0.70, 0.50, 0.75, 0.70, 0.10,
		0.50, 0.20, 0.55, 0.65, 0.45, 0.55, 0.55, 0.70,
	]
	var spec_tint: Array[float] = [
		0.00, 0.10, 0.05, 0.15, 0.30, 0.05, 0.10, 0.80,
		0.05, 0.60, 0.10, 0.05, 0.05, 0.08, 0.25, 0.20,
	]
	_pbr_image = Image.create(16, 2, false, Image.FORMAT_RGBA8)
	for i in 16:
		_pbr_image.set_pixel(i, 0, Color(roughness[i], metallic[i], emiss[i], 1.0))
		_pbr_image.set_pixel(i, 1, Color(subsurface[i], anisotropy[i], normal_str[i], spec_tint[i]))
	_pbr_texture = ImageTexture.create_from_image(_pbr_image)


## Update a single material's emission at runtime (for fire/destruction effects).
## emission: 0.0 = no glow, 1.0 = full glow (scaled by emission_strength in shader).
func set_material_emission(mat_id: int, emission: float) -> void:
	if mat_id < 0 or mat_id >= 16:
		return
	var pixel: Color = _pbr_image.get_pixel(mat_id, 0)
	_pbr_image.set_pixel(mat_id, 0, Color(pixel.r, pixel.g, clampf(emission, 0.0, 1.0), pixel.a))
	_pbr_texture.update(_pbr_image)
