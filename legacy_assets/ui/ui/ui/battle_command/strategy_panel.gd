extends PanelContainer
## Strategy panel — right drawer with Theater/Colony/Profiler/Pheromone tabs.

var _ui: BattleCommandUI
var _tab_buttons: Array[Button] = []
var _tab_content: Array[Control] = []
var _current_tab: int = 0
var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.2

# Team viewing toggle (affects Theater + Colony tabs)
var _viewing_team: int = 0
var _team_buttons: Array[Button] = []
var _team_label: Label

# Theater tab
var _theater_bars: Array[ColorRect] = []
var _theater_labels: Array[Label] = []
var _theater_value_labels: Array[Label] = []
var _theater_snapshot_label: Label

# Colony tab
var _colony_assignments_label: Label
var _colony_coord_label: Label
var _colony_time_label: Label

# Profiler tab
var _profiler_bars: Array[ColorRect] = []
var _profiler_labels: Array[Label] = []
var _profiler_total_label: Label
var _profiler_fow_label: Label
var _profiler_misc_label: Label

# Pheromone tab
var _phero_image: TextureRect
var _phero_channel_label: Label
var _phero_stats_label: Label
var _phero_channel: int = 0
var _phero_team: int = 0

# LLM tab
var _llm_status_label: Label
var _llm_orders_label: Label
var _llm_reasoning_label: Label
var _llm_memory_label: Label
var _llm_commentator_label: Label

const AXIS_NAMES: Array[String] = [
	"Aggression", "Concentration", "Tempo", "Risk Tolerance",
	"Exploitation", "Terrain Control", "Medical Priority",
	"Suppression Dom.", "Intel Coverage",
]

const SUBSYSTEM_NAMES: Array[String] = [
	"Spatial", "Centroids", "Attackers", "CoverValues", "Influence",
	"Visibility", "Suppression", "Reload", "Posture", "Decisions",
	"Peek", "Combat", "Projectiles", "Morale", "Movement",
	"Capture", "Location", "GasEffects", "Pheromones",
]

const PHERO_NAMES: Array[String] = [
	"DANGER", "SUPPRESSION", "CONTACT", "RALLY", "FEAR",
	"COURAGE", "SAFE_ROUTE", "FLANK_OPP",
	"METAL", "CRYSTAL", "ENERGY", "CONGESTION",
	"BUILD_URG", "EXPLORED", "SPARE",
]

