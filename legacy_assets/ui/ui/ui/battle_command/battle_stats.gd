extends PanelContainer
## Battle stats dashboard — live charts of casualties, morale, and force ratio over time.

var _ui: BattleCommandUI
var _record_timer: float = 0.0
const RECORD_INTERVAL: float = 1.0
const MAX_SAMPLES: int = 300  # 5 minutes at 1s intervals

# Time series data
var _time_points: PackedFloat32Array = PackedFloat32Array()
var _alive_t1: PackedFloat32Array = PackedFloat32Array()
var _alive_t2: PackedFloat32Array = PackedFloat32Array()
var _morale_t1: PackedFloat32Array = PackedFloat32Array()
var _morale_t2: PackedFloat32Array = PackedFloat32Array()
var _force_ratio: PackedFloat32Array = PackedFloat32Array()

var _chart_canvas: Control
var _summary_label: Label

# Toggle panel
var _toggle_vbox: HBoxContainer
var _toggles: Dictionary = {}  # name -> CheckButton


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


## Clear time series data (called on sim restart via BattleCommandUI)
func clear_data() -> void:
	_time_points.clear()
	_alive_t1.clear()
	_alive_t2.clear()
	_morale_t1.clear()
	_morale_t2.clear()
	_force_ratio.clear()
	_record_timer = 0.0
	if _summary_label:
		_summary_label.text = "No data yet — start simulation with [U]"
	if _chart_canvas:
		_chart_canvas.queue_redraw()


func _build() -> void:
	# Dim overlay handled globally by BattleCommandUI._modal_dim
	anchors_preset = Control.PRESET_CENTER
	custom_minimum_size = Vector2(620, 460)

	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.06, 0.07, 0.09, 0.98)
	sb.border_width_top = 2
	sb.border_width_bottom = 1
	sb.border_width_left = 1
	sb.border_width_right = 1
	sb.border_color = Color(0.2, 0.25, 0.32)
	sb.corner_radius_top_left = 10
	sb.corner_radius_top_right = 10
	sb.corner_radius_bottom_left = 10
	sb.corner_radius_bottom_right = 10
	sb.content_margin_left = 20.0
	sb.content_margin_right = 20.0
	sb.content_margin_top = 16.0
	sb.content_margin_bottom = 16.0
	sb.shadow_color = Color(0, 0, 0, 0.5)
	sb.shadow_size = 12
	sb.shadow_offset = Vector2(0, 4)
	add_theme_stylebox_override("panel", sb)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 8)
	add_child(vbox)

	# Title
	var title := Label.new()
	title.text = "BATTLE STATISTICS"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 18)
	title.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(title)

	var accent_line := ColorRect.new()
	accent_line.custom_minimum_size = Vector2(0, 2)
	accent_line.color = Color(BattleCommandUI.ACCENT_COLOR, 0.3)
	accent_line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(accent_line)

	# Chart area
	_chart_canvas = Control.new()
	_chart_canvas.custom_minimum_size = Vector2(580, 250)
	_chart_canvas.draw.connect(_on_chart_draw)
	vbox.add_child(_chart_canvas)

	# Summary
	_summary_label = Label.new()
	_summary_label.text = "No data yet — start simulation with [U]"
	_summary_label.add_theme_font_size_override("font_size", 11)
	_summary_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	_summary_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(_summary_label)

	var divider := ColorRect.new()
	divider.custom_minimum_size = Vector2(0, 1)
	divider.color = Color(0.15, 0.17, 0.2)
	divider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(divider)

	# A/B Toggle panel
	var toggle_header := Label.new()
	toggle_header.text = "FEATURE TOGGLES"
	toggle_header.add_theme_font_size_override("font_size", 12)
	toggle_header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(toggle_header)

	_toggle_vbox = HBoxContainer.new()
	_toggle_vbox.add_theme_constant_override("separation", 16)
	vbox.add_child(_toggle_vbox)

	_add_toggle("Context Steering", "context_steering")
	_add_toggle("ORCA Collision", "orca")


func _add_toggle(label_text: String, key: String) -> void:
	var cb := CheckButton.new()
	cb.text = label_text
	cb.button_pressed = true
	cb.add_theme_font_size_override("font_size", 11)
	cb.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	cb.toggled.connect(func(pressed: bool): _on_toggle(key, pressed))
	_toggle_vbox.add_child(cb)
	_toggles[key] = cb


func _on_toggle(key: String, pressed: bool) -> void:
	if not _ui or not _ui.sim:
		return
	match key:
		"context_steering":
			if _ui.sim.has_method("set_context_steering_enabled"):
				_ui.sim.set_context_steering_enabled(pressed)
		"orca":
			if _ui.sim.has_method("set_orca_enabled"):
				_ui.sim.set_orca_enabled(pressed)
	print("[BattleStats] %s: %s" % [key, "ON" if pressed else "OFF"])


