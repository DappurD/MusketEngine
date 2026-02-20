extends PanelContainer
## Live tuning panel: runtime-adjustable parameter sliders.
## Opens as a right-side drawer (P key). Non-modal — sim keeps running.

var _ui: BattleCommandUI
var _sliders: Dictionary = {}   # key -> HSlider
var _labels: Dictionary = {}    # key -> Label (value display)
var _sections: Dictionary = {}  # category -> VBoxContainer
var _headers: Dictionary = {}   # category -> Button (collapse toggle)
var _scroll_vbox: VBoxContainer
var _search_edit: LineEdit

# ── Parameter Definitions ─────────────────────────────────────────────
# Each entry: key (matches C++ set_tuning_param name), label, category,
# min/max/step, default, target ("sim"/"theater"/"colony").
const PARAMS := [
	# ── Movement ──
	{"key": "move_speed", "label": "Move Speed", "cat": "Movement", "min": 0.5, "max": 15.0, "step": 0.1, "default": 4.0, "target": "sim"},
	{"key": "separation_radius", "label": "Separation Radius", "cat": "Movement", "min": 0.5, "max": 5.0, "step": 0.1, "default": 2.0, "target": "sim"},
	{"key": "separation_force", "label": "Separation Force", "cat": "Movement", "min": 0.0, "max": 5.0, "step": 0.1, "default": 1.5, "target": "sim"},
	{"key": "arrive_dist", "label": "Arrive Distance", "cat": "Movement", "min": 0.5, "max": 10.0, "step": 0.5, "default": 3.0, "target": "sim"},
	{"key": "centroid_anchor", "label": "Centroid Anchor", "cat": "Movement", "min": 0.0, "max": 1.0, "step": 0.05, "default": 0.4, "target": "sim"},
	{"key": "catchup_weight", "label": "Catchup Weight", "cat": "Movement", "min": 0.0, "max": 2.0, "step": 0.05, "default": 0.6, "target": "sim"},
	{"key": "combat_drift", "label": "Combat Drift", "cat": "Movement", "min": 0.0, "max": 3.0, "step": 0.1, "default": 0.5, "target": "sim"},
	{"key": "max_step_height", "label": "Max Step Height", "cat": "Movement", "min": 0.1, "max": 3.0, "step": 0.05, "default": 0.85, "target": "sim"},
	# ── Locomotion ──
	{"key": "turn_rate_base", "label": "Turn Rate Base", "cat": "Locomotion", "min": 0.5, "max": 8.0, "step": 0.1, "default": 2.0, "target": "sim"},
	{"key": "turn_rate_bonus", "label": "Turn Rate Bonus", "cat": "Locomotion", "min": 0.0, "max": 12.0, "step": 0.5, "default": 6.0, "target": "sim"},
	{"key": "face_smooth_rate", "label": "Face Smooth Rate", "cat": "Locomotion", "min": 1.0, "max": 20.0, "step": 0.5, "default": 6.0, "target": "sim"},
	{"key": "dead_band_sq", "label": "Dead Band Sq", "cat": "Locomotion", "min": 0.001, "max": 0.5, "step": 0.005, "default": 0.04, "target": "sim"},
	# ── Context Steering ──
	{"key": "steer_order", "label": "Order Weight", "cat": "Steering", "min": 0.0, "max": 3.0, "step": 0.05, "default": 1.0, "target": "sim"},
	{"key": "steer_flow", "label": "Flow Weight", "cat": "Steering", "min": 0.0, "max": 3.0, "step": 0.05, "default": 0.6, "target": "sim"},
	{"key": "steer_pheromone", "label": "Pheromone Weight", "cat": "Steering", "min": 0.0, "max": 3.0, "step": 0.05, "default": 0.4, "target": "sim"},
	{"key": "steer_danger", "label": "Danger Scale", "cat": "Steering", "min": 0.0, "max": 5.0, "step": 0.1, "default": 1.0, "target": "sim"},
	{"key": "steer_obstacle_dist", "label": "Obstacle Dist", "cat": "Steering", "min": 0.5, "max": 8.0, "step": 0.5, "default": 2.0, "target": "sim"},
	{"key": "steer_sample_dist", "label": "Sample Dist", "cat": "Steering", "min": 1.0, "max": 12.0, "step": 0.5, "default": 4.0, "target": "sim"},
	{"key": "steer_temporal", "label": "Temporal Alpha", "cat": "Steering", "min": 0.0, "max": 1.0, "step": 0.05, "default": 0.3, "target": "sim"},
	{"key": "steer_border_dist", "label": "Border Dist", "cat": "Steering", "min": 1.0, "max": 20.0, "step": 0.5, "default": 5.0, "target": "sim"},
	# ── Combat ──
	{"key": "decision_interval", "label": "Decision Interval", "cat": "Combat", "min": 0.05, "max": 2.0, "step": 0.05, "default": 0.35, "target": "sim"},
	{"key": "reload_time", "label": "Reload Time", "cat": "Combat", "min": 0.5, "max": 8.0, "step": 0.1, "default": 2.5, "target": "sim"},
	{"key": "suppression_decay", "label": "Suppression Decay", "cat": "Combat", "min": 0.01, "max": 2.0, "step": 0.05, "default": 0.3, "target": "sim"},
	{"key": "settle_spread", "label": "Settle Spread Mult", "cat": "Combat", "min": 0.0, "max": 10.0, "step": 0.5, "default": 3.0, "target": "sim"},
	{"key": "near_miss_dist", "label": "Near Miss Dist", "cat": "Combat", "min": 1.0, "max": 12.0, "step": 0.5, "default": 4.0, "target": "sim"},
	{"key": "near_miss_supp", "label": "Near Miss Supp", "cat": "Combat", "min": 0.0, "max": 0.5, "step": 0.01, "default": 0.06, "target": "sim"},
	{"key": "hit_supp", "label": "Hit Suppression", "cat": "Combat", "min": 0.0, "max": 1.0, "step": 0.01, "default": 0.15, "target": "sim"},
	{"key": "wall_pen_penalty", "label": "Wall Pen Penalty", "cat": "Combat", "min": 0.0, "max": 50.0, "step": 1.0, "default": 15.0, "target": "sim"},
	# ── Tactical AI ──
	{"key": "cover_seek_radius", "label": "Cover Seek Radius", "cat": "Tactical AI", "min": 2.0, "max": 30.0, "step": 1.0, "default": 10.0, "target": "sim"},
	{"key": "supp_cover_thresh", "label": "Supp Cover Thresh", "cat": "Tactical AI", "min": 0.0, "max": 1.0, "step": 0.05, "default": 0.4, "target": "sim"},
	{"key": "peek_offset", "label": "Peek Offset", "cat": "Tactical AI", "min": 0.2, "max": 3.0, "step": 0.1, "default": 1.0, "target": "sim"},
	{"key": "peek_hide_min", "label": "Peek Hide Min", "cat": "Tactical AI", "min": 0.1, "max": 5.0, "step": 0.1, "default": 0.8, "target": "sim"},
	{"key": "peek_hide_max", "label": "Peek Hide Max", "cat": "Tactical AI", "min": 0.1, "max": 8.0, "step": 0.1, "default": 2.0, "target": "sim"},
	{"key": "peek_expose_min", "label": "Peek Expose Min", "cat": "Tactical AI", "min": 0.1, "max": 5.0, "step": 0.1, "default": 0.5, "target": "sim"},
	{"key": "peek_expose_max", "label": "Peek Expose Max", "cat": "Tactical AI", "min": 0.1, "max": 5.0, "step": 0.1, "default": 1.5, "target": "sim"},
	# ── Explosions ──
	{"key": "grenade_dmg_radius", "label": "Grenade Dmg Radius", "cat": "Explosions", "min": 1.0, "max": 15.0, "step": 0.5, "default": 4.0, "target": "sim"},
	{"key": "grenade_max_dmg", "label": "Grenade Max Dmg", "cat": "Explosions", "min": 0.1, "max": 1.0, "step": 0.05, "default": 0.7, "target": "sim"},
	{"key": "mortar_dmg_radius", "label": "Mortar Dmg Radius", "cat": "Explosions", "min": 2.0, "max": 20.0, "step": 0.5, "default": 7.5, "target": "sim"},
	{"key": "mortar_max_dmg", "label": "Mortar Max Dmg", "cat": "Explosions", "min": 0.1, "max": 2.0, "step": 0.1, "default": 1.0, "target": "sim"},
	{"key": "mortar_max_scatter", "label": "Mortar Max Scatter", "cat": "Explosions", "min": 1.0, "max": 20.0, "step": 0.5, "default": 9.0, "target": "sim"},
	# ── ORCA ──
	{"key": "orca_agent_radius", "label": "Agent Radius", "cat": "ORCA", "min": 0.1, "max": 2.0, "step": 0.05, "default": 0.4, "target": "sim"},
	{"key": "orca_time_horizon", "label": "Time Horizon", "cat": "ORCA", "min": 0.1, "max": 5.0, "step": 0.1, "default": 1.0, "target": "sim"},
	{"key": "orca_neighbor_dist", "label": "Neighbor Dist", "cat": "ORCA", "min": 1.0, "max": 10.0, "step": 0.5, "default": 3.0, "target": "sim"},
	{"key": "orca_wall_probe", "label": "Wall Probe Dist", "cat": "ORCA", "min": 0.5, "max": 5.0, "step": 0.1, "default": 1.5, "target": "sim"},
	# ── Theater ──
	{"key": "tick_interval", "label": "Tick Interval", "cat": "Theater", "min": 0.1, "max": 5.0, "step": 0.1, "default": 1.5, "target": "theater"},
	{"key": "momentum_bonus", "label": "Momentum Bonus", "cat": "Theater", "min": 0.0, "max": 0.5, "step": 0.01, "default": 0.15, "target": "theater"},
	{"key": "min_commitment", "label": "Min Commitment", "cat": "Theater", "min": 1.0, "max": 30.0, "step": 0.5, "default": 8.0, "target": "theater"},
	{"key": "cooldown", "label": "Cooldown", "cat": "Theater", "min": 1.0, "max": 30.0, "step": 0.5, "default": 12.0, "target": "theater"},
	# ── Colony ──
	{"key": "llm_floor", "label": "LLM Floor", "cat": "Colony", "min": 0.0, "max": 200.0, "step": 5.0, "default": 75.0, "target": "colony"},
	{"key": "llm_age_max", "label": "LLM Age Max", "cat": "Colony", "min": 10.0, "max": 300.0, "step": 5.0, "default": 90.0, "target": "colony"},
	{"key": "llm_decay_start", "label": "LLM Decay Start", "cat": "Colony", "min": 5.0, "max": 200.0, "step": 5.0, "default": 60.0, "target": "colony"},
	{"key": "coord_damping", "label": "Coord Damping", "cat": "Colony", "min": 0.0, "max": 1.0, "step": 0.05, "default": 0.5, "target": "colony"},
]