const PHERO_COLORS: Array[Color] = [
	Color(1.0, 0.2, 0.2), Color(1.0, 0.5, 0.1), Color(1.0, 1.0, 0.2), Color(0.2, 0.9, 0.3),
	Color(0.3, 0.3, 1.0), Color(0.7, 0.2, 1.0), Color(1.0, 0.2, 0.8), Color(0.2, 1.0, 1.0),
	Color(0.75, 0.75, 0.8), Color(0.5, 0.7, 1.0), Color(1.0, 1.0, 0.3), Color(0.6, 0.15, 0.15),
	Color(1.0, 0.7, 0.2), Color(0.4, 0.8, 0.4), Color(0.5, 0.5, 0.5),
]


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	anchors_preset = Control.PRESET_RIGHT_WIDE
	offset_top = 42.0
	offset_bottom = -10.0
	offset_right = -10.0
	custom_minimum_size = Vector2(320, 0)
	size = Vector2(320, 0)
	size_flags_vertical = Control.SIZE_EXPAND_FILL

	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.04, 0.045, 0.06, 0.94)
	sb.border_width_top = 1
	sb.border_width_bottom = 1
	sb.border_width_left = 1
	sb.border_width_right = 1
	sb.border_color = Color(0.14, 0.17, 0.22)
	sb.corner_radius_top_left = 6
	sb.corner_radius_top_right = 6
	sb.corner_radius_bottom_left = 6
	sb.corner_radius_bottom_right = 6
	sb.content_margin_left = 10.0
	sb.content_margin_right = 10.0
	sb.content_margin_top = 8.0
	sb.content_margin_bottom = 8.0
	sb.shadow_color = Color(0, 0, 0, 0.25)
	sb.shadow_size = 4
	sb.shadow_offset = Vector2(-2, 2)
	add_theme_stylebox_override("panel", sb)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 4)
	add_child(vbox)

	# ── Team viewing toggle ──
	var team_row := HBoxContainer.new()
	team_row.add_theme_constant_override("separation", 6)
	team_row.alignment = BoxContainer.ALIGNMENT_CENTER
	vbox.add_child(team_row)

	_team_label = Label.new()
	_team_label.text = "VIEW:"
	_team_label.add_theme_font_size_override("font_size", 9)
	_team_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	team_row.add_child(_team_label)

	for i in 2:
		var btn := Button.new()
		btn.text = "Team %d" % (i + 1)
		btn.custom_minimum_size = Vector2(70, 22)
		btn.add_theme_font_size_override("font_size", 10)
		var team_style := StyleBoxFlat.new()
		team_style.bg_color = Color(0.08, 0.09, 0.12, 0.6)
		team_style.corner_radius_top_left = 3
		team_style.corner_radius_top_right = 3
		team_style.corner_radius_bottom_left = 3
		team_style.corner_radius_bottom_right = 3
		team_style.content_margin_left = 4.0
		team_style.content_margin_right = 4.0
		btn.add_theme_stylebox_override("normal", team_style)
		var team_idx: int = i
		btn.pressed.connect(func(): _set_viewing_team(team_idx))
		team_row.add_child(btn)
		_team_buttons.append(btn)

	var t_hint := Label.new()
	t_hint.text = "[T]"
	t_hint.add_theme_font_size_override("font_size", 9)
	t_hint.add_theme_color_override("font_color", Color(0.35, 0.38, 0.42))
	team_row.add_child(t_hint)

	_update_team_buttons()

	# Tab buttons
	var tab_row := HBoxContainer.new()
	tab_row.add_theme_constant_override("separation", 2)
	vbox.add_child(tab_row)

	for tab_name in ["Theater", "Colony", "Profiler", "Phero", "LLM"]:
		var btn := Button.new()
		btn.text = tab_name
		btn.custom_minimum_size = Vector2(70, 26)
		btn.add_theme_font_size_override("font_size", 10)
		var tab_style := StyleBoxFlat.new()
		tab_style.bg_color = Color(0.08, 0.09, 0.12, 0.6)
		tab_style.corner_radius_top_left = 4
		tab_style.corner_radius_top_right = 4
		tab_style.corner_radius_bottom_left = 4
		tab_style.corner_radius_bottom_right = 4
		tab_style.content_margin_left = 4.0
		tab_style.content_margin_right = 4.0
		btn.add_theme_stylebox_override("normal", tab_style)
		var tab_hover := tab_style.duplicate()
		tab_hover.bg_color = Color(0.12, 0.14, 0.18, 0.8)
		btn.add_theme_stylebox_override("hover", tab_hover)
		var tab_pressed := tab_style.duplicate()
		tab_pressed.bg_color = Color(0.15, 0.18, 0.22, 0.9)
		btn.add_theme_stylebox_override("pressed", tab_pressed)
		var idx: int = _tab_buttons.size()
		btn.pressed.connect(func(): set_tab(idx))
		tab_row.add_child(btn)
		_tab_buttons.append(btn)

	var divider := ColorRect.new()
	divider.custom_minimum_size = Vector2(0, 1)
	divider.color = Color(0.12, 0.14, 0.18)
	divider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(divider)

	# Build tabs
	_tab_content.append(_build_theater_tab())
	_tab_content.append(_build_colony_tab())
	_tab_content.append(_build_profiler_tab())
	_tab_content.append(_build_pheromone_tab())
	_tab_content.append(_build_llm_tab())

	for c in _tab_content:
		vbox.add_child(c)

	set_tab(0)


func set_tab(idx: int) -> void:
	_current_tab = idx
	for i in _tab_content.size():
		_tab_content[i].visible = (i == idx)
	for i in _tab_buttons.size():
		if i == idx:
			_tab_buttons[i].add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
			var active_sb := StyleBoxFlat.new()
			active_sb.bg_color = Color(0.1, 0.13, 0.18, 0.8)
			active_sb.border_width_bottom = 2
			active_sb.border_color = BattleCommandUI.ACCENT_COLOR
			active_sb.corner_radius_top_left = 4
			active_sb.corner_radius_top_right = 4
			active_sb.content_margin_left = 4.0
			active_sb.content_margin_right = 4.0
			_tab_buttons[i].add_theme_stylebox_override("normal", active_sb)
		else:
			_tab_buttons[i].add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
			var idle_sb := StyleBoxFlat.new()
			idle_sb.bg_color = Color(0.08, 0.09, 0.12, 0.6)
			idle_sb.corner_radius_top_left = 4
			idle_sb.corner_radius_top_right = 4
			idle_sb.corner_radius_bottom_left = 4
			idle_sb.corner_radius_bottom_right = 4
			idle_sb.content_margin_left = 4.0
			idle_sb.content_margin_right = 4.0
			_tab_buttons[i].add_theme_stylebox_override("normal", idle_sb)


