extends PanelContainer
## Minimap — bottom-right tactical map showing unit positions, squads,
## capture points, and camera viewport cone.
## Toggle K to expand to full tactical overview with terrain, orders,
## heatmaps, per-squad color coding, and role-based unit shapes.

var _ui: BattleCommandUI
var _canvas: Control  # Custom draw surface
var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.15

# Dynamic size — world-proportional, scaled to screen
var _map_size: Vector2 = Vector2(280, 200)
var _small_size: Vector2 = Vector2(280, 200)  # Cached compact size
const MAP_PAD: float = 6.0
const MIN_MAP_W: float = 240.0
const MAX_MAP_W: float = 360.0

# ── Expanded mode ─────────────────────────────────────────────────
var _expanded: bool = false
var _terrain_tex: ImageTexture = null
var _terrain_dirty: bool = true
const TERRAIN_W: int = 150
const TERRAIN_H: int = 100

# ── Hand-picked squad color palettes (first 10 per team) ─────────
# Designed for maximum perceptual separation on dark background.
# Team 1: warm tones. Team 2: cool tones.
const TEAM1_PALETTE: Array[Color] = [
	Color(1.0, 0.6, 0.15),     # 0: Bright orange
	Color(1.0, 0.95, 0.3),     # 1: Yellow
	Color(1.0, 0.35, 0.35),    # 2: Coral red
	Color(0.95, 0.75, 0.55),   # 3: Peach/tan
	Color(0.7, 1.0, 0.35),     # 4: Yellow-green
	Color(1.0, 0.5, 0.7),      # 5: Salmon pink
	Color(0.9, 0.4, 0.1),      # 6: Burnt sienna
	Color(0.85, 0.85, 0.0),    # 7: Gold
	Color(0.6, 0.85, 0.2),     # 8: Lime
	Color(0.95, 0.7, 0.85),    # 9: Warm rose
]
const TEAM2_PALETTE: Array[Color] = [
	Color(0.3, 0.6, 1.0),      # 0: Medium blue
	Color(0.2, 1.0, 0.85),     # 1: Cyan/turquoise
	Color(0.7, 0.35, 1.0),     # 2: Purple
	Color(0.15, 0.4, 0.9),     # 3: Deep blue
	Color(0.85, 0.5, 1.0),     # 4: Orchid/magenta
	Color(0.3, 0.85, 0.65),    # 5: Teal
	Color(0.55, 0.55, 1.0),    # 6: Periwinkle
	Color(0.1, 0.75, 1.0),     # 7: Sky blue
	Color(0.9, 0.3, 0.85),     # 8: Hot pink
	Color(0.45, 0.9, 0.45),    # 9: Mint
]

# Procedural fallback for squads 11-64 (wider hue band + S/V variation)
const TEAM1_HUE_BASE: float = 30.0
const TEAM2_HUE_BASE: float = 210.0
const HUE_RANGE_WIDE: float = 180.0
const GOLDEN_FRAC: float = 0.381966
var _squad_colors: PackedColorArray = PackedColorArray()

# ── Team visibility toggles ───────────────────────────────────────
var _show_team1: bool = true
var _show_team2: bool = true

# ── Expanded-mode feature toggles ─────────────────────────────────
var _show_orders: bool = true
var _show_labels: bool = true

# ── Heatmap overlay (expanded only) ──────────────────────────────
var _heatmap_mode: int = 0  # 0=OFF, 1-4 = channel index into HEATMAP_CHANNELS
var _heatmap_tex: ImageTexture = null
var _heatmap_timer: float = 0.0
const HEATMAP_INTERVAL: float = 0.5
const HEATMAP_CHANNELS: Array[int] = [0, 1, 2, 6]  # DANGER, SUPPRESSION, CONTACT, SAFE_ROUTE
const HEATMAP_NAMES: Array[String] = ["DANGER", "SUPPRESSION", "CONTACT", "SAFE_ROUTE"]
const HEATMAP_COLORS: Array[Color] = [
	Color(1.0, 0.2, 0.2),   # DANGER - red
	Color(1.0, 0.5, 0.1),   # SUPPRESSION - orange
	Color(1.0, 1.0, 0.2),   # CONTACT - yellow
	Color(1.0, 0.2, 0.8),   # SAFE_ROUTE - magenta
]

# ── Goal colors (reuse from voxel_test_camera.gd) ────────────────
const GOAL_COLORS: Dictionary = {
	"capture_poi":   Color(0.2, 0.9, 0.2),
	"defend_poi":    Color(0.3, 0.5, 1.0),
	"assault_enemy": Color(1.0, 0.2, 0.2),
	"defend_base":   Color(0.7, 0.2, 0.9),
	"fire_mission":  Color(1.0, 0.6, 0.1),
	"flank_enemy":   Color(1.0, 1.0, 0.2),
	"hold_position": Color(0.5, 0.5, 0.5),
	"reconnaissance": Color(0.2, 0.8, 0.9),
}
const GOAL_COLOR_DEFAULT: Color = Color(0.6, 0.6, 0.6)
const ACTIVE_GOALS: Array[String] = ["assault_enemy", "flank_enemy", "capture_poi", "fire_mission"]

# ── Cached data for drawing ───────────────────────────────────────
var _t1_positions: PackedVector3Array
var _t2_positions: PackedVector3Array
var _t1_states: PackedInt32Array
var _t2_states: PackedInt32Array
var _t1_squads: PackedInt32Array
var _t2_squads: PackedInt32Array
var _t1_roles: PackedByteArray
var _t2_roles: PackedByteArray
var _capture_positions: Array = []
var _capture_owners: Array = []
var _capture_progress: PackedFloat32Array = PackedFloat32Array()
var _capture_contested: PackedInt32Array = PackedInt32Array()
var _cam_pos: Vector3 = Vector3.ZERO
var _cam_fwd: Vector3 = Vector3.FORWARD
var _cam_right: Vector3 = Vector3.RIGHT
var _cam_fov: float = 55.0
var _world_min: Vector2 = Vector2(-150, -100)
var _world_max: Vector2 = Vector2(150, 100)

# Order data (from camera_script intent dicts)
var _order_data_t1: Array = []  # [{centroid: V2, target: V2, goal: String}]
var _order_data_t2: Array = []


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build_squad_colors()
	_compute_map_size()
	_small_size = _map_size
	_build()


