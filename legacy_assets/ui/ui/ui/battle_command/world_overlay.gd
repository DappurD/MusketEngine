extends Control
## World overlay — projects tactical data (health bars, engagement lines,
## squad labels, state tags) from 3D world space onto the 2D viewport.
##
## Three modes: OFF → BARS → FULL (cycle with V key).
## All drawing happens in _draw() using cam.unproject_position().

var _ui: BattleCommandUI

enum OverlayMode { OFF, BARS, FULL }
var _mode: OverlayMode = OverlayMode.BARS

# Cached projection data (rebuilt every COLLECT_INTERVAL)
var _collect_timer: float = 0.0
const COLLECT_INTERVAL: float = 0.1
const MAX_DRAW: int = 250  # Performance cap

# Per-unit draw data (parallel arrays for perf)
var _draw_count: int = 0
var _draw_screen: PackedVector2Array = PackedVector2Array()
var _draw_hp: PackedFloat32Array = PackedFloat32Array()
var _draw_morale: PackedFloat32Array = PackedFloat32Array()
var _draw_team: PackedInt32Array = PackedInt32Array()
var _draw_state: PackedInt32Array = PackedInt32Array()
var _draw_uid: PackedInt32Array = PackedInt32Array()
var _draw_squad: PackedInt32Array = PackedInt32Array()
var _draw_dist: PackedFloat32Array = PackedFloat32Array()

# Squad centroid draw data
var _squad_count: int = 0
var _squad_screen: PackedVector2Array = PackedVector2Array()
var _squad_id: PackedInt32Array = PackedInt32Array()
var _squad_team: PackedInt32Array = PackedInt32Array()
var _squad_alive: PackedInt32Array = PackedInt32Array()
var _squad_label: PackedStringArray = PackedStringArray()

# Engagement line data (selected unit only)
var _engage_from: Vector2 = Vector2.ZERO
var _engage_to: Vector2 = Vector2.ZERO
var _engage_valid: bool = false

# Visual constants
const BAR_WIDTH: float = 32.0
const BAR_HEIGHT: float = 3.0
const BAR_GAP: float = 1.5
const BAR_OFFSET_Y: float = -14.0  # Above unit head
const DETAIL_DIST: float = 40.0     # Full detail distance
const BASIC_DIST: float = 150.0     # Basic bars distance
const LABEL_FONT_SIZE: int = 9
const SQUAD_FONT_SIZE: int = 11

# Suppression data
var _draw_suppress: PackedFloat32Array = PackedFloat32Array()


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	name = "WorldOverlay"
	anchors_preset = Control.PRESET_FULL_RECT
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	z_index = -1  # Behind all panels

	# Pre-allocate arrays
	_draw_screen.resize(MAX_DRAW)
	_draw_hp.resize(MAX_DRAW)
	_draw_morale.resize(MAX_DRAW)
	_draw_team.resize(MAX_DRAW)
	_draw_state.resize(MAX_DRAW)
	_draw_uid.resize(MAX_DRAW)
	_draw_squad.resize(MAX_DRAW)
	_draw_dist.resize(MAX_DRAW)
	_draw_suppress.resize(MAX_DRAW)

	_squad_screen.resize(128)
	_squad_id.resize(128)
	_squad_team.resize(128)
	_squad_alive.resize(128)
	_squad_label.resize(128)


func cycle_mode() -> void:
	_mode = (_mode + 1) % 3 as OverlayMode
	visible = (_mode != OverlayMode.OFF)
	queue_redraw()
	var mode_names: Array[String] = ["OFF", "BARS", "FULL"]
	print("[WorldOverlay] Mode: %s" % mode_names[_mode])


func get_mode_name() -> String:
	match _mode:
		OverlayMode.OFF: return "OFF"
		OverlayMode.BARS: return "BARS"
		OverlayMode.FULL: return "FULL"
	return "?"


