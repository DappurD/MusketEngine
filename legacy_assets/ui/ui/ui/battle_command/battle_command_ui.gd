extends CanvasLayer
class_name BattleCommandUI
## BattleCommand: Unified battlefield debug & command interface.
## Root CanvasLayer that owns all panels, handles input routing, and
## coordinates the UI lifecycle.
##
## Usage: camera creates this in _ready(), calls setup() with system refs,
## then calls update(delta) every frame.

# ── Theme Constants (shared across all panels) ──────────────────────
const BG_COLOR := Color(0.08, 0.09, 0.11, 0.88)
const BG_SOLID := Color(0.08, 0.09, 0.11, 1.0)
const BORDER_COLOR := Color(0.2, 0.22, 0.25)
const TEXT_PRIMARY := Color(0.9, 0.92, 0.9)
const TEXT_SECONDARY := Color(0.55, 0.58, 0.6)
const TEAM1_COLOR := Color(0.2, 0.75, 0.4)
const TEAM2_COLOR := Color(0.9, 0.25, 0.25)
const ACCENT_COLOR := Color(0.3, 0.7, 1.0)
const WARNING_COLOR := Color(1.0, 0.75, 0.2)
const CRITICAL_COLOR := Color(1.0, 0.2, 0.2)

const STATE_COLORS := {
	0: Color(0.5, 0.5, 0.5),    # ST_IDLE - gray
	1: Color(0.3, 0.7, 1.0),    # ST_MOVING - blue
	2: Color(1.0, 0.3, 0.2),    # ST_ENGAGING - red
	3: Color(0.2, 0.8, 0.4),    # ST_IN_COVER - green
	4: Color(1.0, 0.6, 0.1),    # ST_SUPPRESSING - orange
	5: Color(1.0, 1.0, 0.2),    # ST_FLANKING - yellow
	6: Color(0.7, 0.3, 0.9),    # ST_RETREATING - purple
	7: Color(0.6, 0.6, 0.3),    # ST_RELOADING - olive
	8: Color(0.8, 0.2, 0.2),    # ST_DOWNED - dark red
	9: Color(1.0, 0.1, 0.1),    # ST_BERSERK - bright red
	10: Color(0.3, 0.3, 0.8),   # ST_FROZEN - dark blue
	11: Color(0.3, 0.3, 0.3),   # ST_DEAD - dark gray
	12: Color(0.4, 0.8, 0.9),   # ST_CLIMBING - teal
	13: Color(0.9, 0.5, 0.2),   # ST_FALLING - orange
}

const STATE_NAMES := {
	0: "IDLE", 1: "MOVING", 2: "ENGAGING", 3: "IN_COVER",
	4: "SUPPRESSING", 5: "FLANKING", 6: "RETREATING", 7: "RELOADING",
	8: "DOWNED", 9: "BERSERK", 10: "FROZEN", 11: "DEAD",
	12: "CLIMBING", 13: "FALLING",
}

const ROLE_NAMES := {
	0: "Rifleman", 1: "Leader", 2: "Medic", 3: "MG",
	4: "Marksman", 5: "Grenadier", 6: "Mortar",
}

const ROLE_ICONS := {
	0: "R", 1: "L", 2: "+", 3: "M",
	4: "S", 5: "G", 6: "T",
}

const PERSONALITY_NAMES := {
	0: "Steady", 1: "Berserker", 2: "Catatonic", 3: "Paranoid",
}

const FORMATION_NAMES := ["LINE", "WEDGE", "COLUMN", "CIRCLE"]

const MOVEMENT_MODE_NAMES := {
	0: "Patrol", 1: "Tactical", 2: "Combat", 3: "Stealth", 4: "Rush",
}

const POSTURE_NAMES := { 0: "Stand", 1: "Crouch", 2: "Prone" }

# ── System References ───────────────────────────────────────────────
var sim: SimulationServer
var world: VoxelWorld
var cover_map  # TacticalCoverMap
var influence_map  # InfluenceMapCPP
var gpu_map  # GpuTacticalMap
var theater_t1: TheaterCommander
var theater_t2: TheaterCommander
var colony_t1: ColonyAICPP
var colony_t2: ColonyAICPP
var cam: Camera3D
var time_of_day_ref  # TimeOfDay
var camera_script  # voxel_test_camera.gd instance
var commentator  # LLMCommentator (optional, may be null)