func _build_squad_colors() -> void:
	_squad_colors.resize(128)
	for team in 2:
		var base_hue: float = TEAM1_HUE_BASE if team == 0 else TEAM2_HUE_BASE
		var palette: Array[Color] = TEAM1_PALETTE if team == 0 else TEAM2_PALETTE
		for i in 64:
			if i < palette.size():
				_squad_colors[team * 64 + i] = palette[i]
			else:
				# Wider procedural fallback with S/V variation
				var offset: float = fmod(float(i) * GOLDEN_FRAC, 1.0)
				var hue: float = fmod(base_hue - HUE_RANGE_WIDE * 0.5 + offset * HUE_RANGE_WIDE + 360.0, 360.0)
				var sat: float = 0.55 + 0.35 * fmod(float(i) * 0.618033, 1.0)
				var val: float = 0.7 + 0.25 * fmod(float(i) * 0.4656, 1.0)
				_squad_colors[team * 64 + i] = Color.from_hsv(hue / 360.0, sat, val)


func _get_squad_color(squad_id: int, team: int) -> Color:
	# squad_id is the raw C++ squad_id: 0-63 for team 0, 64-127 for team 1
	# _squad_colors layout: [0..63] = team 0, [64..127] = team 1
	# So squad_id maps directly to the array index.
	if squad_id >= 0 and squad_id < _squad_colors.size():
		return _squad_colors[squad_id]
	return BattleCommandUI.TEAM1_COLOR if team == 0 else BattleCommandUI.TEAM2_COLOR


func _compute_map_size() -> void:
	var world_w: float = _world_max.x - _world_min.x
	var world_h: float = _world_max.y - _world_min.y
	if world_w < 1.0:
		world_w = 300.0
	if world_h < 1.0:
		world_h = 200.0
	var aspect: float = world_w / world_h
	var w: float = clampf(280.0, MIN_MAP_W, MAX_MAP_W)
	var h: float = w / aspect
	_map_size = Vector2(w, h)


func _build() -> void:
	anchors_preset = Control.PRESET_BOTTOM_RIGHT
	offset_right = -10.0
	offset_bottom = -10.0
	custom_minimum_size = _map_size + Vector2(MAP_PAD * 2, MAP_PAD * 2)

	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.04, 0.05, 0.07, 0.92)
	sb.border_width_top = 1
	sb.border_width_bottom = 1
	sb.border_width_left = 1
	sb.border_width_right = 1
	sb.border_color = Color(0.18, 0.22, 0.28)
	sb.corner_radius_top_left = 6
	sb.corner_radius_top_right = 6
	sb.corner_radius_bottom_left = 6
	sb.corner_radius_bottom_right = 6
	sb.content_margin_left = MAP_PAD
	sb.content_margin_right = MAP_PAD
	sb.content_margin_top = MAP_PAD
	sb.content_margin_bottom = MAP_PAD
	sb.shadow_color = Color(0, 0, 0, 0.35)
	sb.shadow_size = 6
	sb.shadow_offset = Vector2(0, 2)
	add_theme_stylebox_override("panel", sb)

	_canvas = Control.new()
	_canvas.custom_minimum_size = _map_size
	_canvas.draw.connect(_on_draw)
	_canvas.mouse_filter = Control.MOUSE_FILTER_STOP
	_canvas.gui_input.connect(_on_canvas_input)
	add_child(_canvas)


# ── Data Update ────────────────────────────────────────────────────

func update_data(delta: float) -> void:
	_update_timer += delta
	if _update_timer < UPDATE_INTERVAL:
		return
	_update_timer = 0.0

	if not _ui or not _ui.sim:
		return

	var sim: SimulationServer = _ui.sim

	# Determine world bounds from voxel world
	if _ui.world:
		var wx: float = _ui.world.get_world_size_x() * _ui.world.get_voxel_scale() * 0.5
		var wz: float = _ui.world.get_world_size_z() * _ui.world.get_voxel_scale() * 0.5
		var new_min := Vector2(-wx, -wz)
		var new_max := Vector2(wx, wz)
		if new_min != _world_min or new_max != _world_max:
			_world_min = new_min
			_world_max = new_max
			_compute_map_size()
			if not _expanded:
				_small_size = _map_size
				custom_minimum_size = _map_size + Vector2(MAP_PAD * 2, MAP_PAD * 2)
				_canvas.custom_minimum_size = _map_size

	# Unit positions — per-team render data
	var rd1: Dictionary = sim.get_render_data_for_team(1) if sim.has_method("get_render_data_for_team") else {}
	var rd2: Dictionary = sim.get_render_data_for_team(2) if sim.has_method("get_render_data_for_team") else {}
	_t1_positions = rd1.get("positions", PackedVector3Array())
	_t2_positions = rd2.get("positions", PackedVector3Array())
	_t1_states = rd1.get("states", PackedInt32Array())
	_t2_states = rd2.get("states", PackedInt32Array())
	_t1_squads = rd1.get("squad_ids", PackedInt32Array())
	_t2_squads = rd2.get("squad_ids", PackedInt32Array())
	_t1_roles = rd1.get("roles", PackedByteArray())
	_t2_roles = rd2.get("roles", PackedByteArray())

	if _t1_positions.is_empty() and _t2_positions.is_empty():
		var all_pos: PackedVector3Array = sim.get_alive_positions()
		var all_teams: PackedInt32Array = sim.get_alive_teams()
		var pos1: PackedVector3Array = PackedVector3Array()
		var pos2: PackedVector3Array = PackedVector3Array()
		for i in all_pos.size():
			if i < all_teams.size():
				if all_teams[i] == 0:
					pos1.append(all_pos[i])
				else:
					pos2.append(all_pos[i])
		_t1_positions = pos1
		_t2_positions = pos2

	# Capture points
	if sim.has_method("get_capture_data"):
		var cap: Dictionary = sim.get_capture_data()
		_capture_positions = cap.get("positions", [])
		_capture_owners = cap.get("owners", [])
		_capture_progress = cap.get("progress", PackedFloat32Array())
		_capture_contested = cap.get("contested", PackedInt32Array())

	# Camera
	if _ui.cam:
		_cam_pos = _ui.cam.global_position
		_cam_fwd = -_ui.cam.global_transform.basis.z
		_cam_right = _ui.cam.global_transform.basis.x
		_cam_fov = _ui.cam.fov

	# Order data (expanded only, read from camera_script intent dicts)
	if _expanded and _show_orders:
		_update_order_data()

	# Heatmap (expanded only, throttled separately)
	if _expanded and _heatmap_mode > 0:
		_heatmap_timer += UPDATE_INTERVAL
		if _heatmap_timer >= HEATMAP_INTERVAL:
			_heatmap_timer = 0.0
			_update_heatmap()

	_canvas.queue_redraw()


