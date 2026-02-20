extends PanelContainer
## Escape menu — polished pause overlay with resume/restart/settings/quit options.

var _ui: BattleCommandUI


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	# Center the panel (dim overlay handled globally by BattleCommandUI._modal_dim)
	anchors_preset = Control.PRESET_CENTER
	custom_minimum_size = Vector2(380, 0)

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
	vbox.add_theme_constant_override("separation", 4)
	add_child(vbox)

	# Title
	var title := Label.new()
	title.text = "PAUSED"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 22)
	title.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(title)

	# Accent line under title
	var accent_line := ColorRect.new()
	accent_line.custom_minimum_size = Vector2(0, 2)
	accent_line.color = Color(BattleCommandUI.ACCENT_COLOR, 0.3)
	accent_line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(accent_line)

	_add_spacer(vbox, 6)

	# Main actions
	_add_menu_button(vbox, "Resume", "ESC", _on_resume)
	_add_menu_button(vbox, "Restart Battle", "U", _on_restart)

	_add_spacer(vbox, 4)
	_add_divider(vbox)
	_add_spacer(vbox, 4)

	# Secondary actions
	_add_menu_button(vbox, "Settings", "", _on_settings)
	_add_menu_button(vbox, "Keybind Reference", "F1", _on_keybinds)
	_add_menu_button(vbox, "Battle Stats", "B", _on_stats)

	_add_spacer(vbox, 4)
	_add_divider(vbox)
	_add_spacer(vbox, 4)

	# Quit actions (danger styling)
	_add_menu_button(vbox, "Quit to Menu", "Q", _on_quit_menu, true)
	_add_menu_button(vbox, "Quit to Desktop", "Ctrl+Q", _on_quit_desktop, true)

	_add_spacer(vbox, 8)

	# Footer
	var footer := Label.new()
	footer.text = "Press ESC to resume"
	footer.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	footer.add_theme_font_size_override("font_size", 10)
	footer.add_theme_color_override("font_color", Color(0.35, 0.38, 0.42))
	vbox.add_child(footer)


func _add_menu_button(parent: VBoxContainer, label_text: String,
		keybind: String, callback: Callable, is_danger: bool = false) -> void:
	var btn := Button.new()
	btn.custom_minimum_size = Vector2(0, 38)
	btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	var style_normal := StyleBoxFlat.new()
	style_normal.bg_color = Color(0.1, 0.11, 0.14, 0.8)
	style_normal.corner_radius_top_left = 5
	style_normal.corner_radius_top_right = 5
	style_normal.corner_radius_bottom_left = 5
	style_normal.corner_radius_bottom_right = 5
	style_normal.content_margin_left = 14.0
	style_normal.content_margin_right = 14.0
	btn.add_theme_stylebox_override("normal", style_normal)

	var style_hover := style_normal.duplicate()
	style_hover.bg_color = Color(0.14, 0.16, 0.2, 0.95)
	style_hover.border_width_left = 3
	style_hover.border_color = BattleCommandUI.ACCENT_COLOR if not is_danger else Color(0.8, 0.3, 0.3)
	btn.add_theme_stylebox_override("hover", style_hover)

	var style_pressed := style_normal.duplicate()
	style_pressed.bg_color = Color(0.12, 0.14, 0.18, 0.95)
	btn.add_theme_stylebox_override("pressed", style_pressed)

	var text_color := BattleCommandUI.TEXT_PRIMARY if not is_danger else Color(0.85, 0.55, 0.55)
	btn.add_theme_color_override("font_color", text_color)
	btn.add_theme_color_override("font_hover_color",
		BattleCommandUI.ACCENT_COLOR if not is_danger else Color(1.0, 0.4, 0.4))
	btn.add_theme_font_size_override("font_size", 14)
	btn.alignment = HORIZONTAL_ALIGNMENT_LEFT

	if keybind.is_empty():
		btn.text = "  %s" % label_text
	else:
		var pad: int = maxi(0, 34 - label_text.length() - keybind.length())
		btn.text = "  %s%s[%s]" % [label_text, " ".repeat(pad), keybind]

	btn.pressed.connect(callback)
	parent.add_child(btn)


func _add_spacer(parent: VBoxContainer, height: float) -> void:
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(0, height)
	parent.add_child(spacer)


func _add_divider(parent: VBoxContainer) -> void:
	var line := ColorRect.new()
	line.custom_minimum_size = Vector2(0, 1)
	line.color = Color(0.15, 0.17, 0.2)
	line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(line)


func _on_resume() -> void:
	if _ui:
		_ui._close_escape_menu()

func _on_restart() -> void:
	if _ui:
		_ui._close_escape_menu()
		if _ui.camera_script and _ui.camera_script.has_method("_restart_simulation"):
			_ui.camera_script._restart_simulation()

func _on_settings() -> void:
	visible = false
	if _ui and _ui.settings_menu:
		_ui.settings_menu.visible = true

func _on_keybinds() -> void:
	# Hide escape menu but keep dim + paused state — keybind overlay is also modal
	if _ui:
		visible = false
		if _ui.keybind_overlay:
			_ui.keybind_overlay.visible = true
		_ui._update_modal_dim()

func _on_stats() -> void:
	# Hide escape menu but keep dim + paused state — battle stats is also modal
	if _ui:
		visible = false
		if _ui.battle_stats:
			_ui.battle_stats.visible = true
		_ui._update_modal_dim()

func _on_quit_menu() -> void:
	if _ui:
		_ui._quit_to_menu()

func _on_quit_desktop() -> void:
	get_tree().quit()
