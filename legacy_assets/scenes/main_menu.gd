extends Control
## Main menu for the AI Proving Ground. Scans scenarios and lets you pick one.

const SCENARIOS_DIR := "res://test/scenarios/"
const VOXEL_TEST_SCENE := "res://scenes/voxel_test.tscn"
const BASE_BUILDING_TEST_SCENE := "res://test/test_base_building.tscn"
const FREE_PLAY_MAP_TYPES: Array[String] = [
	"battlefield",
	"ruined_city",
	"mountain_pass",
	"river_valley",
]

var _seed_input: LineEdit
var _scenario_list: VBoxContainer
var _bb_scenario_list: VBoxContainer
var _status_label: Label
var _free_play_map_select: OptionButton

# Settings controls
var _time_select: OptionButton
var _cycle_check: CheckBox
var _cycle_speed_slider: HSlider
var _cycle_speed_label: Label
var _rain_check: CheckBox
var _units_slider: HSlider
var _units_label: Label
var _t1_formation: OptionButton
var _t2_formation: OptionButton
var _sim_speed_slider: HSlider
var _sim_speed_label: Label
var _squad_slider: HSlider
var _squad_label: Label

# World settings controls
var _voxel_scale_select: OptionButton
var _world_size_select: OptionButton
var _custom_size_row: HBoxContainer
var _custom_x_spin: SpinBox
var _custom_y_spin: SpinBox
var _custom_z_spin: SpinBox
var _mesh_threads_slider: HSlider
var _mesh_threads_label: Label
var _lod1_slider: HSlider
var _lod1_label: Label
var _lod2_slider: HSlider
var _lod2_label: Label
var _lod3_slider: HSlider
var _lod3_label: Label
var _vis_slider: HSlider
var _vis_label: Label
var _pool_check: CheckBox
var _superchunk_check: CheckBox
var _lod3_terrain_check: CheckBox

const TIME_PRESETS: Array[Dictionary] = [
	{"name": "Dawn (06:00)", "hour": 6.0},
	{"name": "Morning (09:00)", "hour": 9.0},
	{"name": "Noon (12:00)", "hour": 12.0},
	{"name": "Afternoon (15:00)", "hour": 15.0},
	{"name": "Golden Hour (17:30)", "hour": 17.5},
	{"name": "Dusk (19:00)", "hour": 19.0},
	{"name": "Night (22:00)", "hour": 22.0},
]
const FORMATION_NAMES: Array[String] = ["LINE", "WEDGE", "COLUMN", "CIRCLE"]

const VOXEL_SCALE_OPTIONS: Array[Dictionary] = [
	{"name": "0.10m (Ultra Fine)", "scale": 0.10},
	{"name": "0.15m (Fine)", "scale": 0.15},
	{"name": "0.20m (High)", "scale": 0.20},
	{"name": "0.25m (Standard)", "scale": 0.25},
	{"name": "0.50m (Low)", "scale": 0.50},
]
const VOXEL_SCALE_DEFAULT_IDX: int = 3  # 0.25m

const WORLD_SIZE_PRESETS: Array[Dictionary] = [
	{"name": "Micro (64m)", "x": 256, "y": 64, "z": 256},
	{"name": "Small (200m)", "x": 800, "y": 64, "z": 800},
	{"name": "Medium (300m)", "x": 1200, "y": 64, "z": 800},
	{"name": "Large (600m)", "x": 2400, "y": 128, "z": 1600},
	{"name": "Custom", "x": 0, "y": 0, "z": 0},
]
const WORLD_SIZE_DEFAULT_IDX: int = 3  # Large