# ── Sub-panels ──────────────────────────────────────────────────────
var top_bar: Control
var escape_menu: Control
var inspector: Control
var strategy: Control
var spawner: Control
var minimap: Control
var event_ticker: Control
var map_mode  # MapModeController (Node3D, not a child of this CanvasLayer)
var keybind_overlay: Control
var battle_stats: Control
var settings_menu: Control
var world_overlay: Control
var tuning_panel: Control

# ── State ───────────────────────────────────────────────────────────
var selected_unit_id: int = -1
var selected_squad_id: int = -1
var is_sim_running: bool = false
var _screenshot_mode: bool = false
var _follow_unit_id: int = -1
var _modal_dim: ColorRect  # Full-screen dim behind modals
var _last_click_time: float = 0.0  # For double-click detection
var _last_click_uid: int = -1

# Cached morale (computed from per-unit data, updated every 0.25s)
var cached_morale_t1: float = 0.0
var cached_morale_t2: float = 0.0
var _morale_timer: float = 0.0
const MORALE_UPDATE_INTERVAL: float = 0.25

# Camera bookmark storage
var _cam_bookmarks: Array[Dictionary] = [{}, {}, {}, {}]  # F5-F8


func _ready() -> void:
	layer = 90
	name = "BattleCommandUI"


## Call from camera after all systems are initialized.
func setup(refs: Dictionary) -> void:
	sim = refs.get("sim")
	world = refs.get("world")
	cover_map = refs.get("cover_map")
	influence_map = refs.get("influence_map")
	gpu_map = refs.get("gpu_map")
	theater_t1 = refs.get("theater_t1")
	theater_t2 = refs.get("theater_t2")
	colony_t1 = refs.get("colony_t1")
	colony_t2 = refs.get("colony_t2")
	cam = refs.get("cam")
	time_of_day_ref = refs.get("time_of_day")
	camera_script = refs.get("camera_script")

	_create_panels()
	print("[BattleCommand] UI initialized")


