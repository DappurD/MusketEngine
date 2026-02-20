extends PanelContainer
## Keybind reference overlay — full-screen keybinding guide toggled with F1.

var _ui: BattleCommandUI


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	# Dim overlay handled globally by BattleCommandUI._modal_dim
	anchors_preset = Control.PRESET_FULL_RECT
	offset_left = 60.0
	offset_right = -60.0
	offset_top = 50.0
	offset_bottom = -50.0

	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.05, 0.06, 0.08, 0.97)
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

	var scroll := ScrollContainer.new()
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(scroll)

	var content := VBoxContainer.new()
	content.add_theme_constant_override("separation", 12)
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(content)

	# Title
	var title := Label.new()
	title.text = "KEYBIND REFERENCE"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 20)
	title.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	content.add_child(title)

	var accent_line := ColorRect.new()
	accent_line.custom_minimum_size = Vector2(0, 2)
	accent_line.color = Color(BattleCommandUI.ACCENT_COLOR, 0.3)
	accent_line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	content.add_child(accent_line)

	var hint := Label.new()
	hint.text = "Press F1 to close"
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hint.add_theme_font_size_override("font_size", 10)
	hint.add_theme_color_override("font_color", Color(0.4, 0.45, 0.5))
	content.add_child(hint)

	# Grid of sections
	var grid := HBoxContainer.new()
	grid.add_theme_constant_override("separation", 24)
	grid.alignment = BoxContainer.ALIGNMENT_CENTER
	content.add_child(grid)

	# Column 1: Navigation + Camera
	var col1 := VBoxContainer.new()
	col1.add_theme_constant_override("separation", 4)
	grid.add_child(col1)

	_add_section(col1, "RTS CAMERA (default)", [
		["WASD", "Pan (camera-relative)"],
		["Q / E", "Rotate camera"],
		["Scroll", "Zoom to cursor"],
		["RMB drag", "Orbit rotate"],
		["MMB drag", "Pan (drag ground)"],
		["Ctrl", "Speed boost (3x)"],
		["Edge of screen", "Auto-pan"],
	])

	_add_section(col1, "FREE-FLY (Tab)", [
		["WASD", "Fly movement"],
		["Space / Shift", "Up / Down"],
		["Mouse", "Look (when captured)"],
		["Scroll", "Adjust move speed"],
		["Tab", "Return to RTS mode"],
	])

	_add_section(col1, "CAMERA", [
		["Tab", "Toggle RTS / Free-fly"],
		["Ctrl+F5-F8", "Save camera bookmark"],
		["F5-F8", "Recall bookmark"],
		["Ctrl+H", "Screenshot mode (hide UI)"],
	])

	_add_section(col1, "ENVIRONMENT", [
		["T", "Toggle day/night cycle"],
		["Shift+T", "Advance +2 hours"],
		["R", "Toggle rain"],
	])

	# Column 2: Panels + Modals
	var col2 := VBoxContainer.new()
	col2.add_theme_constant_override("separation", 4)
	grid.add_child(col2)

	_add_section(col2, "PANELS", [
		["I", "Inspector (left drawer)"],
		["O", "Strategy (right drawer)"],
		["K", "Expand / collapse minimap"],
		["T", "Toggle Team 1/2 (strategy open)"],
		["J", "Event log (expand ticker)"],
		["B", "Battle stats dashboard"],
		["P", "Parameters (live tuning)"],
		["V", "World overlay (OFF/BARS/FULL)"],
		["Ctrl+1-3", "Inspector tab: Unit/Squad/Voxel"],
		["Ctrl+4-7", "Strategy tab: Theater/Colony/Profiler/Phero"],
	])

	_add_section(col2, "EXPANDED MINIMAP (K open)", [
		["H", "Cycle heatmap overlay"],
		["A", "Toggle order arrows"],
		["L", "Toggle squad labels"],
		["1 / 2", "Toggle team 1 / 2 visibility"],
	])

	_add_section(col2, "MODALS", [
		["ESC", "Pause menu / Close panel"],
		["N", "Unit spawner"],
		["F1", "This keybind reference"],
	])

	# Column 3: Simulation + Debug
	var col3 := VBoxContainer.new()
	col3.add_theme_constant_override("separation", 4)
	grid.add_child(col3)

	_add_section(col3, "SIMULATION", [
		["U", "Start / Restart simulation"],
		["5", "Pause / Unpause"],
		["[ / ]", "Time scale down / up"],
		["+ / -", "Unit count (before start)"],
		["1-4", "Team 1 formation"],
		["Shift+1-4", "Team 2 formation"],
	])

	_add_section(col3, "OVERLAYS", [
		["M", "Cycle map mode"],
		["Shift+M", "Map mode off"],
		["Ctrl+M", "Local cover detail"],
		[", / .", "Cycle pheromone channel"],
	])

	_add_section(col3, "SELECTION", [
		["LMB", "Select unit (RTS) / Place threat (fly)"],
		["Double-click", "Follow selected unit"],
		["ESC", "Stop following unit"],
	])

	_add_section(col3, "DEBUG", [
		["F", "Destroy voxel at crosshair"],
		["C", "Clear threat markers"],
	])


func _add_section(parent: Control, title: String, bindings: Array) -> void:
	var header := Label.new()
	header.text = title
	header.add_theme_font_size_override("font_size", 12)
	header.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	parent.add_child(header)

	# Subtle underline for section header
	var underline := ColorRect.new()
	underline.custom_minimum_size = Vector2(0, 1)
	underline.color = Color(BattleCommandUI.ACCENT_COLOR, 0.15)
	underline.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(underline)

	for binding in bindings:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 10)
		parent.add_child(row)

		# Key badge in a PanelContainer for background
		var key_panel := PanelContainer.new()
		key_panel.custom_minimum_size = Vector2(120, 0)
		var key_sb := StyleBoxFlat.new()
		key_sb.bg_color = Color(0.1, 0.12, 0.16, 0.6)
		key_sb.corner_radius_top_left = 3
		key_sb.corner_radius_top_right = 3
		key_sb.corner_radius_bottom_left = 3
		key_sb.corner_radius_bottom_right = 3
		key_sb.content_margin_left = 6.0
		key_sb.content_margin_right = 6.0
		key_sb.content_margin_top = 1.0
		key_sb.content_margin_bottom = 1.0
		key_panel.add_theme_stylebox_override("panel", key_sb)
		row.add_child(key_panel)

		var key_lbl := Label.new()
		key_lbl.text = binding[0]
		key_lbl.add_theme_font_size_override("font_size", 11)
		key_lbl.add_theme_color_override("font_color", Color(0.75, 0.8, 0.85))
		key_panel.add_child(key_lbl)

		var desc_lbl := Label.new()
		desc_lbl.text = binding[1]
		desc_lbl.add_theme_font_size_override("font_size", 11)
		desc_lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
		row.add_child(desc_lbl)

	# Small spacer
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(0, 6)
	parent.add_child(spacer)
