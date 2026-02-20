extends Node3D
class_name VoxelDebugOverlay
## 3D debug overlay for voxel AI data visualization.
## Renders influence grid, cover detail, and threat markers.

var _world: VoxelWorld
var _camera: Camera3D
var _cover_map  # TacticalCoverMap (RefCounted)
var _influence_map  # InfluenceMapCPP (RefCounted)

## Overlay visibility state
var show_influence: bool = false
var show_local_cover: bool = false
var active_layer: int = 0  ## 0=cover_quality, 1=threat, 2=opportunity

## Influence grid overlay (ImmediateMesh)
var _influence_mesh_inst: MeshInstance3D
var _influence_immediate: ImmediateMesh
var _influence_material: StandardMaterial3D

## Local cover detail overlay (ImmediateMesh)
var _cover_mesh_inst: MeshInstance3D
var _cover_immediate: ImmediateMesh
var _cover_material: StandardMaterial3D

## Height cache for influence grid
var _sector_heights: PackedFloat32Array
var _heights_cached: bool = false

## Threat markers
var threat_positions: PackedVector3Array = PackedVector3Array()
var _threat_markers: Array[MeshInstance3D] = []
const MAX_THREATS: int = 4

## Update throttling
var _overlay_timer: float = 0.0
const OVERLAY_INTERVAL: float = 0.5
var _last_detail_center: Vector3 = Vector3.ZERO
const DETAIL_MOVE_THRESHOLD: float = 5.0

const LAYER_NAMES: Array[String] = ["Cover Quality", "Threat", "Opportunity"]


func setup(cam: Camera3D, world: VoxelWorld, cover_map, influence_map) -> void:
	_camera = cam
	_world = world
	_cover_map = cover_map
	_influence_map = influence_map


func _ready() -> void:
	# Influence grid mesh
	_influence_immediate = ImmediateMesh.new()
	_influence_mesh_inst = MeshInstance3D.new()
	_influence_mesh_inst.mesh = _influence_immediate
	_influence_mesh_inst.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_influence_material = _create_overlay_material()
	_influence_mesh_inst.material_override = _influence_material
	add_child(_influence_mesh_inst)

	# Local cover detail mesh
	_cover_immediate = ImmediateMesh.new()
	_cover_mesh_inst = MeshInstance3D.new()
	_cover_mesh_inst.mesh = _cover_immediate
	_cover_mesh_inst.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_cover_material = _create_overlay_material()
	_cover_mesh_inst.material_override = _cover_material
	add_child(_cover_mesh_inst)


func _process(delta: float) -> void:
	_overlay_timer += delta
	if _overlay_timer < OVERLAY_INTERVAL:
		return
	_overlay_timer = 0.0

	if show_influence:
		_rebuild_influence_overlay()
	else:
		_influence_immediate.clear_surfaces()

	if show_local_cover:
		_update_local_cover()
	else:
		_cover_immediate.clear_surfaces()


func _create_overlay_material() -> StandardMaterial3D:
	var mat = StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.no_depth_test = false
	return mat


# ── Influence Grid Overlay ──────────────────────────────────────────

func _rebuild_influence_overlay() -> void:
	_influence_immediate.clear_surfaces()
	if not _influence_map or not _world or not _world.is_initialized():
		return
	if not _heights_cached:
		_cache_sector_heights()

	var sx: int = _influence_map.get_sectors_x()
	var sz: int = _influence_map.get_sectors_z()
	var half: float = _influence_map.get_sector_size() * 0.48

	_influence_immediate.surface_begin(Mesh.PRIMITIVE_TRIANGLES)

	var vertex_count: int = 0
	for x in range(sx):
		for z in range(sz):
			var value: float = _get_layer_value(x, z)
			if value < 0.01:
				continue
			var color: Color = _value_to_color(value)
			var wpos: Vector3 = _influence_map.sector_to_world(x, z)
			var y: float = _sector_heights[x * sz + z] + 0.2

			_emit_quad(wpos.x, y, wpos.z, half, color)
			vertex_count += 6

	if vertex_count > 0:
		_influence_immediate.surface_end()
	else:
		_influence_immediate.clear_surfaces()


