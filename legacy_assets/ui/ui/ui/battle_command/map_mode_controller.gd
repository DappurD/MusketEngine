extends Node3D
## Map mode controller — unified 3D overlay system managing all world-space visualizations.
## Only ONE overlay is active at a time (mutual exclusion).
##
## Modes 1-4: Delegate to existing scene overlay nodes (influence, pheromone).
## Modes 5-9: Self-contained heatmap renderer (PlaneMesh + ImageTexture).

var _ui: BattleCommandUI

enum MapMode {
	NONE = 0,
	INFLUENCE_COVER = 1,
	INFLUENCE_THREAT = 2,
	INFLUENCE_OPPORTUNITY = 3,
	PHEROMONE = 4,
	PRESSURE = 5,
	COVER_SHADOW = 6,
	HEIGHT_MAP = 7,
	GAS_DENSITY = 8,
	FOW_VISIBILITY = 9,
}

const MODE_NAMES: Array[String] = [
	"", "Cover Quality", "Threat", "Opportunity",
	"Pheromone", "Pressure Field", "Cover Shadow",
	"Height Map", "Gas Density", "FOW Visibility",
]

var _current_mode: int = MapMode.NONE
var _cover_detail_visible: bool = false

# References to existing overlay instances (if available in scene)
var _voxel_overlay  # VoxelDebugOverlay
var _phero_overlay  # PheromoneDebugOverlay

# ── Self-contained heatmap overlay (modes 5-9) ────────────────────────
var _heatmap_mesh: MeshInstance3D
var _heatmap_material: StandardMaterial3D
var _heatmap_image: Image
var _heatmap_texture: ImageTexture
var _heatmap_timer: float = 0.0
const HEATMAP_INTERVAL: float = 0.5  # 2Hz update

# Grid dimensions
var _pw: int = 150    # pressure grid width
var _ph: int = 100    # pressure grid height
var _cw: int = 600    # cover grid width
var _ch: int = 400    # cover grid height
var _world_w: float = 600.0
var _world_h: float = 400.0

# Height map cache (expensive to build, refresh on demand)
var _height_cache: PackedFloat32Array
var _height_dirty: bool = true
var _h_min: float = 0.0
var _h_max: float = 50.0

# FOW grid (built per-frame from unit visibility)
var _fow_grid: PackedFloat32Array
var _fow_viewing_team: int = 1  # 1-based for team_can_see() API


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	# Try to find existing overlay nodes in the scene
	if ui.cam:
		var scene_root: Node = ui.cam.get_parent()
		for child in scene_root.get_children():
			if child.get_class() == "VoxelDebugOverlay" or child.get_script() and child.get_script().resource_path.find("voxel_debug_overlay") >= 0:
				_voxel_overlay = child
			if child.get_class() == "PheromoneDebugOverlay" or child.get_script() and child.get_script().resource_path.find("pheromone_debug_overlay") >= 0:
				_phero_overlay = child
	if _ui.camera_script:
		for child in _ui.camera_script.get_children():
			var sname: String = child.name.to_lower()
			if sname.find("voxeldebug") >= 0 or sname.find("overlay") >= 0:
				if child is Node3D and not child.get_script() == null:
					var spath: String = child.get_script().resource_path if child.get_script() else ""
					if spath.find("voxel_debug_overlay") >= 0:
						_voxel_overlay = child
					elif spath.find("pheromone_debug_overlay") >= 0:
						_phero_overlay = child

	# Cache grid dimensions from C++
	if _ui.gpu_map:
		if _ui.gpu_map.has_method("get_pressure_width"):
			_pw = _ui.gpu_map.get_pressure_width()
			_ph = _ui.gpu_map.get_pressure_height()
		if _ui.gpu_map.has_method("get_cover_width"):
			_cw = _ui.gpu_map.get_cover_width()
			_ch = _ui.gpu_map.get_cover_height()

	if _ui.world:
		_world_w = _ui.world.get_world_size_x() * _ui.world.get_voxel_scale()
		_world_h = _ui.world.get_world_size_z() * _ui.world.get_voxel_scale()

	_fow_grid.resize(_pw * _ph)
	_create_heatmap_mesh()