func _build_theater_tab() -> VBoxContainer:
	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 2)

	var header := Label.new()
	header.text = "THEATER COMMANDER"
	header.add_theme_font_size_override("font_size", 12)
	header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(header)

	for i in AXIS_NAMES.size():
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 4)
		vbox.add_child(row)

		var name_lbl := Label.new()
		name_lbl.text = AXIS_NAMES[i]
		name_lbl.custom_minimum_size = Vector2(110, 0)
		name_lbl.add_theme_font_size_override("font_size", 10)
		name_lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
		row.add_child(name_lbl)
		_theater_labels.append(name_lbl)

		var bar_bg := ColorRect.new()
		bar_bg.custom_minimum_size = Vector2(130, 10)
		bar_bg.color = Color(0.15, 0.16, 0.18)
		row.add_child(bar_bg)

		var bar := ColorRect.new()
		bar.custom_minimum_size = Vector2(0, 10)
		bar.size = Vector2(0, 10)
		bar.color = BattleCommandUI.ACCENT_COLOR
		bar.position = bar_bg.position
		bar_bg.add_child(bar)
		_theater_bars.append(bar)

		var val_lbl := Label.new()
		val_lbl.text = "0.00"
		val_lbl.custom_minimum_size = Vector2(35, 0)
		val_lbl.add_theme_font_size_override("font_size", 10)
		val_lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
		row.add_child(val_lbl)
		_theater_value_labels.append(val_lbl)

	var theater_div := ColorRect.new()
	theater_div.custom_minimum_size = Vector2(0, 1)
	theater_div.color = Color(0.12, 0.14, 0.18)
	theater_div.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(theater_div)

	_theater_snapshot_label = Label.new()
	_theater_snapshot_label.text = "Snapshot: --"
	_theater_snapshot_label.add_theme_font_size_override("font_size", 10)
	_theater_snapshot_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	_theater_snapshot_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_theater_snapshot_label)

	return vbox


func _build_colony_tab() -> VBoxContainer:
	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 4)

	var header := Label.new()
	header.text = "COLONY AI ASSIGNMENTS"
	header.add_theme_font_size_override("font_size", 12)
	header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(header)

	_colony_assignments_label = Label.new()
	_colony_assignments_label.text = "No assignments"
	_colony_assignments_label.add_theme_font_size_override("font_size", 10)
	_colony_assignments_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	_colony_assignments_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_colony_assignments_label)

	_colony_coord_label = Label.new()
	_colony_coord_label.text = "Coordination: --"
	_colony_coord_label.add_theme_font_size_override("font_size", 10)
	_colony_coord_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	_colony_coord_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_colony_coord_label)

	_colony_time_label = Label.new()
	_colony_time_label.text = "Plan time: --"
	_colony_time_label.add_theme_font_size_override("font_size", 10)
	_colony_time_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	vbox.add_child(_colony_time_label)

	return vbox