func _update_order_data() -> void:
	_order_data_t1.clear()
	_order_data_t2.clear()

	if not _ui or not _ui.camera_script:
		return
	var cs = _ui.camera_script
	var sim: SimulationServer = _ui.sim

	# Team 1 orders
	var goals_t1: Dictionary = cs.get("_last_intent_goal_name_t1") if cs.get("_last_intent_goal_name_t1") != null else {}
	var targets_t1: Dictionary = cs.get("_last_intent_target_t1") if cs.get("_last_intent_target_t1") != null else {}
	for sq_id in goals_t1:
		var goal_name: String = goals_t1[sq_id]
		var target: Vector3 = targets_t1.get(sq_id, Vector3.ZERO)
		if target == Vector3.ZERO or goal_name == "":
			continue
		var centroid: Vector3 = sim.get_squad_centroid(sq_id) if sim.has_method("get_squad_centroid") else Vector3.ZERO
		if centroid == Vector3.ZERO:
			continue
		_order_data_t1.append({
			"centroid": _world_to_map(centroid),
			"target": _world_to_map(target),
			"goal": goal_name,
		})

	# Team 2 orders
	var goals_t2: Dictionary = cs.get("_last_intent_goal_name_t2") if cs.get("_last_intent_goal_name_t2") != null else {}
	var targets_t2: Dictionary = cs.get("_last_intent_target_t2") if cs.get("_last_intent_target_t2") != null else {}
	for sq_id in goals_t2:
		var goal_name: String = goals_t2[sq_id]
		var target: Vector3 = targets_t2.get(sq_id, Vector3.ZERO)
		if target == Vector3.ZERO or goal_name == "":
			continue
		var centroid: Vector3 = sim.get_squad_centroid(sq_id) if sim.has_method("get_squad_centroid") else Vector3.ZERO
		if centroid == Vector3.ZERO:
			continue
		_order_data_t2.append({
			"centroid": _world_to_map(centroid),
			"target": _world_to_map(target),
			"goal": goal_name,
		})


func _update_heatmap() -> void:
	if not _ui or not _ui.sim or _heatmap_mode <= 0:
		return
	var channel_idx: int = _heatmap_mode - 1
	if channel_idx >= HEATMAP_CHANNELS.size():
		return
	var channel: int = HEATMAP_CHANNELS[channel_idx]
	var hm_color: Color = HEATMAP_COLORS[channel_idx]

	var team: int = 0
	if _ui.strategy and _ui.strategy.get("_viewing_team") != null:
		team = _ui.strategy._viewing_team

	var data: PackedFloat32Array = PackedFloat32Array()
	if _ui.sim.has_method("get_pheromone_data"):
		data = _ui.sim.get_pheromone_data(team, channel)
	if data.is_empty():
		return

	var max_val: float = 0.001
	for v in data:
		if v > max_val:
			max_val = v

	var pw: int = 150
	var ph: int = 100
	if data.size() < pw * ph:
		return

	var img := Image.create(pw, ph, false, Image.FORMAT_RGBA8)
	for gz in ph:
		for gx in pw:
			var v: float = data[gz * pw + gx]
			var t: float = clampf(v / max_val, 0.0, 1.0)
			if t < 0.01:
				img.set_pixel(gx, gz, Color(0, 0, 0, 0))
			else:
				var intensity: float = sqrt(t)
				img.set_pixel(gx, gz, Color(
					hm_color.r * intensity,
					hm_color.g * intensity,
					hm_color.b * intensity,
					intensity * 0.7))

	if _heatmap_tex == null:
		_heatmap_tex = ImageTexture.create_from_image(img)
	else:
		_heatmap_tex.update(img)


# ── Drawing ────────────────────────────────────────────────────────

func _on_draw() -> void:
	var size: Vector2 = _map_size

	# Layer 1: Background
	_canvas.draw_rect(Rect2(Vector2.ZERO, size), Color(0.025, 0.03, 0.045), true)

	# Layer 2: Terrain texture
	if _terrain_tex:
		_canvas.draw_texture_rect(_terrain_tex, Rect2(Vector2.ZERO, size), false,
			Color(1, 1, 1, 0.75 if _expanded else 0.5))

	# Layer 3: Grid lines
	var grid_color := Color(0.08, 0.09, 0.12)
	var grid_steps_x: int = 12 if _expanded else 6
	var grid_steps_z: int = 8 if _expanded else 4
	for i in range(1, grid_steps_x):
		var x: float = size.x * float(i) / float(grid_steps_x)
		_canvas.draw_line(Vector2(x, 0), Vector2(x, size.y), grid_color, 1.0)
	for i in range(1, grid_steps_z):
		var y: float = size.y * float(i) / float(grid_steps_z)
		_canvas.draw_line(Vector2(0, y), Vector2(size.x, y), grid_color, 1.0)

	# Layer 4: Heatmap overlay (expanded only)
	if _expanded and _heatmap_mode > 0 and _heatmap_tex:
		_canvas.draw_texture_rect(_heatmap_tex, Rect2(Vector2.ZERO, size), false,
			Color(1, 1, 1, 0.5))

	# Layer 5: Order arrows (expanded only)
	if _expanded and _show_orders:
		if _show_team1:
			_draw_order_arrows(_order_data_t1)
		if _show_team2:
			_draw_order_arrows(_order_data_t2)

	# Layer 6: Squad bounding rects
	if _show_team1:
		_draw_squad_groups(_t1_positions, _t1_states, _t1_squads, 0)
	if _show_team2:
		_draw_squad_groups(_t2_positions, _t2_states, _t2_squads, 1)

	# Border
	_canvas.draw_rect(Rect2(Vector2.ZERO, size), Color(0.12, 0.14, 0.18), false, 1.0)

	# Layer 7: Unit icons (role-shaped, squad-colored)
	if _show_team1:
		_draw_units(_t1_positions, _t1_states, _t1_squads, _t1_roles, 0)
	if _show_team2:
		_draw_units(_t2_positions, _t2_states, _t2_squads, _t2_roles, 1)

	# Selected unit highlight
	_draw_selected_unit()

	# Layer 8: Capture points + arcs
	_draw_capture_points()

	# Layer 9: Camera cone
	_draw_camera_cone()

	# Expanded-only: title bar, labels, legend
	if _expanded:
		_draw_title_bar()
		if _show_labels:
			_draw_squad_labels()
		_draw_legend()
	else:
		# Compact label
		_canvas.draw_string(ThemeDB.fallback_font, Vector2(4, size.y - 4),
			"TACTICAL MAP", HORIZONTAL_ALIGNMENT_LEFT, -1, 8,
			Color(0.3, 0.35, 0.4))