func _ready() -> void:
	# Dark background
	var bg = ColorRect.new()
	bg.color = Color(0.08, 0.08, 0.12)
	bg.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	add_child(bg)

	# Main layout
	var margin = MarginContainer.new()
	margin.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 60)
	margin.add_theme_constant_override("margin_right", 60)
	margin.add_theme_constant_override("margin_top", 40)
	margin.add_theme_constant_override("margin_bottom", 40)
	add_child(margin)

	var vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 16)
	margin.add_child(vbox)

	# Title
	var title = Label.new()
	title.text = "V-SAF AI PROVING GROUND"
	title.add_theme_font_size_override("font_size", 36)
	title.add_theme_color_override("font_color", Color(0.9, 0.95, 1.0))
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(title)

	var subtitle = Label.new()
	subtitle.text = "Select a scenario or launch free play"
	subtitle.add_theme_font_size_override("font_size", 16)
	subtitle.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(subtitle)

	# Seed input row
	var seed_row = HBoxContainer.new()
	seed_row.alignment = BoxContainer.ALIGNMENT_CENTER
	seed_row.add_theme_constant_override("separation", 8)
	vbox.add_child(seed_row)

	var seed_label = Label.new()
	seed_label.text = "Seed (0 = random):"
	seed_label.add_theme_font_size_override("font_size", 14)
	seed_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	seed_row.add_child(seed_label)

	_seed_input = LineEdit.new()
	_seed_input.text = "0"
	_seed_input.custom_minimum_size = Vector2(120, 0)
	_seed_input.add_theme_font_size_override("font_size", 14)
	seed_row.add_child(_seed_input)

	# --- GAME SETTINGS PANEL ---
	_build_settings_panel(vbox)

	# Separator
	var sep = HSeparator.new()
	sep.add_theme_constant_override("separation", 8)
	vbox.add_child(sep)

	# Free play button
	var free_play_row = HBoxContainer.new()
	free_play_row.alignment = BoxContainer.ALIGNMENT_CENTER
	free_play_row.add_theme_constant_override("separation", 10)
	vbox.add_child(free_play_row)

	var map_label = Label.new()
	map_label.text = "Free Play Map:"
	map_label.add_theme_font_size_override("font_size", 14)
	map_label.add_theme_color_override("font_color", Color(0.65, 0.7, 0.78))
	free_play_row.add_child(map_label)

	_free_play_map_select = OptionButton.new()
	_free_play_map_select.custom_minimum_size = Vector2(220, 0)
	_free_play_map_select.add_theme_font_size_override("font_size", 14)
	for map_type: String in FREE_PLAY_MAP_TYPES:
		_free_play_map_select.add_item(map_type.replace("_", " ").to_upper())
	_free_play_map_select.select(0)
	free_play_row.add_child(_free_play_map_select)

	var free_play = Button.new()
	free_play.text = "FREE PLAY (Normal Interactive Mode)"
	free_play.custom_minimum_size = Vector2(0, 50)
	free_play.add_theme_font_size_override("font_size", 18)
	free_play.pressed.connect(_on_free_play)
	vbox.add_child(free_play)

	var sep2 = HSeparator.new()
	sep2.add_theme_constant_override("separation", 8)
	vbox.add_child(sep2)

	# Combat Scenarios header
	var scenarios_header = Label.new()
	scenarios_header.text = "COMBAT SCENARIOS"
	scenarios_header.add_theme_font_size_override("font_size", 20)
	scenarios_header.add_theme_color_override("font_color", Color(0.7, 0.8, 0.9))
	vbox.add_child(scenarios_header)

	# Scrollable scenario list
	var scroll = ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(0, 200)
	scroll.size_flags_vertical = SIZE_EXPAND_FILL
	vbox.add_child(scroll)

	_scenario_list = VBoxContainer.new()
	_scenario_list.size_flags_horizontal = SIZE_EXPAND_FILL
	_scenario_list.add_theme_constant_override("separation", 8)
	scroll.add_child(_scenario_list)

	# Base Building Scenarios header
	var sep3 = HSeparator.new()
	sep3.add_theme_constant_override("separation", 8)
	vbox.add_child(sep3)

	var bb_header = Label.new()
	bb_header.text = "BASE BUILDING SCENARIOS (Experimental)"
	bb_header.add_theme_font_size_override("font_size", 20)
	bb_header.add_theme_color_override("font_color", Color(0.7, 0.9, 0.7))
	vbox.add_child(bb_header)

	# Base building scenario list
	var bb_scroll = ScrollContainer.new()
	bb_scroll.custom_minimum_size = Vector2(0, 150)
	bb_scroll.size_flags_vertical = SIZE_EXPAND_FILL
	vbox.add_child(bb_scroll)

	_bb_scenario_list = VBoxContainer.new()
	_bb_scenario_list.size_flags_horizontal = SIZE_EXPAND_FILL
	_bb_scenario_list.add_theme_constant_override("separation", 8)
	bb_scroll.add_child(_bb_scenario_list)

	# Status label at bottom
	_status_label = Label.new()
	_status_label.add_theme_font_size_override("font_size", 13)
	_status_label.add_theme_color_override("font_color", Color(0.4, 0.45, 0.5))
	_status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(_status_label)

	_scan_scenarios()


