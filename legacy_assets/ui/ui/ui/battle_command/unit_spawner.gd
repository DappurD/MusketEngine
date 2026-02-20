extends PanelContainer
## Unit spawner — modal overlay for placing units on the battlefield at runtime.

var _ui: BattleCommandUI
var _selected_team: int = 0
var _selected_role: int = 0
var _selected_count: int = 8
var _selected_formation: int = 0  # LINE
var _selected_personality: int = -1  # -1 = random
var _repeat_mode: bool = false

var _role_buttons: Array[Button] = []
var _count_buttons: Array[Button] = []
var _team_buttons: Array[Button] = []
var _status_label: Label
var _spawn_start_pos: Vector3 = Vector3.ZERO


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	# Dim overlay handled globally by BattleCommandUI._modal_dim
	anchors_preset = Control.PRESET_CENTER
	custom_minimum_size = Vector2(480, 0)

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
	title.text = "UNIT SPAWNER"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 18)
	title.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	vbox.add_child(title)

	var accent_line := ColorRect.new()
	accent_line.custom_minimum_size = Vector2(0, 2)
	accent_line.color = Color(BattleCommandUI.ACCENT_COLOR, 0.3)
	accent_line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(accent_line)

	# Team selection
	var team_row := HBoxContainer.new()
	team_row.add_theme_constant_override("separation", 12)
	team_row.alignment = BoxContainer.ALIGNMENT_CENTER
	vbox.add_child(team_row)

	var team_label := Label.new()
	team_label.text = "Team:"
	team_label.add_theme_font_size_override("font_size", 12)
	team_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	team_row.add_child(team_label)

	for i in 2:
		var btn := Button.new()
		btn.text = "Team %d" % (i + 1)
		btn.custom_minimum_size = Vector2(100, 28)
		btn.add_theme_font_size_override("font_size", 12)
		var team_idx: int = i
		btn.pressed.connect(func(): _select_team(team_idx))
		team_row.add_child(btn)
		_team_buttons.append(btn)
	_update_team_buttons()

	# Role grid
	var role_label := Label.new()
	role_label.text = "Role (press 1-7):"
	role_label.add_theme_font_size_override("font_size", 11)
	role_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	vbox.add_child(role_label)

	var role_grid := GridContainer.new()
	role_grid.columns = 4
	role_grid.add_theme_constant_override("h_separation", 8)
	role_grid.add_theme_constant_override("v_separation", 8)
	vbox.add_child(role_grid)

	var role_names: Array = ["Rifleman", "Leader", "Medic", "MG", "Marksman", "Grenadier", "Mortar"]
	var role_icons: Array = ["R", "L", "+", "M", "S", "G", "T"]
	for i in role_names.size():
		var btn := Button.new()
		btn.text = "%s\n%s [%d]" % [role_icons[i], role_names[i], i + 1]
		btn.custom_minimum_size = Vector2(100, 50)
		btn.add_theme_font_size_override("font_size", 11)
		var role_idx: int = i
		btn.pressed.connect(func(): _select_role(role_idx))
		role_grid.add_child(btn)
		_role_buttons.append(btn)
	_update_role_buttons()

	# Count selection
	var count_row := HBoxContainer.new()
	count_row.add_theme_constant_override("separation", 8)
	count_row.alignment = BoxContainer.ALIGNMENT_CENTER
	vbox.add_child(count_row)

	var count_label := Label.new()
	count_label.text = "Count:"
	count_label.add_theme_font_size_override("font_size", 12)
	count_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	count_row.add_child(count_label)

	for c in [1, 8, 50, 100]:
		var btn := Button.new()
		btn.text = str(c)
		btn.custom_minimum_size = Vector2(50, 28)
		btn.add_theme_font_size_override("font_size", 12)
		var count_val: int = c
		btn.pressed.connect(func(): _select_count(count_val))
		count_row.add_child(btn)
		_count_buttons.append(btn)
	_update_count_buttons()

	# Formation dropdown
	var form_row := HBoxContainer.new()
	form_row.add_theme_constant_override("separation", 8)
	vbox.add_child(form_row)

	var form_label := Label.new()
	form_label.text = "Formation:"
	form_label.add_theme_font_size_override("font_size", 11)
	form_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	form_row.add_child(form_label)

	var form_option := OptionButton.new()
	for f in BattleCommandUI.FORMATION_NAMES:
		form_option.add_item(f)
	form_option.selected = 0
	form_option.item_selected.connect(func(idx: int): _selected_formation = idx)
	form_row.add_child(form_option)

	# Instructions
	var instr_div := ColorRect.new()
	instr_div.custom_minimum_size = Vector2(0, 1)
	instr_div.color = Color(0.15, 0.17, 0.2)
	instr_div.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(instr_div)

	var instr := Label.new()
	instr.text = "Click on battlefield to spawn at location\nDrag to set facing direction\nShift+Click = repeat mode"
	instr.add_theme_font_size_override("font_size", 10)
	instr.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	instr.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(instr)

	# Quick spawn buttons
	var quick_row := HBoxContainer.new()
	quick_row.add_theme_constant_override("separation", 8)
	quick_row.alignment = BoxContainer.ALIGNMENT_CENTER
	vbox.add_child(quick_row)

	for pair in [["+ Squad (8)", 8], ["+ Platoon (32)", 32], ["+ Company (100)", 100]]:
		var btn := Button.new()
		btn.text = pair[0]
		btn.custom_minimum_size = Vector2(120, 28)
		btn.add_theme_font_size_override("font_size", 11)
		var cnt: int = pair[1]
		btn.pressed.connect(func(): _select_count(cnt); _quick_spawn_at_camera())
		quick_row.add_child(btn)

	# Status
	_status_label = Label.new()
	_status_label.text = "[N or ESC to close]"
	_status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_status_label.add_theme_font_size_override("font_size", 10)
	_status_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	vbox.add_child(_status_label)