# ── Title Bar (expanded mode) ─────────────────────────────────────

func _draw_title_bar() -> void:
	var size: Vector2 = _map_size

	# Semi-transparent header strip
	_canvas.draw_rect(Rect2(Vector2.ZERO, Vector2(size.x, 22)),
		Color(0, 0, 0, 0.5), true)
	_canvas.draw_line(Vector2(0, 22), Vector2(size.x, 22),
		Color(BattleCommandUI.ACCENT_COLOR, 0.2), 1.0)

	# Title
	_canvas.draw_string(ThemeDB.fallback_font, Vector2(6, 15),
		"TACTICAL OVERVIEW", HORIZONTAL_ALIGNMENT_LEFT, -1, 11,
		BattleCommandUI.ACCENT_COLOR)

	# Active feature status (right-aligned)
	var status_parts: PackedStringArray = PackedStringArray()
	if not _show_team1:
		status_parts.append("T1:OFF")
	if not _show_team2:
		status_parts.append("T2:OFF")
	if _heatmap_mode > 0:
		var ch_idx: int = _heatmap_mode - 1
		var ch_name: String = HEATMAP_NAMES[ch_idx] if ch_idx < HEATMAP_NAMES.size() else "?"
		status_parts.append(ch_name)
	if not _show_orders:
		status_parts.append("ORD:OFF")
	if not _show_labels:
		status_parts.append("LBL:OFF")
	if not status_parts.is_empty():
		var status_str: String = "  ".join(status_parts)
		_canvas.draw_string(ThemeDB.fallback_font,
			Vector2(size.x - 6, 15), status_str,
			HORIZONTAL_ALIGNMENT_RIGHT, int(size.x * 0.5), 9,
			Color(0.55, 0.6, 0.65))

	# Heatmap channel indicator (below title bar, colored)
	if _heatmap_mode > 0:
		var ch_idx: int = _heatmap_mode - 1
		var ch_name: String = HEATMAP_NAMES[ch_idx] if ch_idx < HEATMAP_NAMES.size() else "?"
		var ch_color: Color = HEATMAP_COLORS[ch_idx] if ch_idx < HEATMAP_COLORS.size() else Color.WHITE
		_canvas.draw_string(ThemeDB.fallback_font,
			Vector2(6, 36), "HEATMAP: " + ch_name,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 10, ch_color)


# ── Unit Icons (role-shaped, squad-colored) ────────────────────────

func _draw_units(positions: PackedVector3Array, states: PackedInt32Array,
		squads: PackedInt32Array, roles: PackedByteArray, team: int) -> void:
	var r: float = 4.5 if _expanded else 2.5
	var dead_r: float = 2.0 if _expanded else 1.0
	var compact: bool = not _expanded

	for i in positions.size():
		var mp: Vector2 = _world_to_map(positions[i])
		var is_dead: bool = (i < states.size() and states[i] == 11)
		var sq_color: Color = _get_squad_color(squads[i], team) if i < squads.size() else (
			BattleCommandUI.TEAM1_COLOR if team == 0 else BattleCommandUI.TEAM2_COLOR)
		var alpha: float = 0.15 if is_dead else 0.85
		var color: Color = Color(sq_color, alpha)
		var role: int = roles[i] if i < roles.size() else 0
		var s: float = dead_r if is_dead else r

		# Compact mode: all units as circles (shapes too small to distinguish)
		if compact:
			_canvas.draw_circle(mp, s, color)
			continue

		# Expanded mode: role-based shapes with dark outline for contrast
		var outline: Color = Color(0, 0, 0, alpha * 0.6)
		var os: float = s + 1.2  # Outline slightly larger
		match role:
			0:  # RIFLEMAN — circle
				_canvas.draw_circle(mp, os, outline)
				_canvas.draw_circle(mp, s, color)
			1:  # LEADER — diamond (prominent, 15% larger)
				_draw_diamond(mp, os * 1.15, outline)
				_draw_diamond(mp, s * 1.15, color)
			2:  # MEDIC — plus/cross
				_draw_plus(mp, os, outline)
				_draw_plus(mp, s, color)
			3:  # MG — square
				_canvas.draw_rect(Rect2(mp - Vector2(os, os), Vector2(os * 2, os * 2)), outline, true)
				_canvas.draw_rect(Rect2(mp - Vector2(s, s), Vector2(s * 2, s * 2)), color, true)
			4:  # MARKSMAN — triangle up
				_draw_triangle_up(mp, os, outline)
				_draw_triangle_up(mp, s, color)
			5:  # GRENADIER — star (4-point)
				_draw_star(mp, os, outline)
				_draw_star(mp, s, color)
			6:  # MORTAR — hexagon
				_draw_hexagon(mp, os, outline)
				_draw_hexagon(mp, s, color)
			_:  # Unknown — circle fallback
				_canvas.draw_circle(mp, os, outline)
				_canvas.draw_circle(mp, s, color)


func _draw_diamond(center: Vector2, s: float, color: Color) -> void:
	_canvas.draw_colored_polygon(PackedVector2Array([
		center + Vector2(0, -s),
		center + Vector2(s, 0),
		center + Vector2(0, s),
		center + Vector2(-s, 0),
	]), color)


func _draw_plus(center: Vector2, s: float, color: Color) -> void:
	var t: float = maxf(s * 0.45, 1.5)
	_canvas.draw_line(center + Vector2(-s, 0), center + Vector2(s, 0), color, t)
	_canvas.draw_line(center + Vector2(0, -s), center + Vector2(0, s), color, t)