func _build_settings_panel(parent: VBoxContainer) -> void:
	var panel = PanelContainer.new()
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.1, 0.1, 0.15)
	style.corner_radius_top_left = 8
	style.corner_radius_top_right = 8
	style.corner_radius_bottom_left = 8
	style.corner_radius_bottom_right = 8
	style.content_margin_left = 20
	style.content_margin_right = 20
	style.content_margin_top = 14
	style.content_margin_bottom = 14
	panel.add_theme_stylebox_override("panel", style)
	parent.add_child(panel)

	var content = VBoxContainer.new()
	content.add_theme_constant_override("separation", 10)
	panel.add_child(content)

	var header = Label.new()
	header.text = "GAME SETTINGS"
	header.add_theme_font_size_override("font_size", 18)
	header.add_theme_color_override("font_color", Color(0.8, 0.85, 0.95))
	content.add_child(header)

	# Two-column layout: Environment | Battle
	var columns = HBoxContainer.new()
	columns.add_theme_constant_override("separation", 40)
	content.add_child(columns)

	# --- Left column: Environment ---
	var env_col = VBoxContainer.new()
	env_col.size_flags_horizontal = SIZE_EXPAND_FILL
	env_col.add_theme_constant_override("separation", 6)
	columns.add_child(env_col)

	var env_header = Label.new()
	env_header.text = "Environment"
	env_header.add_theme_font_size_override("font_size", 15)
	env_header.add_theme_color_override("font_color", Color(0.6, 0.75, 0.9))
	env_col.add_child(env_header)

	# Time of Day
	var time_row = HBoxContainer.new()
	time_row.add_theme_constant_override("separation", 8)
	env_col.add_child(time_row)
	var time_label = Label.new()
	time_label.text = "Time of Day:"
	time_label.add_theme_font_size_override("font_size", 13)
	time_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	time_label.custom_minimum_size.x = 110
	time_row.add_child(time_label)
	_time_select = OptionButton.new()
	_time_select.custom_minimum_size = Vector2(180, 0)
	_time_select.add_theme_font_size_override("font_size", 13)
	for preset: Dictionary in TIME_PRESETS:
		_time_select.add_item(preset["name"])
	_time_select.select(2)  # Default: Noon
	time_row.add_child(_time_select)

	# Day/Night Cycle
	var cycle_row = HBoxContainer.new()
	cycle_row.add_theme_constant_override("separation", 8)
	env_col.add_child(cycle_row)
	var cycle_label = Label.new()
	cycle_label.text = "Day/Night Cycle:"
	cycle_label.add_theme_font_size_override("font_size", 13)
	cycle_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	cycle_label.custom_minimum_size.x = 110
	cycle_row.add_child(cycle_label)
	_cycle_check = CheckBox.new()
	_cycle_check.text = "Enable"
	_cycle_check.add_theme_font_size_override("font_size", 13)
	_cycle_check.toggled.connect(_on_cycle_toggled)
	cycle_row.add_child(_cycle_check)

	# Cycle Speed (only visible when cycle enabled)
	var speed_row = HBoxContainer.new()
	speed_row.add_theme_constant_override("separation", 8)
	env_col.add_child(speed_row)
	var speed_label = Label.new()
	speed_label.text = "Cycle Speed:"
	speed_label.add_theme_font_size_override("font_size", 13)
	speed_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	speed_label.custom_minimum_size.x = 110
	speed_row.add_child(speed_label)
	_cycle_speed_slider = HSlider.new()
	_cycle_speed_slider.min_value = 30.0
	_cycle_speed_slider.max_value = 600.0
	_cycle_speed_slider.step = 30.0
	_cycle_speed_slider.value = 120.0
	_cycle_speed_slider.custom_minimum_size = Vector2(120, 0)
	_cycle_speed_slider.value_changed.connect(_on_cycle_speed_changed)
	speed_row.add_child(_cycle_speed_slider)
	_cycle_speed_label = Label.new()
	_cycle_speed_label.text = "120x"
	_cycle_speed_label.add_theme_font_size_override("font_size", 13)
	_cycle_speed_label.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	_cycle_speed_label.custom_minimum_size.x = 50
	speed_row.add_child(_cycle_speed_label)
	speed_row.visible = false  # Hidden until cycle enabled

	# Rain
	var rain_row = HBoxContainer.new()
	rain_row.add_theme_constant_override("separation", 8)
	env_col.add_child(rain_row)
	var rain_label = Label.new()
	rain_label.text = "Weather:"
	rain_label.add_theme_font_size_override("font_size", 13)
	rain_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	rain_label.custom_minimum_size.x = 110
	rain_row.add_child(rain_label)
	_rain_check = CheckBox.new()
	_rain_check.text = "Rain"
	_rain_check.add_theme_font_size_override("font_size", 13)
	rain_row.add_child(_rain_check)

	# --- Right column: Battle ---
	var battle_col = VBoxContainer.new()
	battle_col.size_flags_horizontal = SIZE_EXPAND_FILL
	battle_col.add_theme_constant_override("separation", 6)
	columns.add_child(battle_col)

	var battle_header = Label.new()
	battle_header.text = "Battle"
	battle_header.add_theme_font_size_override("font_size", 15)
	battle_header.add_theme_color_override("font_color", Color(0.9, 0.7, 0.6))
	battle_col.add_child(battle_header)

	# Units per Team
	var units_row = HBoxContainer.new()
	units_row.add_theme_constant_override("separation", 8)
	battle_col.add_child(units_row)
	var units_label = Label.new()
	units_label.text = "Units/Team:"
	units_label.add_theme_font_size_override("font_size", 13)
	units_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	units_label.custom_minimum_size.x = 110
	units_row.add_child(units_label)
	_units_slider = HSlider.new()
	_units_slider.min_value = 50
	_units_slider.max_value = 1000
	_units_slider.step = 50
	_units_slider.value = 500
	_units_slider.custom_minimum_size = Vector2(120, 0)
	_units_slider.value_changed.connect(_on_units_changed)
	units_row.add_child(_units_slider)
	_units_label = Label.new()
	_units_label.text = "500"
	_units_label.add_theme_font_size_override("font_size", 13)
	_units_label.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	_units_label.custom_minimum_size.x = 40
	units_row.add_child(_units_label)

	# Team 1 Formation
	var t1_row = HBoxContainer.new()
	t1_row.add_theme_constant_override("separation", 8)
	battle_col.add_child(t1_row)
	var t1_label = Label.new()
	t1_label.text = "Team 1 Form:"
	t1_label.add_theme_font_size_override("font_size", 13)
	t1_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	t1_label.custom_minimum_size.x = 110
	t1_row.add_child(t1_label)
	_t1_formation = OptionButton.new()
	_t1_formation.custom_minimum_size = Vector2(130, 0)
	_t1_formation.add_theme_font_size_override("font_size", 13)
	for fname: String in FORMATION_NAMES:
		_t1_formation.add_item(fname)
	_t1_formation.select(0)  # LINE
	t1_row.add_child(_t1_formation)

	# Team 2 Formation
	var t2_row = HBoxContainer.new()
	t2_row.add_theme_constant_override("separation", 8)
	battle_col.add_child(t2_row)
	var t2_label = Label.new()
	t2_label.text = "Team 2 Form:"
	t2_label.add_theme_font_size_override("font_size", 13)
	t2_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	t2_label.custom_minimum_size.x = 110
	t2_row.add_child(t2_label)
	_t2_formation = OptionButton.new()
	_t2_formation.custom_minimum_size = Vector2(130, 0)
	_t2_formation.add_theme_font_size_override("font_size", 13)
	for fname2: String in FORMATION_NAMES:
		_t2_formation.add_item(fname2)
	_t2_formation.select(1)  # WEDGE
	t2_row.add_child(_t2_formation)

	# Sim Speed
	var sim_row = HBoxContainer.new()
	sim_row.add_theme_constant_override("separation", 8)
	battle_col.add_child(sim_row)
	var sim_label = Label.new()
	sim_label.text = "Sim Speed:"
	sim_label.add_theme_font_size_override("font_size", 13)
	sim_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	sim_label.custom_minimum_size.x = 110
	sim_row.add_child(sim_label)
	_sim_speed_slider = HSlider.new()
	_sim_speed_slider.min_value = 0.25
	_sim_speed_slider.max_value = 4.0
	_sim_speed_slider.step = 0.25
	_sim_speed_slider.value = 1.0
	_sim_speed_slider.custom_minimum_size = Vector2(120, 0)
	_sim_speed_slider.value_changed.connect(_on_sim_speed_changed)
	sim_row.add_child(_sim_speed_slider)
	_sim_speed_label = Label.new()
	_sim_speed_label.text = "1.0x"
	_sim_speed_label.add_theme_font_size_override("font_size", 13)
	_sim_speed_label.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	_sim_speed_label.custom_minimum_size.x = 40
	sim_row.add_child(_sim_speed_label)

	# Squad Size
	var squad_row = HBoxContainer.new()
	squad_row.add_theme_constant_override("separation", 8)
	battle_col.add_child(squad_row)
	var squad_label = Label.new()
	squad_label.text = "Squad Size:"
	squad_label.add_theme_font_size_override("font_size", 13)
	squad_label.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	squad_label.custom_minimum_size.x = 110
	squad_row.add_child(squad_label)
	_squad_slider = HSlider.new()
	_squad_slider.min_value = 4
	_squad_slider.max_value = 20
	_squad_slider.step = 2
	_squad_slider.value = 10
	_squad_slider.custom_minimum_size = Vector2(120, 0)
	_squad_slider.value_changed.connect(_on_squad_size_changed)
	squad_row.add_child(_squad_slider)
	_squad_label = Label.new()
	_squad_label.text = "10"
	_squad_label.add_theme_font_size_override("font_size", 13)
	_squad_label.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	_squad_label.custom_minimum_size.x = 40
	squad_row.add_child(_squad_label)

	# --- Third column: World ---
	var world_col = VBoxContainer.new()
	world_col.size_flags_horizontal = SIZE_EXPAND_FILL
	world_col.add_theme_constant_override("separation", 6)
	columns.add_child(world_col)

	var world_header = Label.new()
	world_header.text = "World"
	world_header.add_theme_font_size_override("font_size", 15)
	world_header.add_theme_color_override("font_color", Color(0.75, 0.9, 0.65))
	world_col.add_child(world_header)

	# Voxel Scale
	_add_world_row(world_col, "Voxel Scale:", func(row: HBoxContainer) -> void:
		_voxel_scale_select = OptionButton.new()
		_voxel_scale_select.custom_minimum_size = Vector2(160, 0)
		_voxel_scale_select.add_theme_font_size_override("font_size", 13)
		for opt: Dictionary in VOXEL_SCALE_OPTIONS:
			_voxel_scale_select.add_item(opt["name"])
		_voxel_scale_select.select(VOXEL_SCALE_DEFAULT_IDX)
		row.add_child(_voxel_scale_select)
	)

	# World Size
	_add_world_row(world_col, "World Size:", func(row: HBoxContainer) -> void:
		_world_size_select = OptionButton.new()
		_world_size_select.custom_minimum_size = Vector2(160, 0)
		_world_size_select.add_theme_font_size_override("font_size", 13)
		for preset: Dictionary in WORLD_SIZE_PRESETS:
			_world_size_select.add_item(preset["name"])
		_world_size_select.select(WORLD_SIZE_DEFAULT_IDX)
		_world_size_select.item_selected.connect(_on_world_size_changed)
		row.add_child(_world_size_select)
	)

	# Custom X/Y/Z (hidden unless Custom selected)
	_custom_size_row = HBoxContainer.new()
	_custom_size_row.add_theme_constant_override("separation", 4)
	_custom_size_row.visible = false
	world_col.add_child(_custom_size_row)
	_custom_x_spin = _make_spin("X:", 128, 8000, 2400)
	_custom_size_row.add_child(_custom_x_spin.get_parent())
	_custom_y_spin = _make_spin("Y:", 32, 512, 128)
	_custom_size_row.add_child(_custom_y_spin.get_parent())
	_custom_z_spin = _make_spin("Z:", 128, 8000, 1600)
	_custom_size_row.add_child(_custom_z_spin.get_parent())

	# Mesh Threads
	_add_world_slider(world_col, "Mesh Threads:", 1, 16, 1, 8, func(val: float) -> void:
		_mesh_threads_label.text = "%d" % int(val)
	, "_mesh_threads")

	# LOD 1 Distance
	_add_world_slider(world_col, "LOD 1 Dist:", 10, 100, 5, 30, func(val: float) -> void:
		_lod1_label.text = "%dm" % int(val)
	, "_lod1")

	# LOD 2 Distance
	_add_world_slider(world_col, "LOD 2 Dist:", 30, 300, 10, 80, func(val: float) -> void:
		_lod2_label.text = "%dm" % int(val)
	, "_lod2")

	# LOD 3 Distance
	_add_world_slider(world_col, "LOD 3 Dist:", 50, 400, 10, 150, func(val: float) -> void:
		_lod3_label.text = "%dm" % int(val)
	, "_lod3")

	# Visibility Radius
	_add_world_slider(world_col, "Visibility:", 100, 800, 25, 400, func(val: float) -> void:
		_vis_label.text = "%dm" % int(val)
	, "_vis")

	# Feature toggles
	var toggles_row = HBoxContainer.new()
	toggles_row.add_theme_constant_override("separation", 12)
	world_col.add_child(toggles_row)
	_pool_check = _make_check("Instance Pool", true)
	toggles_row.add_child(_pool_check)
	_superchunk_check = _make_check("SC LOD2", true)
	toggles_row.add_child(_superchunk_check)
	_lod3_terrain_check = _make_check("LOD3 Terrain", true)
	toggles_row.add_child(_lod3_terrain_check)