func _create_panels() -> void:
	# ── Global modal dim overlay ──
	# Added as early child so it renders behind modal panels.
	# Modals use z_index=10 to appear above the dim (z_index=5),
	# which is above base panels (z_index=0).
	_modal_dim = ColorRect.new()
	_modal_dim.name = "ModalDim"
	_modal_dim.anchors_preset = Control.PRESET_FULL_RECT
	_modal_dim.color = Color(0.0, 0.02, 0.04, 0.55)
	_modal_dim.mouse_filter = Control.MOUSE_FILTER_STOP
	_modal_dim.z_index = 5
	_modal_dim.visible = false
	add_child(_modal_dim)

	# Phase 1: Escape menu
	var EscapeMenuScript = load("res://ui/ui/battle_command/escape_menu.gd")
	escape_menu = EscapeMenuScript.new()
	escape_menu.name = "EscapeMenu"
	escape_menu.visible = false
	escape_menu.z_index = 10
	add_child(escape_menu)

	# Phase 2: Top bar
	var TopBarScript = load("res://ui/ui/battle_command/top_bar.gd")
	top_bar = TopBarScript.new()
	top_bar.name = "TopBar"
	add_child(top_bar)

	# Phase 3: Inspector panel (left drawer)
	var InspectorScript = load("res://ui/ui/battle_command/inspector_panel.gd")
	inspector = InspectorScript.new()
	inspector.name = "InspectorPanel"
	inspector.visible = false
	add_child(inspector)

	# Phase 5: Strategy panel (right drawer)
	var StrategyScript = load("res://ui/ui/battle_command/strategy_panel.gd")
	strategy = StrategyScript.new()
	strategy.name = "StrategyPanel"
	strategy.visible = false
	add_child(strategy)

	# Phase 4: Unit spawner (modal)
	var SpawnerScript = load("res://ui/ui/battle_command/unit_spawner.gd")
	spawner = SpawnerScript.new()
	spawner.name = "UnitSpawner"
	spawner.visible = false
	spawner.z_index = 10
	add_child(spawner)

	# Phase 7: Event ticker
	var TickerScript = load("res://ui/ui/battle_command/event_ticker.gd")
	event_ticker = TickerScript.new()
	event_ticker.name = "EventTicker"
	add_child(event_ticker)

	# Phase 7: Minimap
	var MinimapScript = load("res://ui/ui/battle_command/minimap_panel.gd")
	minimap = MinimapScript.new()
	minimap.name = "MinimapPanel"
	add_child(minimap)

	# Phase 7: Keybind overlay
	var KeybindScript = load("res://ui/ui/battle_command/keybind_overlay.gd")
	keybind_overlay = KeybindScript.new()
	keybind_overlay.name = "KeybindOverlay"
	keybind_overlay.visible = false
	keybind_overlay.z_index = 10
	add_child(keybind_overlay)

	# Phase 8: Battle stats
	var StatsScript = load("res://ui/ui/battle_command/battle_stats.gd")
	battle_stats = StatsScript.new()
	battle_stats.name = "BattleStats"
	battle_stats.visible = false
	battle_stats.z_index = 10
	add_child(battle_stats)

	# Phase 8: Settings menu
	var SettingsScript = load("res://ui/ui/battle_command/settings_menu.gd")
	settings_menu = SettingsScript.new()
	settings_menu.name = "SettingsMenu"
	settings_menu.visible = false
	settings_menu.z_index = 10
	add_child(settings_menu)

	# Tuning panel (right-side drawer, non-modal)
	var TuningScript = load("res://ui/ui/battle_command/tuning_panel.gd")
	tuning_panel = TuningScript.new()
	tuning_panel.name = "TuningPanel"
	tuning_panel.visible = false
	add_child(tuning_panel)

	# World overlay (projects 3D data onto viewport)
	var OverlayScript = load("res://ui/ui/battle_command/world_overlay.gd")
	world_overlay = OverlayScript.new()
	world_overlay.name = "WorldOverlay"
	add_child(world_overlay)

	# Phase 6: Map mode controller (Node3D — added to scene root, not CanvasLayer)
	var MapModeScript = load("res://ui/ui/battle_command/map_mode_controller.gd")
	map_mode = MapModeScript.new()
	map_mode.name = "MapModeController"
	if cam:
		cam.get_parent().add_child(map_mode)

	# Pass references to panels that need them
	if escape_menu.has_method("setup"):
		escape_menu.setup(self)
	if top_bar.has_method("setup"):
		top_bar.setup(self)
	if inspector.has_method("setup"):
		inspector.setup(self)
	if strategy.has_method("setup"):
		strategy.setup(self)
	if spawner.has_method("setup"):
		spawner.setup(self)
	if event_ticker.has_method("setup"):
		event_ticker.setup(self)
	if minimap.has_method("setup"):
		minimap.setup(self)
	if keybind_overlay.has_method("setup"):
		keybind_overlay.setup(self)
	if battle_stats.has_method("setup"):
		battle_stats.setup(self)
	if settings_menu.has_method("setup"):
		settings_menu.setup(self)
	if tuning_panel and tuning_panel.has_method("setup"):
		tuning_panel.setup(self)
	if world_overlay and world_overlay.has_method("setup"):
		world_overlay.setup(self)
	if map_mode and map_mode.has_method("setup"):
		map_mode.setup(self)


# ── Input Routing ───────────────────────────────────────────────────