func update_data(delta: float) -> void:
	_record_timer += delta
	if _record_timer < RECORD_INTERVAL:
		return
	_record_timer = 0.0

	if not _ui or not _ui.sim:
		return

	var sim: SimulationServer = _ui.sim
	var total: int = sim.get_unit_count() / 2
	if total <= 0:
		return

	var a1: int = sim.get_alive_count_for_team(1)
	var a2: int = sim.get_alive_count_for_team(2)
	_time_points.append(sim.get_game_time())
	_alive_t1.append(float(a1))
	_alive_t2.append(float(a2))
	# Use cached morale from BattleCommandUI (C++ doesn't expose avg_morale keys)
	_morale_t1.append(_ui.cached_morale_t1)
	_morale_t2.append(_ui.cached_morale_t2)
	_force_ratio.append(float(a1) / float(a1 + a2) if (a1 + a2) > 0 else 0.5)

	# Trim to max samples
	if _time_points.size() > MAX_SAMPLES:
		_time_points = _time_points.slice(1)
		_alive_t1 = _alive_t1.slice(1)
		_alive_t2 = _alive_t2.slice(1)
		_morale_t1 = _morale_t1.slice(1)
		_morale_t2 = _morale_t2.slice(1)
		_force_ratio = _force_ratio.slice(1)

	# Update summary
	_summary_label.text = "T1: %d alive (%.0f%% morale) | T2: %d alive (%.0f%% morale) | Force: %.1f:%.1f" % [
		a1, _ui.cached_morale_t1 * 100,
		a2, _ui.cached_morale_t2 * 100,
		float(a1) / float(total) * 100, float(a2) / float(total) * 100]

	_chart_canvas.queue_redraw()


func _on_chart_draw() -> void:
	var size: Vector2 = _chart_canvas.size
	if size.x < 10 or size.y < 10:
		return

	# Background
	_chart_canvas.draw_rect(Rect2(Vector2.ZERO, size), Color(0.04, 0.05, 0.07), true)

	# Grid
	for i in 5:
		var y: float = size.y * float(i) / 4.0
		_chart_canvas.draw_line(Vector2(0, y), Vector2(size.x, y), Color(0.1, 0.11, 0.13), 1.0)

	if _time_points.size() < 2:
		_chart_canvas.draw_string(ThemeDB.fallback_font, Vector2(size.x * 0.3, size.y * 0.5),
			"Waiting for data...", HORIZONTAL_ALIGNMENT_LEFT, -1, 12, BattleCommandUI.TEXT_SECONDARY)
		return

	# Find max for alive series
	var max_alive: float = 1.0
	for v in _alive_t1:
		if v > max_alive:
			max_alive = v
	for v in _alive_t2:
		if v > max_alive:
			max_alive = v

	var count: int = _time_points.size()

	# Draw alive curves
	_draw_line_series(_alive_t1, max_alive, BattleCommandUI.TEAM1_COLOR, count, size)
	_draw_line_series(_alive_t2, max_alive, BattleCommandUI.TEAM2_COLOR, count, size)

	# Draw morale curves (scaled to same chart, 0-1 mapped to 0-max_alive)
	var morale_scale: float = max_alive
	_draw_line_series(_morale_t1, 1.0 / morale_scale * max_alive, Color(0.15, 0.4, 0.8, 0.5), count, size)
	_draw_line_series(_morale_t2, 1.0 / morale_scale * max_alive, Color(0.7, 0.15, 0.15, 0.5), count, size)

	# Legend
	_chart_canvas.draw_string(ThemeDB.fallback_font, Vector2(4, 14),
		"T1 Alive", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, BattleCommandUI.TEAM1_COLOR)
	_chart_canvas.draw_string(ThemeDB.fallback_font, Vector2(80, 14),
		"T2 Alive", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, BattleCommandUI.TEAM2_COLOR)
	_chart_canvas.draw_string(ThemeDB.fallback_font, Vector2(160, 14),
		"T1 Morale", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color(0.15, 0.4, 0.8, 0.7))
	_chart_canvas.draw_string(ThemeDB.fallback_font, Vector2(250, 14),
		"T2 Morale", HORIZONTAL_ALIGNMENT_LEFT, -1, 10, Color(0.7, 0.15, 0.15, 0.7))


func _draw_line_series(data: PackedFloat32Array, max_val: float, color: Color, count: int, size: Vector2) -> void:
	if count < 2 or max_val < 0.001:
		return

	var points: PackedVector2Array = PackedVector2Array()
	for i in count:
		var x: float = float(i) / float(count - 1) * size.x
		var y: float = size.y - (data[i] / max_val * size.y)
		points.append(Vector2(x, clampf(y, 0, size.y)))

	if points.size() >= 2:
		_chart_canvas.draw_polyline(points, color, 1.5, true)
