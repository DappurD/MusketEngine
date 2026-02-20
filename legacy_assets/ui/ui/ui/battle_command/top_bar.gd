extends PanelContainer
## Top bar — always-visible team summary, game clock, speed, FPS, and tick time.
## Layout: [T1 badge + bar + count + morale] [clock + speed + FPS] [T2 mirrored]

var _ui: BattleCommandUI

# Team 1 (left)
var _t1_badge: Label
var _t1_bar: ColorRect
var _t1_bar_bg: ColorRect
var _t1_label: Label
var _t1_morale: Label
var _t1_kills: Label

# Center
var _clock_label: Label
var _speed_label: Label
var _fps_label: Label
var _map_mode_label: Label
var _overlay_label: Label
var _follow_label: Label

# Team 2 (right)
var _t2_badge: Label
var _t2_bar: ColorRect
var _t2_bar_bg: ColorRect
var _t2_label: Label
var _t2_morale: Label
var _t2_kills: Label

var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.1
const BAR_WIDTH: float = 100.0
const BAR_HEIGHT: float = 10.0

# Kill tracking — initial alive per team, set once on first update
var _initial_t1: int = -1
var _initial_t2: int = -1


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	anchors_preset = Control.PRESET_TOP_WIDE
	custom_minimum_size = Vector2(0, 36)
	size_flags_horizontal = Control.SIZE_EXPAND_FILL

	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.04, 0.045, 0.06, 0.94)
	sb.border_width_bottom = 1
	sb.border_color = Color(0.12, 0.14, 0.18)
	sb.content_margin_left = 16.0
	sb.content_margin_right = 16.0
	sb.content_margin_top = 4.0
	sb.content_margin_bottom = 4.0
	add_theme_stylebox_override("panel", sb)

	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 0)
	hbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_child(hbox)

	# ── Team 1 (left) ──
	var t1_box := HBoxContainer.new()
	t1_box.add_theme_constant_override("separation", 8)
	t1_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	t1_box.alignment = BoxContainer.ALIGNMENT_BEGIN
	hbox.add_child(t1_box)

	_t1_badge = Label.new()
	_t1_badge.text = "TEAM 1"
	_t1_badge.add_theme_font_size_override("font_size", 10)
	_t1_badge.add_theme_color_override("font_color", BattleCommandUI.TEAM1_COLOR)
	t1_box.add_child(_t1_badge)

	var bar1 := _make_bar_container()
	t1_box.add_child(bar1)
	_t1_bar_bg = bar1.get_child(0)
	_t1_bar = bar1.get_child(1)
	_t1_bar.color = BattleCommandUI.TEAM1_COLOR

	_t1_label = Label.new()
	_t1_label.text = "0/0"
	_t1_label.add_theme_font_size_override("font_size", 13)
	_t1_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	t1_box.add_child(_t1_label)

	_t1_morale = Label.new()
	_t1_morale.text = ""
	_t1_morale.add_theme_font_size_override("font_size", 11)
	_t1_morale.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	t1_box.add_child(_t1_morale)

	_t1_kills = Label.new()
	_t1_kills.text = ""
	_t1_kills.add_theme_font_size_override("font_size", 10)
	_t1_kills.add_theme_color_override("font_color", BattleCommandUI.TEAM1_COLOR)
	t1_box.add_child(_t1_kills)

	# ── Center ──
	var center := HBoxContainer.new()
	center.add_theme_constant_override("separation", 16)
	center.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	center.alignment = BoxContainer.ALIGNMENT_CENTER
	hbox.add_child(center)

	_clock_label = Label.new()
	_clock_label.text = "0:00"
	_clock_label.add_theme_font_size_override("font_size", 16)
	_clock_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	center.add_child(_clock_label)

	_speed_label = Label.new()
	_speed_label.text = ""
	_speed_label.add_theme_font_size_override("font_size", 12)
	_speed_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	center.add_child(_speed_label)

	_map_mode_label = Label.new()
	_map_mode_label.text = ""
	_map_mode_label.add_theme_font_size_override("font_size", 10)
	_map_mode_label.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	center.add_child(_map_mode_label)

	_overlay_label = Label.new()
	_overlay_label.text = ""
	_overlay_label.add_theme_font_size_override("font_size", 10)
	_overlay_label.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	center.add_child(_overlay_label)

	_follow_label = Label.new()
	_follow_label.text = ""
	_follow_label.add_theme_font_size_override("font_size", 10)
	_follow_label.add_theme_color_override("font_color", BattleCommandUI.WARNING_COLOR)
	center.add_child(_follow_label)

	_fps_label = Label.new()
	_fps_label.text = "-- FPS"
	_fps_label.add_theme_font_size_override("font_size", 11)
	_fps_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	center.add_child(_fps_label)

	# ── Team 2 (right) ──
	var t2_box := HBoxContainer.new()
	t2_box.add_theme_constant_override("separation", 8)
	t2_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	t2_box.alignment = BoxContainer.ALIGNMENT_END
	hbox.add_child(t2_box)

	_t2_kills = Label.new()
	_t2_kills.text = ""
	_t2_kills.add_theme_font_size_override("font_size", 10)
	_t2_kills.add_theme_color_override("font_color", BattleCommandUI.TEAM2_COLOR)
	t2_box.add_child(_t2_kills)

	_t2_morale = Label.new()
	_t2_morale.text = ""
	_t2_morale.add_theme_font_size_override("font_size", 11)
	_t2_morale.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	t2_box.add_child(_t2_morale)

	_t2_label = Label.new()
	_t2_label.text = "0/0"
	_t2_label.add_theme_font_size_override("font_size", 13)
	_t2_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	t2_box.add_child(_t2_label)

	var bar2 := _make_bar_container()
	t2_box.add_child(bar2)
	_t2_bar_bg = bar2.get_child(0)
	_t2_bar = bar2.get_child(1)
	_t2_bar.color = BattleCommandUI.TEAM2_COLOR

	_t2_badge = Label.new()
	_t2_badge.text = "TEAM 2"
	_t2_badge.add_theme_font_size_override("font_size", 10)
	_t2_badge.add_theme_color_override("font_color", BattleCommandUI.TEAM2_COLOR)
	t2_box.add_child(_t2_badge)