func _draw_triangle_up(center: Vector2, s: float, color: Color) -> void:
	_canvas.draw_colored_polygon(PackedVector2Array([
		center + Vector2(0, -s),
		center + Vector2(s, s * 0.7),
		center + Vector2(-s, s * 0.7),
	]), color)


func _draw_star(center: Vector2, s: float, color: Color) -> void:
	# 4-point star: inner radius = 40% of outer
	var inner: float = s * 0.4
	var pts: PackedVector2Array = PackedVector2Array()
	for j in 8:
		var angle: float = j * PI / 4.0 - PI / 2.0
		var dist: float = s if j % 2 == 0 else inner
		pts.append(center + Vector2(cos(angle), sin(angle)) * dist)
	_canvas.draw_colored_polygon(pts, color)


func _draw_hexagon(center: Vector2, s: float, color: Color) -> void:
	var pts: PackedVector2Array = PackedVector2Array()
	for j in 6:
		var angle: float = j * PI / 3.0 - PI / 6.0
		pts.append(center + Vector2(cos(angle), sin(angle)) * s)
	_canvas.draw_colored_polygon(pts, color)


# ── Squad Bounding Rects ───────────────────────────────────────────

func _draw_squad_groups(positions: PackedVector3Array, states: PackedInt32Array,
		squads: PackedInt32Array, team: int) -> void:
	if squads.is_empty() or positions.is_empty():
		return

	var groups: Dictionary = {}
	for i in positions.size():
		if i >= squads.size():
			break
		var is_dead: bool = (i < states.size() and states[i] == 11)
		if is_dead:
			continue
		var sq: int = squads[i]
		if not groups.has(sq):
			groups[sq] = []
		groups[sq].append(_world_to_map(positions[i]))

	var outline_alpha: float = 0.15
	var min_spread: float = 12.0 if _expanded else 8.0
	for sq_id in groups:
		var pts: Array = groups[sq_id]
		if pts.size() < 2:
			continue

		var min_p: Vector2 = pts[0]
		var max_p: Vector2 = pts[0]
		for p in pts:
			min_p.x = minf(min_p.x, p.x)
			min_p.y = minf(min_p.y, p.y)
			max_p.x = maxf(max_p.x, p.x)
			max_p.y = maxf(max_p.y, p.y)

		# Skip degenerate boxes where units are tightly clustered
		var spread: Vector2 = max_p - min_p
		if spread.x < min_spread and spread.y < min_spread:
			continue

		var pad: float = 2.0
		min_p -= Vector2(pad, pad)
		max_p += Vector2(pad, pad)

		var rect := Rect2(min_p, max_p - min_p)
		var sq_color: Color = _get_squad_color(sq_id, team)
		_canvas.draw_rect(rect, Color(sq_color, outline_alpha), false, 1.0)


# ── Order Arrows ───────────────────────────────────────────────────

const MAX_ARROW_LEN: float = 120.0
const MIN_ARROW_LEN: float = 10.0

func _draw_order_arrows(orders: Array) -> void:
	for order in orders:
		var centroid: Vector2 = order["centroid"]
		var target_pt: Vector2 = order["target"]
		var goal_name: String = order["goal"]

		var dir: Vector2 = target_pt - centroid
		var arrow_len: float = dir.length()
		if arrow_len < MIN_ARROW_LEN:
			continue

		var color: Color = GOAL_COLORS.get(goal_name, GOAL_COLOR_DEFAULT)
		var is_active: bool = goal_name in ACTIVE_GOALS
		var alpha: float = 0.6 if is_active else 0.3
		var line_width: float = 1.5 if is_active else 1.0

		# Cap arrow length
		var draw_target: Vector2 = target_pt
		if arrow_len > MAX_ARROW_LEN:
			draw_target = centroid + dir.normalized() * MAX_ARROW_LEN

		_canvas.draw_line(centroid, draw_target, Color(color, alpha), line_width)

		# Arrowhead at draw endpoint
		var norm_dir: Vector2 = dir.normalized()
		var perp: Vector2 = Vector2(-norm_dir.y, norm_dir.x)
		var arrow_size: float = 5.0 if is_active else 4.0
		var tip: Vector2 = draw_target
		var left: Vector2 = draw_target - norm_dir * arrow_size + perp * arrow_size * 0.4
		var right_pt: Vector2 = draw_target - norm_dir * arrow_size - perp * arrow_size * 0.4
		_canvas.draw_colored_polygon(
			PackedVector2Array([tip, left, right_pt]),
			Color(color, alpha))


# ── Camera Cone ────────────────────────────────────────────────────

func _draw_camera_cone() -> void:
	var cam_mp: Vector2 = _world_to_map(_cam_pos)

	_canvas.draw_circle(cam_mp, 4.0, Color(BattleCommandUI.ACCENT_COLOR, 0.7))
	_canvas.draw_circle(cam_mp, 2.0, BattleCommandUI.ACCENT_COLOR)

	var fwd_2d: Vector2 = Vector2(_cam_fwd.x, _cam_fwd.z)
	if fwd_2d.length_squared() < 0.01:
		return
	fwd_2d = fwd_2d.normalized()

	var cone_len: float = 40.0
	var half_fov: float = deg_to_rad(_cam_fov * 0.5)
	var cone_map_len: float = cone_len / (_world_max.x - _world_min.x) * _map_size.x
	cone_map_len = minf(cone_map_len, 60.0)  # Cap visual size in expanded mode

	var left: Vector2 = fwd_2d.rotated(-half_fov) * cone_map_len
	var right: Vector2 = fwd_2d.rotated(half_fov) * cone_map_len

	var cone_pts: PackedVector2Array = [cam_mp, cam_mp + left, cam_mp + right]
	_canvas.draw_colored_polygon(cone_pts, Color(BattleCommandUI.ACCENT_COLOR, 0.08))
	_canvas.draw_line(cam_mp, cam_mp + left, Color(BattleCommandUI.ACCENT_COLOR, 0.35), 1.0)
	_canvas.draw_line(cam_mp, cam_mp + right, Color(BattleCommandUI.ACCENT_COLOR, 0.35), 1.0)
	_canvas.draw_line(cam_mp + left, cam_mp + right, Color(BattleCommandUI.ACCENT_COLOR, 0.2), 1.0)