func _get_layer_value(sx: int, sz: int) -> float:
	match active_layer:
		0: return _influence_map.get_cover_quality(sx, sz)
		1: return _influence_map.get_threat(sx, sz)
		2: return _influence_map.get_opportunity(sx, sz)
	return 0.0


func _value_to_color(value: float) -> Color:
	match active_layer:
		0:  # cover_quality: blue -> green
			var t: float = clampf(value, 0.0, 1.0)
			return Color(0.0, t, 1.0 - t * 0.5, 0.4)
		1:  # threat: yellow -> red
			var t: float = clampf(value / 10.0, 0.0, 1.0)
			return Color(1.0, 1.0 - t, 0.0, 0.35 + t * 0.3)
		2:  # opportunity: green -> yellow
			var t: float = clampf(value / 3.0, 0.0, 1.0)
			return Color(t, 1.0, 0.0, 0.35 + t * 0.2)
	return Color.WHITE


func _emit_quad(cx: float, y: float, cz: float, half: float, color: Color) -> void:
	# Two triangles forming a flat quad
	var p0 = Vector3(cx - half, y, cz - half)
	var p1 = Vector3(cx + half, y, cz - half)
	var p2 = Vector3(cx + half, y, cz + half)
	var p3 = Vector3(cx - half, y, cz + half)
	_influence_immediate.surface_set_color(color)
	_influence_immediate.surface_add_vertex(p0)
	_influence_immediate.surface_add_vertex(p1)
	_influence_immediate.surface_add_vertex(p2)
	_influence_immediate.surface_set_color(color)
	_influence_immediate.surface_add_vertex(p0)
	_influence_immediate.surface_add_vertex(p2)
	_influence_immediate.surface_add_vertex(p3)


func _cache_sector_heights() -> void:
	if not _influence_map or not _world:
		return
	var sx: int = _influence_map.get_sectors_x()
	var sz: int = _influence_map.get_sectors_z()
	_sector_heights.resize(sx * sz)
	var max_y: int = _world.get_world_size_y() - 1
	var scale: float = _world.get_voxel_scale()

	for x in range(sx):
		for z in range(sz):
			var wpos: Vector3 = _influence_map.sector_to_world(x, z)
			var vpos: Vector3i = _world.world_to_voxel(Vector3(wpos.x, 0, wpos.z))
			var surface_y: int = 0
			for vy in range(max_y, -1, -1):
				if _world.get_voxel(vpos.x, vy, vpos.z) != 0:
					surface_y = vy + 1
					break
			_sector_heights[x * sz + z] = float(surface_y) * scale
	_heights_cached = true


# ── Local Cover Detail ──────────────────────────────────────────────

func _update_local_cover() -> void:
	if not _cover_map or not _camera or not _world or not _world.is_initialized():
		_cover_immediate.clear_surfaces()
		return

	# Get camera crosshair hit
	var forward: Vector3 = -_camera.global_transform.basis.z
	var hit: Dictionary = _world.raycast_dict(_camera.global_position, forward, 200.0)
	if hit.is_empty() or not hit.get("hit", false):
		return

	var center: Vector3 = hit["world_pos"]
	if center.distance_to(_last_detail_center) < DETAIL_MOVE_THRESHOLD:
		return  # Don't rebuild if camera hasn't moved much
	_last_detail_center = center
	_rebuild_local_cover(center)