func _build_profiler_tab() -> ScrollContainer:
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 2)
	scroll.add_child(vbox)

	var header := Label.new()
	header.text = "TICK PROFILER"
	header.add_theme_font_size_override("font_size", 12)
	header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(header)

	_profiler_total_label = Label.new()
	_profiler_total_label.text = "Total: --"
	_profiler_total_label.add_theme_font_size_override("font_size", 11)
	_profiler_total_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	vbox.add_child(_profiler_total_label)

	for i in SUBSYSTEM_NAMES.size():
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 4)
		vbox.add_child(row)

		var name_lbl := Label.new()
		name_lbl.text = SUBSYSTEM_NAMES[i]
		name_lbl.custom_minimum_size = Vector2(90, 0)
		name_lbl.add_theme_font_size_override("font_size", 9)
		name_lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
		row.add_child(name_lbl)
		_profiler_labels.append(name_lbl)

		var bar := ColorRect.new()
		bar.custom_minimum_size = Vector2(0, 8)
		bar.size = Vector2(0, 8)
		bar.color = BattleCommandUI.TEAM1_COLOR
		bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		row.add_child(bar)
		_profiler_bars.append(bar)

	var prof_div := ColorRect.new()
	prof_div.custom_minimum_size = Vector2(0, 1)
	prof_div.color = Color(0.12, 0.14, 0.18)
	prof_div.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(prof_div)

	_profiler_fow_label = Label.new()
	_profiler_fow_label.text = "FOW: --"
	_profiler_fow_label.add_theme_font_size_override("font_size", 10)
	_profiler_fow_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	_profiler_fow_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_profiler_fow_label)

	_profiler_misc_label = Label.new()
	_profiler_misc_label.text = ""
	_profiler_misc_label.add_theme_font_size_override("font_size", 10)
	_profiler_misc_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	_profiler_misc_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_profiler_misc_label)

	return scroll


func _build_pheromone_tab() -> VBoxContainer:
	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 4)

	var header := Label.new()
	header.text = "PHEROMONE VIEWER"
	header.add_theme_font_size_override("font_size", 12)
	header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(header)

	_phero_channel_label = Label.new()
	_phero_channel_label.text = "Channel: DANGER (T1)"
	_phero_channel_label.add_theme_font_size_override("font_size", 11)
	_phero_channel_label.add_theme_color_override("font_color", PHERO_COLORS[0])
	vbox.add_child(_phero_channel_label)

	_phero_image = TextureRect.new()
	_phero_image.custom_minimum_size = Vector2(280, 170)
	_phero_image.stretch_mode = TextureRect.STRETCH_SCALE
	vbox.add_child(_phero_image)

	_phero_stats_label = Label.new()
	_phero_stats_label.text = "Peak: 0  Total: 0  Coverage: 0%"
	_phero_stats_label.add_theme_font_size_override("font_size", 10)
	_phero_stats_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	vbox.add_child(_phero_stats_label)

	var hint := Label.new()
	hint.text = "[,/.] Channel  [T in phero tab] Team"
	hint.add_theme_font_size_override("font_size", 9)
	hint.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	vbox.add_child(hint)

	return vbox


func update_data(delta: float) -> void:
	_update_timer += delta
	if _update_timer < UPDATE_INTERVAL:
		return
	_update_timer = 0.0

	if not _ui or not _ui.sim:
		return

	match _current_tab:
		0: _update_theater()
		1: _update_colony()
		2: _update_profiler()
		3: _update_pheromone()
		4: _update_llm()


func _update_theater() -> void:
	var tc: TheaterCommander = _ui.theater_t2 if _viewing_team == 1 else _ui.theater_t1
	if not tc:
		return

	if tc.has_method("get_axis_values"):
		var axes: Dictionary = tc.get_axis_values()
		var i: int = 0
		for axis_name in AXIS_NAMES:
			if i >= _theater_bars.size():
				break
			var val: float = axes.get(axis_name.to_lower().replace(" ", "_").replace(".", ""), 0.0)
			# Try alternate key formats
			if val == 0.0:
				val = tc.get_axis(i) if tc.has_method("get_axis") else 0.0
			_theater_bars[i].size.x = 130.0 * clampf(val, 0.0, 1.0)
			_theater_value_labels[i].text = "%.2f" % val
			# Color by axis semantics
			if val > 0.7:
				_theater_bars[i].color = BattleCommandUI.TEAM1_COLOR
			elif val > 0.4:
				_theater_bars[i].color = BattleCommandUI.ACCENT_COLOR
			else:
				_theater_bars[i].color = BattleCommandUI.WARNING_COLOR
			i += 1

	if tc.has_method("get_debug_info"):
		var info: Dictionary = tc.get_debug_info()
		var fr: float = info.get("force_ratio", 0.0)
		var cr: float = info.get("casualty_rate_norm", 0.0)
		var poi: float = info.get("poi_ownership_ratio", 0.0)
		_theater_snapshot_label.text = "Force: %.2f | Casualties: %.2f | POI: %.0f%%" % [fr, cr, poi * 100]