## Called by camera's _input BEFORE its own handling.
## Returns true if event was consumed (camera should skip it).
func try_handle_input(event: InputEvent) -> bool:
	if not event is InputEventKey or not event.pressed:
		# Handle mouse click for unit selection and spawner placement
		if event is InputEventMouseButton and event.pressed:
			if event.button_index == MOUSE_BUTTON_LEFT:
				if is_modal_open():
					if spawner.visible and spawner.has_method("handle_click"):
						spawner.handle_click(event)
						return true
					return false
				# Click viewport to re-capture mouse (free-fly only)
				# In RTS mode the mouse is always visible — don't intercept LMB
				if _is_freefly_mode() and not _is_mouse_captured():
					Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
					if camera_script:
						camera_script._mouse_captured = true
					return true
		return false

	var key: int = event.keycode
	var shift: bool = event.shift_pressed
	var ctrl: bool = event.ctrl_pressed

	# ── ESC Cascade ──
	if key == KEY_ESCAPE:
		# Follow-cam detach first (highest priority, before modals)
		if _follow_unit_id >= 0:
			_follow_unit_id = -1
			return true
		if spawner.visible:
			_close_spawner()
			return true
		if settings_menu.visible:
			settings_menu.visible = false
			escape_menu.visible = true
			return true
		if keybind_overlay.visible:
			keybind_overlay.visible = false
			# Return to escape menu only if sim is paused (meaning we came from ESC menu)
			if camera_script and camera_script._sim_paused:
				escape_menu.visible = true
			_update_modal_dim()
			return true
		if tuning_panel.visible:
			tuning_panel.visible = false
			return true
		if battle_stats.visible:
			battle_stats.visible = false
			# Return to escape menu only if sim is paused (meaning we came from ESC menu)
			if camera_script and camera_script._sim_paused:
				escape_menu.visible = true
			_update_modal_dim()
			return true
		if minimap and minimap.get("_expanded") and minimap._expanded:
			minimap.toggle_expanded()
			return true
		if inspector.visible or strategy.visible:
			inspector.visible = false
			strategy.visible = false
			_update_bottom_layout()
			return true
		if escape_menu.visible:
			_close_escape_menu()
			return true
		# Nothing open → open escape menu
		_open_escape_menu()
		return true

	# ── Block input when escape menu is open (except ESC handled above) ──
	if escape_menu.visible:
		# Allow Q and Ctrl+Q in escape menu
		if key == KEY_Q:
			if ctrl:
				get_tree().quit()
			else:
				_quit_to_menu()
			return true
		if key == KEY_U:
			_close_escape_menu()
			if camera_script and camera_script.has_method("_restart_simulation"):
				camera_script._restart_simulation()
			return true
		return true  # Consume everything else while menu is open

	# ── Block input when spawner is open (except N/ESC) ──
	if spawner.visible:
		if key == KEY_N:
			_close_spawner()
			return true
		if spawner.has_method("handle_key"):
			return spawner.handle_key(event)
		return true

	# ── Expanded minimap keys (H/A/L/1/2 routed to minimap) ──
	if minimap and minimap.get("_expanded") and minimap._expanded:
		if minimap.has_method("try_handle_expanded_input"):
			if minimap.try_handle_expanded_input(event):
				return true

	# ── Block keybinds when a text field has focus (e.g. tuning panel search) ──
	var focus_owner := get_viewport().gui_get_focus_owner() if get_viewport() else null
	if focus_owner is LineEdit or focus_owner is TextEdit:
		return false

	# ── Panel Toggles ──
	if key == KEY_I and not ctrl:
		inspector.visible = not inspector.visible
		_update_bottom_layout()
		return true

	if key == KEY_O and not ctrl:
		strategy.visible = not strategy.visible
		_update_bottom_layout()
		return true

	if key == KEY_N and not ctrl:
		_open_spawner()
		return true

	if key == KEY_J and not ctrl:
		if event_ticker.has_method("toggle_expanded"):
			event_ticker.toggle_expanded()
		return true

	if key == KEY_V and not ctrl:
		if world_overlay and world_overlay.has_method("cycle_mode"):
			world_overlay.cycle_mode()
		return true

	if key == KEY_K and not ctrl:
		if minimap and minimap.has_method("toggle_expanded"):
			minimap.toggle_expanded()
		return true

	if key == KEY_B and not ctrl:
		battle_stats.visible = not battle_stats.visible
		_update_modal_dim()
		return true

	if key == KEY_P and not ctrl:
		tuning_panel.visible = not tuning_panel.visible
		return true

	if key == KEY_F1 and not ctrl:
		keybind_overlay.visible = not keybind_overlay.visible
		_update_modal_dim()
		return true

	# ── Tab Cycling (Ctrl+1-7) ──
	if ctrl:
		if key == KEY_1:
			_ensure_visible(inspector)
			if inspector.has_method("set_tab"):
				inspector.set_tab(0)
			_update_bottom_layout()
			return true
		if key == KEY_2:
			_ensure_visible(inspector)
			if inspector.has_method("set_tab"):
				inspector.set_tab(1)
			_update_bottom_layout()
			return true
		if key == KEY_3:
			_ensure_visible(inspector)
			if inspector.has_method("set_tab"):
				inspector.set_tab(2)
			_update_bottom_layout()
			return true
		if key == KEY_4:
			_ensure_visible(strategy)
			if strategy.has_method("set_tab"):
				strategy.set_tab(0)
			_update_bottom_layout()
			return true
		if key == KEY_5:
			_ensure_visible(strategy)
			if strategy.has_method("set_tab"):
				strategy.set_tab(1)
			_update_bottom_layout()
			return true
		if key == KEY_6:
			_ensure_visible(strategy)
			if strategy.has_method("set_tab"):
				strategy.set_tab(2)
			_update_bottom_layout()
			return true
		if key == KEY_7:
			_ensure_visible(strategy)
			if strategy.has_method("set_tab"):
				strategy.set_tab(3)
			_update_bottom_layout()
			return true

	# ── Map Mode (M) ──
	if key == KEY_M:
		if shift:
			if map_mode and map_mode.has_method("set_mode"):
				map_mode.set_mode(0)  # NONE
			return true
		elif ctrl:
			# Toggle local cover detail
			if map_mode and map_mode.has_method("toggle_cover_detail"):
				map_mode.toggle_cover_detail()
			return true
		else:
			if map_mode and map_mode.has_method("cycle_mode"):
				map_mode.cycle_mode()
			return true

	# ── Screenshot Mode (Ctrl+H) ──
	if key == KEY_H and ctrl:
		_toggle_screenshot_mode()
		return true

	# ── Camera Bookmarks ──
	if ctrl and key >= KEY_F5 and key <= KEY_F8:
		_save_bookmark(key - KEY_F5)
		return true
	if not ctrl and key >= KEY_F5 and key <= KEY_F8:
		_recall_bookmark(key - KEY_F5)
		return true

	# ── Team Toggle (T when strategy panel visible, non-pheromone tab) ──
	if key == KEY_T and not shift and not ctrl:
		if strategy.visible and strategy.has_method("toggle_viewing_team"):
			# Pheromone tab handles T itself (team toggle for phero viewer)
			if strategy.has_method("handle_pheromone_key"):
				if strategy.handle_pheromone_key(event):
					return true
			# Otherwise toggle viewing team for Theater/Colony tabs
			strategy.toggle_viewing_team()
			return true

	# ── Pheromone Channel Cycling (, / .) ──
	if key == KEY_COMMA or key == KEY_PERIOD:
		if strategy.has_method("handle_pheromone_key"):
			if strategy.handle_pheromone_key(event):
				return true
		return false

	return false