func _draw_selected_unit() -> void:
	if not _ui or _ui.selected_unit_id < 0 or not _ui.sim:
		return
	var uid: int = _ui.selected_unit_id
	if not _ui.sim.is_alive(uid):
		return
	var pos: Vector3 = _ui.sim.get_position(uid)
	var mp: Vector2 = _world_to_map(pos)

	_canvas.draw_circle(mp, 5.0, Color(1, 1, 1, 0.3))
	_canvas.draw_arc(mp, 6.0, 0, TAU, 24, Color.WHITE, 1.5)
	_canvas.draw_circle(mp, 2.5, Color.WHITE)


# ── Capture Points ─────────────────────────────────────────────────

func _draw_capture_points() -> void:
	if _capture_positions.is_empty():
		return

	var obj_names: Array = ["A", "B", "C", "D", "E", "F", "G", "H"]
	for i in _capture_positions.size():
		var cp_pos: Vector3 = _capture_positions[i]
		var mp: Vector2 = _world_to_map(cp_pos)
		var owner: int = _capture_owners[i] if i < _capture_owners.size() else -1

		var color: Color
		if owner == 0:
			color = BattleCommandUI.TEAM1_COLOR
		elif owner == 1:
			color = BattleCommandUI.TEAM2_COLOR
		else:
			color = Color(0.6, 0.6, 0.6)

		var ds: float = 5.0 if _expanded else 4.0
		var diamond: PackedVector2Array = [
			mp + Vector2(0, -ds), mp + Vector2(ds, 0),
			mp + Vector2(0, ds), mp + Vector2(-ds, 0),
		]
		_canvas.draw_colored_polygon(diamond, Color(color, 0.6))
		_canvas.draw_polyline(diamond + PackedVector2Array([diamond[0]]),
			Color(color, 0.9), 1.5)

		if _expanded:
			if i < _capture_progress.size():
				var progress: float = _capture_progress[i]
				if progress > 0.01:
					_canvas.draw_arc(mp, ds + 2.5, -PI / 2.0,
						-PI / 2.0 + TAU * progress, 24,
						Color(color, 0.8), 2.0)

			if i < _capture_contested.size() and _capture_contested[i] > 0:
				_canvas.draw_arc(mp, ds + 4.5, 0, TAU, 16,
					Color(1, 0.8, 0.2, 0.5), 1.0)

			var obj_name: String = obj_names[i] if i < obj_names.size() else str(i)
			var label: String = "OBJ " + obj_name
			if i < _capture_progress.size():
				var pct: int = int(_capture_progress[i] * 100.0)
				label += " (%d%%)" % pct
			if i < _capture_contested.size() and _capture_contested[i] > 0:
				label += " !"

			_canvas.draw_string(ThemeDB.fallback_font,
				mp + Vector2(ds + 3, 3), label,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color(color, 0.9))


# ── Squad Labels (expanded mode) ──────────────────────────────────

func _draw_squad_labels() -> void:
	if _show_team1:
		_draw_squad_centroid_labels(_t1_positions, _t1_states, _t1_squads, 0)
	if _show_team2:
		_draw_squad_centroid_labels(_t2_positions, _t2_states, _t2_squads, 1)


func _draw_squad_centroid_labels(positions: PackedVector3Array,
		states: PackedInt32Array, squads: PackedInt32Array, team: int) -> void:
	if squads.is_empty() or positions.is_empty():
		return

	var sums: Dictionary = {}
	var counts: Dictionary = {}
	for i in positions.size():
		if i >= squads.size():
			break
		var is_dead: bool = (i < states.size() and states[i] == 11)
		if is_dead:
			continue
		var sq: int = squads[i]
		if not sums.has(sq):
			sums[sq] = Vector3.ZERO
			counts[sq] = 0
		sums[sq] += positions[i]
		counts[sq] += 1

	for sq_id in sums:
		var cnt: int = counts[sq_id]
		if cnt < 2:
			continue
		var centroid: Vector3 = sums[sq_id] / float(cnt)
		var mp: Vector2 = _world_to_map(centroid)

		var sq_color: Color = _get_squad_color(sq_id, team)
		# Team-local index (0-63) — team 2 raw IDs are 64-127
		var display_id: int = sq_id % 64
		var label: String = "%d" % display_id

		# Position above centroid to avoid overlapping unit dots
		var label_offset: Vector2 = Vector2(-6, -18)

		# Small pill background with squad-colored border
		var pill_size: Vector2 = Vector2(22, 11)
		_canvas.draw_rect(Rect2(mp + label_offset - Vector2(2, 1), pill_size),
			Color(0, 0, 0, 0.55), true)
		_canvas.draw_rect(Rect2(mp + label_offset - Vector2(2, 1), pill_size),
			Color(sq_color, 0.35), false, 1.0)

		_canvas.draw_string(ThemeDB.fallback_font,
			mp + label_offset + Vector2(0, 9), label,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 8,
			Color(sq_color, 0.9))


# ── Legend (expanded mode) ─────────────────────────────────────────