func update_data(delta: float) -> void:
	if _mode == OverlayMode.OFF:
		return

	_collect_timer += delta
	if _collect_timer < COLLECT_INTERVAL:
		return
	_collect_timer = 0.0

	_collect_unit_data()
	if _mode == OverlayMode.FULL:
		_collect_squad_data()
		_collect_engagement_data()
	else:
		_squad_count = 0
		_engage_valid = false

	queue_redraw()


func _collect_unit_data() -> void:
	if not _ui or not _ui.sim or not _ui.cam:
		_draw_count = 0
		return

	var sim: SimulationServer = _ui.sim
	var cam: Camera3D = _ui.cam
	var cam_pos: Vector3 = cam.global_position
	var unit_count: int = sim.get_unit_count()
	var max_dist_sq: float = BASIC_DIST * BASIC_DIST
	var count: int = 0

	for uid in unit_count:
		if count >= MAX_DRAW:
			break
		if not sim.is_alive(uid):
			continue

		var pos: Vector3 = sim.get_position(uid)
		var dx: float = pos.x - cam_pos.x
		var dy: float = pos.y - cam_pos.y
		var dz: float = pos.z - cam_pos.z
		var dist_sq: float = dx * dx + dy * dy + dz * dz

		if dist_sq > max_dist_sq:
			continue

		# Behind camera check
		var head_pos: Vector3 = pos + Vector3(0, 1.6, 0)
		if cam.is_position_behind(head_pos):
			continue

		var screen_pos: Vector2 = cam.unproject_position(head_pos)
		# Clip to viewport bounds (with margin)
		var vp_size: Vector2 = get_viewport_rect().size
		if screen_pos.x < -50 or screen_pos.x > vp_size.x + 50:
			continue
		if screen_pos.y < -50 or screen_pos.y > vp_size.y + 50:
			continue

		_draw_screen[count] = screen_pos
		_draw_hp[count] = sim.get_health(uid)
		_draw_morale[count] = sim.get_morale(uid)
		_draw_team[count] = sim.get_team(uid)
		_draw_state[count] = sim.get_state(uid)
		_draw_uid[count] = uid
		_draw_squad[count] = sim.get_squad_id(uid)
		_draw_dist[count] = sqrt(dist_sq)
		_draw_suppress[count] = sim.get_suppression(uid)
		count += 1

	_draw_count = count


func _collect_squad_data() -> void:
	if not _ui or not _ui.sim or not _ui.cam:
		_squad_count = 0
		return

	var sim: SimulationServer = _ui.sim
	var cam: Camera3D = _ui.cam
	var unit_count: int = sim.get_unit_count()

	# Accumulate squad centroids
	var sq_pos: Dictionary = {}   # squad_id -> Vector3 sum
	var sq_cnt: Dictionary = {}   # squad_id -> alive count
	var sq_team: Dictionary = {}  # squad_id -> team

	for uid in unit_count:
		if not sim.is_alive(uid):
			continue
		var sq: int = sim.get_squad_id(uid)
		var pos: Vector3 = sim.get_position(uid)
		var team: int = sim.get_team(uid)
		if sq_pos.has(sq):
			sq_pos[sq] += pos
			sq_cnt[sq] += 1
		else:
			sq_pos[sq] = pos
			sq_cnt[sq] = 1
			sq_team[sq] = team

	var count: int = 0
	for sq_id in sq_pos:
		if count >= 128:
			break
		var centroid: Vector3 = sq_pos[sq_id] / float(sq_cnt[sq_id])
		var label_pos: Vector3 = centroid + Vector3(0, 3.0, 0)  # Float above units

		if cam.is_position_behind(label_pos):
			continue

		var screen: Vector2 = cam.unproject_position(label_pos)
		var vp_size: Vector2 = get_viewport_rect().size
		if screen.x < -50 or screen.x > vp_size.x + 50:
			continue
		if screen.y < -100 or screen.y > vp_size.y + 50:
			continue

		_squad_screen[count] = screen
		_squad_id[count] = sq_id
		_squad_team[count] = sq_team[sq_id]
		_squad_alive[count] = sq_cnt[sq_id]
		_squad_label[count] = "S%d [%d]" % [sq_id, sq_cnt[sq_id]]
		count += 1

	_squad_count = count


