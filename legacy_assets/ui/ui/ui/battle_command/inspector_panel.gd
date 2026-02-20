extends PanelContainer
## Inspector panel — left drawer with Unit/Squad/Voxel tabs.

var _ui: BattleCommandUI
var _tab_container: VBoxContainer
var _tab_buttons: Array[Button] = []
var _tab_content: Array[Control] = []
var _current_tab: int = 0
var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.15

# Unit tab labels
var _unit_role_label: Label
var _unit_state_label: Label
var _unit_hp_bar: ColorRect
var _unit_hp_bg: ColorRect
var _unit_hp_label: Label
var _unit_morale_bar: ColorRect
var _unit_morale_bg: ColorRect
var _unit_morale_label: Label
var _unit_suppress_bar: ColorRect
var _unit_suppress_bg: ColorRect
var _unit_suppress_label: Label
var _unit_ammo_label: Label
var _unit_posture_label: Label
var _unit_personality_label: Label
var _unit_movement_label: Label
var _unit_target_label: Label
var _unit_squad_label: Label
var _unit_peek_label: Label
var _unit_reasoning_label: Label  # "Why is this unit doing that?" context

# Squad tab labels
var _squad_id_label: Label
var _squad_formation_label: Label
var _squad_alive_label: Label
var _squad_morale_label: Label
var _squad_goal_label: Label
var _squad_coord_label: Label
var _squad_advance_label: Label
var _squad_llm_label: Label  # LLM directive display

# Voxel tab labels
var _voxel_coord_label: Label
var _voxel_material_label: Label
var _voxel_cover_label: Label
var _voxel_threat_label: Label
var _voxel_gas_label: Label
var _voxel_distance_label: Label


func setup(ui: BattleCommandUI) -> void:
	_ui = ui
	_build()


func _build() -> void:
	# Left edge, below top bar
	anchors_preset = Control.PRESET_LEFT_WIDE
	offset_top = 42.0
	offset_bottom = -10.0
	offset_left = 10.0
	custom_minimum_size = Vector2(280, 0)
	size = Vector2(280, 0)
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
	sb.shadow_offset = Vector2(2, 2)
	add_theme_stylebox_override("panel", sb)

	_tab_container = VBoxContainer.new()
	_tab_container.add_theme_constant_override("separation", 4)
	add_child(_tab_container)

	# Tab buttons
	var tab_row := HBoxContainer.new()
	tab_row.add_theme_constant_override("separation", 4)
	_tab_container.add_child(tab_row)

	for tab_name in ["Unit", "Squad", "Voxel"]:
		var btn := Button.new()
		btn.text = tab_name
		btn.custom_minimum_size = Vector2(78, 26)
		btn.add_theme_font_size_override("font_size", 11)
		var tab_style := StyleBoxFlat.new()
		tab_style.bg_color = Color(0.08, 0.09, 0.12, 0.6)
		tab_style.corner_radius_top_left = 4
		tab_style.corner_radius_top_right = 4
		tab_style.corner_radius_bottom_left = 4
		tab_style.corner_radius_bottom_right = 4
		tab_style.content_margin_left = 6.0
		tab_style.content_margin_right = 6.0
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

	# Tab divider
	var divider := ColorRect.new()
	divider.custom_minimum_size = Vector2(0, 1)
	divider.color = Color(0.12, 0.14, 0.18)
	divider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_tab_container.add_child(divider)

	# Build tab content panels
	_tab_content.append(_build_unit_tab())
	_tab_content.append(_build_squad_tab())
	_tab_content.append(_build_voxel_tab())

	for c in _tab_content:
		_tab_container.add_child(c)

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
			active_sb.content_margin_left = 6.0
			active_sb.content_margin_right = 6.0
			_tab_buttons[i].add_theme_stylebox_override("normal", active_sb)
		else:
			_tab_buttons[i].add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
			var idle_sb := StyleBoxFlat.new()
			idle_sb.bg_color = Color(0.08, 0.09, 0.12, 0.6)
			idle_sb.corner_radius_top_left = 4
			idle_sb.corner_radius_top_right = 4
			idle_sb.corner_radius_bottom_left = 4
			idle_sb.corner_radius_bottom_right = 4
			idle_sb.content_margin_left = 6.0
			idle_sb.content_margin_right = 6.0
			_tab_buttons[i].add_theme_stylebox_override("normal", idle_sb)