func _draw_legend() -> void:
	var size: Vector2 = _map_size
	var strip_h: float = 52.0
	var y: float = size.y - strip_h

	# Background strip with top border accent
	_canvas.draw_rect(Rect2(Vector2(0, y), Vector2(size.x, strip_h)),
		Color(0, 0, 0, 0.5), true)
	_canvas.draw_line(Vector2(0, y), Vector2(size.x, y),
		Color(BattleCommandUI.ACCENT_COLOR, 0.15), 1.0)

	# === Row 1: Squad color swatches ===
	var row1_y: float = y + 4.0
	var x: float = 4.0

	var t1_active: Array[int] = _get_active_squad_ids(_t1_squads, _t1_states)
	var t2_active: Array[int] = _get_active_squad_ids(_t2_squads, _t2_states)

	# Team 1 swatches (or "hidden" indicator)
	if _show_team1 and not t1_active.is_empty():
		_canvas.draw_string(ThemeDB.fallback_font,
			Vector2(x, row1_y + 10), "T1:", HORIZONTAL_ALIGNMENT_LEFT, -1, 10,
			BattleCommandUI.TEAM1_COLOR)
		x += 24.0
		for sq_id in t1_active:
			var c: Color = _get_squad_color(sq_id, 0)
			_canvas.draw_rect(Rect2(Vector2(x, row1_y), Vector2(12, 12)), c, true)
			_canvas.draw_rect(Rect2(Vector2(x, row1_y), Vector2(12, 12)),
				Color(1, 1, 1, 0.15), false, 1.0)
			x += 14.0
			if x > size.x * 0.38:
				break
		x += 8.0
	elif not _show_team1:
		_canvas.draw_string(ThemeDB.fallback_font,
			Vector2(x, row1_y + 10), "T1: hidden", HORIZONTAL_ALIGNMENT_LEFT,
			-1, 9, Color(0.4, 0.4, 0.4, 0.5))
		x += 70.0

	# Team 2 swatches (or "hidden" indicator)
	if _show_team2 and not t2_active.is_empty():
		_canvas.draw_string(ThemeDB.fallback_font,
			Vector2(x, row1_y + 10), "T2:", HORIZONTAL_ALIGNMENT_LEFT, -1, 10,
			BattleCommandUI.TEAM2_COLOR)
		x += 24.0
		for sq_id in t2_active:
			var c: Color = _get_squad_color(sq_id, 1)
			_canvas.draw_rect(Rect2(Vector2(x, row1_y), Vector2(12, 12)), c, true)
			_canvas.draw_rect(Rect2(Vector2(x, row1_y), Vector2(12, 12)),
				Color(1, 1, 1, 0.15), false, 1.0)
			x += 14.0
			if x > size.x * 0.78:
				break
	elif not _show_team2:
		_canvas.draw_string(ThemeDB.fallback_font,
			Vector2(x, row1_y + 10), "T2: hidden", HORIZONTAL_ALIGNMENT_LEFT,
			-1, 9, Color(0.4, 0.4, 0.4, 0.5))

	# === Row 2: Role icons + keybind hints ===
	var row2_y: float = y + 20.0
	var rx: float = 4.0
	var legend_s: float = 4.5
	var legend_color: Color = Color(0.65, 0.7, 0.75)
	var role_labels: Array[String] = ["Rfl", "Ldr", "Med", "MG", "Snp", "Gre", "Mtr"]

	for ri in 7:
		var lp: Vector2 = Vector2(rx + 6, row2_y + 8)
		match ri:
			0: _canvas.draw_circle(lp, legend_s, legend_color)
			1: _draw_diamond(lp, legend_s * 1.15, legend_color)
			2: _draw_plus(lp, legend_s, legend_color)
			3: _canvas.draw_rect(Rect2(lp - Vector2(legend_s, legend_s),
					Vector2(legend_s * 2, legend_s * 2)), legend_color, true)
			4: _draw_triangle_up(lp, legend_s, legend_color)
			5: _draw_star(lp, legend_s, legend_color)
			6: _draw_hexagon(lp, legend_s, legend_color)

		_canvas.draw_string(ThemeDB.fallback_font,
			Vector2(rx + 14, row2_y + 12), role_labels[ri],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 9, Color(0.5, 0.55, 0.6))
		rx += 42.0

	# Keybind hints (right side of row 2)
	var hint: String = "H:heatmap  A:orders  L:labels  1/2:team  K:close"
	_canvas.draw_string(ThemeDB.fallback_font,
		Vector2(size.x - 6, row2_y + 12), hint,
		HORIZONTAL_ALIGNMENT_RIGHT, int(size.x * 0.5), 8,
		Color(0.35, 0.4, 0.45))


func _get_active_squad_ids(squads: PackedInt32Array, states: PackedInt32Array) -> Array[int]:
	var seen: Dictionary = {}
	for i in squads.size():
		var is_dead: bool = (i < states.size() and states[i] == 11)
		if is_dead:
			continue
		seen[squads[i]] = true
	var result: Array[int] = []
	for sq_id in seen:
		result.append(sq_id)
	result.sort()
	return result


# ── Coordinate Conversion ─────────────────────────────────────────

func _world_to_map(world_pos: Vector3) -> Vector2:
	var range_x: float = _world_max.x - _world_min.x
	var range_z: float = _world_max.y - _world_min.y
	if range_x < 1.0:
		range_x = 300.0
	if range_z < 1.0:
		range_z = 200.0

	var nx: float = (world_pos.x - _world_min.x) / range_x
	var nz: float = (world_pos.z - _world_min.y) / range_z
	return Vector2(
		clampf(nx * _map_size.x, 0, _map_size.x),
		clampf(nz * _map_size.y, 0, _map_size.y))


func _map_to_world(map_pos: Vector2) -> Vector3:
	var range_x: float = _world_max.x - _world_min.x
	var range_z: float = _world_max.y - _world_min.y
	if range_x < 1.0:
		range_x = 300.0
	if range_z < 1.0:
		range_z = 200.0

	var nx: float = clampf(map_pos.x / _map_size.x, 0.0, 1.0)
	var nz: float = clampf(map_pos.y / _map_size.y, 0.0, 1.0)
	return Vector3(
		_world_min.x + nx * range_x,
		0.0,
		_world_min.y + nz * range_z)


# ── Canvas Input (click-to-jump) ──────────────────────────────────

func _on_canvas_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT:
			var local_pos: Vector2 = event.position
			var world_pos: Vector3 = _map_to_world(local_pos)
			if _ui and _ui.camera_script:
				var cs = _ui.camera_script
				if cs.get("_camera_mode") != null and cs._camera_mode == cs.CameraMode.RTS:
					cs._rts_focus.x = world_pos.x
					cs._rts_focus.z = world_pos.z
				else:
					var pos: Vector3 = cs.global_position
					pos.x = world_pos.x
					pos.z = world_pos.z
					cs.global_position = pos


# ── Expanded Mode Input Handler ───────────────────────────────────

func try_handle_expanded_input(event: InputEvent) -> bool:
	if not event is InputEventKey or not event.pressed:
		return false
	# Don't consume modified keys — Ctrl+1 for tabs, Shift+1 for formations
	if event.ctrl_pressed or event.shift_pressed or event.alt_pressed:
		return false
	match event.keycode:
		KEY_H:
			_cycle_heatmap()
			return true
		KEY_A:
			_show_orders = not _show_orders
			_canvas.queue_redraw()
			return true
		KEY_L:
			_show_labels = not _show_labels
			_canvas.queue_redraw()
			return true
		KEY_1:
			_show_team1 = not _show_team1
			_canvas.queue_redraw()
			return true
		KEY_2:
			_show_team2 = not _show_team2
			_canvas.queue_redraw()
			return true
	return false