func _collect_engagement_data() -> void:
	_engage_valid = false
	if not _ui or not _ui.sim or not _ui.cam:
		return

	var sel: int = _ui.selected_unit_id
	if sel < 0:
		return

	var sim: SimulationServer = _ui.sim
	var cam: Camera3D = _ui.cam

	if sel >= sim.get_unit_count() or not sim.is_alive(sel):
		return

	var target: int = sim.get_target(sel) if sim.has_method("get_target") else -1
	if target < 0 or target >= sim.get_unit_count() or not sim.is_alive(target):
		return

	var from_pos: Vector3 = sim.get_position(sel) + Vector3(0, 1.0, 0)
	var to_pos: Vector3 = sim.get_position(target) + Vector3(0, 1.0, 0)

	if cam.is_position_behind(from_pos) and cam.is_position_behind(to_pos):
		return

	_engage_from = cam.unproject_position(from_pos)
	_engage_to = cam.unproject_position(to_pos)
	_engage_valid = true


func _draw() -> void:
	if _mode == OverlayMode.OFF or _draw_count == 0:
		return

	var font: Font = ThemeDB.fallback_font
	var selected: int = _ui.selected_unit_id if _ui else -1

	# Draw unit bars
	for i in _draw_count:
		var screen: Vector2 = _draw_screen[i]
		var hp: float = _draw_hp[i]
		var dist: float = _draw_dist[i]
		var team: int = _draw_team[i]
		var uid: int = _draw_uid[i]
		var team_color: Color = BattleCommandUI.TEAM1_COLOR if team == 0 else BattleCommandUI.TEAM2_COLOR
		var is_selected: bool = (uid == selected)

		# Scale bars by distance (smaller when far)
		var scale: float = clampf(1.0 - (dist - 10.0) / BASIC_DIST, 0.3, 1.0)
		var bw: float = BAR_WIDTH * scale
		var bh: float = BAR_HEIGHT * scale

		var bar_pos: Vector2 = screen + Vector2(-bw * 0.5, BAR_OFFSET_Y * scale)

		# HP bar background
		draw_rect(Rect2(bar_pos, Vector2(bw, bh)), Color(0.1, 0.1, 0.1, 0.6))
		# HP bar fill
		var hp_ratio: float = clampf(hp, 0.0, 1.0)
		var hp_color: Color = _hp_color(hp_ratio)
		draw_rect(Rect2(bar_pos, Vector2(bw * hp_ratio, bh)), hp_color)

		# Selection highlight ring
		if is_selected:
			draw_rect(Rect2(bar_pos - Vector2(1, 1), Vector2(bw + 2, bh + 2)), BattleCommandUI.ACCENT_COLOR, false, 1.5)

		# Suppression flash — orange border flash when suppressed (all modes)
		var suppress: float = _draw_suppress[i]
		if suppress > 0.3:
			var flash_alpha: float = clampf((suppress - 0.3) * 1.4, 0.0, 0.8)
			var flash_rect: Rect2 = Rect2(bar_pos - Vector2(2, 2), Vector2(bw + 4, bh + 4))
			draw_rect(flash_rect, Color(1.0, 0.5, 0.1, flash_alpha), false, 1.5)

		# Detailed info when close or in FULL mode
		if _mode == OverlayMode.FULL and dist < DETAIL_DIST:
			# Morale bar (below HP)
			var morale_pos: Vector2 = bar_pos + Vector2(0, bh + BAR_GAP)
			var morale: float = clampf(_draw_morale[i], 0.0, 1.0)
			draw_rect(Rect2(morale_pos, Vector2(bw, bh * 0.7)), Color(0.1, 0.1, 0.1, 0.5))
			var morale_color: Color = Color(0.3, 0.5, 1.0, 0.8)
			if morale < 0.3:
				morale_color = Color(1.0, 0.2, 0.2, 0.9)  # Critical morale = red
			elif morale < 0.6:
				morale_color = Color(1.0, 0.7, 0.2, 0.85)  # Low morale = orange
			draw_rect(Rect2(morale_pos, Vector2(bw * morale, bh * 0.7)), morale_color)

			# Suppression bar (below morale, only when suppressed)
			if suppress > 0.05:
				var sup_pos: Vector2 = morale_pos + Vector2(0, bh * 0.7 + BAR_GAP)
				draw_rect(Rect2(sup_pos, Vector2(bw, bh * 0.5)), Color(0.1, 0.1, 0.1, 0.4))
				draw_rect(Rect2(sup_pos, Vector2(bw * clampf(suppress, 0.0, 1.0), bh * 0.5)), Color(1.0, 0.5, 0.1, 0.75))

			# State label
			var state: int = _draw_state[i]
			var state_name: String = BattleCommandUI.STATE_NAMES.get(state, "?")
			var state_color: Color = BattleCommandUI.STATE_COLORS.get(state, Color.WHITE)
			var label_y_offset: float = bh + BAR_GAP + bh * 0.7 + 2
			if suppress > 0.05:
				label_y_offset += bh * 0.5 + BAR_GAP  # Shift down when suppress bar shown
			var label_pos: Vector2 = bar_pos + Vector2(0, label_y_offset)
			draw_string(font, label_pos + Vector2(0, LABEL_FONT_SIZE),
				state_name, HORIZONTAL_ALIGNMENT_LEFT, int(bw),
				LABEL_FONT_SIZE, Color(state_color, 0.85))

			# Team color dot
			draw_circle(screen + Vector2(0, BAR_OFFSET_Y * scale - 4), 2.5 * scale, team_color)

	# Draw squad labels (FULL mode only)
	if _mode == OverlayMode.FULL:
		for i in _squad_count:
			var screen: Vector2 = _squad_screen[i]
			var team: int = _squad_team[i]
			var label: String = _squad_label[i]
			var team_color: Color = BattleCommandUI.TEAM1_COLOR if team == 0 else BattleCommandUI.TEAM2_COLOR

			# Background pill
			var text_w: float = font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, SQUAD_FONT_SIZE).x
			var pill_rect: Rect2 = Rect2(
				screen.x - text_w * 0.5 - 4, screen.y - SQUAD_FONT_SIZE - 2,
				text_w + 8, SQUAD_FONT_SIZE + 6)
			draw_rect(pill_rect, Color(0.04, 0.05, 0.07, 0.75), true)
			draw_rect(pill_rect, Color(team_color, 0.5), false, 1.0)
			draw_string(font, Vector2(screen.x - text_w * 0.5, screen.y),
				label, HORIZONTAL_ALIGNMENT_LEFT, -1, SQUAD_FONT_SIZE, team_color)

	# Draw engagement line (selected unit → target)
	if _engage_valid:
		var line_color: Color = Color(1.0, 0.3, 0.2, 0.6)
		draw_line(_engage_from, _engage_to, line_color, 1.5, true)
		# Arrowhead at target end
		var dir: Vector2 = (_engage_to - _engage_from).normalized()
		var perp: Vector2 = Vector2(-dir.y, dir.x)
		var arrow_size: float = 8.0
		var tip: Vector2 = _engage_to
		draw_line(tip, tip - dir * arrow_size + perp * arrow_size * 0.4, line_color, 1.5)
		draw_line(tip, tip - dir * arrow_size - perp * arrow_size * 0.4, line_color, 1.5)


static func _hp_color(ratio: float) -> Color:
	if ratio > 0.7:
		return Color(0.2, 0.85, 0.3, 0.85)
	elif ratio > 0.3:
		return Color(1.0, 0.75, 0.2, 0.85)
	else:
		return Color(1.0, 0.2, 0.2, 0.85)