const CATEGORY_ORDER := [
	"Movement", "Locomotion", "Steering", "Combat",
	"Tactical AI", "Explosions", "ORCA", "Theater", "Colony"
]


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	# Right-anchored drawer
	set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	custom_minimum_size = Vector2(340, 0)
	size_flags_horizontal = Control.SIZE_SHRINK_END
	offset_left = -340

	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.06, 0.07, 0.09, 0.95)
	sb.border_width_left = 2
	sb.border_color = Color(0.2, 0.25, 0.32)
	sb.content_margin_left = 10.0
	sb.content_margin_right = 10.0
	sb.content_margin_top = 8.0
	sb.content_margin_bottom = 8.0
	add_theme_stylebox_override("panel", sb)

	var root_vbox := VBoxContainer.new()
	root_vbox.add_theme_constant_override("separation", 4)
	add_child(root_vbox)

	# Title row
	var title_row := HBoxContainer.new()
	root_vbox.add_child(title_row)

	var title := Label.new()
	title.text = "PARAMETERS"
	title.add_theme_font_size_override("font_size", 16)
	title.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title_row.add_child(title)

	var close_btn := Button.new()
	close_btn.text = "X"
	close_btn.add_theme_font_size_override("font_size", 12)
	var btn_sb := StyleBoxFlat.new()
	btn_sb.bg_color = Color(0.15, 0.1, 0.1, 0.8)
	btn_sb.corner_radius_top_left = 4
	btn_sb.corner_radius_top_right = 4
	btn_sb.corner_radius_bottom_left = 4
	btn_sb.corner_radius_bottom_right = 4
	close_btn.add_theme_stylebox_override("normal", btn_sb)
	close_btn.add_theme_color_override("font_color", BattleCommandUI.CRITICAL_COLOR)
	close_btn.pressed.connect(func(): visible = false)
	title_row.add_child(close_btn)

	# Accent line
	var accent := ColorRect.new()
	accent.custom_minimum_size = Vector2(0, 2)
	accent.color = Color(BattleCommandUI.ACCENT_COLOR, 0.3)
	accent.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root_vbox.add_child(accent)

	# Search filter
	_search_edit = LineEdit.new()
	_search_edit.placeholder_text = "Filter..."
	_search_edit.add_theme_font_size_override("font_size", 11)
	var search_sb := StyleBoxFlat.new()
	search_sb.bg_color = Color(0.1, 0.11, 0.14)
	search_sb.corner_radius_top_left = 4
	search_sb.corner_radius_top_right = 4
	search_sb.corner_radius_bottom_left = 4
	search_sb.corner_radius_bottom_right = 4
	search_sb.content_margin_left = 6.0
	search_sb.content_margin_right = 6.0
	search_sb.content_margin_top = 4.0
	search_sb.content_margin_bottom = 4.0
	_search_edit.add_theme_stylebox_override("normal", search_sb)
	_search_edit.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	_search_edit.add_theme_color_override("font_placeholder_color", BattleCommandUI.TEXT_SECONDARY)
	_search_edit.text_changed.connect(_on_search_changed)
	root_vbox.add_child(_search_edit)

	# Scroll container
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	root_vbox.add_child(scroll)

	_scroll_vbox = VBoxContainer.new()
	_scroll_vbox.add_theme_constant_override("separation", 2)
	_scroll_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_scroll_vbox)

	# Build categories
	for cat in CATEGORY_ORDER:
		_build_category(cat)

	# Bottom buttons
	var btn_row := HBoxContainer.new()
	btn_row.add_theme_constant_override("separation", 6)
	root_vbox.add_child(btn_row)

	_add_bottom_btn(btn_row, "Reset All", _on_reset_all)
	_add_bottom_btn(btn_row, "Save", _on_save_preset)
	_add_bottom_btn(btn_row, "Load", _on_load_preset)