## Returns true if the mouse look / camera movement should be blocked.
func is_modal_open() -> bool:
	if not escape_menu or not spawner or not settings_menu \
		or not keybind_overlay or not battle_stats:
		return false
	return escape_menu.visible or spawner.visible or settings_menu.visible \
		or keybind_overlay.visible or battle_stats.visible


## Returns true if the mouse is currently captured by the game.
func _is_mouse_captured() -> bool:
	return Input.mouse_mode == Input.MOUSE_MODE_CAPTURED


## Returns true if camera is in free-fly mode (not RTS).
func _is_freefly_mode() -> bool:
	if camera_script and camera_script.get("_camera_mode") != null:
		return camera_script._camera_mode != camera_script.CameraMode.RTS
	return false


# ── Update (called every frame from camera) ─────────────────────────

func update(delta: float) -> void:
	if _screenshot_mode:
		return

	# Compute cached morale from per-unit data (C++ doesn't expose avg_morale)
	_morale_timer += delta
	if _morale_timer >= MORALE_UPDATE_INTERVAL and sim:
		_morale_timer = 0.0
		var sum_m1: float = 0.0
		var sum_m2: float = 0.0
		var cnt_t1: int = 0
		var cnt_t2: int = 0
		var unit_count: int = sim.get_unit_count()
		for uid in unit_count:
			if not sim.is_alive(uid):
				continue
			var m: float = sim.get_morale(uid)
			if sim.get_team(uid) == 1:
				sum_m1 += m
				cnt_t1 += 1
			else:
				sum_m2 += m
				cnt_t2 += 1
		cached_morale_t1 = sum_m1 / float(cnt_t1) if cnt_t1 > 0 else 0.0
		cached_morale_t2 = sum_m2 / float(cnt_t2) if cnt_t2 > 0 else 0.0

	if top_bar and top_bar.has_method("update_data"):
		top_bar.update_data(delta)

	if inspector.visible and inspector.has_method("update_data"):
		inspector.update_data(delta)

	if strategy.visible and strategy.has_method("update_data"):
		strategy.update_data(delta)

	if event_ticker and event_ticker.has_method("update_data"):
		event_ticker.update_data(delta)

	if minimap and minimap.has_method("update_data"):
		minimap.update_data(delta)

	if battle_stats.visible and battle_stats.has_method("update_data"):
		battle_stats.update_data(delta)

	if world_overlay and world_overlay.has_method("update_data"):
		world_overlay.update_data(delta)

	# Follow cam — validate unit still exists and is alive
	if _follow_unit_id >= 0 and sim and cam:
		if _follow_unit_id < sim.get_unit_count() and sim.is_alive(_follow_unit_id):
			var target_pos: Vector3 = sim.get_position(_follow_unit_id)
			cam.global_position = cam.global_position.lerp(target_pos + Vector3(0, 5, -8), delta * 3.0)
			cam.look_at(target_pos + Vector3.UP, Vector3.UP)
		else:
			_follow_unit_id = -1