func handle_key(event: InputEventKey) -> bool:
	if not event.pressed:
		return false
	# 1-7 select role
	var key: int = event.keycode
	if key >= KEY_1 and key <= KEY_7:
		_select_role(key - KEY_1)
		return true
	return false


func handle_click(event: InputEventMouseButton) -> void:
	if not _ui or not _ui.cam:
		return
	# Raycast from mouse position to world
	var camera: Camera3D = _ui.cam
	var mouse_pos: Vector2 = event.position
	var from: Vector3 = camera.project_ray_origin(mouse_pos)
	var dir: Vector3 = camera.project_ray_normal(mouse_pos)

	# Check for ground hit using voxel world
	if _ui.world:
		var hit = _ui.world.raycast_dict(from, dir, 500.0)
		if hit and hit is Dictionary and not hit.is_empty():
			var hit_pos: Vector3 = hit.get("hit_pos", Vector3.ZERO)
			_do_spawn(hit_pos, Vector3.FORWARD)
			_status_label.text = "Spawned %d at (%.0f, %.0f)" % [_selected_count, hit_pos.x, hit_pos.z]
			if not event.shift_pressed and _ui:
				# Auto-close after single spawn (shift = repeat mode)
				_ui._close_spawner()


func _do_spawn(pos: Vector3, facing: Vector3) -> void:
	if not _ui or not _ui.sim:
		_status_label.text = "ERROR: No simulation running"
		return

	# Check if sim has spawn_squad_at method (C++ runtime spawning)
	if _ui.sim.has_method("spawn_squad_at"):
		_ui.sim.spawn_squad_at(pos, facing, _selected_team + 1, _selected_count, _selected_role, _selected_formation)
		print("[Spawner] Spawned %d %s for Team %d at %s" % [
			_selected_count, BattleCommandUI.ROLE_NAMES.get(_selected_role, "?"), _selected_team + 1, str(pos)])
	else:
		# Fallback: notify user that C++ spawn API isn't available yet
		_status_label.text = "Runtime spawn needs C++ API (spawn_squad_at). Use U to restart with settings."
		print("[Spawner] spawn_squad_at() not available — need C++ implementation")


func _quick_spawn_at_camera() -> void:
	if not _ui or not _ui.cam:
		return
	var cam_pos: Vector3 = _ui.cam.global_position
	var cam_fwd: Vector3 = -_ui.cam.global_transform.basis.z
	# Project 20m forward from camera at ground level
	var spawn_pos := cam_pos + cam_fwd * 20.0
	# Query terrain height instead of hardcoding y=0
	if _ui.world and _ui.world.has_method("get_column_top_y"):
		var vx: int = int(spawn_pos.x / _ui.world.get_voxel_scale())
		var vz: int = int(spawn_pos.z / _ui.world.get_voxel_scale())
		spawn_pos.y = float(_ui.world.get_column_top_y(vx, vz)) * _ui.world.get_voxel_scale()
	else:
		spawn_pos.y = 0.0
	_do_spawn(spawn_pos, cam_fwd)


func _select_team(idx: int) -> void:
	_selected_team = idx
	_update_team_buttons()

func _select_role(idx: int) -> void:
	_selected_role = idx
	_update_role_buttons()

func _select_count(c: int) -> void:
	_selected_count = c
	_update_count_buttons()


func _update_team_buttons() -> void:
	for i in _team_buttons.size():
		var active: bool = (i == _selected_team)
		var color: Color = (BattleCommandUI.TEAM1_COLOR if i == 0 else BattleCommandUI.TEAM2_COLOR)
		_team_buttons[i].add_theme_color_override("font_color", color if active else BattleCommandUI.TEXT_SECONDARY)

func _update_role_buttons() -> void:
	for i in _role_buttons.size():
		var active: bool = (i == _selected_role)
		_role_buttons[i].add_theme_color_override("font_color",
			BattleCommandUI.ACCENT_COLOR if active else BattleCommandUI.TEXT_PRIMARY)

func _update_count_buttons() -> void:
	var counts: Array = [1, 8, 50, 100]
	for i in _count_buttons.size():
		var active: bool = (counts[i] == _selected_count)
		_count_buttons[i].add_theme_color_override("font_color",
			BattleCommandUI.ACCENT_COLOR if active else BattleCommandUI.TEXT_PRIMARY)