func _build_category(cat: String) -> void:
	# Category header (clickable collapse toggle)
	var header_btn := Button.new()
	header_btn.text = "  %s" % cat.to_upper()
	header_btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
	header_btn.add_theme_font_size_override("font_size", 12)
	header_btn.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	var hdr_sb := StyleBoxFlat.new()
	hdr_sb.bg_color = Color(0.1, 0.12, 0.15, 0.6)
	hdr_sb.corner_radius_top_left = 3
	hdr_sb.corner_radius_top_right = 3
	hdr_sb.corner_radius_bottom_left = 3
	hdr_sb.corner_radius_bottom_right = 3
	hdr_sb.content_margin_left = 4.0
	hdr_sb.content_margin_top = 2.0
	hdr_sb.content_margin_bottom = 2.0
	header_btn.add_theme_stylebox_override("normal", hdr_sb)
	var hdr_hover := hdr_sb.duplicate()
	hdr_hover.bg_color = Color(0.14, 0.16, 0.2, 0.8)
	header_btn.add_theme_stylebox_override("hover", hdr_hover)
	_scroll_vbox.add_child(header_btn)
	_headers[cat] = header_btn

	# Section container
	var section := VBoxContainer.new()
	section.add_theme_constant_override("separation", 1)
	_scroll_vbox.add_child(section)
	_sections[cat] = section

	header_btn.pressed.connect(func(): section.visible = not section.visible)

	# Add param sliders for this category
	for def in PARAMS:
		if def.cat == cat:
			_add_param_slider(section, def)

	# Category divider
	var div := ColorRect.new()
	div.custom_minimum_size = Vector2(0, 1)
	div.color = Color(0.12, 0.14, 0.18)
	div.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scroll_vbox.add_child(div)