func _update_colony() -> void:
	var col: ColonyAICPP = _ui.colony_t2 if _viewing_team == 1 else _ui.colony_t1
	if not col or not col.has_method("get_debug_info"):
		return

	var info: Dictionary = col.get_debug_info()
	if info.has("assignments"):
		var lines: String = ""
		var assignments: Dictionary = info["assignments"]
		for sq_id in assignments:
			lines += "Sq %s -> %s\n" % [str(sq_id), str(assignments[sq_id])]
		_colony_assignments_label.text = lines if not lines.is_empty() else "No assignments"

	if col.has_method("get_last_plan_ms"):
		_colony_time_label.text = "Plan time: %.1fms" % col.get_last_plan_ms()


func _update_profiler() -> void:
	var stats: Dictionary = _ui.sim.get_debug_stats()

	# Total tick
	var total_ms: float = _ui.sim.get_last_tick_ms()
	_profiler_total_label.text = "Total tick: %.2fms (budget: 1.0ms)" % total_ms
	_profiler_total_label.add_theme_color_override("font_color",
		BattleCommandUI.CRITICAL_COLOR if total_ms > 2.0 else
		BattleCommandUI.WARNING_COLOR if total_ms > 1.0 else
		BattleCommandUI.TEAM1_COLOR)

	# Per-subsystem bars from stats (C++ key is "sub_ema")
	if stats.has("sub_ema"):
		var ema_data = stats["sub_ema"]
		var max_us: float = 500.0  # Scale bar to 500us max
		for i in mini(ema_data.size(), _profiler_bars.size()):
			var us: float = ema_data[i]
			var bar_width: float = clampf(us / max_us, 0.0, 1.0) * 130.0
			_profiler_bars[i].size.x = bar_width
			_profiler_bars[i].custom_minimum_size.x = bar_width
			if us < 100:
				_profiler_bars[i].color = BattleCommandUI.TEAM1_COLOR
			elif us < 500:
				_profiler_bars[i].color = BattleCommandUI.WARNING_COLOR
			else:
				_profiler_bars[i].color = BattleCommandUI.CRITICAL_COLOR

	# FOW stats
	var fow_checks: int = stats.get("fow_vis_checks", 0)
	var fow_hits: int = stats.get("fow_vis_hits", 0)
	var hit_rate: float = (float(fow_hits) / float(fow_checks) * 100.0) if fow_checks > 0 else 0.0
	_profiler_fow_label.text = "FOW: %d checks, %d hits (%.0f%%), skipped: %d" % [
		fow_checks, fow_hits, hit_rate, stats.get("fow_targets_skipped", 0)]

	# Misc
	var proj: int = stats.get("active_projectiles", 0)
	var rubble: int = stats.get("active_rubble", 0)
	_profiler_misc_label.text = "Projectiles: %d | Rubble: %d" % [proj, rubble]


func _update_pheromone() -> void:
	if not _ui.sim:
		return

	# Update channel label
	var ch_name: String = PHERO_NAMES[_phero_channel] if _phero_channel < PHERO_NAMES.size() else "?"
	var ch_color: Color = PHERO_COLORS[_phero_channel] if _phero_channel < PHERO_COLORS.size() else Color.WHITE
	_phero_channel_label.text = "Channel: %s (T%d)" % [ch_name, _phero_team + 1]
	_phero_channel_label.add_theme_color_override("font_color", ch_color)

	# Get pheromone data and render heatmap
	var data: PackedFloat32Array = PackedFloat32Array()
	if _ui.sim.has_method("get_pheromone_data"):
		data = _ui.sim.get_pheromone_data(_phero_team, _phero_channel)
		if data.size() > 0:
			_render_heatmap(data, ch_color)

	# Stats
	_phero_stats_label.text = "Cells: %d" % data.size() if data.size() > 0 else "No data"