## Helper: create a labeled row in world column
func _add_world_row(parent: VBoxContainer, label_text: String, setup: Callable) -> void:
	var row = HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	parent.add_child(row)
	var lbl = Label.new()
	lbl.text = label_text
	lbl.add_theme_font_size_override("font_size", 13)
	lbl.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	lbl.custom_minimum_size.x = 90
	row.add_child(lbl)
	setup.call(row)


## Helper: create a slider row with value label (stores refs via field_prefix)
func _add_world_slider(parent: VBoxContainer, label_text: String,
		min_val: float, max_val: float, step_val: float, default_val: float,
		on_change: Callable, field_prefix: String) -> void:
	var row = HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	parent.add_child(row)
	var lbl = Label.new()
	lbl.text = label_text
	lbl.add_theme_font_size_override("font_size", 13)
	lbl.add_theme_color_override("font_color", Color(0.6, 0.65, 0.7))
	lbl.custom_minimum_size.x = 90
	row.add_child(lbl)
	var slider = HSlider.new()
	slider.min_value = min_val
	slider.max_value = max_val
	slider.step = step_val
	slider.value = default_val
	slider.custom_minimum_size = Vector2(100, 0)
	slider.value_changed.connect(on_change)
	row.add_child(slider)
	var val_label = Label.new()
	val_label.add_theme_font_size_override("font_size", 13)
	val_label.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	val_label.custom_minimum_size.x = 45
	row.add_child(val_label)
	# Store references by field name
	match field_prefix:
		"_mesh_threads":
			_mesh_threads_slider = slider
			_mesh_threads_label = val_label
			val_label.text = "%d" % int(default_val)
		"_lod1":
			_lod1_slider = slider
			_lod1_label = val_label
			val_label.text = "%dm" % int(default_val)
		"_lod2":
			_lod2_slider = slider
			_lod2_label = val_label
			val_label.text = "%dm" % int(default_val)
		"_lod3":
			_lod3_slider = slider
			_lod3_label = val_label
			val_label.text = "%dm" % int(default_val)
		"_vis":
			_vis_slider = slider
			_vis_label = val_label
			val_label.text = "%dm" % int(default_val)