func _cycle_heatmap() -> void:
	_heatmap_mode = (_heatmap_mode + 1) % (HEATMAP_CHANNELS.size() + 1)
	if _heatmap_mode == 0:
		_heatmap_tex = null
	else:
		_heatmap_timer = HEATMAP_INTERVAL
	_canvas.queue_redraw()


# ── Expand / Collapse ─────────────────────────────────────────────

func toggle_expanded() -> void:
	_expanded = not _expanded
	if _expanded:
		_show_team1 = true
		_show_team2 = true
		_show_orders = true
		_show_labels = true
	else:
		_heatmap_mode = 0
		_heatmap_tex = null
	_apply_layout()
	if _expanded and _terrain_dirty:
		_build_terrain_texture()
	_canvas.queue_redraw()


func _apply_layout() -> void:
	if _expanded:
		var vp_size: Vector2 = get_viewport().get_visible_rect().size
		var exp_w: float = vp_size.x * 0.6
		var world_w: float = _world_max.x - _world_min.x
		var world_h: float = _world_max.y - _world_min.y
		if world_w < 1.0:
			world_w = 300.0
		if world_h < 1.0:
			world_h = 200.0
		var exp_h: float = exp_w * (world_h / world_w)
		if exp_h > vp_size.y * 0.7:
			exp_h = vp_size.y * 0.7
			exp_w = exp_h * (world_w / world_h)

		_map_size = Vector2(exp_w, exp_h)
		custom_minimum_size = _map_size + Vector2(MAP_PAD * 2, MAP_PAD * 2)
		_canvas.custom_minimum_size = _map_size

		anchors_preset = Control.PRESET_CENTER
		offset_left = -exp_w * 0.5 - MAP_PAD
		offset_right = exp_w * 0.5 + MAP_PAD
		offset_top = -exp_h * 0.5 - MAP_PAD
		offset_bottom = exp_h * 0.5 + MAP_PAD
		z_index = 2
	else:
		_map_size = _small_size
		custom_minimum_size = _map_size + Vector2(MAP_PAD * 2, MAP_PAD * 2)
		_canvas.custom_minimum_size = _map_size

		anchors_preset = Control.PRESET_BOTTOM_RIGHT
		offset_right = -10.0
		offset_bottom = -10.0
		offset_left = 0.0
		offset_top = 0.0
		z_index = 0

		if _ui and _ui.strategy and _ui.strategy.visible:
			offset_right = -340.0


# ── Terrain Texture (Hillshade + Contour Lines) ───────────────────

func _build_terrain_texture() -> void:
	if not _ui or not _ui.gpu_map:
		return

	var world_w: float = _world_max.x - _world_min.x
	var world_h: float = _world_max.y - _world_min.y

	var heights: PackedFloat32Array = PackedFloat32Array()
	heights.resize(TERRAIN_W * TERRAIN_H)
	var h_min: float = 1000.0
	var h_max: float = -1000.0

	for gz in TERRAIN_H:
		for gx in TERRAIN_W:
			var wx: float = _world_min.x + (float(gx) + 0.5) / float(TERRAIN_W) * world_w
			var wz: float = _world_min.y + (float(gz) + 0.5) / float(TERRAIN_H) * world_h
			var h: float = _ui.gpu_map.get_terrain_height_m(wx, wz)
			heights[gz * TERRAIN_W + gx] = h
			if h < h_min:
				h_min = h
			if h > h_max:
				h_max = h

	var h_range: float = h_max - h_min
	if h_range < 0.5:
		h_range = 1.0

	var light_dir: Vector3 = Vector3(-1, 1, -1).normalized()

	var img := Image.create(TERRAIN_W, TERRAIN_H, false, Image.FORMAT_RGBA8)
	for gz in TERRAIN_H:
		for gx in TERRAIN_W:
			var idx: int = gz * TERRAIN_W + gx
			var h: float = heights[idx]
			var t: float = clampf((h - h_min) / h_range, 0.0, 1.0)

			var c: Color
			if t < 0.4:
				var lt: float = t / 0.4
				c = Color(0.08, 0.15, 0.06).lerp(Color(0.2, 0.22, 0.1), lt)
			elif t < 0.7:
				var lt: float = (t - 0.4) / 0.3
				c = Color(0.2, 0.22, 0.1).lerp(Color(0.35, 0.28, 0.18), lt)
			else:
				var lt: float = (t - 0.7) / 0.3
				c = Color(0.35, 0.28, 0.18).lerp(Color(0.55, 0.54, 0.52), lt)

			var dx: float = 0.0
			var dz: float = 0.0
			if gx > 0 and gx < TERRAIN_W - 1:
				dx = heights[idx + 1] - heights[idx - 1]
			if gz > 0 and gz < TERRAIN_H - 1:
				dz = heights[(gz + 1) * TERRAIN_W + gx] - heights[(gz - 1) * TERRAIN_W + gx]

			var normal: Vector3 = Vector3(-dx, 2.0, -dz).normalized()
			var shade: float = clampf(normal.dot(light_dir), 0.4, 1.0)
			c = Color(c.r * shade, c.g * shade, c.b * shade)

			var contour_interval: float = 5.0
			var h_level: int = int(floor(h / contour_interval))
			var is_contour: bool = false
			if gx > 0:
				if int(floor(heights[idx - 1] / contour_interval)) != h_level:
					is_contour = true
			if not is_contour and gz > 0:
				if int(floor(heights[(gz - 1) * TERRAIN_W + gx] / contour_interval)) != h_level:
					is_contour = true
			if is_contour:
				c = Color(c.r * 0.7, c.g * 0.7, c.b * 0.7)

			img.set_pixel(gx, gz, Color(c.r, c.g, c.b, 0.65))

	if _terrain_tex == null:
		_terrain_tex = ImageTexture.create_from_image(img)
	else:
		_terrain_tex.update(img)
	_terrain_dirty = false


## Call when terrain is destroyed to rebuild height texture on next expand.
func invalidate_terrain() -> void:
	_terrain_dirty = true