func on_unit_selected(unit_id: int) -> void:
	set_tab(0)
	if not visible:
		visible = true
		if _ui:
			_ui._update_bottom_layout()


func _build_unit_tab() -> VBoxContainer:
	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 3)

	_unit_role_label = _add_label(vbox, "-- No Unit Selected --", 13, BattleCommandUI.ACCENT_COLOR)
	_unit_state_label = _add_label(vbox, "State: --", 11, BattleCommandUI.TEXT_SECONDARY)

	# HP bar
	var hp_row := _make_bar_row("HP", BattleCommandUI.TEAM1_COLOR)
	_unit_hp_bg = hp_row[0]
	_unit_hp_bar = hp_row[1]
	_unit_hp_label = hp_row[2]
	vbox.add_child(hp_row[3])

	# Morale bar
	var mor_row := _make_bar_row("MRL", Color(0.3, 0.5, 1.0))
	_unit_morale_bg = mor_row[0]
	_unit_morale_bar = mor_row[1]
	_unit_morale_label = mor_row[2]
	vbox.add_child(mor_row[3])

	# Suppression bar
	var sup_row := _make_bar_row("SUP", Color(1.0, 0.6, 0.1))
	_unit_suppress_bg = sup_row[0]
	_unit_suppress_bar = sup_row[1]
	_unit_suppress_label = sup_row[2]
	vbox.add_child(sup_row[3])

	var sep := ColorRect.new()
	sep.custom_minimum_size = Vector2(0, 1)
	sep.color = Color(0.12, 0.14, 0.18)
	sep.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	vbox.add_child(sep)

	_unit_ammo_label = _add_label(vbox, "Ammo: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_unit_posture_label = _add_label(vbox, "Posture: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_unit_personality_label = _add_label(vbox, "Personality: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_unit_movement_label = _add_label(vbox, "Movement: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_unit_target_label = _add_label(vbox, "Target: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_unit_squad_label = _add_label(vbox, "Squad: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_unit_peek_label = _add_label(vbox, "", 10, BattleCommandUI.TEXT_SECONDARY)
	_unit_reasoning_label = _add_label(vbox, "", 10, BattleCommandUI.WARNING_COLOR)

	# Action buttons row
	var action_row := HBoxContainer.new()
	action_row.add_theme_constant_override("separation", 6)
	vbox.add_child(action_row)

	var follow_btn := Button.new()
	follow_btn.text = "Follow"
	follow_btn.custom_minimum_size = Vector2(70, 24)
	follow_btn.add_theme_font_size_override("font_size", 10)
	_apply_action_button_style(follow_btn, BattleCommandUI.ACCENT_COLOR)
	follow_btn.pressed.connect(_on_follow_pressed)
	action_row.add_child(follow_btn)

	var squad_btn := Button.new()
	squad_btn.text = "Select Squad"
	squad_btn.custom_minimum_size = Vector2(90, 24)
	squad_btn.add_theme_font_size_override("font_size", 10)
	_apply_action_button_style(squad_btn, BattleCommandUI.TEXT_SECONDARY)
	squad_btn.pressed.connect(_on_select_squad_pressed)
	action_row.add_child(squad_btn)

	# Steering rose placeholder
	var rose_label := _add_label(vbox, "[I/D Rose: see Ctrl+1]", 10, BattleCommandUI.TEXT_SECONDARY)
	rose_label.tooltip_text = "8-direction interest/danger steering rose"

	return vbox


func _build_squad_tab() -> VBoxContainer:
	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 3)

	_squad_id_label = _add_label(vbox, "-- No Squad Selected --", 13, BattleCommandUI.ACCENT_COLOR)
	_squad_formation_label = _add_label(vbox, "Formation: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_squad_alive_label = _add_label(vbox, "Alive: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_squad_morale_label = _add_label(vbox, "Morale: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_squad_goal_label = _add_label(vbox, "Goal: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_squad_coord_label = _add_label(vbox, "Coord: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_squad_advance_label = _add_label(vbox, "Advance: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_squad_llm_label = _add_label(vbox, "", 10, Color(1.0, 0.85, 0.3))  # Gold — matches commentator bark color

	return vbox


func _build_voxel_tab() -> VBoxContainer:
	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 3)

	var header := _add_label(vbox, "CROSSHAIR INFO", 12, BattleCommandUI.ACCENT_COLOR)
	header.tooltip_text = "Data at camera crosshair position"
	_voxel_coord_label = _add_label(vbox, "Voxel: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_voxel_material_label = _add_label(vbox, "Material: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_voxel_cover_label = _add_label(vbox, "Cover: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_voxel_threat_label = _add_label(vbox, "Threat: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_voxel_gas_label = _add_label(vbox, "Gas: --", 11, BattleCommandUI.TEXT_PRIMARY)
	_voxel_distance_label = _add_label(vbox, "Distance: --", 11, BattleCommandUI.TEXT_SECONDARY)

	return vbox


func update_data(delta: float) -> void:
	_update_timer += delta
	if _update_timer < UPDATE_INTERVAL:
		return
	_update_timer = 0.0

	if not _ui or not _ui.sim:
		return

	match _current_tab:
		0: _update_unit_tab()
		1: _update_squad_tab()
		2: _update_voxel_tab()


func _update_unit_tab() -> void:
	var uid: int = _ui.selected_unit_id
	var sim: SimulationServer = _ui.sim
	# Bounds check: uid may be stale after sim restart
	if uid < 0 or uid >= sim.get_unit_count() or not sim.is_alive(uid):
		_unit_role_label.text = "-- No Unit Selected --"
		_unit_role_label.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
		return

	var role: int = sim.get_role(uid)
	var team: int = sim.get_team(uid)
	var state: int = sim.get_state(uid)
	var team_color: Color = BattleCommandUI.TEAM1_COLOR if team == 0 else BattleCommandUI.TEAM2_COLOR

	_unit_role_label.text = "[%s] %s  T%d #%d" % [
		BattleCommandUI.ROLE_ICONS.get(role, "?"),
		BattleCommandUI.ROLE_NAMES.get(role, "Unknown"),
		team + 1, uid]
	_unit_role_label.add_theme_color_override("font_color", team_color)

	var state_name: String = BattleCommandUI.STATE_NAMES.get(state, "?")
	var state_color: Color = BattleCommandUI.STATE_COLORS.get(state, BattleCommandUI.TEXT_SECONDARY)
	_unit_state_label.text = "State: %s" % state_name
	_unit_state_label.add_theme_color_override("font_color", state_color)

	# Bars
	var hp: float = sim.get_health(uid)
	_unit_hp_bar.size.x = 160.0 * hp
	_unit_hp_bar.color = BattleCommandUI.ratio_color(hp)
	_unit_hp_label.text = "%d%%" % int(hp * 100)

	var morale: float = sim.get_morale(uid)
	_unit_morale_bar.size.x = 160.0 * morale
	_unit_morale_bar.color = BattleCommandUI.ratio_color(morale)
	_unit_morale_label.text = "%d%%" % int(morale * 100)

	var sup: float = sim.get_suppression(uid)
	_unit_suppress_bar.size.x = 160.0 * sup
	_unit_suppress_label.text = "%d%%" % int(sup * 100)

	# Details
	var ammo: int = sim.get_ammo(uid)
	var mag: int = sim.get_mag_size(uid)
	_unit_ammo_label.text = "Ammo: %d/%d" % [ammo, mag]

	var posture: int = sim.get_posture(uid)
	_unit_posture_label.text = "Posture: %s" % BattleCommandUI.POSTURE_NAMES.get(posture, "?")

	var pers: int = sim.get_unit_personality(uid)
	_unit_personality_label.text = "Personality: %s" % BattleCommandUI.PERSONALITY_NAMES.get(pers, "?")

	var mmode: int = sim.get_unit_movement_mode(uid)
	_unit_movement_label.text = "Movement: %s" % BattleCommandUI.MOVEMENT_MODE_NAMES.get(mmode, "?")

	var target: int = sim.get_target(uid)
	if target >= 0 and target < sim.get_unit_count() and sim.is_alive(target):
		var tpos: Vector3 = sim.get_position(target)
		var upos: Vector3 = sim.get_position(uid)
		var dist: float = upos.distance_to(tpos)
		var vis: bool = sim.team_can_see(team, target)
		_unit_target_label.text = "Target: #%d  %.0fm  %s" % [target, dist, "LOS" if vis else "FOW"]
	elif target >= 0:
		_unit_target_label.text = "Target: #%d (dead)" % target
	else:
		_unit_target_label.text = "Target: none"

	var sq: int = sim.get_squad_id(uid)
	_unit_squad_label.text = "Squad: %d" % sq

	# Peek state
	if sim.has_method("is_peeking") and sim.is_peeking(uid):
		_unit_peek_label.text = "Peeking"
		_unit_peek_label.add_theme_color_override("font_color", BattleCommandUI.ACCENT_COLOR)
	else:
		_unit_peek_label.text = ""

	# Unit reasoning — explain WHY the unit is in its current state
	_unit_reasoning_label.text = _get_unit_reasoning(sim, uid, state, morale, sup, pers)


func _on_follow_pressed() -> void:
	if _ui and _ui.selected_unit_id >= 0:
		_ui.start_follow_cam(_ui.selected_unit_id)


func _on_select_squad_pressed() -> void:
	if not _ui or not _ui.sim:
		return
	var uid: int = _ui.selected_unit_id
	if uid < 0 or uid >= _ui.sim.get_unit_count():
		return
	var sq: int = _ui.sim.get_squad_id(uid)
	_ui.select_squad(sq)
	set_tab(1)  # Switch to squad tab


func _apply_action_button_style(btn: Button, accent: Color) -> void:
	var normal := StyleBoxFlat.new()
	normal.bg_color = Color(accent, 0.15)
	normal.border_width_bottom = 1
	normal.border_color = Color(accent, 0.4)
	normal.corner_radius_top_left = 3
	normal.corner_radius_top_right = 3
	normal.corner_radius_bottom_left = 3
	normal.corner_radius_bottom_right = 3
	normal.content_margin_left = 6.0
	normal.content_margin_right = 6.0
	normal.content_margin_top = 2.0
	normal.content_margin_bottom = 2.0
	btn.add_theme_stylebox_override("normal", normal)
	btn.add_theme_color_override("font_color", accent)
	var hover := normal.duplicate()
	hover.bg_color = Color(accent, 0.25)
	btn.add_theme_stylebox_override("hover", hover)
	var pressed := normal.duplicate()
	pressed.bg_color = Color(accent, 0.35)
	btn.add_theme_stylebox_override("pressed", pressed)


func _update_squad_tab() -> void:
	var sq_id: int = _ui.selected_squad_id
	if sq_id < 0:
		_squad_id_label.text = "-- No Squad Selected --"
		return

	var sim: SimulationServer = _ui.sim
	# Bounds check — squad ID from previous sim may be invalid
	var max_squads: int = sim.get_unit_count() / 8 + 1  # rough upper bound
	if sq_id >= max_squads * 2:
		_squad_id_label.text = "-- Invalid Squad --"
		return

	_squad_id_label.text = "Squad #%d" % sq_id

	var form: int = sim.get_squad_formation(sq_id)
	var form_name: String = BattleCommandUI.FORMATION_NAMES[form] if form >= 0 and form < 4 else "?"
	_squad_formation_label.text = "Formation: %s  spread: %.0fm" % [form_name, sim.get_squad_formation_spread(sq_id)]

	var alive: int = sim.get_squad_alive_count(sq_id)
	_squad_alive_label.text = "Alive: %d" % alive

	# Squad morale — average from alive members
	var sum_morale: float = 0.0
	var morale_count: int = 0
	for uid in sim.get_unit_count():
		if sim.is_alive(uid) and sim.get_squad_id(uid) == sq_id:
			sum_morale += sim.get_morale(uid)
			morale_count += 1
	if morale_count > 0:
		var avg_m: float = sum_morale / float(morale_count)
		_squad_morale_label.text = "Morale: %d%%" % int(avg_m * 100)
		_squad_morale_label.add_theme_color_override("font_color", BattleCommandUI.ratio_color(avg_m))
	else:
		_squad_morale_label.text = "Morale: --"

	# Centroid
	var centroid: Vector3 = sim.get_squad_centroid(sq_id)
	_squad_advance_label.text = "Centroid: (%.0f, %.0f)" % [centroid.x, centroid.z]

	# Colony goal — check correct team's colony AI (team2 squads offset by 64)
	var colony = _ui.colony_t1
	if sq_id >= 64 and _ui.colony_t2:
		colony = _ui.colony_t2
	if colony and colony.has_method("get_debug_info"):
		var info: Dictionary = colony.get_debug_info()
		if info.has("assignments"):
			var assignments: Dictionary = info["assignments"]
			if assignments.has(sq_id):
				_squad_goal_label.text = "Goal: %s" % str(assignments[sq_id])
			else:
				_squad_goal_label.text = "Goal: unassigned"
	else:
		_squad_goal_label.text = "Goal: --"

	# Coordination tag
	if colony and colony.has_method("get_squad_coord_tag"):
		var tag: int = colony.get_squad_coord_tag(sq_id)
		_squad_coord_label.text = "Coord tag: %d" % tag if tag > 0 else "Coord: none"
	else:
		_squad_coord_label.text = "Coord: --"

	# LLM directive (if sector commander active)
	_squad_llm_label.text = _get_squad_llm_directive(sq_id)


func _get_unit_reasoning(sim: SimulationServer, uid: int, state: int,
		morale: float, sup: float, pers: int) -> String:
	## Explain WHY the unit is in its current state.
	var pers_name: String = BattleCommandUI.PERSONALITY_NAMES.get(pers, "?")
	match state:
		9:  # ST_BERSERK
			return "BERSERK: %s personality, morale %.0f%%" % [pers_name, morale * 100]
		10:  # ST_FROZEN
			return "FROZEN: %s personality, morale %.0f%%" % [pers_name, morale * 100]
		6:  # ST_RETREATING
			return "Retreating: morale %.0f%% (below threshold)" % [morale * 100]
		4:  # ST_SUPPRESSING
			return "Suppressing: blind fire at last-known pos"
		3:  # ST_IN_COVER
			if sup > 0.5:
				return "In cover: suppression %.0f%%" % [sup * 100]
			return "In cover: seeking protection"
		5:  # ST_FLANKING
			return "Flanking: maneuvering for angle"
		7:  # ST_RELOADING
			return "Reloading weapon"
		12:  # ST_CLIMBING
			return "Climbing: vertical traversal"
		13:  # ST_FALLING
			return "Falling!"
	return ""


func _get_squad_llm_directive(sq_id: int) -> String:
	## Get LLM directive for this squad from sector commander.
	var cs = _ui.camera_script
	if not cs:
		return ""

	# Determine which sector commander handles this squad
	var sector_cmd = null
	if sq_id < 64 and cs.has("_llm_sector_cmd"):
		sector_cmd = cs.get("_llm_sector_cmd")
	elif sq_id >= 64 and cs.has("_llm_sector_cmd_t2"):
		sector_cmd = cs.get("_llm_sector_cmd_t2")

	if not sector_cmd or not sector_cmd.has_method("get_current_orders"):
		return ""

	var orders: Array = sector_cmd.get_current_orders()
	for order in orders:
		if order.get("squad", -1) == (sq_id if sq_id < 64 else sq_id - 64):
			var sector: String = order.get("sector", "?")
			var intent: String = order.get("intent", "?")
			var conf: float = order.get("confidence", 0.0)
			return "LLM: %s %s (conf: %.0f%%)" % [intent, sector, conf * 100]

	return ""


func _update_voxel_tab() -> void:
	if not _ui.cam or not _ui.world:
		return

	# Raycast from camera center
	var cam_pos: Vector3 = _ui.cam.global_position
	var cam_fwd: Vector3 = -_ui.cam.global_transform.basis.z
	var hit = _ui.world.raycast_dict(cam_pos, cam_fwd, 200.0)
	if not hit or (hit is Dictionary and hit.is_empty()):
		_voxel_coord_label.text = "Voxel: (no hit)"
		_voxel_material_label.text = "Material: --"
		_voxel_cover_label.text = "Cover: --"
		_voxel_threat_label.text = "Threat: --"
		_voxel_gas_label.text = "Gas: --"
		_voxel_distance_label.text = "Distance: --"
		return

	# VoxelHit fields: hit_pos (Vector3), voxel_x/y/z, material_id, distance
	if hit is Dictionary:
		var vx: int = hit.get("voxel_x", 0)
		var vy: int = hit.get("voxel_y", 0)
		var vz: int = hit.get("voxel_z", 0)
		var mat_id: int = hit.get("material_id", 0)
		var dist: float = hit.get("distance", 0.0)
		var hit_pos: Vector3 = hit.get("hit_pos", Vector3.ZERO)

		_voxel_coord_label.text = "Voxel: [%d, %d, %d]" % [vx, vy, vz]

		var mat_names: Array[String] = ["Air", "Dirt", "Stone", "Wood", "Steel", "Concrete",
			"Brick", "Glass", "Sand", "Water", "Grass", "Gravel", "Sandbag", "Clay", "MetalPlate", "Rust"]
		var mat_name: String = mat_names[mat_id] if mat_id < mat_names.size() else "ID:%d" % mat_id
		_voxel_material_label.text = "Material: %s" % mat_name

		# Cover/threat from tactical systems
		if _ui.cover_map:
			var cover_val: float = _ui.cover_map.get_best_cover_at(hit_pos)
			_voxel_cover_label.text = "Cover: %.2f" % cover_val

		if _ui.influence_map:
			var threat: float = _ui.influence_map.get_threat_at(hit_pos)
			_voxel_threat_label.text = "Threat: %.2f" % threat

		if _ui.gpu_map and _ui.gpu_map.has_method("sample_gas_density"):
			var gas_d: float = _ui.gpu_map.sample_gas_density(hit_pos)
			if gas_d > 0.01:
				var gas_type: int = _ui.gpu_map.sample_gas_type(hit_pos)
				var gas_names: Array[String] = ["None", "Smoke", "Tear Gas", "Toxic"]
				_voxel_gas_label.text = "Gas: %s (%.2f)" % [gas_names[gas_type] if gas_type < gas_names.size() else "?", gas_d]
			else:
				_voxel_gas_label.text = "Gas: clear"

		_voxel_distance_label.text = "Distance: %.1fm" % dist


# ── Helpers ─────────────────────────────────────────────────────────

func _add_label(parent: Control, text: String, font_size: int, color: Color) -> Label:
	var lbl := Label.new()
	lbl.text = text
	lbl.add_theme_font_size_override("font_size", font_size)
	lbl.add_theme_color_override("font_color", color)
	lbl.autowrap_mode = TextServer.AUTOWRAP_WORD
	parent.add_child(lbl)
	return lbl


## Returns [bg_rect, fill_rect, value_label, container_hbox]
func _make_bar_row(label_text: String, fill_color: Color) -> Array:
	var hbox := HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 6)

	var lbl := Label.new()
	lbl.text = label_text
	lbl.custom_minimum_size = Vector2(30, 0)
	lbl.add_theme_font_size_override("font_size", 10)
	lbl.add_theme_color_override("font_color", BattleCommandUI.TEXT_SECONDARY)
	hbox.add_child(lbl)

	var bar_container := Control.new()
	bar_container.custom_minimum_size = Vector2(160, 10)
	bar_container.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	hbox.add_child(bar_container)

	var bg := ColorRect.new()
	bg.color = Color(0.15, 0.16, 0.18)
	bg.position = Vector2.ZERO
	bg.size = Vector2(160, 10)
	bar_container.add_child(bg)

	var fill := ColorRect.new()
	fill.color = fill_color
	fill.position = Vector2.ZERO
	fill.size = Vector2(160, 10)
	bar_container.add_child(fill)

	var val_label := Label.new()
	val_label.text = "0%"
	val_label.custom_minimum_size = Vector2(35, 0)
	val_label.add_theme_font_size_override("font_size", 10)
	val_label.add_theme_color_override("font_color", BattleCommandUI.TEXT_PRIMARY)
	hbox.add_child(val_label)

	return [bg, fill, val_label, hbox]