## Helper: create a SpinBox with label inside an HBox
func _make_spin(label_text: String, min_val: int, max_val: int, default_val: int) -> SpinBox:
	var hbox = HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 2)
	var lbl = Label.new()
	lbl.text = label_text
	lbl.add_theme_font_size_override("font_size", 12)
	lbl.add_theme_color_override("font_color", Color(0.55, 0.6, 0.65))
	hbox.add_child(lbl)
	var spin = SpinBox.new()
	spin.min_value = min_val
	spin.max_value = max_val
	spin.step = 32
	spin.value = default_val
	spin.custom_minimum_size = Vector2(70, 0)
	spin.add_theme_font_size_override("font_size", 12)
	hbox.add_child(spin)
	return spin


## Helper: create a small checkbox
func _make_check(label_text: String, default_on: bool) -> CheckBox:
	var check = CheckBox.new()
	check.text = label_text
	check.button_pressed = default_on
	check.add_theme_font_size_override("font_size", 12)
	return check


func _on_world_size_changed(idx: int) -> void:
	# Show custom size inputs only when "Custom" is selected
	_custom_size_row.visible = (idx == WORLD_SIZE_PRESETS.size() - 1)


func _on_cycle_toggled(pressed: bool) -> void:
	# Show/hide cycle speed slider
	if _cycle_speed_slider:
		_cycle_speed_slider.get_parent().visible = pressed