func get_mode_name() -> String:
	if _current_mode >= 0 and _current_mode < MODE_NAMES.size():
		return MODE_NAMES[_current_mode]
	return ""


func set_mode(mode: int) -> void:
	_current_mode = mode
	_apply_mode()
	if mode != MapMode.NONE:
		print("[MapMode] %s" % get_mode_name())
	else:
		print("[MapMode] OFF")


func cycle_mode() -> void:
	_current_mode = (_current_mode + 1) % MapMode.size()
	_apply_mode()
	if _current_mode != MapMode.NONE:
		print("[MapMode] %s" % get_mode_name())
	else:
		print("[MapMode] OFF")


func toggle_cover_detail() -> void:
	_cover_detail_visible = not _cover_detail_visible
	if _voxel_overlay:
		_voxel_overlay.show_local_cover = _cover_detail_visible
	print("[MapMode] Local cover detail: %s" % ("ON" if _cover_detail_visible else "OFF"))


func invalidate_height_cache() -> void:
	_height_dirty = true


func _apply_mode() -> void:
	# Turn off all overlays first
	if _voxel_overlay:
		_voxel_overlay.show_influence = false
	if _phero_overlay:
		_phero_overlay.visible = false
	if _heatmap_mesh:
		_heatmap_mesh.visible = false

	match _current_mode:
		MapMode.NONE:
			pass

		MapMode.INFLUENCE_COVER:
			if _voxel_overlay:
				_voxel_overlay.show_influence = true
				_voxel_overlay.active_layer = 0

		MapMode.INFLUENCE_THREAT:
			if _voxel_overlay:
				_voxel_overlay.show_influence = true
				_voxel_overlay.active_layer = 1

		MapMode.INFLUENCE_OPPORTUNITY:
			if _voxel_overlay:
				_voxel_overlay.show_influence = true
				_voxel_overlay.active_layer = 2

		MapMode.PHEROMONE:
			if _phero_overlay:
				_phero_overlay.visible = true

		MapMode.PRESSURE, MapMode.COVER_SHADOW, MapMode.HEIGHT_MAP, \
		MapMode.GAS_DENSITY, MapMode.FOW_VISIBILITY:
			if _heatmap_mesh:
				_heatmap_mesh.visible = true
				_heatmap_timer = HEATMAP_INTERVAL  # Force immediate first render


# ── Heatmap Mesh ──────────────────────────────────────────────────────

func _create_heatmap_mesh() -> void:
	_heatmap_mesh = MeshInstance3D.new()
	_heatmap_mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_heatmap_mesh.visible = false
	add_child(_heatmap_mesh)

	var plane := PlaneMesh.new()
	plane.size = Vector2(_world_w, _world_h)
	plane.orientation = PlaneMesh.FACE_Y
	_heatmap_mesh.mesh = plane

	_heatmap_material = StandardMaterial3D.new()
	_heatmap_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_heatmap_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_heatmap_material.cull_mode = BaseMaterial3D.CULL_DISABLED
	_heatmap_material.albedo_color = Color(1, 1, 1, 0.7)
	_heatmap_mesh.material_override = _heatmap_material

	_heatmap_mesh.global_position = Vector3(0.0, 0.3, 0.0)


func _process(delta: float) -> void:
	if _current_mode < MapMode.PRESSURE:
		return

	_heatmap_timer += delta
	if _heatmap_timer < HEATMAP_INTERVAL:
		return
	_heatmap_timer = 0.0

	_update_heatmap()


func _ensure_image(w: int, h: int) -> void:
	if _heatmap_image and _heatmap_image.get_width() == w and _heatmap_image.get_height() == h:
		return
	_heatmap_image = Image.create(w, h, false, Image.FORMAT_RGBA8)
	_heatmap_texture = ImageTexture.create_from_image(_heatmap_image)
	_heatmap_material.albedo_texture = _heatmap_texture


func _update_heatmap() -> void:
	match _current_mode:
		MapMode.PRESSURE:
			_render_pressure()
		MapMode.COVER_SHADOW:
			_render_cover_shadow()
		MapMode.HEIGHT_MAP:
			_render_height_map()
		MapMode.GAS_DENSITY:
			_render_gas_density()
		MapMode.FOW_VISIBILITY:
			_render_fow_visibility()