func _rebuild_local_cover(center: Vector3) -> void:
	_cover_immediate.clear_surfaces()

	var cells_x: int = _cover_map.get_cells_x()
	var cells_z: int = _cover_map.get_cells_z()
	var scale: float = _world.get_voxel_scale()
	var world_off_x: float = float(_world.get_world_size_x()) * scale * 0.5
	var world_off_z: float = float(_world.get_world_size_z()) * scale * 0.5

	# Cell size is CELL_VOXELS * voxel_scale = 4 * 0.25 = 1.0m
	var cell_size: float = 4.0 * scale
	var center_cx: int = int((center.x + world_off_x) / cell_size)
	var center_cz: int = int((center.z + world_off_z) / cell_size)
	var radius: int = 20  # 20 cells each direction = 20m

	var max_vy: int = mini(_world.get_world_size_y() - 1, 80)

	var vertex_count: int = 0
	_cover_immediate.surface_begin(Mesh.PRIMITIVE_TRIANGLES)

	for dx in range(-radius, radius + 1):
		for dz in range(-radius, radius + 1):
			var cx: int = center_cx + dx
			var cz: int = center_cz + dz
			if cx < 0 or cx >= cells_x or cz < 0 or cz >= cells_z:
				continue
			var value: float = _cover_map.get_cell_cover(cx, cz)
			if value < 0.01:
				continue

			# Cell world position
			var wx: float = (float(cx) + 0.5) * cell_size - world_off_x
			var wz: float = (float(cz) + 0.5) * cell_size - world_off_z

			# Get terrain Y
			var vpos: Vector3i = _world.world_to_voxel(Vector3(wx, 0, wz))
			var surface_y: int = 0
			for vy in range(max_vy, -1, -1):
				if _world.get_voxel(vpos.x, vy, vpos.z) != 0:
					surface_y = vy + 1
					break
			var y: float = float(surface_y) * scale + 0.15

			var t: float = clampf(value, 0.0, 1.0)
			var color = Color(0.1, 0.3 + t * 0.5, 1.0, 0.25 + t * 0.35)
			var half: float = cell_size * 0.48

			var p0 = Vector3(wx - half, y, wz - half)
			var p1 = Vector3(wx + half, y, wz - half)
			var p2 = Vector3(wx + half, y, wz + half)
			var p3 = Vector3(wx - half, y, wz + half)
			_cover_immediate.surface_set_color(color)
			_cover_immediate.surface_add_vertex(p0)
			_cover_immediate.surface_add_vertex(p1)
			_cover_immediate.surface_add_vertex(p2)
			_cover_immediate.surface_set_color(color)
			_cover_immediate.surface_add_vertex(p0)
			_cover_immediate.surface_add_vertex(p2)
			_cover_immediate.surface_add_vertex(p3)
			vertex_count += 6

	if vertex_count > 0:
		_cover_immediate.surface_end()
	else:
		_cover_immediate.clear_surfaces()


# ── Threat Markers ──────────────────────────────────────────────────

func place_threat(world_pos: Vector3) -> void:
	if threat_positions.size() >= MAX_THREATS:
		remove_threat(0)
	threat_positions.append(world_pos)
	_create_threat_visual(world_pos)
	_recalculate_cover()
	print("[DebugOverlay] Placed threat #%d at %s" % [threat_positions.size(), world_pos])


func clear_threats() -> void:
	threat_positions.clear()
	for m in _threat_markers:
		m.queue_free()
	_threat_markers.clear()
	_recalculate_cover()
	print("[DebugOverlay] Cleared all threats")


func remove_threat(idx: int) -> void:
	if idx < 0 or idx >= threat_positions.size():
		return
	threat_positions.remove_at(idx)
	if idx < _threat_markers.size():
		_threat_markers[idx].queue_free()
		_threat_markers.remove_at(idx)


func _create_threat_visual(pos: Vector3) -> void:
	var marker = MeshInstance3D.new()
	var box = BoxMesh.new()
	box.size = Vector3(0.5, 3.0, 0.5)
	marker.mesh = box
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(1.0, 0.2, 0.1, 0.8)
	mat.emission_enabled = true
	mat.emission = Color(1.0, 0.2, 0.1)
	mat.emission_energy_multiplier = 2.0
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	marker.material_override = mat
	marker.position = pos + Vector3(0, 1.5, 0)
	add_child(marker)
	_threat_markers.append(marker)


func _recalculate_cover() -> void:
	if _cover_map:
		_cover_map.update_cover(threat_positions)
	if _influence_map:
		# Feed threat markers as fake enemy units so threat/opportunity layers populate
		var positions = PackedVector3Array()
		var teams = PackedInt32Array()
		var in_combat = PackedFloat32Array()
		for pos in threat_positions:
			positions.append(pos)
			teams.append(2)  # Enemy team
			in_combat.append(1.0)
		_influence_map.update(positions, teams, in_combat)
		_influence_map.update_cover_quality()
	_heights_cached = false  # Force height cache rebuild
	_last_detail_center = Vector3.INF  # Force detail rebuild