func _on_cycle_speed_changed(value: float) -> void:
	if _cycle_speed_label:
		_cycle_speed_label.text = "%dx" % int(value)


func _on_units_changed(value: float) -> void:
	if _units_label:
		_units_label.text = "%d" % int(value)


func _on_sim_speed_changed(value: float) -> void:
	if _sim_speed_label:
		_sim_speed_label.text = "%.2gx" % value


func _on_squad_size_changed(value: float) -> void:
	if _squad_label:
		_squad_label.text = str(int(value))


func _apply_settings() -> void:
	# Write all settings to ScenarioState for the game scene to read
	var time_idx: int = _time_select.selected if _time_select else 2
	time_idx = clampi(time_idx, 0, TIME_PRESETS.size() - 1)
	ScenarioState.starting_hour = TIME_PRESETS[time_idx]["hour"]
	ScenarioState.day_night_cycle = _cycle_check.button_pressed if _cycle_check else false
	ScenarioState.day_night_speed = _cycle_speed_slider.value if _cycle_speed_slider else 120.0
	ScenarioState.rain_enabled = _rain_check.button_pressed if _rain_check else false
	ScenarioState.units_per_team = int(_units_slider.value) if _units_slider else 500
	ScenarioState.team1_formation = _t1_formation.selected if _t1_formation else 0
	ScenarioState.team2_formation = _t2_formation.selected if _t2_formation else 1
	ScenarioState.sim_time_scale = _sim_speed_slider.value if _sim_speed_slider else 1.0
	ScenarioState.squad_size = int(_squad_slider.value) if _squad_slider else 10
	ScenarioState.seed_override = int(_seed_input.text) if _seed_input.text.is_valid_int() else 0

	# World settings
	var scale_idx: int = _voxel_scale_select.selected if _voxel_scale_select else VOXEL_SCALE_DEFAULT_IDX
	scale_idx = clampi(scale_idx, 0, VOXEL_SCALE_OPTIONS.size() - 1)
	ScenarioState.world_voxel_scale = VOXEL_SCALE_OPTIONS[scale_idx]["scale"]

	var size_idx: int = _world_size_select.selected if _world_size_select else WORLD_SIZE_DEFAULT_IDX
	size_idx = clampi(size_idx, 0, WORLD_SIZE_PRESETS.size() - 1)
	ScenarioState.world_size_preset = WORLD_SIZE_PRESETS[size_idx]["name"]

	if size_idx == WORLD_SIZE_PRESETS.size() - 1:
		# Custom size
		ScenarioState.world_size_x = int(_custom_x_spin.value) if _custom_x_spin else 2400
		ScenarioState.world_size_y = int(_custom_y_spin.value) if _custom_y_spin else 128
		ScenarioState.world_size_z = int(_custom_z_spin.value) if _custom_z_spin else 1600
	else:
		ScenarioState.world_size_x = WORLD_SIZE_PRESETS[size_idx]["x"]
		ScenarioState.world_size_y = WORLD_SIZE_PRESETS[size_idx]["y"]
		ScenarioState.world_size_z = WORLD_SIZE_PRESETS[size_idx]["z"]

	ScenarioState.renderer_mesh_threads = int(_mesh_threads_slider.value) if _mesh_threads_slider else 8
	ScenarioState.renderer_lod1_distance = _lod1_slider.value if _lod1_slider else 30.0
	ScenarioState.renderer_lod2_distance = _lod2_slider.value if _lod2_slider else 80.0
	ScenarioState.renderer_lod3_distance = _lod3_slider.value if _lod3_slider else 150.0
	ScenarioState.renderer_visibility_radius = _vis_slider.value if _vis_slider else 400.0
	ScenarioState.renderer_use_instance_pool = _pool_check.button_pressed if _pool_check else true
	ScenarioState.renderer_use_superchunk_lod2 = _superchunk_check.button_pressed if _superchunk_check else true
	ScenarioState.renderer_use_lod3_terrain = _lod3_terrain_check.button_pressed if _lod3_terrain_check else true