## Select a unit by ID (called from inspector click or external).
func select_unit(unit_id: int) -> void:
	selected_unit_id = unit_id
	if sim and unit_id >= 0 and unit_id < sim.get_unit_count():
		selected_squad_id = sim.get_squad_id(unit_id)
	if inspector.has_method("on_unit_selected"):
		inspector.on_unit_selected(unit_id)


## Start following a unit with the camera.
func start_follow_cam(unit_id: int) -> void:
	if sim and unit_id >= 0 and unit_id < sim.get_unit_count() and sim.is_alive(unit_id):
		_follow_unit_id = unit_id
		select_unit(unit_id)
		print("[BattleCommand] Following unit #%d" % unit_id)


## Stop following.
func stop_follow_cam() -> void:
	_follow_unit_id = -1


## Select all units in a squad (sets selected_squad_id, selects leader or first alive).
func select_squad(squad_id: int) -> void:
	selected_squad_id = squad_id
	if not sim:
		return
	# Find first alive unit in squad (prefer leader role=1)
	var first_alive: int = -1
	for uid in sim.get_unit_count():
		if sim.is_alive(uid) and sim.get_squad_id(uid) == squad_id:
			if first_alive < 0:
				first_alive = uid
			if sim.get_role(uid) == 1:  # Leader
				first_alive = uid
				break
	if first_alive >= 0:
		select_unit(first_alive)


## Try to select unit near a world position (from viewport click).
func try_select_unit_at(world_pos: Vector3) -> bool:
	if not sim:
		return false
	# Direct scan of all units — avoids array mismatch between alive_positions and render IDs
	var best_dist: float = 4.0  # 2m squared threshold
	var best_uid: int = -1
	var count: int = sim.get_unit_count()
	for uid in count:
		if not sim.is_alive(uid):
			continue
		var p: Vector3 = sim.get_position(uid)
		var dx: float = p.x - world_pos.x
		var dz: float = p.z - world_pos.z
		var d2: float = dx * dx + dz * dz
		if d2 < best_dist:
			best_dist = d2
			best_uid = uid
	if best_uid >= 0:
		select_unit(best_uid)
		return true
	return false


# ── Escape Menu ─────────────────────────────────────────────────────

func _open_escape_menu() -> void:
	escape_menu.visible = true
	_update_modal_dim()
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	if camera_script:
		camera_script._mouse_captured = false
	# Pause sim
	if camera_script:
		camera_script._sim_paused = true


func _close_escape_menu() -> void:
	escape_menu.visible = false
	_update_modal_dim()
	# Resume sim
	if camera_script:
		camera_script._sim_paused = false
	# Restore mouse mode based on camera mode
	_restore_mouse_mode()


func _quit_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/main_menu.tscn")


## Restore mouse mode appropriate for the current camera mode.
func _restore_mouse_mode() -> void:
	if camera_script and camera_script.get("_camera_mode") != null:
		if camera_script._camera_mode == camera_script.CameraMode.RTS:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			camera_script._mouse_captured = false
			return
	# Default: capture mouse (free-fly mode)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	if camera_script:
		camera_script._mouse_captured = true


# ── Spawner ─────────────────────────────────────────────────────────

func _open_spawner() -> void:
	spawner.visible = true
	_update_modal_dim()
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	if camera_script:
		camera_script._mouse_captured = false
		camera_script._sim_paused = true


func _close_spawner() -> void:
	spawner.visible = false
	_update_modal_dim()
	# Restore mouse mode based on camera mode
	_restore_mouse_mode()
	if camera_script:
		camera_script._sim_paused = false


# ── Screenshot Mode ─────────────────────────────────────────────────

func _toggle_screenshot_mode() -> void:
	_screenshot_mode = not _screenshot_mode
	visible = not _screenshot_mode
	print("[BattleCommand] Screenshot mode: %s" % ("ON" if _screenshot_mode else "OFF"))