func _add_param_slider(parent: Control, def: Dictionary) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 4)
	row.name = "row_%s" % def.key
	parent.add_child(row)

	var lbl := Label.new()
	lbl.text = def.label
	lbl.custom_minimum_size = Vector2(120, 0)
	lbl.add_theme_font_size_override("font_size", 10)
	lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	row.add_child(lbl)

	var slider := HSlider.new()
	slider.min_value = def.min
	slider.max_value = def.max
	slider.value = def.default
	slider.step = def.step
	slider.custom_minimum_size = Vector2(120, 0)
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	slider.value_changed.connect(func(val: float): _on_param_changed(def, val))
	row.add_child(slider)
	_sliders[def.key] = slider

	var val_lbl := Label.new()
	val_lbl.text = _format_val(def.default, def.step)
	val_lbl.custom_minimum_size = Vector2(45, 0)
	val_lbl.add_theme_font_size_override("font_size", 10)
	val_lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	val_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	row.add_child(val_lbl)
	_labels[def.key] = val_lbl


func _on_param_changed(def: Dictionary, val: float) -> void:
	if _labels.has(def.key):
		_labels[def.key].text = _format_val(val, def.step)
	if not _ui:
		return
	match def.target:
		"sim":
			if _ui.sim:
				_ui.sim.set_tuning_param(def.key, val)
		"theater":
			if _ui.theater_t1:
				_ui.theater_t1.set_tuning_param(def.key, val)
			if _ui.theater_t2:
				_ui.theater_t2.set_tuning_param(def.key, val)
		"colony":
			if _ui.colony_t1:
				_ui.colony_t1.set_tuning_param(def.key, val)
			if _ui.colony_t2:
				_ui.colony_t2.set_tuning_param(def.key, val)