func _scan_scenarios() -> void:
	var dir = DirAccess.open(SCENARIOS_DIR)
	if not dir:
		_status_label.text = "No scenarios directory found at %s" % SCENARIOS_DIR
		return

	var files: PackedStringArray = []
	dir.list_dir_begin()
	var file_name: String = dir.get_next()
	while not file_name.is_empty():
		if file_name.ends_with(".json"):
			files.append(file_name)
		file_name = dir.get_next()
	dir.list_dir_end()
	files.sort()

	if files.is_empty():
		_status_label.text = "No scenario files found"
		return

	var combat_count = 0
	var bb_count = 0

	for fname: String in files:
		var path: String = SCENARIOS_DIR + fname

		# Base building scenarios (bb_*.json)
		if fname.begins_with("bb_"):
			_add_base_building_card(fname, path)
			bb_count += 1
			continue

		# Combat scenarios (standard format)
		var config: ScenarioLoader.ScenarioConfig = ScenarioLoader.load_scenario(path)
		if config == null:
			continue

		var errors: Array[String] = ScenarioLoader.validate(config)
		if not errors.is_empty():
			continue

		_add_scenario_card(config)
		combat_count += 1

	_status_label.text = "%d combat scenario(s), %d base building scenario(s) loaded" % [combat_count, bb_count]


func _add_scenario_card(config: ScenarioLoader.ScenarioConfig) -> void:
	var card = PanelContainer.new()
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.12, 0.13, 0.18)
	style.corner_radius_top_left = 6
	style.corner_radius_top_right = 6
	style.corner_radius_bottom_left = 6
	style.corner_radius_bottom_right = 6
	style.content_margin_left = 16
	style.content_margin_right = 16
	style.content_margin_top = 12
	style.content_margin_bottom = 12
	card.add_theme_stylebox_override("panel", style)

	var hbox = HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 16)
	card.add_child(hbox)

	# Info column
	var info = VBoxContainer.new()
	info.size_flags_horizontal = SIZE_EXPAND_FILL
	info.add_theme_constant_override("separation", 4)
	hbox.add_child(info)

	var name_label = Label.new()
	name_label.text = config.name
	name_label.add_theme_font_size_override("font_size", 18)
	name_label.add_theme_color_override("font_color", Color(0.9, 0.92, 1.0))
	info.add_child(name_label)

	var desc_label = Label.new()
	desc_label.text = config.description
	desc_label.add_theme_font_size_override("font_size", 13)
	desc_label.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	desc_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.add_child(desc_label)

	# Tags row
	var tc1: ScenarioLoader.TeamConfig = config.teams[0] if config.teams.size() > 0 else null
	var tc2: ScenarioLoader.TeamConfig = config.teams[1] if config.teams.size() > 1 else null
	var units_str: String = ("%dv%d" % [tc1.units, tc2.units]) if (tc1 and tc2) else "?"
	var tier_color: Color
	match config.scale_tier:
		"micro": tier_color = Color(0.3, 0.8, 0.4)
		"medium": tier_color = Color(0.8, 0.7, 0.2)
		"large": tier_color = Color(0.9, 0.3, 0.3)
		_: tier_color = Color(0.5, 0.5, 0.5)

	var ws: Vector3i = config.map.world_size
	var map_str: String = "%dx%d %s" % [ws.x, ws.z, config.map.terrain_type]
	var meta_label = Label.new()
	meta_label.text = "%s | %s | %s | %ds" % [config.scale_tier.to_upper(), units_str, map_str, int(config.duration_sec)]
	meta_label.add_theme_font_size_override("font_size", 12)
	meta_label.add_theme_color_override("font_color", tier_color)
	info.add_child(meta_label)

	# Launch button
	var btn = Button.new()
	btn.text = "LAUNCH"
	btn.custom_minimum_size = Vector2(100, 40)
	btn.add_theme_font_size_override("font_size", 16)
	btn.pressed.connect(_on_scenario_selected.bind(config))
	hbox.add_child(btn)

	_scenario_list.add_child(card)