# ── Camera Bookmarks ────────────────────────────────────────────────

func _save_bookmark(idx: int) -> void:
	if not cam:
		return
	_cam_bookmarks[idx] = {
		"pos": cam.global_position,
		"rot": cam.global_rotation,
	}
	print("[BattleCommand] Bookmark %d saved" % (idx + 5))


func _recall_bookmark(idx: int) -> void:
	if not cam:
		return
	var bm: Dictionary = _cam_bookmarks[idx]
	if bm.is_empty():
		return
	cam.global_position = bm["pos"]
	cam.global_rotation = bm["rot"]
	print("[BattleCommand] Bookmark %d recalled" % (idx + 5))


# ── Layout Coordination ────────────────────────────────────────────

## Show/hide the global dim behind modals. Called after any modal visibility change.
func _update_modal_dim() -> void:
	if not _modal_dim:
		return
	var any_modal: bool = escape_menu.visible or spawner.visible or \
		settings_menu.visible or battle_stats.visible or keybind_overlay.visible
	_modal_dim.visible = any_modal


## Shift bottom-corner elements (ticker, minimap) to avoid side drawer overlap.
func _update_bottom_layout() -> void:
	# Event ticker: shift right when inspector is open
	if event_ticker:
		if inspector.visible:
			event_ticker.offset_left = 300.0  # 280 panel + 20 gap
		else:
			event_ticker.offset_left = 10.0

	# Minimap: shift left when strategy is open (skip if expanded)
	if minimap and not (minimap.get("_expanded") and minimap._expanded):
		if strategy.visible:
			minimap.offset_right = -340.0  # 320 panel + 20 gap
		else:
			minimap.offset_right = -10.0


# ── Helpers ─────────────────────────────────────────────────────────

func _ensure_visible(panel: Control) -> void:
	if not panel.visible:
		panel.visible = true


## Get a color for a ratio value (0-1): green > 0.7, yellow 0.3-0.7, red < 0.3
static func ratio_color(ratio: float) -> Color:
	if ratio > 0.7:
		return Color(0.2, 0.85, 0.3)
	elif ratio > 0.3:
		return WARNING_COLOR
	else:
		return CRITICAL_COLOR


## Format seconds as mm:ss
static func format_time(seconds: float) -> String:
	var m: int = int(seconds) / 60
	var s: int = int(seconds) % 60
	return "%d:%02d" % [m, s]


## Create a styled panel background (StyleBoxFlat).
static func make_panel_style(bg: Color = BG_COLOR, border: Color = BORDER_COLOR,
		corner: int = 6, shadow: bool = false) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.border_width_top = 1
	sb.border_width_bottom = 1
	sb.border_width_left = 1
	sb.border_width_right = 1
	sb.border_color = border
	sb.corner_radius_top_left = corner
	sb.corner_radius_top_right = corner
	sb.corner_radius_bottom_left = corner
	sb.corner_radius_bottom_right = corner
	sb.content_margin_left = 10.0
	sb.content_margin_right = 10.0
	sb.content_margin_top = 8.0
	sb.content_margin_bottom = 8.0
	if shadow:
		sb.shadow_color = Color(0, 0, 0, 0.35)
		sb.shadow_size = 6
		sb.shadow_offset = Vector2(0, 2)
	return sb


## Reset stale state when sim starts/stops/restarts.
func reset_on_sim_change() -> void:
	selected_unit_id = -1
	selected_squad_id = -1
	_follow_unit_id = -1
	cached_morale_t1 = 0.0
	cached_morale_t2 = 0.0
	# Clear battle stats time series
	if battle_stats and battle_stats.has_method("clear_data"):
		battle_stats.clear_data()
	# Reset event ticker tracking
	if event_ticker and event_ticker.has_method("reset_tracking"):
		event_ticker.reset_tracking()
	# Reset commentator tracking
	if commentator and commentator.has_method("reset_tracking"):
		commentator.reset_tracking()


func _exit_tree() -> void:
	# Clean up map_mode which was added to scene root, not this CanvasLayer
	if map_mode and is_instance_valid(map_mode):
		map_mode.queue_free()
		map_mode = null


## Create a horizontal bar (ColorRect inside a container) for stat display.
static func make_bar(width: float, height: float, color: Color) -> ColorRect:
	var bar := ColorRect.new()
	bar.custom_minimum_size = Vector2(width, height)
	bar.color = color
	return bar