func _render_heatmap(data: PackedFloat32Array, tint: Color) -> void:
	# Find max for normalization
	var max_val: float = 0.001
	for v in data:
		if v > max_val:
			max_val = v

	# Create image (150 wide x 100 tall for standard grid)
	var w: int = 150
	var h: int = 100
	if data.size() != w * h:
		# Adjust if different size
		w = int(sqrt(data.size() * 1.5))
		h = data.size() / w if w > 0 else 1

	var img := Image.create(w, h, false, Image.FORMAT_RGBA8)
	for y in h:
		for x in w:
			var idx: int = y * w + x
			if idx >= data.size():
				break
			var intensity: float = pow(data[idx] / max_val, 0.4)  # Gamma boost
			var c := Color(tint.r * intensity, tint.g * intensity, tint.b * intensity, clampf(intensity * 0.9, 0.0, 0.9))
			img.set_pixel(x, y, c)

	_phero_image.texture = ImageTexture.create_from_image(img)


## Toggle which team we're viewing (called from BattleCommandUI on T key press)
func toggle_viewing_team() -> void:
	_set_viewing_team(1 - _viewing_team)


func _set_viewing_team(team: int) -> void:
	_viewing_team = clampi(team, 0, 1)
	_update_team_buttons()
	# Force immediate refresh of current tab
	_update_timer = UPDATE_INTERVAL


func _update_team_buttons() -> void:
	for i in _team_buttons.size():
		var active: bool = (i == _viewing_team)
		var color: Color = (BattleCommandUI.TEAM1_COLOR if i == 0 else BattleCommandUI.TEAM2_COLOR)
		_team_buttons[i].add_theme_color_override("font_color", color if active else BattleCommandUI.TEXT_SECONDARY)
		var btn_style := StyleBoxFlat.new()
		if active:
			btn_style.bg_color = Color(color, 0.2)
			btn_style.border_width_bottom = 2
			btn_style.border_color = color
		else:
			btn_style.bg_color = Color(0.08, 0.09, 0.12, 0.6)
		btn_style.corner_radius_top_left = 3
		btn_style.corner_radius_top_right = 3
		btn_style.corner_radius_bottom_left = 3
		btn_style.corner_radius_bottom_right = 3
		btn_style.content_margin_left = 4.0
		btn_style.content_margin_right = 4.0
		_team_buttons[i].add_theme_stylebox_override("normal", btn_style)


func _build_llm_tab() -> ScrollContainer:
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 4)
	scroll.add_child(vbox)

	var header := Label.new()
	header.text = "LLM SECTOR COMMAND"
	header.add_theme_font_size_override("font_size", 12)
	header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(header)

	_llm_status_label = Label.new()
	_llm_status_label.text = "Status: --"
	_llm_status_label.add_theme_font_size_override("font_size", 10)
	_llm_status_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	_llm_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_llm_status_label)

	var orders_header := Label.new()
	orders_header.text = "ACTIVE DIRECTIVES"
	orders_header.add_theme_font_size_override("font_size", 11)
	orders_header.add_theme_color_override("font_color", Color(1.0, 0.85, 0.3))
	vbox.add_child(orders_header)

	_llm_orders_label = Label.new()
	_llm_orders_label.text = "No active directives"
	_llm_orders_label.add_theme_font_size_override("font_size", 10)
	_llm_orders_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	_llm_orders_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_llm_orders_label)

	var div1 := ColorRect.new()
	div1.custom_minimum_size = Vector2(0, 1)
	div1.color = Color(0.12, 0.14, 0.18)
	div1.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(div1)

	var reason_header := Label.new()
	reason_header.text = "LAST REASONING"
	reason_header.add_theme_font_size_override("font_size", 11)
	reason_header.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	vbox.add_child(reason_header)

	_llm_reasoning_label = Label.new()
	_llm_reasoning_label.text = "--"
	_llm_reasoning_label.add_theme_font_size_override("font_size", 9)
	_llm_reasoning_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	_llm_reasoning_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_llm_reasoning_label)

	var div2 := ColorRect.new()
	div2.custom_minimum_size = Vector2(0, 1)
	div2.color = Color(0.12, 0.14, 0.18)
	div2.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(div2)

	_llm_memory_label = Label.new()
	_llm_memory_label.text = "Battle Memory: --"
	_llm_memory_label.add_theme_font_size_override("font_size", 10)
	_llm_memory_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	_llm_memory_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_llm_memory_label)

	_llm_commentator_label = Label.new()
	_llm_commentator_label.text = ""
	_llm_commentator_label.add_theme_font_size_override("font_size", 10)
	_llm_commentator_label.add_theme_color_override("font_color", Color(1.0, 0.85, 0.3))
	_llm_commentator_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	vbox.add_child(_llm_commentator_label)

	return scroll