func _on_free_play() -> void:
	_apply_settings()
	ScenarioState.is_scenario_mode = false
	ScenarioState.scenario_config = null
	var idx: int = _free_play_map_select.selected if _free_play_map_select else 0
	idx = clampi(idx, 0, FREE_PLAY_MAP_TYPES.size() - 1)
	ScenarioState.free_play_terrain_type = FREE_PLAY_MAP_TYPES[idx]
	get_tree().change_scene_to_file(VOXEL_TEST_SCENE)


func _on_scenario_selected(config: ScenarioLoader.ScenarioConfig) -> void:
	_apply_settings()
	ScenarioState.is_scenario_mode = true
	ScenarioState.scenario_config = config
	# Scenario launch ignores free-play map override.
	ScenarioState.free_play_terrain_type = "battlefield"
	get_tree().change_scene_to_file(VOXEL_TEST_SCENE)


func _add_base_building_card(fname: String, path: String) -> void:
	# Parse basic info from JSON
	var file = FileAccess.open(path, FileAccess.READ)
	if not file:
		return

	var json = JSON.new()
	var err: Error = json.parse(file.get_as_text())
	file.close()

	if err != OK:
		return

	var data: Dictionary = json.data
	if not data is Dictionary:
		return

	var scenario_id: String = data.get("id", fname)
	var scenario_name: String = data.get("name", fname)
	var description: String = data.get("description", "")
	var duration: int = int(data.get("duration", 180))
	var test_focus: String = data.get("test_focus", "")

	# Create card
	var card = PanelContainer.new()
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.12, 0.18, 0.14)  # Slight green tint for BB scenarios
	style.corner_radius_top_left = 6
	style.corner_radius_top_right = 6
	style.corner_radius_bottom_left = 6
	style.corner_radius_bottom_right = 6
	style.content_margin_left = 16
	style.content_margin_right = 16
	style.content_margin_top = 12
	style.content_margin_bottom = 12
	card.add_theme_stylebox_override("panel", style)

	var hbox = HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 16)
	card.add_child(hbox)

	# Info column
	var info = VBoxContainer.new()
	info.size_flags_horizontal = SIZE_EXPAND_FILL
	info.add_theme_constant_override("separation", 4)
	hbox.add_child(info)

	var name_label = Label.new()
	name_label.text = scenario_name
	name_label.add_theme_font_size_override("font_size", 18)
	name_label.add_theme_color_override("font_color", Color(0.8, 1.0, 0.85))
	info.add_child(name_label)

	var desc_label = Label.new()
	desc_label.text = description if not description.is_empty() else test_focus
	desc_label.add_theme_font_size_override("font_size", 13)
	desc_label.add_theme_color_override("font_color", Color(0.5, 0.6, 0.55))
	desc_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.add_child(desc_label)

	# Meta info
	var meta_label = Label.new()
	meta_label.text = "EXPERIMENTAL | %ds | Manual Setup Required" % duration
	meta_label.add_theme_font_size_override("font_size", 12)
	meta_label.add_theme_color_override("font_color", Color(0.6, 0.8, 0.6))
	info.add_child(meta_label)

	# Launch button
	var btn = Button.new()
	btn.text = "LAUNCH TEST"
	btn.custom_minimum_size = Vector2(120, 40)
	btn.add_theme_font_size_override("font_size", 16)
	btn.pressed.connect(_on_base_building_selected.bind(scenario_id))
	hbox.add_child(btn)

	_bb_scenario_list.add_child(card)


func _on_base_building_selected(scenario_id: String) -> void:
	# Pass scenario info to ScenarioState
	ScenarioState.is_bb_scenario = true
	ScenarioState.bb_scenario_path = SCENARIOS_DIR + scenario_id + ".json"
	ScenarioState.seed_override = int(_seed_input.text) if _seed_input.text.is_valid_int() else 0
	print("[MainMenu] Launching base building scenario: %s" % scenario_id)
	get_tree().change_scene_to_file(BASE_BUILDING_TEST_SCENE)
