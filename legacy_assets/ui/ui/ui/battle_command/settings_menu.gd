extends PanelContainer
## Settings menu — accessible from escape menu. Lighting sliders, UI options.

var _ui: BattleCommandUI
var _sliders: Dictionary = {}  # name -> HSlider
var _labels: Dictionary = {}   # name -> Label


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	# Dim overlay handled globally by BattleCommandUI._modal_dim
	anchors_preset = Control.PRESET_CENTER
	custom_minimum_size = Vector2(450, 0)

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

	# Title + back button
	var title_row := HBoxContainer.new()
	vbox.add_child(title_row)

	var back_btn := Button.new()
	back_btn.text = "< Back"
	back_btn.add_theme_font_size_override("font_size", 11)
	var back_style := StyleBoxFlat.new()
	back_style.bg_color = Color(0.1, 0.11, 0.14, 0.8)
	back_style.corner_radius_top_left = 4
	back_style.corner_radius_top_right = 4
	back_style.corner_radius_bottom_left = 4
	back_style.corner_radius_bottom_right = 4
	back_style.content_margin_left = 8.0
	back_style.content_margin_right = 8.0
	back_btn.add_theme_stylebox_override("normal", back_style)
	var back_hover := back_style.duplicate()
	back_hover.bg_color = Color(0.14, 0.16, 0.2, 0.95)
	back_btn.add_theme_stylebox_override("hover", back_hover)
	back_btn.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	back_btn.add_theme_color_override("font_hover_color", BattleCommandUI.ACCENT_COLOR)
	back_btn.pressed.connect(_on_back)
	title_row.add_child(back_btn)

	var title := Label.new()
	title.text = "  SETTINGS"
	title.add_theme_font_size_override("font_size", 18)
	title.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	title_row.add_child(title)

	var accent_line := ColorRect.new()
	accent_line.custom_minimum_size = Vector2(0, 2)
	accent_line.color = Color(BattleCommandUI.ACCENT_COLOR, 0.3)
	accent_line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(accent_line)

	# Lighting section
	var light_header := Label.new()
	light_header.text = "LIGHTING"
	light_header.add_theme_font_size_override("font_size", 13)
	light_header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(light_header)

	_add_slider(vbox, "Sun Energy", 0.0, 2.0, 0.6, "sun_energy")
	_add_slider(vbox, "Ambient Light", 0.0, 1.0, 0.45, "ambient")
	_add_slider(vbox, "GI Intensity", 0.0, 1.0, 0.25, "gi_intensity")
	_add_slider(vbox, "Glow", 0.0, 2.0, 0.7, "glow")
	_add_slider(vbox, "SSAO", 0.0, 2.0, 1.0, "ssao")

	var section_div := ColorRect.new()
	section_div.custom_minimum_size = Vector2(0, 1)
	section_div.color = Color(0.15, 0.17, 0.2)
	section_div.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(section_div)

	# UI section
	var ui_header := Label.new()
	ui_header.text = "UI"
	ui_header.add_theme_font_size_override("font_size", 13)
	ui_header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(ui_header)

	# Placeholder for future UI settings
	var placeholder := Label.new()
	placeholder.text = "UI scale and audio options coming soon"
	placeholder.add_theme_font_size_override("font_size", 10)
	placeholder.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	vbox.add_child(placeholder)


func _add_slider(parent: Control, label_text: String, min_val: float, max_val: float, default_val: float, key: String) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	parent.add_child(row)

	var lbl := Label.new()
	lbl.text = label_text
	lbl.custom_minimum_size = Vector2(110, 0)
	lbl.add_theme_font_size_override("font_size", 11)
	lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	row.add_child(lbl)

	var slider := HSlider.new()
	slider.min_value = min_val
	slider.max_value = max_val
	slider.value = default_val
	slider.step = 0.01
	slider.custom_minimum_size = Vector2(200, 0)
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	slider.value_changed.connect(func(val: float): _on_slider_changed(key, val))
	row.add_child(slider)
	_sliders[key] = slider

	var val_lbl := Label.new()
	val_lbl.text = "%.2f" % default_val
	val_lbl.custom_minimum_size = Vector2(40, 0)
	val_lbl.add_theme_font_size_override("font_size", 11)
	val_lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	row.add_child(val_lbl)
	_labels[key] = val_lbl


func _on_slider_changed(key: String, val: float) -> void:
	if _labels.has(key):
		_labels[key].text = "%.2f" % val

	if not _ui or not _ui.cam:
		return

	# Apply lighting changes to the scene
	var scene_root: Node = _ui.cam.get_parent()
	match key:
		"sun_energy":
			var sun: DirectionalLight3D = scene_root.get_node_or_null("DirectionalLight3D")
			if sun:
				sun.light_energy = val
		"ambient":
			var env: Environment = _get_env()
			if env:
				env.ambient_light_energy = val
		"gi_intensity":
			if _ui.camera_script and _ui.camera_script._rc_effect:
				_ui.camera_script._rc_effect.gi_intensity = val
		"glow":
			var env: Environment = _get_env()
			if env:
				env.glow_intensity = val
		"ssao":
			var env: Environment = _get_env()
			if env:
				env.ssao_intensity = val


func _get_env() -> Environment:
	if not _ui or not _ui.cam:
		return null
	var we: WorldEnvironment = _ui.cam.get_parent().get_node_or_null("WorldEnvironment")
	if we:
		return we.environment
	return null


func _on_back() -> void:
	visible = false
	if _ui and _ui.escape_menu:
		_ui.escape_menu.visible = true
	if _ui:
		_ui._update_modal_dim()