func _update_llm() -> void:
	if not _ui or not _ui.camera_script:
		_llm_status_label.text = "Status: No camera ref"
		return

	var cs = _ui.camera_script
	var sector_cmd = cs.get("_llm_sector_cmd") if _viewing_team == 0 else cs.get("_llm_sector_cmd_t2")

	# ── Status ──
	if not sector_cmd:
		_llm_status_label.text = "Status: Disabled (no env vars)"
		_llm_orders_label.text = "No active directives"
		_llm_reasoning_label.text = "--"
		_llm_memory_label.text = "Battle Memory: --"
	else:
		var stats: Dictionary = sector_cmd.get_stats()
		var enabled: bool = stats.get("enabled", false)
		var pending: bool = stats.get("pending", false)
		var status_str := "PENDING..." if pending else ("ACTIVE" if enabled else "DISABLED")
		_llm_status_label.text = "Status: %s | Req: %d | OK: %d | Fail: %d | Reject: %d\nLatency: %.0fms | Interval: %.0fs | Failures: %d" % [
			status_str,
			stats.get("requests", 0),
			stats.get("success", 0),
			stats.get("failures", 0),
			stats.get("rejected_orders", 0),
			stats.get("avg_latency_ms", 0.0),
			stats.get("interval_sec", 0.0),
			stats.get("consecutive_failures", 0),
		]

		# ── Active orders ──
		var orders: Array = sector_cmd.get_current_orders()
		if orders.is_empty():
			_llm_orders_label.text = "No active directives"
		else:
			var lines: String = ""
			for order in orders:
				var conf: float = order.get("confidence", 0.0)
				var conf_color := "HIGH" if conf >= 0.8 else ("MED" if conf >= 0.5 else "LOW")
				lines += "Sq %d -> %s %s (conf: %.0f%% %s)\n" % [
					order.get("squad", -1),
					order.get("intent", "?"),
					order.get("sector_label", "?"),
					conf * 100.0,
					conf_color,
				]
			_llm_orders_label.text = lines.strip_edges()

		# ── Reasoning ──
		var reasoning: String = sector_cmd.get("_last_reasoning") if sector_cmd else ""
		if reasoning.is_empty():
			_llm_reasoning_label.text = "--"
		else:
			_llm_reasoning_label.text = reasoning.left(300) + ("..." if reasoning.length() > 300 else "")

		# ── Battle memory ──
		var mem_cycles: int = stats.get("memory_cycles", 0)
		var last_score: float = stats.get("last_outcome_score", 0.0)
		var score_str := "%.1f" % last_score
		var score_label := "winning" if last_score > 0.2 else ("losing" if last_score < -0.2 else "even")
		_llm_memory_label.text = "Battle Memory: %d cycles | Last: %s (%s)" % [mem_cycles, score_str, score_label]

	# ── Commentator stats ──
	var commentator = cs.get("_llm_commentator")
	if commentator and commentator.has_method("get_stats"):
		var c_stats: Dictionary = commentator.get_stats()
		if c_stats.get("enabled", false):
			var budget_str := ""
			var remaining: float = c_stats.get("budget_remaining", -1.0)
			if remaining >= 0:
				budget_str = " | Budget: $%.4f left" % remaining
			_llm_commentator_label.text = "Commentator: %d barks | $%.4f cost | %d/min%s" % [
				c_stats.get("total_requests", 0),
				c_stats.get("total_cost_usd", 0.0),
				c_stats.get("barks_this_minute", 0),
				budget_str,
			]
		else:
			_llm_commentator_label.text = ""
	else:
		_llm_commentator_label.text = ""


## Handle ,/. channel cycling and T team toggle when pheromone tab is active
func handle_pheromone_key(event: InputEventKey) -> bool:
	if _current_tab != 3:
		return false
	if event.keycode == KEY_COMMA:
		_phero_channel = (_phero_channel + 14) % 15  # wraps correctly without going negative
		return true
	if event.keycode == KEY_PERIOD:
		_phero_channel = (_phero_channel + 1) % 15
		return true
	if event.keycode == KEY_T and not event.shift_pressed:
		_phero_team = 1 - _phero_team
		return true
	return false