# ── Mode 5: Pressure Field ────────────────────────────────────────────
# R=threat, G=goal, B=safety, A=0

func _render_pressure() -> void:
	if not _ui.gpu_map or not _ui.gpu_map.has_method("get_pressure_debug"):
		return
	_ensure_image(_pw, _ph)

	var data: PackedFloat32Array = _ui.gpu_map.get_pressure_debug()
	if data.is_empty():
		return

	var max_t: float = 0.01
	var max_g: float = 0.01
	var i: int = 0
	while i < data.size():
		max_t = maxf(max_t, data[i])
		max_g = maxf(max_g, data[i + 1])
		i += 4

	for y in _ph:
		for x in _pw:
			var idx: int = (y * _pw + x) * 4
			if idx + 1 >= data.size():
				continue
			var threat: float = data[idx] / max_t
			var goal: float = data[idx + 1] / max_g
			var intensity: float = maxf(threat, goal)
			var a: float = clampf(intensity * 0.8, 0.0, 0.8)
			if a < 0.02:
				_heatmap_image.set_pixel(x, y, Color(0, 0, 0, 0))
			else:
				_heatmap_image.set_pixel(x, y, Color(
					clampf(threat, 0.0, 1.0),
					clampf(minf(threat, goal) * 0.3, 0.0, 0.3),
					clampf(goal, 0.0, 1.0),
					a))

	_heatmap_texture.update(_heatmap_image)


# ── Mode 6: Cover Shadow ──────────────────────────────────────────────
# 600x400 → downsample to 150x100

func _render_cover_shadow() -> void:
	if not _ui.gpu_map or not _ui.gpu_map.has_method("get_cover_debug"):
		return
	_ensure_image(_pw, _ph)

	var data: PackedFloat32Array = _ui.gpu_map.get_cover_debug()
	if data.is_empty():
		return

	var sx: int = _cw / _pw  # Downsample factor (typically 4)
	var sz: int = _ch / _ph

	for py in _ph:
		for px in _pw:
			var sum: float = 0.0
			var count: int = 0
			for dz in sz:
				for dx in sx:
					var cx: int = px * sx + dx
					var cz: int = py * sz + dz
					var idx: int = cz * _cw + cx
					if idx < data.size():
						sum += data[idx]
						count += 1
			var avg: float = sum / float(count) if count > 0 else 0.0
			var intensity: float = clampf(avg, 0.0, 1.0)
			if intensity < 0.05:
				_heatmap_image.set_pixel(px, py, Color(0, 0, 0, 0))
			else:
				_heatmap_image.set_pixel(px, py, Color(
					0.0, intensity * 0.85, intensity * 0.15, intensity * 0.7))

	_heatmap_texture.update(_heatmap_image)


# ── Mode 7: Height Map ────────────────────────────────────────────────

func _render_height_map() -> void:
	if not _ui.gpu_map or not _ui.gpu_map.has_method("get_terrain_height_m"):
		return
	_ensure_image(_pw, _ph)

	if _height_dirty or _height_cache.size() != _pw * _ph:
		_rebuild_height_cache()

	var h_range: float = maxf(_h_max - _h_min, 1.0)
	for y in _ph:
		for x in _pw:
			var h: float = _height_cache[y * _pw + x]
			var norm: float = clampf((h - _h_min) / h_range, 0.0, 1.0)

			var c: Color
			if norm < 0.3:
				# Dark green lowlands
				var t: float = norm / 0.3
				c = Color(0.08 + t * 0.1, 0.18 + t * 0.15, 0.05)
			elif norm < 0.6:
				# Brown-green midlands
				var t: float = (norm - 0.3) / 0.3
				c = Color(0.18 + t * 0.15, 0.33 - t * 0.08, 0.05 + t * 0.05)
			else:
				# Light brown to gray highlands
				var t: float = (norm - 0.6) / 0.4
				c = Color(0.33 + t * 0.35, 0.25 + t * 0.35, 0.1 + t * 0.45)
			c.a = 0.75
			_heatmap_image.set_pixel(x, y, c)

	_heatmap_texture.update(_heatmap_image)