func _on_search_changed(text: String) -> void:
	var filter := text.strip_edges().to_lower()
	for def in PARAMS:
		var row: Control = _sliders[def.key].get_parent()
		if filter.is_empty():
			row.visible = true
		else:
			row.visible = def.label.to_lower().contains(filter) or \
				def.key.to_lower().contains(filter) or \
				def.cat.to_lower().contains(filter)
	# Show/hide category headers based on whether any child is visible
	for cat in CATEGORY_ORDER:
		var section: VBoxContainer = _sections[cat]
		var any_visible := false
		for child in section.get_children():
			if child.visible:
				any_visible = true
				break
		_headers[cat].visible = any_visible or filter.is_empty()
		section.visible = any_visible or filter.is_empty()


func _on_reset_all() -> void:
	if _ui and _ui.sim:
		_ui.sim.reset_tuning_params()
	if _ui and _ui.theater_t1:
		_ui.theater_t1.reset_tuning_params()
	if _ui and _ui.theater_t2:
		_ui.theater_t2.reset_tuning_params()
	if _ui and _ui.colony_t1:
		_ui.colony_t1.reset_tuning_params()
	if _ui and _ui.colony_t2:
		_ui.colony_t2.reset_tuning_params()
	# Reset all sliders to defaults
	for def in PARAMS:
		if _sliders.has(def.key):
			_sliders[def.key].set_value_no_signal(def.default)
			_labels[def.key].text = _format_val(def.default, def.step)


func _on_save_preset() -> void:
	var data := {}
	for def in PARAMS:
		if _sliders.has(def.key):
			data[def.key] = _sliders[def.key].value
	var dir_path := "user://tuning_presets"
	DirAccess.make_dir_recursive_absolute(dir_path)
	var path := dir_path + "/default.json"
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(data, "\t"))
		f.close()
		print("[TuningPanel] Saved preset to %s" % path)


func _on_load_preset() -> void:
	var path := "user://tuning_presets/default.json"
	if not FileAccess.file_exists(path):
		print("[TuningPanel] No preset found at %s" % path)
		return
	var f := FileAccess.open(path, FileAccess.READ)
	if not f:
		return
	var json := JSON.new()
	if json.parse(f.get_as_text()) != OK:
		push_warning("[TuningPanel] Failed to parse preset")
		return
	var data: Dictionary = json.data
	for def in PARAMS:
		if data.has(def.key) and _sliders.has(def.key):
			var val: float = data[def.key]
			_sliders[def.key].value = val  # triggers _on_param_changed


func _add_bottom_btn(parent: Control, text: String, callback: Callable) -> void:
	var btn := Button.new()
	btn.text = text
	btn.add_theme_font_size_override("font_size", 10)
	btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var btn_sb := StyleBoxFlat.new()
	btn_sb.bg_color = Color(0.1, 0.12, 0.15, 0.8)
	btn_sb.corner_radius_top_left = 4
	btn_sb.corner_radius_top_right = 4
	btn_sb.corner_radius_bottom_left = 4
	btn_sb.corner_radius_bottom_right = 4
	btn_sb.content_margin_top = 4.0
	btn_sb.content_margin_bottom = 4.0
	btn.add_theme_stylebox_override("normal", btn_sb)
	var btn_hover := btn_sb.duplicate()
	btn_hover.bg_color = Color(0.14, 0.16, 0.2, 0.95)
	btn.add_theme_stylebox_override("hover", btn_hover)
	btn.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	btn.add_theme_color_override("font_hover_color", BattleCommandUI.ACCENT_COLOR)
	btn.pressed.connect(callback)
	parent.add_child(btn)


func _format_val(val: float, step: float) -> String:
	if step >= 1.0:
		return "%.0f" % val
	elif step >= 0.1:
		return "%.1f" % val
	elif step >= 0.01:
		return "%.2f" % val
	return "%.3f" % val