func _make_bar_container() -> Control:
	var container := Control.new()
	container.custom_minimum_size = Vector2(BAR_WIDTH, BAR_HEIGHT)
	container.size_flags_vertical = Control.SIZE_SHRINK_CENTER

	var bg := ColorRect.new()
	bg.color = Color(0.1, 0.11, 0.13)
	bg.position = Vector2.ZERO
	bg.size = Vector2(BAR_WIDTH, BAR_HEIGHT)
	container.add_child(bg)

	var fill := ColorRect.new()
	fill.color = Color.WHITE
	fill.position = Vector2.ZERO
	fill.size = Vector2(BAR_WIDTH, BAR_HEIGHT)
	container.add_child(fill)

	return container


func update_data(delta: float) -> void:
	_update_timer += delta
	if _update_timer < UPDATE_INTERVAL:
		return
	_update_timer = 0.0

	# Pre-sim state: show FPS and "Press U to start" when no sim
	if not _ui or not _ui.sim:
		_initial_t1 = -1  # Reset kill tracking for next battle
		_initial_t2 = -1
		_clock_label.text = "--:--"
		_speed_label.text = "Press [U]"
		_speed_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
		_t1_label.text = ""
		_t2_label.text = ""
		_t1_morale.text = ""
		_t2_morale.text = ""
		_t1_kills.text = ""
		_t2_kills.text = ""
		_t1_bar.size.x = 0
		_t2_bar.size.x = 0
		var fps: int = Engine.get_frames_per_second()
		_fps_label.text = "%d FPS" % fps
		_fps_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
		_map_mode_label.text = ""
		_overlay_label.text = ""
		_follow_label.text = ""
		return

	var sim: SimulationServer = _ui.sim
	var total: int = sim.get_unit_count() / 2
	if total <= 0:
		total = 1

	var alive1: int = sim.get_alive_count_for_team(1)
	var alive2: int = sim.get_alive_count_for_team(2)

	# Capture initial alive per team (supports asymmetric battles)
	if _initial_t1 < 0:
		_initial_t1 = alive1
		_initial_t2 = alive2

	var ratio1: float = float(alive1) / float(total)
	_t1_label.text = "%d/%d" % [alive1, _initial_t1]
	_t1_bar.size.x = BAR_WIDTH * ratio1
	_t1_bar.color = BattleCommandUI.ratio_color(ratio1)

	var ratio2: float = float(alive2) / float(total)
	_t2_label.text = "%d/%d" % [alive2, _initial_t2]
	_t2_bar.size.x = BAR_WIDTH * ratio2
	_t2_bar.color = BattleCommandUI.ratio_color(ratio2)

	# Kill counters: Team 1's kills = Team 2's losses, and vice versa
	var t1_kills: int = _initial_t2 - alive2  # T1 killed T2 units
	var t2_kills: int = _initial_t1 - alive1  # T2 killed T1 units
	if t1_kills > 0:
		_t1_kills.text = "%d kills" % t1_kills
	else:
		_t1_kills.text = ""
	if t2_kills > 0:
		_t2_kills.text = "%d kills" % t2_kills
	else:
		_t2_kills.text = ""

	# Morale from cached per-unit computation (C++ doesn't expose avg_morale keys)
	var m1: float = _ui.cached_morale_t1
	var m2: float = _ui.cached_morale_t2
	_t1_morale.text = "%d%% morale" % int(m1 * 100)
	_t1_morale.add_theme_color_override("font_color", BattleCommandUI.ratio_color(m1))
	_t2_morale.text = "%d%% morale" % int(m2 * 100)
	_t2_morale.add_theme_color_override("font_color", BattleCommandUI.ratio_color(m2))

	_clock_label.text = BattleCommandUI.format_time(sim.get_game_time())

	if _ui.camera_script:
		if _ui.camera_script._sim_paused:
			_speed_label.text = "PAUSED"
			_speed_label.add_theme_color_override("font_color", BattleCommandUI.CRITICAL_COLOR)
		else:
			var scale: float = _ui.camera_script._sim_time_scale
			_speed_label.text = "%.1fx" % scale
			if absf(scale - 1.0) < 0.01:
				_speed_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
			else:
				_speed_label.add_theme_color_override("font_color", BattleCommandUI.WARNING_COLOR)

	var fps: int = Engine.get_frames_per_second()
	var tick_ms: float = sim.get_last_tick_ms()
	_fps_label.text = "%d FPS  %.1fms" % [fps, tick_ms]
	if fps < 30:
		_fps_label.add_theme_color_override("font_color", BattleCommandUI.CRITICAL_COLOR)
	elif fps < 50:
		_fps_label.add_theme_color_override("font_color", BattleCommandUI.WARNING_COLOR)
	else:
		_fps_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)

	if _ui.map_mode and _ui.map_mode.has_method("get_mode_name"):
		var mode_name: String = _ui.map_mode.get_mode_name()
		_map_mode_label.text = mode_name if not mode_name.is_empty() else ""

	# World overlay mode indicator
	if _ui.world_overlay and _ui.world_overlay.has_method("get_mode_name"):
		var overlay_name: String = _ui.world_overlay.get_mode_name()
		_overlay_label.text = "OVL: %s" % overlay_name if overlay_name != "OFF" else ""
	else:
		_overlay_label.text = ""

	# Follow-cam indicator
	if _ui._follow_unit_id >= 0:
		_follow_label.text = "FOLLOW #%d [ESC]" % _ui._follow_unit_id
	else:
		_follow_label.text = ""