func _rebuild_height_cache() -> void:
	_height_cache.resize(_pw * _ph)
	_h_min = 999.0
	_h_max = 0.0
	var half_w: float = _world_w * 0.5
	var half_h: float = _world_h * 0.5
	var cell_w: float = _world_w / float(_pw)
	var cell_h: float = _world_h / float(_ph)

	for pz in _ph:
		for px in _pw:
			var wx: float = -half_w + (float(px) + 0.5) * cell_w
			var wz: float = -half_h + (float(pz) + 0.5) * cell_h
			var h: float = _ui.gpu_map.get_terrain_height_m(wx, wz)
			_height_cache[pz * _pw + px] = h
			_h_min = minf(_h_min, h)
			_h_max = maxf(_h_max, h)

	_height_dirty = false


# ── Mode 8: Gas Density ───────────────────────────────────────────────

func _render_gas_density() -> void:
	if not _ui.gpu_map:
		return
	_ensure_image(_pw, _ph)

	var half_w: float = _world_w * 0.5
	var half_h: float = _world_h * 0.5
	var cell_w: float = _world_w / float(_pw)
	var cell_h: float = _world_h / float(_ph)

	for y in _ph:
		for x in _pw:
			var wx: float = -half_w + (float(x) + 0.5) * cell_w
			var wz: float = -half_h + (float(y) + 0.5) * cell_h
			var pos := Vector3(wx, 0.0, wz)
			var density: float = _ui.gpu_map.sample_gas_density(pos)

			if density < 0.01:
				_heatmap_image.set_pixel(x, y, Color(0, 0, 0, 0))
				continue

			var gas_type: int = _ui.gpu_map.sample_gas_type(pos)
			var tint: Color
			match gas_type:
				1:  # Smoke — white
					tint = Color(0.9, 0.9, 0.9)
				2:  # Tear gas — yellow-green
					tint = Color(0.7, 0.8, 0.2)
				3:  # Toxic — purple
					tint = Color(0.6, 0.15, 0.7)
				_:
					tint = Color(0.5, 0.5, 0.5)

			var a: float = clampf(density * 0.85, 0.0, 0.85)
			_heatmap_image.set_pixel(x, y, Color(tint.r, tint.g, tint.b, a))

	_heatmap_texture.update(_heatmap_image)


# ── Mode 9: FOW Visibility ────────────────────────────────────────────

func _render_fow_visibility() -> void:
	if not _ui.sim:
		return
	_ensure_image(_pw, _ph)

	_fow_grid.fill(0.0)

	var cell_w: float = _world_w / float(_pw)
	var cell_h: float = _world_h / float(_ph)
	var half_w: float = _world_w * 0.5
	var half_h: float = _world_h * 0.5
	var vis_r: int = ceili(8.0 / cell_w)  # 8m stamp radius in cells

	var sim: SimulationServer = _ui.sim
	var unit_count: int = sim.get_unit_count()

	for uid in unit_count:
		if not sim.is_alive(uid):
			continue
		var unit_team: int = sim.get_team(uid)

		# Own units always visible; enemies checked via FOW
		var is_vis: bool
		if (unit_team + 1) == _fow_viewing_team:
			is_vis = true
		else:
			is_vis = sim.team_can_see(_fow_viewing_team, uid)

		if not is_vis:
			continue

		var pos: Vector3 = sim.get_position(uid)
		var cx: int = int((pos.x + half_w) / cell_w)
		var cz: int = int((pos.z + half_h) / cell_h)

		# Stamp circle
		for dz in range(-vis_r, vis_r + 1):
			for dx in range(-vis_r, vis_r + 1):
				if dx * dx + dz * dz > vis_r * vis_r:
					continue
				var gx: int = cx + dx
				var gz: int = cz + dz
				if gx >= 0 and gx < _pw and gz >= 0 and gz < _ph:
					_fow_grid[gz * _pw + gx] = 1.0

	# Render: fog = dark, visible = transparent
	for y in _ph:
		for x in _pw:
			if _fow_grid[y * _pw + x] > 0.5:
				_heatmap_image.set_pixel(x, y, Color(0, 0, 0, 0))
			else:
				_heatmap_image.set_pixel(x, y, Color(0.0, 0.0, 0.05, 0.6))

	_heatmap_texture.update(_heatmap_image)
