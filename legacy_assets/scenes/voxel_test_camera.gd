extends Camera3D
## Voxel test scene controller — RTS camera (primary) + free-fly camera (Tab toggle).
## RTS: WASD pan, Q/E rotate, scroll zoom-to-cursor, RMB orbit, MMB drag, edge pan.
## Free-fly: WASD fly, mouse look, scroll speed. Tab to switch modes.
## F: destroy, U: sim, ESC: menu, I/O: panels, N: spawner, F1: keybinds.

@export var move_speed: float = 20.0
@export var look_sensitivity: float = 0.002
@export var speed_step: float = 5.0

## ── Camera Mode ────────────────────────────────────────────────────
enum CameraMode { RTS, FREE_FLY }
var _camera_mode: CameraMode = CameraMode.RTS

## ── Free-fly state ─────────────────────────────────────────────────
var _velocity: Vector3 = Vector3.ZERO
var _yaw: float = 0.0
var _pitch: float = -0.35  # Slight downward look
var _mouse_captured: bool = false
var _freefly_saved_pos: Vector3 = Vector3.ZERO
var _freefly_saved_yaw: float = 0.0
var _freefly_saved_pitch: float = -0.35

## ── RTS Camera State (orbital) ─────────────────────────────────────
var _rts_focus: Vector3 = Vector3.ZERO       ## Ground point camera orbits
var _rts_yaw: float = 0.0                    ## Orbital yaw (degrees, 0=north)
var _rts_height: float = 35.0                ## Current height above focus
var _rts_height_target: float = 35.0         ## Smoothed zoom target
var _rts_pan_velocity: Vector2 = Vector2.ZERO ## Momentum pan (XZ)
var _rts_rmb_dragging: bool = false           ## Right-click rotate active
var _rts_mmb_dragging: bool = false           ## Middle-click drag-pan active
var _rts_drag_start_mouse: Vector2 = Vector2.ZERO
var _rts_drag_start_focus: Vector3 = Vector3.ZERO

## RTS Camera Constants
const RTS_MIN_HEIGHT: float = 10.0
const RTS_MAX_HEIGHT: float = 120.0
const RTS_ZOOM_SPEED: float = 5.0            ## Height change per scroll tick
const RTS_PAN_SPEED: float = 30.0            ## Base pan speed (m/s)
const RTS_PAN_ACCEL: float = 8.0             ## Acceleration rate
const RTS_PAN_DECEL: float = 6.0             ## Deceleration rate (momentum coast)
const RTS_ROTATION_SPEED: float = 90.0       ## Q/E rotation (deg/s)
const RTS_MOUSE_ROTATE_SPEED: float = 0.18   ## Degrees per pixel of RMB drag
const RTS_EDGE_PAN_MARGIN: float = 40.0      ## Pixels from edge to trigger pan
const RTS_PITCH_CLOSE: float = -62.0         ## Steep when zoomed in
const RTS_PITCH_FAR: float = -42.0           ## Flat when zoomed out
const RTS_FOV_CLOSE: float = 48.0
const RTS_FOV_FAR: float = 62.0

## ── BattleCommandUI ────────────────────────────────────────────────
var _battle_ui  ## BattleCommandUI (CanvasLayer)

## Debug systems (created after world initializes)
var _world: VoxelWorld
var _cover_map  # TacticalCoverMap
var _influence_map  # InfluenceMapCPP
var _overlay: VoxelDebugOverlay
var _hud: VoxelDebugHUD
var _debug_ready: bool = false
var _autostart: bool = false
var _gpu_map  # GpuTacticalMap
var _svdag_renderer  # SVDAGRenderer

## SimulationServer test state
var _sim: SimulationServer
var _sim_running: bool = false
var _mm_team1: MultiMeshInstance3D
var _mm_team2: MultiMeshInstance3D
var _mm_tracers: MultiMeshInstance3D
var _mm_grenades: MultiMeshInstance3D
var _mm_corpses: MultiMeshInstance3D
var _sim_hud_label: Label
var _sim_help_label: Label
var _gpu_tick_timer: float = 0.0

## Sim configuration (adjustable at runtime)
var _sim_units_per_team: int = 500
var _sim_team1_formation: int = SimulationServer.FORM_LINE
var _sim_team2_formation: int = SimulationServer.FORM_WEDGE
const FORMATION_NAMES: Array[String] = ["LINE", "WEDGE", "COLUMN", "CIRCLE"]
var _show_help: bool = false
var _sim_paused: bool = false
var _phero_hud = null  # PheromoneDebugHUD instance
var _phero_overlay_team: int = 0  # Which team's pheromones to show
var _sim_time_scale: float = 1.0
var _squad_max_advance: Dictionary = {}  # squad_id -> max advance distance

## Capture point visuals
var _capture_markers: Array[Node3D] = []

## VAT (Vertex Animation Textures)
var _rest_vertices: PackedVector3Array
var _vat_texture: ImageTexture
var _vat_vert_idx: int = 0

## Theater Commander (strategic AI)
var _theater_commander: TheaterCommander
var _theater_commander_t2: TheaterCommander

## Colony AI C++ (auction/scoring layer)
var _colony_ai_cpp: ColonyAICPP
var _colony_ai_cpp_t2: ColonyAICPP
var _colony_plan_timer: float = 0.0
var _colony_plan_timer_t2: float = 0.275  # Stagger team 2 by half interval
const COLONY_PLAN_INTERVAL: float = 0.55
var _gpu_map_timer: float = 0.0
var _cover_map_timer: float = 0.25  # stagger from GPU map
const GPU_MAP_INTERVAL: float = 0.5
const COVER_MAP_INTERVAL: float = 0.5
var _t1_squad_count: int = 0
var _t2_squad_count: int = 0
var _team2_squad_base: int = 64

## LLM Theater Advisors (optional strategic layer - supports multiple simultaneously)
var _llm_advisor: LLMTheaterAdvisor = null  # Primary (cloud or local)
var _llm_advisor_t2: LLMTheaterAdvisor = null  # Team 2 primary
var _llm_advisor_local: LLMTheaterAdvisor = null  # Optional local model (Team 1)
var _llm_advisor_local_t2: LLMTheaterAdvisor = null  # Optional local model (Team 2)

## LLM Sector Commanders (sector order mode - replaces bias advisors when active)
var _llm_sector_cmd: LLMSectorCommander = null
var _llm_sector_cmd_t2: LLMSectorCommander = null

## LLM Commentator (event-driven barks — neutral or per-team)
var _llm_commentator: LLMCommentator = null
var _llm_commentator_t2: LLMCommentator = null  # Only used in team mode

## Track last intent per squad for hysteresis (prevent jitter) — per-team
var _last_intent_target_t1: Dictionary = {}  # {sq_id: Vector3}
var _last_intent_action_t1: Dictionary = {}  # {sq_id: String}
var _last_intent_goal_name_t1: Dictionary = {}  # {sq_id: String}
var _last_intent_target_t2: Dictionary = {}
var _last_intent_action_t2: Dictionary = {}
var _last_intent_goal_name_t2: Dictionary = {}
var _last_intent_seen_t1: Dictionary = {}  # {sim_sq_id: sim_time_sec}
var _last_intent_seen_t2: Dictionary = {}  # {sim_sq_id: sim_time_sec}
var _goal_change_count: int = 0  ## Cumulative goal reassignment counter
var _goal_change_log_timer: float = 0.0  ## Throttle console log
const INTENT_HYSTERESIS_DIST: float = 10.0  # only re-rally if target moved > 10m
const INTENT_RECENT_WINDOW_SEC: float = 3.0
const INTENT_METRIC_WARMUP_SEC: float = 3.0

## Visual debug state
var _selected_squad_id: int = -1  # -1 = none selected
var _cached_render_data1: Dictionary = {}
var _cached_render_data2: Dictionary = {}

## Goal color map for debug visualization
const GOAL_COLORS: Dictionary = {
	"capture_poi":   Color(0.2, 0.9, 0.2),
	"defend_poi":    Color(0.3, 0.5, 1.0),
	"assault_enemy": Color(1.0, 0.2, 0.2),
	"defend_base":   Color(0.7, 0.2, 0.9),
	"fire_mission":  Color(1.0, 0.6, 0.1),
	"flank_enemy":   Color(1.0, 1.0, 0.2),
	"hold_position": Color(0.5, 0.5, 0.5),
}
const GOAL_COLOR_DEFAULT: Color = Color(0.6, 0.6, 0.6)

## Squad HUD
var _squad_hud_visible: bool = false
var _squad_hud_panel: PanelContainer
var _squad_hud_rows: Array[Button] = []
var _squad_hud_timer: float = 0.0
const SQUAD_HUD_INTERVAL: float = 0.25
var _debug_canvas: CanvasLayer

## Centroid markers + waypoint lines
var _centroid_markers: Array[MeshInstance3D] = []
var _centroid_labels: Array[Label3D] = []
var _waypoint_mesh_inst: MeshInstance3D
var _waypoint_immediate: ImmediateMesh
var _waypoint_labels: Array[Label3D] = []

## Minimap
var _minimap: Control  # SquadMinimap
var _minimap_timer: float = 0.0
const MINIMAP_INTERVAL: float = 0.15

## Proving Ground (scenario mode)
var _scenario_mode: bool = false
var _headless: bool = false
var _batch_mode: bool = false  # Like headless (auto-quit) but keeps GPU active
var _deterministic_mode: bool = false
var _fixed_sim_step: float = 1.0 / 60.0
var _fixed_step_accum: float = 0.0
var _diag_disable_context_steer: bool = false
var _diag_disable_orca: bool = false
var _diag_tune_overrides: Dictionary = {}  # {"sim:key"/"colony:key"/"theater:key" or "key": value}
var _scenario_config: ScenarioLoader.ScenarioConfig
var _kpi_collector: KPICollector
var _scenario_elapsed: float = 0.0
var _scenario_seed: int = 0
var _output_path: String = ""
var _scenario_ended: bool = false
var _duration_override: float = -1.0
var _results_overlay: CanvasLayer

## Lighting panel (F4 toggle)
var _rc_effect: RadianceCascadesEffect
var _contact_shadow_effect: VoxelPostEffect
var _lighting_panel: PanelContainer
var _lighting_visible: bool = false
var _lighting_sliders: Dictionary = {}  # name → HSlider
var _lighting_labels: Dictionary = {}   # name → Label (value readout)

## Camera shake (Phase 3C — explosion impulse)
var _shake_amplitude: float = 0.0   ## Current shake intensity (decays to 0)
var _shake_decay: float = 0.0       ## How fast shake decays (per-second)
var _shake_offset: Vector3 = Vector3.ZERO  ## Applied offset this frame

## Suppression screen effects (Phase 3B)
var _suppression_overlay: ColorRect
var _suppression_canvas: CanvasLayer
var _suppression_intensity: float = 0.0  ## 0-1, drives vignette + desaturation
const SUPPRESSION_SAMPLE_INTERVAL: float = 0.2
var _suppression_sample_timer: float = 0.0

## Time of day (Phase 4A)
var _time_of_day: TimeOfDay

## Battle haze (Phase 5C — persistent fog over combat zones)
const BATTLE_HAZE_POOL_SIZE: int = 8
const BATTLE_HAZE_UPDATE_INTERVAL: float = 1.0  ## Seconds between haze position updates
var _battle_haze_volumes: Array[FogVolume] = []
var _battle_haze_materials: Array[FogMaterial] = []
var _battle_haze_initialized: bool = false
var _battle_haze_timer: float = 0.0

## Weather system (Phase 4B)
var _rain_particles: GPUParticles3D
var _rain_active: bool = false
var _weather_wetness: float = 0.0  ## 0-1, drives surface wetness shader

## Destruction VFX pipeline
var _effects: Effects
var _structural_integrity: StructuralIntegrity
var _renderer: VoxelWorldRenderer
var _capture_spheres: Array[MeshInstance3D] = []
var _capture_labels: Array[Label3D] = []
var _capture_rings: Array[MeshInstance3D] = []
const CAPTURE_NAMES: Array[String] = ["Alpha", "Bravo", "Charlie", "Delta", "Echo"]
var _active_capture_positions: Array[Vector3] = []
const ACTIVE_RUNTIME_PATH_NOTE := "main_menu.tscn -> voxel_test.tscn -> voxel_test_camera.gd -> SimulationServer/ColonyAICPP"

## Suppression screen-space shader (Phase 3B)
## Vignette darkening + desaturation + red tint at edges. Intensity 0-1.
const _SUPPRESSION_SHADER_CODE: String = """
shader_type canvas_item;
uniform float intensity : hint_range(0.0, 1.0) = 0.0;
uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;

void fragment() {
	vec2 uv = SCREEN_UV;
	vec4 screen = textureLod(screen_tex, uv, 0.0);

	// Vignette: darken edges proportional to intensity
	vec2 center_dist = uv - vec2(0.5);
	float vignette = dot(center_dist, center_dist);  // 0 at center, 0.5 at corners
	float vignette_strength = vignette * intensity * 3.0;  // Stronger at edges

	// Desaturation: lerp toward luminance
	float lum = dot(screen.rgb, vec3(0.299, 0.587, 0.114));
	vec3 desat = mix(screen.rgb, vec3(lum), intensity * 0.6);

	// Red tint at high suppression (blood in eyes feel)
	vec3 tinted = mix(desat, desat * vec3(1.1, 0.85, 0.8), intensity * 0.5);

	// Darken by vignette
	tinted *= (1.0 - vignette_strength);

	COLOR = vec4(tinted, intensity * 0.3);
}
"""


func _ready() -> void:
	_parse_cli_args()
	_log_active_runtime_path()

	# Apply menu settings (free play only — scenarios override these)
	if not ScenarioState.is_scenario_mode:
		_sim_units_per_team = ScenarioState.units_per_team
		_sim_team1_formation = ScenarioState.team1_formation
		_sim_team2_formation = ScenarioState.team2_formation
		_sim_time_scale = ScenarioState.sim_time_scale

	if not _headless:
		# RTS mode is default — mouse visible, orbital camera
		_camera_mode = CameraMode.RTS
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_mouse_captured = false
		_rts_focus = Vector3(0.0, 0.0, 0.0)
		_rts_height = 35.0
		_rts_height_target = 35.0
		_rts_yaw = 0.0
		_update_rts_transform()

		# Attach compositor effects for screen-space GI + contact shadows
		_rc_effect = RadianceCascadesEffect.new()
		_rc_effect.cascade_count = 4
		_rc_effect.gi_intensity = 0.25
		_rc_effect.sky_color = Color(0.1, 0.15, 0.25)
		_rc_effect.base_probe_spacing = 4
		_rc_effect.depth_threshold = 0.02

		_contact_shadow_effect = VoxelPostEffect.new()
		_contact_shadow_effect.shadow_strength = 0.5
		_contact_shadow_effect.max_distance = 0.05
		_contact_shadow_effect.thickness = 0.005
		_contact_shadow_effect.light_direction = Vector2(0.4, -0.6)

		var comp = Compositor.new()
		comp.compositor_effects = [_rc_effect, _contact_shadow_effect]
		compositor = comp

		# Suppression screen effects (Phase 3B)
		_suppression_canvas = CanvasLayer.new()
		_suppression_canvas.layer = 100  # On top of everything
		add_child(_suppression_canvas)
		_suppression_overlay = ColorRect.new()
		_suppression_overlay.anchors_preset = Control.PRESET_FULL_RECT
		_suppression_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
		var sup_shader = Shader.new()
		sup_shader.code = _SUPPRESSION_SHADER_CODE
		var sup_mat = ShaderMaterial.new()
		sup_mat.shader = sup_shader
		sup_mat.set_shader_parameter("intensity", 0.0)
		_suppression_overlay.material = sup_mat
		_suppression_canvas.add_child(_suppression_overlay)

		# Time of Day system (Phase 4A) — initialized after scene nodes are ready
		call_deferred("_init_time_of_day")

		# Rain particle system (Phase 4B) — created but hidden
		_init_rain_system()

		# Apply rain setting from menu
		if ScenarioState.rain_enabled:
			_rain_active = true
			if _rain_particles:
				_rain_particles.emitting = true
				_rain_particles.visible = true

		# BattleCommandUI (wired with system refs in _try_init_debug)
		var battle_ui_script = load("res://ui/ui/battle_command/battle_command_ui.gd")
		if battle_ui_script:
			_battle_ui = battle_ui_script.new()
			add_child(_battle_ui)

		# Initialise SVDAG renderer
		# var svdag_renderer_script = load("res://scenes/svdag_renderer.gd")
		# if svdag_renderer_script:
		_svdag_renderer = SVDAGRenderer.new()
		add_child(_svdag_renderer)
		print("[SVDAG] Initialized.")


func _init_time_of_day() -> void:
	var sun_node: DirectionalLight3D = get_parent().get_node_or_null("DirectionalLight3D")
	var fill_node: DirectionalLight3D = get_parent().get_node_or_null("DirectionalLight3D_Fill")
	var env: Environment = _get_environment()
	if not sun_node or not env:
		return
	_time_of_day = TimeOfDay.new()
	_time_of_day.name = "TimeOfDay"
	add_child(_time_of_day)
	_time_of_day.setup(sun_node, fill_node, env)
	# Apply menu settings
	_time_of_day.set_time(ScenarioState.starting_hour)
	_time_of_day.cycling = ScenarioState.day_night_cycle
	_time_of_day.time_speed = ScenarioState.day_night_speed
	print("[TimeOfDay] Initialized — noon, cycle paused (press T to toggle, Numpad +/- for speed)")


func _init_rain_system() -> void:
	_rain_particles = GPUParticles3D.new()
	var rain_mat = ParticleProcessMaterial.new()
	rain_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	rain_mat.emission_box_extents = Vector3(40.0, 0.5, 40.0)
	rain_mat.direction = Vector3(0, -1, 0)
	rain_mat.spread = 3.0
	rain_mat.initial_velocity_min = 25.0
	rain_mat.initial_velocity_max = 35.0
	rain_mat.gravity = Vector3(0, -9.8, 0)
	rain_mat.damping_min = 0.0
	rain_mat.damping_max = 0.0
	rain_mat.scale_min = 0.02
	rain_mat.scale_max = 0.04
	rain_mat.color = Color(0.7, 0.75, 0.85, 0.4)
	_rain_particles.process_material = rain_mat
	_rain_particles.amount = 8000
	_rain_particles.lifetime = 2.0
	_rain_particles.visibility_aabb = AABB(Vector3(-50, -60, -50), Vector3(100, 120, 100))

	# Stretched quad mesh for rain streaks
	var rain_mesh = QuadMesh.new()
	rain_mesh.size = Vector2(0.02, 0.6)  # Thin, tall streak
	var streak_mat = StandardMaterial3D.new()
	streak_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	streak_mat.albedo_color = Color(0.75, 0.8, 0.9, 0.3)
	streak_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	streak_mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	rain_mesh.material = streak_mat
	_rain_particles.draw_pass_1 = rain_mesh

	_rain_particles.emitting = false
	_rain_particles.visible = false
	add_child(_rain_particles)


func _log_active_runtime_path() -> void:
	print("[RuntimePath] Active runtime path: %s | headless=%s" % [ACTIVE_RUNTIME_PATH_NOTE, str(_headless)])


func _parse_diag_tune_key(raw_key: String) -> Dictionary:
	var out: Dictionary = {
		"target": "sim",
		"key": raw_key,
		"has_prefix": false,
		"valid_prefix": true,
	}
	var sep: int = raw_key.find(":")
	if sep <= 0:
		return out
	out["has_prefix"] = true
	var maybe_target: String = raw_key.substr(0, sep).to_lower()
	var maybe_key: String = raw_key.substr(sep + 1)
	if maybe_key.is_empty():
		out["valid_prefix"] = false
		return out
	if maybe_target == "sim" or maybe_target == "colony" or maybe_target == "theater":
		out["target"] = maybe_target
		out["key"] = maybe_key
	else:
		out["valid_prefix"] = false
	return out


func _apply_diag_tune_overrides_for(target: String, log_prefix: String) -> void:
	for raw_key_var in _diag_tune_overrides:
		var raw_key: String = str(raw_key_var)
		var parsed: Dictionary = _parse_diag_tune_key(raw_key)
		if bool(parsed.get("has_prefix", false)) and not bool(parsed.get("valid_prefix", true)):
			continue
		var parsed_target: String = str(parsed.get("target", "sim"))
		if parsed_target != target:
			continue
		var key: String = str(parsed.get("key", raw_key))
		var val: float = float(_diag_tune_overrides[raw_key_var])
		var applied_count: int = 0
		match target:
			"sim":
				if _sim:
					_sim.set_tuning_param(key, val)
					applied_count = 1
			"colony":
				if _colony_ai_cpp:
					_colony_ai_cpp.set_tuning_param(key, val)
					applied_count += 1
				if _colony_ai_cpp_t2:
					_colony_ai_cpp_t2.set_tuning_param(key, val)
					applied_count += 1
			"theater":
				if _theater_commander:
					_theater_commander.set_tuning_param(key, val)
					applied_count += 1
				if _theater_commander_t2:
					_theater_commander_t2.set_tuning_param(key, val)
					applied_count += 1
		if applied_count > 0:
			print("[%s] DIAG: Tuning %s:%s = %.3f (x%d)" % [log_prefix, target, key, val, applied_count])


func _parse_cli_args() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var i = 0
	var scenario_id = ""
	while i < args.size():
		match args[i]:
			"--scenario":
				i += 1
				if i < args.size():
					scenario_id = args[i]
			"--seed":
				i += 1
				if i < args.size():
					_scenario_seed = int(args[i])
			"--headless":
				_headless = true
			"--batch":
				_batch_mode = true
			"--deterministic":
				_deterministic_mode = true
			"--fixed-step":
				i += 1
				if i < args.size():
					_fixed_sim_step = maxf(0.001, float(args[i]))
			"--duration":
				i += 1
				if i < args.size():
					_duration_override = float(args[i])
			"--output":
				i += 1
				if i < args.size():
					_output_path = args[i]
			"--no-context-steer":
				_diag_disable_context_steer = true
			"--autostart":
				_autostart = true
				print("[ProvingGround] Auto-start enabled")
			"--no-orca":
				_diag_disable_orca = true
			"--tune":
				i += 1
				if i < args.size():
					var kv: PackedStringArray = args[i].split("=", true, 1)
					if kv.size() == 2:
						var parsed: Dictionary = _parse_diag_tune_key(kv[0])
						if bool(parsed.get("has_prefix", false)) and not bool(parsed.get("valid_prefix", true)):
							printerr("[ProvingGround] Invalid --tune target in '%s' (use sim:/colony:/theater:)" % kv[0])
						else:
							_diag_tune_overrides[kv[0]] = float(kv[1])
							print("[ProvingGround] Tune override: %s:%s = %s" % [
								str(parsed.get("target", "sim")),
								str(parsed.get("key", kv[0])),
								kv[1]
							])
		i += 1

	if scenario_id.is_empty():
		# Check if launched from main menu via ScenarioState autoload
		var state: Node = get_node_or_null("/root/ScenarioState")
		if state and state.get("is_scenario_mode") and state.get("scenario_config"):
			_scenario_config = state.scenario_config
			_scenario_seed = state.seed_override if state.seed_override != 0 else _scenario_config.sim_seed
			if _scenario_seed == 0:
				_scenario_seed = randi()
			_scenario_mode = true
			_output_path = "res://test/results/%s_%d.json" % [_scenario_config.id, _scenario_seed]
			print("[ProvingGround] Scenario (menu): %s | Seed: %d | Duration: %.0fs" % [
				_scenario_config.name, _scenario_seed, _scenario_config.duration_sec])
			# Reset state so returning to menu doesn't auto-launch
			state.is_scenario_mode = false
		# Check for autostart flag file (written by Antigravity plugin)
		if not _autostart and FileAccess.file_exists("user://autostart.flag"):
			_autostart = true
			DirAccess.remove_absolute(ProjectSettings.globalize_path("user://autostart.flag"))
			print("[ProvingGround] Auto-start via flag file")
		return

	# Load scenario from CLI
	var path = "res://test/scenarios/%s.json" % scenario_id
	_scenario_config = ScenarioLoader.load_scenario(path)
	if _scenario_config == null:
		printerr("[ProvingGround] Failed to load scenario: %s" % path)
		get_tree().quit(3)  # CONFIG_ERROR
		return

	var errors: Array[String] = ScenarioLoader.validate(_scenario_config)
	if not errors.is_empty():
		for e: String in errors:
			printerr("[ProvingGround] Validation error: %s" % e)
		get_tree().quit(3)
		return

	_scenario_mode = true
	if _scenario_seed == 0:
		_scenario_seed = _scenario_config.sim_seed
	if _scenario_seed == 0:
		_scenario_seed = randi()
	if _deterministic_mode:
		print("[ProvingGround] Deterministic mode ON (fixed_step=%.4f)" % _fixed_sim_step)
	if _duration_override > 0.0:
		_scenario_config.duration_sec = _duration_override
	if _output_path.is_empty():
		_output_path = "res://test/results/%s_%d.json" % [scenario_id, _scenario_seed]

	print("[ProvingGround] Scenario: %s | Seed: %d | Duration: %.0fs | Headless: %s | Batch: %s" % [
		scenario_id, _scenario_seed, _scenario_config.duration_sec, str(_headless), str(_batch_mode)])


func _input(event: InputEvent) -> void:
	if _headless or _batch_mode:
		return

	# ── Step 1: BattleCommandUI gets first crack ──
	if _battle_ui and _battle_ui.try_handle_input(event):
		return  # Consumed by UI (ESC menu, panels, spawner, etc.)

	# ── Step 2: Mode-independent keys ──
	if event is InputEventKey and event.pressed:
		if event.keycode == KEY_TAB:
			_toggle_camera_mode()
			return

	# ── Step 3: Mode-dependent camera input ──
	if _camera_mode == CameraMode.RTS:
		_handle_rts_input(event)
	else:
		_handle_freefly_input(event)

	# ── Step 4: Shared keybinds (both modes) ──
	_handle_shared_keybinds(event)


func _handle_rts_input(event: InputEvent) -> void:
	## RTS-specific mouse/scroll handling.
	# Scroll wheel: zoom-to-cursor
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_rts_zoom_to_cursor(true)
			return
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_rts_zoom_to_cursor(false)
			return

	# Right-click drag: orbit rotation
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			if event.pressed:
				_rts_rmb_dragging = true
				_rts_drag_start_mouse = get_viewport().get_mouse_position()
			else:
				_rts_rmb_dragging = false

	# Middle-click drag: pan
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_MIDDLE:
			if event.pressed:
				_rts_mmb_dragging = true
				_rts_drag_start_mouse = get_viewport().get_mouse_position()
				_rts_drag_start_focus = _rts_focus
			else:
				_rts_mmb_dragging = false

	# Mouse motion: rotation drag or pan drag
	if event is InputEventMouseMotion:
		if _rts_rmb_dragging:
			_rts_yaw -= event.relative.x * RTS_MOUSE_ROTATE_SPEED
			_rts_yaw = fmod(_rts_yaw, 360.0)
		elif _rts_mmb_dragging:
			var sensitivity: float = 0.05 * (_rts_height / 35.0)
			var yaw_rad: float = deg_to_rad(_rts_yaw)
			var screen_right := Vector3(cos(yaw_rad + PI / 2.0), 0, sin(yaw_rad + PI / 2.0))
			var screen_up := Vector3(-sin(yaw_rad), 0, cos(yaw_rad))
			_rts_focus -= screen_right * event.relative.x * sensitivity
			_rts_focus -= screen_up * event.relative.y * sensitivity
			_rts_pan_velocity = Vector2.ZERO  # Cancel momentum when dragging

	# LMB: unit selection in RTS mode (double-click = follow-cam)
	if event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_LEFT and _debug_ready:
			if _battle_ui:
				var ground_pos: Vector3 = _get_rts_mouse_ground()
				if event.double_click:
					# Double-click: select + follow
					if _battle_ui.try_select_unit_at(ground_pos):
						_battle_ui.start_follow_cam(_battle_ui.selected_unit_id)
				else:
					_battle_ui.try_select_unit_at(ground_pos)


func _handle_freefly_input(event: InputEvent) -> void:
	## Free-fly mouse look and scroll-speed.
	if event is InputEventMouseMotion and _mouse_captured:
		_yaw -= event.relative.x * look_sensitivity
		_pitch -= event.relative.y * look_sensitivity
		_pitch = clampf(_pitch, -PI * 0.49, PI * 0.49)
		_update_rotation()

	if event is InputEventMouseButton and event.pressed:
		# Scroll controls speed
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			move_speed = minf(move_speed + speed_step, 200.0)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			move_speed = maxf(move_speed - speed_step, 5.0)
		# LMB: place threat marker
		if event.button_index == MOUSE_BUTTON_LEFT and _mouse_captured and _debug_ready:
			_place_threat()


func _handle_shared_keybinds(event: InputEvent) -> void:
	## Keybinds shared between RTS and free-fly modes.
	if not event is InputEventKey or not event.pressed:
		return

	if event.keycode == KEY_F:
		_test_destroy()

	if not _debug_ready:
		return

	if event.keycode == KEY_F2:
		_overlay.show_influence = not _overlay.show_influence
		var state: String = "ON (%s)" % VoxelDebugOverlay.LAYER_NAMES[_overlay.active_layer] if _overlay.show_influence else "OFF"
		print("[Debug] Influence overlay: %s" % state)

	if event.keycode == KEY_F3:
		if not _overlay.show_influence:
			_overlay.show_influence = true
			_overlay.active_layer = 0
		else:
			_overlay.active_layer = (_overlay.active_layer + 1) % 3
			if _overlay.active_layer == 0:
				_overlay.show_influence = false
				print("[Debug] Influence overlay: OFF")
				return
		print("[Debug] Layer: %s" % VoxelDebugOverlay.LAYER_NAMES[_overlay.active_layer])

	if event.keycode == KEY_G:
		_overlay.show_local_cover = not _overlay.show_local_cover
		print("[Debug] Local cover detail: %s" % ("ON" if _overlay.show_local_cover else "OFF"))

	if event.keycode == KEY_C:
		_overlay.clear_threats()

	if event.keycode == KEY_P:
		_test_gpu_pressure_map()

	if event.keycode == KEY_U:
		_restart_simulation()

	if event.keycode == KEY_H:
		_toggle_help()

	if event.keycode == KEY_F4:
		_toggle_lighting_panel()

	if event.keycode == KEY_L:
		_squad_hud_visible = not _squad_hud_visible
		if _squad_hud_panel:
			_squad_hud_panel.visible = _squad_hud_visible
		_set_centroid_markers_visible(_squad_hud_visible)
		if _squad_hud_visible and _mouse_captured:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			_mouse_captured = false
		print("[Debug] Squad HUD: %s" % ("ON" if _squad_hud_visible else "OFF"))

	if event.keycode == KEY_F6:
		_toggle_pheromone_overlay()

	# 5: pause/unpause simulation
	if event.keycode == KEY_5:
		_sim_paused = not _sim_paused
		print("[SimTest] %s" % ("PAUSED" if _sim_paused else "RESUMED"))

	# [ and ]: adjust time scale
	if event.keycode == KEY_BRACKETLEFT:
		_sim_time_scale = maxf(_sim_time_scale - 0.25, 0.25)
		print("[SimTest] Speed: %.2fx" % _sim_time_scale)
	if event.keycode == KEY_BRACKETRIGHT:
		_sim_time_scale = minf(_sim_time_scale + 0.25, 4.0)
		print("[SimTest] Speed: %.2fx" % _sim_time_scale)

	# +/- to adjust unit count (before starting or for next restart)
	if event.keycode == KEY_EQUAL or event.keycode == KEY_KP_ADD:
		_sim_units_per_team = mini(_sim_units_per_team + 100, 1000)
		print("[SimTest] Units per team: %d (press U to restart)" % _sim_units_per_team)
	if event.keycode == KEY_MINUS or event.keycode == KEY_KP_SUBTRACT:
		_sim_units_per_team = maxi(_sim_units_per_team - 100, 50)
		print("[SimTest] Units per team: %d (press U to restart)" % _sim_units_per_team)

	# 1-4: change team 1 formation, Shift+1-4: change team 2 formation
	if not event.ctrl_pressed:  # Avoid conflict with Ctrl+1-7 panel tabs
		var form_key: int = -1
		if event.keycode == KEY_1: form_key = 0
		elif event.keycode == KEY_2: form_key = 1
		elif event.keycode == KEY_3: form_key = 2
		elif event.keycode == KEY_4: form_key = 3

		if form_key >= 0:
			if event.shift_pressed:
				_sim_team2_formation = form_key
				print("[SimTest] Team 2 formation: %s" % FORMATION_NAMES[form_key])
				_apply_formations()
			else:
				_sim_team1_formation = form_key
				print("[SimTest] Team 1 formation: %s" % FORMATION_NAMES[form_key])
				_apply_formations()

	# T: toggle time-of-day cycle
	if event.keycode == KEY_T and not event.shift_pressed:
		if _time_of_day:
			_time_of_day.cycling = not _time_of_day.cycling
			if _time_of_day.cycling and _time_of_day.time_speed <= 0.0:
				_time_of_day.time_speed = 120.0  # Default: 2 min game = 1 sec real
			print("[TimeOfDay] Cycle %s (speed: %.0fx, time: %s %s)" % [
				"ON" if _time_of_day.cycling else "OFF",
				_time_of_day.time_speed,
				_time_of_day.get_time_string(),
				_time_of_day.get_period_name()])

	# Shift+T: advance time by 2 hours
	if event.keycode == KEY_T and event.shift_pressed:
		if _time_of_day:
			_time_of_day.set_time(_time_of_day.current_hour + 2.0)
			print("[TimeOfDay] Jumped to %s (%s)" % [
				_time_of_day.get_time_string(), _time_of_day.get_period_name()])

	# R: toggle rain
	if event.keycode == KEY_R and not event.shift_pressed:
		_toggle_rain()


func _toggle_rain() -> void:
	_rain_active = not _rain_active
	if _rain_particles:
		_rain_particles.emitting = _rain_active
		_rain_particles.visible = _rain_active
	print("[Weather] Rain %s" % ("ON" if _rain_active else "OFF"))


func _process_rts_camera(delta: float) -> void:
	## Per-frame RTS camera movement: WASD pan, edge pan, Q/E rotate, smooth zoom.
	# ── WASD Pan Input ──
	var pan_input := Vector2.ZERO
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		pan_input.y -= 1.0
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		pan_input.y += 1.0
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		pan_input.x -= 1.0
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		pan_input.x += 1.0

	# ── Edge Pan (only when not dragging and no modal open) ──
	if not _rts_rmb_dragging and not _rts_mmb_dragging:
		if not _battle_ui or not _battle_ui.is_modal_open():
			var mouse_pos: Vector2 = get_viewport().get_mouse_position()
			var vp_size: Vector2 = get_viewport().get_visible_rect().size
			if mouse_pos.x >= 0 and mouse_pos.y >= 0:  # Mouse in window
				if mouse_pos.x < RTS_EDGE_PAN_MARGIN:
					pan_input.x -= 1.0
				elif mouse_pos.x > vp_size.x - RTS_EDGE_PAN_MARGIN:
					pan_input.x += 1.0
				if mouse_pos.y < RTS_EDGE_PAN_MARGIN:
					pan_input.y -= 1.0
				elif mouse_pos.y > vp_size.y - RTS_EDGE_PAN_MARGIN:
					pan_input.y += 1.0

	# ── Ctrl speed boost ──
	var speed_mult: float = 3.0 if Input.is_key_pressed(KEY_CTRL) else 1.0

	# ── Height-dependent pan speed (faster when zoomed out) ──
	var zoom_alpha: float = _get_rts_zoom_alpha()
	var effective_speed: float = lerpf(RTS_PAN_SPEED * 0.5, RTS_PAN_SPEED * 2.5, zoom_alpha) * speed_mult

	# ── Momentum: smooth acceleration/deceleration ──
	if pan_input.length_squared() > 0.01:
		pan_input = pan_input.normalized()
		var target_vel := pan_input * effective_speed
		_rts_pan_velocity = _rts_pan_velocity.lerp(target_vel, 1.0 - exp(-RTS_PAN_ACCEL * delta))
	else:
		_rts_pan_velocity = _rts_pan_velocity.lerp(Vector2.ZERO, 1.0 - exp(-RTS_PAN_DECEL * delta))
		if _rts_pan_velocity.length_squared() < 0.01:
			_rts_pan_velocity = Vector2.ZERO

	# ── Apply pan velocity (camera-relative using yaw) ──
	if _rts_pan_velocity.length_squared() > 0.001:
		var yaw_rad: float = deg_to_rad(_rts_yaw)
		var world_dir := Vector3(
			_rts_pan_velocity.x * cos(yaw_rad) - _rts_pan_velocity.y * sin(yaw_rad),
			0.0,
			_rts_pan_velocity.x * sin(yaw_rad) + _rts_pan_velocity.y * cos(yaw_rad)
		)
		_rts_focus += world_dir * delta

	# ── Q/E Rotation ──
	var rot_input: float = 0.0
	if Input.is_key_pressed(KEY_Q):
		rot_input -= 1.0
	if Input.is_key_pressed(KEY_E):
		rot_input += 1.0
	if abs(rot_input) > 0.01:
		_rts_yaw += rot_input * RTS_ROTATION_SPEED * delta
		_rts_yaw = fmod(_rts_yaw, 360.0)

	# ── Smooth zoom interpolation ──
	_rts_height = lerpf(_rts_height, _rts_height_target, clampf(delta * 12.0, 0.0, 1.0))

	# ── Clamp focus to world bounds ──
	if _world and _world.is_initialized():
		var half_w: float = float(_world.get_world_size_x()) * _world.get_voxel_scale() * 0.5
		var half_h: float = float(_world.get_world_size_z()) * _world.get_voxel_scale() * 0.5
		_rts_focus.x = clampf(_rts_focus.x, -half_w, half_w)
		_rts_focus.z = clampf(_rts_focus.z, -half_h, half_h)

	# ── Final transform ──
	_update_rts_transform()


func _process_freefly_camera(delta: float) -> void:
	## Per-frame free-fly camera movement: WASD + mouse look.
	var input_dir = Vector3.ZERO
	if Input.is_key_pressed(KEY_W):
		input_dir.z -= 1.0
	if Input.is_key_pressed(KEY_S):
		input_dir.z += 1.0
	if Input.is_key_pressed(KEY_A):
		input_dir.x -= 1.0
	if Input.is_key_pressed(KEY_D):
		input_dir.x += 1.0
	if Input.is_key_pressed(KEY_SPACE):
		input_dir.y += 1.0
	if Input.is_key_pressed(KEY_SHIFT):
		input_dir.y -= 1.0

	var speed: float = move_speed
	if Input.is_key_pressed(KEY_CTRL):
		speed *= 3.0

	if input_dir.length_squared() > 0.01:
		input_dir = input_dir.normalized()

	var move: Vector3 = (global_transform.basis * input_dir) * speed * delta
	global_position += move


func _tick_simulation_step(sim_delta: float) -> void:
	_sim.tick(sim_delta)
	if _theater_commander:
		_theater_commander.tick(sim_delta)
	if _theater_commander_t2:
		_theater_commander_t2.tick(sim_delta)
	# LLM advisors (async, non-blocking)
	if _llm_advisor:
		_llm_advisor.tick(sim_delta)
	if _llm_advisor_t2:
		_llm_advisor_t2.tick(sim_delta)
	# Local advisors (dual mode comparison)
	if _llm_advisor_local:
		_llm_advisor_local.tick(sim_delta)
	if _llm_advisor_local_t2:
		_llm_advisor_local_t2.tick(sim_delta)
	# LLM sector commanders (sector order mode)
	if _llm_sector_cmd:
		_llm_sector_cmd.tick(sim_delta)
	if _llm_sector_cmd_t2:
		_llm_sector_cmd_t2.tick(sim_delta)
	# LLM commentator (event-driven barks)
	if _llm_commentator:
		_llm_commentator.tick(sim_delta)
	if _llm_commentator_t2:
		_llm_commentator_t2.tick(sim_delta)
	if _colony_ai_cpp:
		_colony_plan_timer -= sim_delta
		if _colony_plan_timer <= 0.0:
			_colony_plan_timer = COLONY_PLAN_INTERVAL
			var batch: Dictionary = _colony_ai_cpp.plan_intents()
			_apply_intent_batch(batch, 1)
	if _colony_ai_cpp_t2:
		_colony_plan_timer_t2 -= sim_delta
		if _colony_plan_timer_t2 <= 0.0:
			_colony_plan_timer_t2 = COLONY_PLAN_INTERVAL
			var batch_t2: Dictionary = _colony_ai_cpp_t2.plan_intents()
			_apply_intent_batch(batch_t2, 2)

	# GPU tactical map (flow field + pressure diffusion) â€” every ~0.5s
	if _gpu_map and _gpu_map.is_gpu_available():
		_gpu_map_timer -= sim_delta
		if _gpu_map_timer <= 0.0:
			_gpu_map_timer = GPU_MAP_INTERVAL
			_tick_gpu_map()

	# Cover map update (threat shadow casting) â€” every ~0.5s
	if _cover_map:
		_cover_map_timer -= sim_delta
		if _cover_map_timer <= 0.0:
			_cover_map_timer = COVER_MAP_INTERVAL
			_tick_cover_map()

	# KPI sampling (scenario mode)
	if _scenario_mode and _kpi_collector:
		var scenario_metrics: Dictionary = _collect_intent_coverage_metrics()
		_kpi_collector.sample(sim_delta, _sim, _theater_commander, _colony_ai_cpp, null, scenario_metrics)
		_scenario_elapsed += sim_delta
		_check_scenario_end()


func _process(delta: float) -> void:
	# Deferred debug system init: wait for world to be ready
	if not _debug_ready:
		_try_init_debug()
		# Auto-start simulation if --autostart was passed (for automated testing)
		if _debug_ready and _autostart:
			_autostart = false  # Only fire once
			print("[ProvingGround] Auto-starting simulation...")
			_restart_simulation()

	# Camera movement (skip in headless/batch scenario mode)
	if not _headless and not _batch_mode:
		if _camera_mode == CameraMode.RTS:
			_process_rts_camera(delta)
		else:
			_process_freefly_camera(delta)

		# Camera shake (decay + apply high-freq offset) — both modes
		if _shake_amplitude > 0.001:
			_shake_amplitude *= exp(-_shake_decay * delta)
			_shake_offset = Vector3(
				randf_range(-1.0, 1.0),
				randf_range(-1.0, 1.0),
				randf_range(-0.3, 0.3)
			) * _shake_amplitude
			global_position += _shake_offset
		elif _shake_offset.length_squared() > 0.0:
			_shake_offset = Vector3.ZERO

	# Tick simulation if running
	if _sim_running and _sim:
		if not _sim_paused:
			if _deterministic_mode:
				_fixed_step_accum += delta * _sim_time_scale
				var steps: int = 0
				var max_steps_per_frame: int = 8
				while _fixed_step_accum >= _fixed_sim_step and steps < max_steps_per_frame:
					_tick_simulation_step(_fixed_sim_step)
					_fixed_step_accum -= _fixed_sim_step
					steps += 1
				if steps == max_steps_per_frame and _fixed_step_accum >= _fixed_sim_step:
					# Prevent runaway catch-up if render stalls; keep remainder deterministic.
					_fixed_step_accum = fmod(_fixed_step_accum, _fixed_sim_step)
				if not _headless:
					_update_sim_render()
				return

			var sim_delta: float = delta * _sim_time_scale
			_sim.tick(sim_delta)
			if _theater_commander:
				_theater_commander.tick(sim_delta)
			if _theater_commander_t2:
				_theater_commander_t2.tick(sim_delta)
			# LLM advisors (async, non-blocking)
			if _llm_advisor:
				_llm_advisor.tick(sim_delta)
			if _llm_advisor_t2:
				_llm_advisor_t2.tick(sim_delta)
			# Local advisors (dual mode comparison)
			if _llm_advisor_local:
				_llm_advisor_local.tick(sim_delta)
			if _llm_advisor_local_t2:
				_llm_advisor_local_t2.tick(sim_delta)
			# LLM sector commanders (sector order mode)
			if _llm_sector_cmd:
				_llm_sector_cmd.tick(sim_delta)
			if _llm_sector_cmd_t2:
				_llm_sector_cmd_t2.tick(sim_delta)
			# LLM commentator (event-driven barks)
			if _llm_commentator:
				_llm_commentator.tick(sim_delta)
			if _llm_commentator_t2:
				_llm_commentator_t2.tick(sim_delta)
			if _colony_ai_cpp:
				_colony_plan_timer -= sim_delta
				if _colony_plan_timer <= 0.0:
					_colony_plan_timer = COLONY_PLAN_INTERVAL
					var batch: Dictionary = _colony_ai_cpp.plan_intents()
					_apply_intent_batch(batch, 1)
			if _colony_ai_cpp_t2:
				_colony_plan_timer_t2 -= sim_delta
				if _colony_plan_timer_t2 <= 0.0:
					_colony_plan_timer_t2 = COLONY_PLAN_INTERVAL
					var batch_t2: Dictionary = _colony_ai_cpp_t2.plan_intents()
					_apply_intent_batch(batch_t2, 2)

			# GPU tactical map (flow field + pressure diffusion) — every ~0.5s
			if _gpu_map and _gpu_map.is_gpu_available():
				_gpu_map_timer -= sim_delta
				if _gpu_map_timer <= 0.0:
					_gpu_map_timer = GPU_MAP_INTERVAL
					_tick_gpu_map()

			if _svdag_renderer:
				# Temporarily updating the SVO each frame if we're not headless
				var svo_buffer = _world.build_svo()
				if svo_buffer.size() > 0:
					# print("[SVDAG] Passing ", svo_buffer.size(), " bytes to SVDAG.")
					_svdag_renderer.set_svo_buffer(svo_buffer, _world.get_world_size_x(), _world.get_world_size_y(), _world.get_world_size_z())
				else:
					pass
					# print("[SVDAG] svo_buffer empty!")

			# Cover map update (threat shadow casting) — every ~0.5s
			if _cover_map:
				_cover_map_timer -= sim_delta
				if _cover_map_timer <= 0.0:
					_cover_map_timer = COVER_MAP_INTERVAL
					_tick_cover_map()

			# KPI sampling (scenario mode)
			if _scenario_mode and _kpi_collector:
				# Clamp sim_delta to prevent first-frame spike from burning duration
				var clamped_delta: float = minf(sim_delta, 0.1)
				var scenario_metrics: Dictionary = _collect_intent_coverage_metrics()
				_kpi_collector.sample(clamped_delta, _sim, _theater_commander, _colony_ai_cpp, null, scenario_metrics)
				_scenario_elapsed += clamped_delta
				_check_scenario_end()

		if not _headless:
			_update_sim_render()

	if not _headless and not _batch_mode:
		# BattleCommandUI frame update
		if _battle_ui:
			_battle_ui.update(delta)

		# Throttled Squad HUD update
		if _squad_hud_visible:
			_squad_hud_timer -= delta
			if _squad_hud_timer <= 0.0:
				_squad_hud_timer = SQUAD_HUD_INTERVAL
				_update_squad_hud()

		# Throttled minimap update
		if _minimap and _minimap.visible:
			_minimap_timer -= delta
			if _minimap_timer <= 0.0:
				_minimap_timer = MINIMAP_INTERVAL
				_update_minimap()

		# Update hero chunk physics sync + combat VFX pools
		if _effects:
			_effects.update_hero_chunks(delta)
			_effects.update_muzzle_flashes(delta)
			_effects.update_impact_pool(delta)
			_effects.update_decal_pool(delta)
			_effects.update_footstep_pool(delta)
			_effects.update_fire_smoke_pool(delta)

		# Suppression screen effects (Phase 3B) — sample pheromone at camera
		if _sim_running and _sim and _suppression_overlay:
			_suppression_sample_timer -= delta
			if _suppression_sample_timer <= 0.0:
				_suppression_sample_timer = SUPPRESSION_SAMPLE_INTERVAL
				_update_suppression_intensity()
			# Smooth interpolation toward target
			var target: float = _suppression_intensity
			var current: float = _suppression_overlay.material.get_shader_parameter("intensity")
			var smoothed: float = lerpf(current, target, 1.0 - exp(-8.0 * delta))
			_suppression_overlay.material.set_shader_parameter("intensity", smoothed)
			_suppression_overlay.visible = smoothed > 0.01

		# Time of day tick (Phase 4A)
		if _time_of_day:
			_time_of_day.tick(delta)

		# Battle haze over combat zones (Phase 5C)
		if _sim_running and _sim:
			if not _battle_haze_initialized:
				_init_battle_haze()
			_battle_haze_timer -= delta
			if _battle_haze_timer <= 0.0:
				_battle_haze_timer = BATTLE_HAZE_UPDATE_INTERVAL
				_update_battle_haze()

		# Rain follows camera + wetness ramp (Phase 4B)
		if _rain_particles:
			_rain_particles.global_position = Vector3(global_position.x, global_position.y + 40.0, global_position.z)
			# Wetness ramps up when raining, down when not
			var wetness_target: float = 1.0 if _rain_active else 0.0
			var wetness_speed: float = 0.1 if _rain_active else 0.033  # 10s up, 30s down
			_weather_wetness = move_toward(_weather_wetness, wetness_target, wetness_speed * delta)
			# Pass wetness to voxel shader
			if _renderer and _weather_wetness != _renderer.get_weather_wetness():
				_renderer.set_weather_wetness(_weather_wetness)


## Apply camera shake impulse. Amplitude decays exponentially.
## distance: world distance from explosion to camera.
## blast_radius: radius of the explosion (bigger = stronger shake farther away).
func apply_camera_shake(distance: float, blast_radius: float) -> void:
	var effective_range: float = blast_radius * 8.0  # Shake felt up to 8x blast radius
	if distance > effective_range:
		return
	# Amplitude: strong up close, falls off with distance squared
	var falloff: float = 1.0 - clampf(distance / effective_range, 0.0, 1.0)
	falloff *= falloff  # Quadratic falloff
	var amplitude: float = clampf(blast_radius * 0.02 * falloff, 0.0, 0.15)
	# Only override if this shake is stronger than current
	if amplitude > _shake_amplitude:
		_shake_amplitude = amplitude
		_shake_decay = 6.0  # ~0.5s to decay to negligible


## Sample pheromone channels at camera position to drive suppression screen effects.
func _update_suppression_intensity() -> void:
	var cam_pos: Vector3 = global_position
	var total: float = 0.0
	# Sample DANGER (ch 0) and SUPPRESSION (ch 1) from both teams
	for team in [0, 1]:
		var phero = _sim.get_pheromone_map(team)
		if phero:
			total += phero.sample(cam_pos, 0)  # DANGER
			total += phero.sample(cam_pos, 1)  # SUPPRESSION
	# Normalize: typical combat deposits 0.5-2.0 density at epicenter
	_suppression_intensity = clampf(total * 0.3, 0.0, 0.85)


## Initialize battle haze FogVolume pool (Phase 5C).
func _init_battle_haze() -> void:
	for i in BATTLE_HAZE_POOL_SIZE:
		var fog = FogVolume.new()
		fog.size = Vector3(20.0, 6.0, 20.0)
		fog.shape = RenderingServer.FOG_VOLUME_SHAPE_ELLIPSOID
		var fog_mat = FogMaterial.new()
		fog_mat.density = 0.0  # Starts invisible
		fog_mat.albedo = Color(0.45, 0.42, 0.38, 1.0)  # Dusty brown-gray
		fog.material = fog_mat
		fog.visible = false
		add_child(fog)
		_battle_haze_volumes.append(fog)
		_battle_haze_materials.append(fog_mat)
	_battle_haze_initialized = true


## Update battle haze positions — place FogVolumes at highest-suppression zones.
func _update_battle_haze() -> void:
	if not _sim or not _battle_haze_initialized:
		return

	# Collect top suppression hotspots from both teams' pheromone maps
	var hotspots: Array[Dictionary] = []
	for team in [0, 1]:
		var phero = _sim.get_pheromone_map(team)
		if not phero:
			continue
		# Sample a grid of points and find highest-suppression cells
		var data: PackedFloat32Array = phero.get_channel_data(1)  # SUPPRESSION channel
		if data.is_empty():
			continue
		# Query pheromone grid dimensions from map
		var grid_w: int = phero.get_width()
		var grid_h: int = phero.get_height()
		var cell_size: float = phero.get_cell_size()
		var half_x: float = float(grid_w) * cell_size * 0.5
		var half_z: float = float(grid_h) * cell_size * 0.5
		# Find top cells by stepping through at coarse intervals
		for gz in range(0, grid_h, 5):
			for gx in range(0, grid_w, 5):
				var idx: int = gz * grid_w + gx
				if idx < data.size() and data[idx] > 0.3:
					var world_x: float = float(gx) * cell_size - half_x + cell_size * 2.5
					var world_z: float = float(gz) * cell_size - half_z + cell_size * 2.5
					hotspots.append({"pos": Vector3(world_x, 3.0, world_z), "val": data[idx]})

	# Sort by suppression value descending
	hotspots.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a["val"] > b["val"])

	# Assign top hotspots to fog volumes
	for i in BATTLE_HAZE_POOL_SIZE:
		if i < hotspots.size():
			var spot: Dictionary = hotspots[i]
			var fog: FogVolume = _battle_haze_volumes[i]
			fog.global_position = spot["pos"]
			# Density proportional to suppression intensity (0.005-0.02)
			var density: float = clampf(spot["val"] * 0.015, 0.005, 0.02)
			_battle_haze_materials[i].density = density
			fog.visible = true
		else:
			_battle_haze_volumes[i].visible = false
			_battle_haze_materials[i].density = 0.0


func _update_rotation() -> void:
	rotation = Vector3(_pitch, _yaw, 0)


# ── RTS Camera ──────────────────────────────────────────────────────

func _get_rts_zoom_alpha() -> float:
	return clampf((_rts_height - RTS_MIN_HEIGHT) / maxf(RTS_MAX_HEIGHT - RTS_MIN_HEIGHT, 0.001), 0.0, 1.0)


func _update_rts_transform() -> void:
	var zoom_alpha: float = _get_rts_zoom_alpha()

	# Dynamic pitch: steeper close, flatter far
	var pitch_deg: float = lerpf(RTS_PITCH_CLOSE, RTS_PITCH_FAR, zoom_alpha)
	var pitch_rad: float = deg_to_rad(pitch_deg)

	# Dynamic FOV: narrower close, wider far
	fov = lerpf(RTS_FOV_CLOSE, RTS_FOV_FAR, zoom_alpha)

	# Ground-following: adjust focus Y to terrain height
	var ground_y: float = _get_ground_height(_rts_focus.x, _rts_focus.z)
	_rts_focus.y = ground_y

	# Compute camera position offset from focus (orbital)
	var yaw_rad: float = deg_to_rad(_rts_yaw)
	var clamped_pitch: float = clampf(abs(pitch_rad), 0.05, PI / 2.0 - 0.05)
	var horizontal_dist: float = _rts_height / tan(clamped_pitch)

	var offset := Vector3(
		horizontal_dist * sin(yaw_rad),
		_rts_height,
		horizontal_dist * cos(yaw_rad)
	)

	global_position = _rts_focus + offset
	look_at(_rts_focus, Vector3.UP)


func _get_rts_mouse_ground() -> Vector3:
	## Ray-intersect mouse position with ground plane at _rts_focus.y.
	var mouse_pos: Vector2 = get_viewport().get_mouse_position()
	var from: Vector3 = project_ray_origin(mouse_pos)
	var dir: Vector3 = project_ray_normal(mouse_pos)
	var plane_y: float = _rts_focus.y
	if abs(dir.y) < 0.001:
		return _rts_focus
	var t: float = (plane_y - from.y) / dir.y
	if t < 0:
		return _rts_focus
	return from + dir * t


func _rts_zoom_to_cursor(scroll_in: bool) -> void:
	## SOTA zoom-to-cursor: terrain under mouse stays fixed during zoom.
	# Step 1: ground position under cursor BEFORE zoom
	var mouse_ground_before: Vector3 = _get_rts_mouse_ground()

	# Step 2: adjust height target
	if scroll_in:
		_rts_height_target = maxf(RTS_MIN_HEIGHT, _rts_height_target - RTS_ZOOM_SPEED)
	else:
		_rts_height_target = minf(RTS_MAX_HEIGHT, _rts_height_target + RTS_ZOOM_SPEED)

	# Step 3: preview transform at new height
	var old_height: float = _rts_height
	_rts_height = _rts_height_target
	_update_rts_transform()
	var mouse_ground_after: Vector3 = _get_rts_mouse_ground()
	_rts_height = old_height  # Restore (will lerp in _process)

	# Step 4: shift focus so cursor-ground stays fixed
	var correction: Vector3 = mouse_ground_before - mouse_ground_after
	correction.y = 0.0
	_rts_focus += correction


func _get_ground_height(wx: float, wz: float) -> float:
	## Query terrain elevation for ground-following.
	if not _world or not _world.is_initialized():
		return 0.0
	# Use GpuTacticalMap if available (1m resolution, smooth)
	if _gpu_map and _gpu_map.has_method("get_terrain_height_m"):
		return _gpu_map.get_terrain_height_m(wx, wz)
	# Fallback: voxel-resolution height
	var vpos: Vector3i = _world.world_to_voxel(Vector3(wx, 0.0, wz))
	var top_y: int = _world.get_column_top_y(vpos.x, vpos.z)
	if top_y < 0:
		return 0.0
	return float(top_y) * _world.get_voxel_scale()


# ── Debug System Init ───────────────────────────────────────────────

func _try_init_debug() -> void:
	_world = get_parent().get_node_or_null("VoxelWorld") as VoxelWorld
	if not _world or not _world.is_initialized():
		return

	# Create TacticalCoverMap (needed for sim logic, not just visuals)
	_cover_map = TacticalCoverMap.new()
	_cover_map.setup(_world.get_world_size_x(), _world.get_world_size_z(), _world.get_voxel_scale())

	# Create InfluenceMapCPP (needed for TheaterCommander/ColonyAI)
	var map_w: float = float(_world.get_world_size_x()) * _world.get_voxel_scale()
	var map_h: float = float(_world.get_world_size_z()) * _world.get_voxel_scale()
	_influence_map = InfluenceMapCPP.new()
	_influence_map.setup(1, map_w, map_h, 4.0)

	if not _headless:
		# Create overlay (3D)
		_overlay = VoxelDebugOverlay.new()
		_overlay.setup(self, _world, _cover_map, _influence_map)
		get_parent().add_child(_overlay)

		# Create HUD (2D) — skip if BattleCommandUI replaces it
		if not _battle_ui:
			_hud = VoxelDebugHUD.new()
			_hud.setup(self, _world, _cover_map, _influence_map)
			get_parent().add_child(_hud)

		# Create Effects node for destruction VFX
		_effects = get_parent().get_node_or_null("Effects") as Effects
		if not _effects:
			_effects = Effects.new()
			_effects.name = "Effects"
			get_parent().add_child(_effects)
		_effects.set_voxel_world(_world)

		# Get renderer reference for island spawning
		_renderer = get_parent().get_node_or_null("VoxelWorld/Renderer") as VoxelWorldRenderer
		if _renderer and _batch_mode:
			# Batch runs do not need physics chunk bodies and can exceed Jolt body
			# limits on large maps (30k+ chunks).
			_renderer.generate_collisions = false
			print("[ProvingGround] Batch mode: chunk collision body generation disabled")

	# Create StructuralIntegrity for island detection
	_structural_integrity = StructuralIntegrity.new()

	_debug_ready = true
	print("[VoxelTest] Debug systems initialized (cover grid: %dx%d, influence grid: %dx%d)" % [
		_cover_map.get_cells_x(), _cover_map.get_cells_z(),
		_influence_map.get_sectors_x(), _influence_map.get_sectors_z()
	])

	# Wire BattleCommandUI with system references
	if _battle_ui and _battle_ui.has_method("setup"):
		_battle_ui.setup({
			"sim": null,  # Updated when sim starts
			"world": _world,
			"cover_map": _cover_map,
			"influence_map": _influence_map,
			"gpu_map": _gpu_map,
			"cam": self,
			"camera_script": self,
		})

	# Auto-start scenario if in scenario mode
	if _scenario_mode:
		_start_scenario()


# ── Camera Mode Toggle ──────────────────────────────────────────────

func _toggle_camera_mode() -> void:
	if _camera_mode == CameraMode.RTS:
		# Switch to free-fly: start from current orbital position
		_camera_mode = CameraMode.FREE_FLY
		# Derive free-fly orientation from current RTS orbital angles
		var zoom_alpha: float = _get_rts_zoom_alpha()
		_pitch = deg_to_rad(lerpf(RTS_PITCH_CLOSE, RTS_PITCH_FAR, zoom_alpha))
		_yaw = deg_to_rad(_rts_yaw)
		_update_rotation()
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		_mouse_captured = true
		print("[Camera] Free-fly mode (Tab to return to RTS)")
	else:
		# Switch to RTS: derive focus from where camera is looking
		_camera_mode = CameraMode.RTS
		_freefly_saved_pos = global_position
		_freefly_saved_yaw = _yaw
		_freefly_saved_pitch = _pitch
		# Ray-intersect look direction with Y=0 plane to find focus
		var forward: Vector3 = -global_transform.basis.z
		if abs(forward.y) > 0.001:
			var t: float = -global_position.y / forward.y
			if t > 0:
				_rts_focus = global_position + forward * t
			else:
				_rts_focus = Vector3(global_position.x, 0, global_position.z)
		else:
			_rts_focus = Vector3(global_position.x, 0, global_position.z)
		_rts_height = clampf(global_position.y - _rts_focus.y, RTS_MIN_HEIGHT, RTS_MAX_HEIGHT)
		_rts_height_target = _rts_height
		_rts_yaw = rad_to_deg(_yaw)
		_rts_pan_velocity = Vector2.ZERO
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_mouse_captured = false
		print("[Camera] RTS mode (Tab for free-fly, Q/E rotate, scroll zoom)")


# ── Destruction ─────────────────────────────────────────────────────

func _test_destroy() -> void:
	if not _world or not _world.is_initialized():
		print("[VoxelTest] World not ready")
		return

	var forward: Vector3 = -global_transform.basis.z
	var hit: Dictionary = _world.raycast_dict(global_position, forward, 100.0)
	if hit.is_empty() or not hit.get("hit", false):
		print("[VoxelTest] No voxel hit (aiming at empty air)")
		return

	var target: Vector3 = hit["world_pos"]
	var result: Dictionary = _world.destroy_sphere_ex(target, 2.0, 16)
	var destroyed: int = result.get("destroyed", 0)
	var dominant_mat: int = result.get("dominant_material", 0)
	print("[VoxelTest] Destroyed %d voxels at %s (dominant_mat=%d, rubble=%d)" % [
		destroyed, target, dominant_mat, _world.get_active_rubble_count()
	])

	# Full VFX pipeline
	if _effects and destroyed > 0:
		_effects.spawn_explosion(target, 5.0)
		_effects.spawn_voxel_destruction(
			target, 2.0, dominant_mat,
			result.get("material_histogram", PackedInt32Array()),
			result.get("debris", [])
		)

	# Structural integrity check
	if _structural_integrity and _renderer and destroyed > 0:
		var islands: Array = _structural_integrity.detect_islands(_world, target, 4.0)
		for island in islands:
			_renderer.spawn_island(island)
			print("[VoxelTest] Island detached: %d voxels, %.0f mass" % [
				island.get("voxel_count", 0), island.get("mass", 0.0)
			])

		# Support propagation: collapse voxels beyond material support distance
		var weakened: Array = _structural_integrity.detect_weakened_voxels(_world, target, 4.0)
		if weakened.size() > 0:
			print("[VoxelTest] Support propagation: %d weakened voxels" % weakened.size())
			for v in weakened:
				var vpos: Vector3i = v["position"]
				_world.set_voxel_dirty(vpos.x, vpos.y, vpos.z, 0)
			# Cascade: re-run island detection on newly disconnected regions
			var cascade_islands: Array = _structural_integrity.detect_islands(_world, target, 6.0)
			for island in cascade_islands:
				_renderer.spawn_island(island)
				print("[VoxelTest] Cascade island: %d voxels, %.0f mass" % [
					island.get("voxel_count", 0), island.get("mass", 0.0)
				])

	# Recalculate cover if threats exist
	if _debug_ready and _overlay.threat_positions.size() > 0:
		_overlay._recalculate_cover()


# ── Threat Placement ────────────────────────────────────────────────

func _place_threat() -> void:
	if not _world or not _world.is_initialized():
		return

	var forward: Vector3 = -global_transform.basis.z
	var hit: Dictionary = _world.raycast_dict(global_position, forward, 200.0)
	if hit.is_empty() or not hit.get("hit", false):
		print("[VoxelTest] No voxel hit for threat placement")
		return

	_overlay.place_threat(hit["world_pos"])


# ── GPU Pressure Map Test ──────────────────────────────────────────

func _test_gpu_pressure_map() -> void:
	if not _world or not _world.is_initialized():
		print("[GpuTest] World not ready")
		return

	var map_w: float = float(_world.get_world_size_x()) * _world.get_voxel_scale()
	var map_h: float = float(_world.get_world_size_z()) * _world.get_voxel_scale()

	# Create GPU map if not yet
	if _gpu_map == null:
		_gpu_map = GpuTacticalMap.new()
		_gpu_map.setup(_world, map_w, map_h)
		if not _gpu_map.is_gpu_available():
			print("[GpuTest] FAIL: GPU compute not available")
			_gpu_map = null
			return
		print("[GpuTest] GPU map created: pressure=%dx%d, cover=%dx%d" % [
			_gpu_map.get_pressure_width(), _gpu_map.get_pressure_height(),
			_gpu_map.get_cover_width(), _gpu_map.get_cover_height()
		])

	# Synthetic test data: enemies at map center, friendlies at west
	var enemies = PackedVector3Array()
	enemies.append(Vector3(0, 4, 0))       # Center
	enemies.append(Vector3(20, 4, 10))     # Slightly NE
	enemies.append(Vector3(-10, 4, -15))   # Slightly SW

	var friendlies = PackedVector3Array()
	friendlies.append(Vector3(-100, 4, 0))  # West
	friendlies.append(Vector3(-90, 4, 10))
	friendlies.append(Vector3(-95, 4, -10))

	var threats = PackedVector3Array()
	threats.append(Vector3(0, 4, 0))        # Threat at center

	var goals = PackedVector3Array()
	goals.append(Vector3(-50, 4, 0))        # Goal between teams

	var strengths = PackedFloat32Array()
	strengths.append(3.0)

	# Run tick
	var t0: int = Time.get_ticks_usec()
	_gpu_map.tick(friendlies, enemies, threats, goals, strengths)
	var elapsed: int = Time.get_ticks_usec() - t0

	print("[GpuTest] tick() completed in %d us" % elapsed)

	# Query results at various positions
	var test_points: Array[Vector3] = [
		Vector3(0, 4, 0),       # At enemy (high threat)
		Vector3(-50, 4, 0),     # At goal (high goal, medium threat)
		Vector3(-100, 4, 0),    # At friendly (low threat)
		Vector3(100, 4, 0),     # Far east (low everything)
	]

	for p: Vector3 in test_points:
		var threat: float = _gpu_map.get_threat_at(p)
		var goal: float = _gpu_map.get_goal_at(p)
		var cover: float = _gpu_map.get_cover_at(p)
		var flow: Vector3 = _gpu_map.get_flow_vector(p)
		print("[GpuTest]  pos=(%d,%d) threat=%.2f goal=%.2f cover=%.2f flow=(%.2f,%.2f)" % [
			int(p.x), int(p.z), threat, goal, cover, flow.x, flow.z
		])

	# Check pressure debug array has data
	var pressure: PackedFloat32Array = _gpu_map.get_pressure_debug()
	var cover_data: PackedFloat32Array = _gpu_map.get_cover_debug()
	var p_nonzero: int = 0
	for v: float in pressure:
		if v > 0.001:
			p_nonzero += 1
	var c_nonzero: int = 0
	for v: float in cover_data:
		if v > 0.001:
			c_nonzero += 1

	print("[GpuTest] Pressure: %d/%d cells non-zero" % [p_nonzero, pressure.size()])
	print("[GpuTest] Cover: %d/%d cells non-zero" % [c_nonzero, cover_data.size()])

	if p_nonzero > 0:
		print("[GpuTest] PASS: Pressure diffusion is working")
	else:
		print("[GpuTest] WARN: No pressure data — check shader")


# ── SimulationServer Test ─────────────────────────────────────────

func _stop_simulation() -> void:
	if not _sim_running:
		return
	_sim_running = false

	# Clear BattleCommandUI references and reset stale state
	if _battle_ui:
		_battle_ui.reset_on_sim_change()
		_battle_ui.sim = null
		_battle_ui.theater_t1 = null
		_battle_ui.theater_t2 = null
		_battle_ui.colony_t1 = null
		_battle_ui.colony_t2 = null

	_sim = null
	_theater_commander = null
	_theater_commander_t2 = null
	_colony_ai_cpp = null
	_colony_ai_cpp_t2 = null
	_influence_map = null
	_gpu_map = null
	_llm_sector_cmd = null
	_llm_sector_cmd_t2 = null
	if _llm_commentator:
		_llm_commentator.reset_tracking()
	_llm_commentator = null
	_llm_commentator_t2 = null
	if _mm_team1:
		_mm_team1.queue_free()
		_mm_team1 = null
	if _mm_team2:
		_mm_team2.queue_free()
		_mm_team2 = null
	if _mm_tracers:
		_mm_tracers.queue_free()
		_mm_tracers = null
	if _mm_grenades:
		_mm_grenades.queue_free()
		_mm_grenades = null
	if _mm_corpses:
		_mm_corpses.queue_free()
		_mm_corpses = null
	if _sim_hud_label:
		_sim_hud_label.queue_free()
		_sim_hud_label = null
	for marker: Node3D in _capture_markers:
		marker.queue_free()
	_capture_markers.clear()
	_capture_spheres.clear()
	_capture_labels.clear()
	_capture_rings.clear()
	_gpu_tick_timer = 0.0
	_gpu_map_timer = 0.0
	_cover_map_timer = 0.25
	_fixed_step_accum = 0.0
	# Clean up visual debug systems
	for m: MeshInstance3D in _centroid_markers:
		m.queue_free()
	_centroid_markers.clear()
	_centroid_labels.clear()
	for wl: Label3D in _waypoint_labels:
		wl.queue_free()
	_waypoint_labels.clear()
	if _waypoint_mesh_inst:
		_waypoint_mesh_inst.queue_free()
		_waypoint_mesh_inst = null
		_waypoint_immediate = null
	if _debug_canvas:
		_debug_canvas.queue_free()
		_debug_canvas = null
	_squad_hud_panel = null
	_squad_hud_rows.clear()
	_minimap = null
	_squad_hud_visible = false
	_selected_squad_id = -1
	_last_intent_goal_name_t1.clear()
	_last_intent_target_t1.clear()
	_last_intent_action_t1.clear()
	_last_intent_seen_t1.clear()
	_last_intent_goal_name_t2.clear()
	_last_intent_target_t2.clear()
	_last_intent_action_t2.clear()
	_last_intent_seen_t2.clear()
	_t1_squad_count = 0
	_t2_squad_count = 0
	_team2_squad_base = 64
	_cached_render_data1.clear()
	_cached_render_data2.clear()
	print("[SimTest] Simulation stopped")


func _restart_simulation() -> void:
	if _sim_running:
		_stop_simulation()
		# Wait a frame for queue_free to process
		await get_tree().process_frame
	if _scenario_mode and _scenario_config:
		_start_scenario_sim()
	else:
		_test_simulation_server()


func _assign_random_personality(uid: int) -> void:
	var roll: float = randf()
	var pers: int = SimulationServer.PERS_STEADY
	if roll < 0.10:
		pers = SimulationServer.PERS_BERSERKER
	elif roll < 0.20:
		pers = SimulationServer.PERS_CATATONIC
	elif roll < 0.25:
		pers = SimulationServer.PERS_PARANOID
	_sim.set_unit_personality(uid, pers)


func _team2_sim_squad_id(local_sq: int) -> int:
	return _team2_squad_base + local_sq


func _configure_squad_id_layout(t1_squads: int, t2_squads: int, context_label: String) -> bool:
	if not _sim:
		printerr("[%s] SimulationServer not initialized for squad ID layout" % context_label)
		return false

	var max_squads: int = 2048  # SimulationServer::MAX_SQUADS
	var total_requested: int = t1_squads + t2_squads
	if total_requested > max_squads:
		var msg: String = "[%s] Requested %d squads (T1=%d, T2=%d) exceeds SimulationServer capacity (%d)." % [
			context_label, total_requested, t1_squads, t2_squads, max_squads
		]
		printerr(msg)
		if _scenario_mode:
			get_tree().quit(3)
		return false

	_team2_squad_base = t1_squads
	return true


func _preflight_unit_capacity(t1_units: int, t2_units: int, context_label: String) -> bool:
	if not _sim:
		printerr("[%s] SimulationServer not initialized for unit-capacity preflight" % context_label)
		return false
	var max_units: int = 12288  # SimulationServer::MAX_UNITS
	var requested_units: int = t1_units + t2_units
	if requested_units > max_units:
		var msg: String = "[%s] Requested %d units (T1=%d, T2=%d) exceeds SimulationServer capacity (%d)." % [
			context_label, requested_units, t1_units, t2_units, max_units
		]
		printerr(msg)
		if _scenario_mode:
			get_tree().quit(3)
		return false
	return true


func _collect_intent_coverage_metrics() -> Dictionary:
	var out: Dictionary = {
		"squads_missing_recent_intent_t1": 0,
		"squads_missing_recent_intent_t2": 0,
		"gpu_cull_invalid_entries_dropped": 0,
		"gpu_cull_over_capacity_frames": 0
	}
	if not _sim or not _sim_running:
		return out

	var now_t: float = _sim.get_game_time()
	if now_t < INTENT_METRIC_WARMUP_SEC:
		return out

	var missing_t1: int = 0
	for sq_i: int in range(_t1_squad_count):
		if _sim.get_squad_alive_count(sq_i) <= 0:
			continue
		var last_seen: float = float(_last_intent_seen_t1.get(sq_i, -1.0))
		if last_seen < 0.0 or (now_t - last_seen) > INTENT_RECENT_WINDOW_SEC:
			missing_t1 += 1

	var missing_t2: int = 0
	for sq_i: int in range(_t2_squad_count):
		var sim_sq_id: int = _team2_sim_squad_id(sq_i)
		if _sim.get_squad_alive_count(sim_sq_id) <= 0:
			continue
		var last_seen: float = float(_last_intent_seen_t2.get(sim_sq_id, -1.0))
		if last_seen < 0.0 or (now_t - last_seen) > INTENT_RECENT_WINDOW_SEC:
			missing_t2 += 1

	out["squads_missing_recent_intent_t1"] = missing_t1
	out["squads_missing_recent_intent_t2"] = missing_t2
	if _renderer and _renderer.has_method("get_gpu_cull_debug_stats"):
		var cull_stats: Dictionary = _renderer.get_gpu_cull_debug_stats()
		out["gpu_cull_invalid_entries_dropped"] = int(cull_stats.get("invalid_entries_dropped", 0))
		out["gpu_cull_over_capacity_frames"] = int(cull_stats.get("over_capacity_frames", 0))
	return out


func _apply_formations() -> void:
	if not _sim or not _sim_running:
		return
	for sq: int in range(_t1_squad_count):
		_sim.set_squad_formation(sq, _sim_team1_formation)
	for sq: int in range(_t2_squad_count):
		_sim.set_squad_formation(_team2_sim_squad_id(sq), _sim_team2_formation)


# ── Proving Ground: Scenario Lifecycle ─────────────────────────────

func _start_scenario() -> void:
	print("[ProvingGround] Starting scenario: %s" % _scenario_config.name)
	_kpi_collector = KPICollector.new()

	# Apply scenario config
	var tc1: ScenarioLoader.TeamConfig = _scenario_config.teams[0]
	var tc2: ScenarioLoader.TeamConfig = _scenario_config.teams[1]
	_sim_units_per_team = tc1.units
	_sim_team1_formation = tc1.formation
	_sim_team2_formation = tc2.formation
	_sim_time_scale = _scenario_config.time_scale

	_start_scenario_sim()


func _start_scenario_sim() -> void:
	## Scenario-aware variant of _test_simulation_server().
	if not _world or not _world.is_initialized():
		printerr("[ProvingGround] World not ready")
		get_tree().quit(2)
		return

	var map_w: float = float(_world.get_world_size_x()) * _world.get_voxel_scale()
	var map_h: float = float(_world.get_world_size_z()) * _world.get_voxel_scale()
	_fixed_step_accum = 0.0

	# Create SimulationServer with deterministic seed
	_sim = SimulationServer.new()
	_sim.setup(map_w, map_h)
	_sim.set_seed(_scenario_seed)
	_sim.set_tuning_param("movement_model", float(_scenario_config.movement_model))
	print("[ProvingGround] Movement model: %d (0=legacy_blend, 1=hierarchical_v2)" % _scenario_config.movement_model)

	# Apply diagnostic system toggles (CLI --no-context-steer, --no-orca, --tune)
	if _diag_disable_context_steer:
		_sim.set_context_steering_enabled(false)
		print("[ProvingGround] DIAG: Context steering DISABLED (old additive mode)")
	if _diag_disable_orca:
		_sim.set_orca_enabled(false)
		print("[ProvingGround] DIAG: ORCA collision avoidance DISABLED")
	_apply_diag_tune_overrides_for("sim", "ProvingGround")

	# GPU tactical map (optional, may not be available in headless)
	if not _gpu_map:
		_gpu_map = GpuTacticalMap.new()
		_gpu_map.setup(_world, map_w, map_h)
		if not _gpu_map.is_gpu_available():
			print("[ProvingGround] GPU tactical map unavailable — no flow field or gas (headless=%s)" % str(_headless))
			_gpu_map = null
		else:
			print("[ProvingGround] GPU tactical map active — flow field + gas enabled")
	if _gpu_map:
		_sim.set_gpu_tactical_map(_gpu_map)

	var tc1: ScenarioLoader.TeamConfig = _scenario_config.teams[0]
	var tc2: ScenarioLoader.TeamConfig = _scenario_config.teams[1]
	var t1_spawn_center: Vector3 = tc1.spawn_center
	var t2_spawn_center: Vector3 = tc2.spawn_center
	var spawn_y: float = t1_spawn_center.y
	var capture_positions: Array[Vector3] = _scenario_config.capture_points.duplicate()
	var total_units: int = tc1.units + tc2.units
	var scale_policy: String = _scenario_config.scale_policy
	if scale_policy == "adaptive_contact":
		var requested_gap: float = _scenario_config.spawn_front_gap_m
		if requested_gap <= 0.0:
			# Use a conservative effective advance speed model at scale.
			# In practice not all squads advance symmetrically every tick.
			var nominal_advance_speed_mps: float = 3.0
			requested_gap = _scenario_config.contact_sla_sec * nominal_advance_speed_mps
		var spawn_gap: float = clampf(requested_gap, 40.0, map_w * 0.75)
		t1_spawn_center.x = -spawn_gap * 0.5
		t2_spawn_center.x = spawn_gap * 0.5

		var objective_count: int = _scenario_config.objective_count_override
		if objective_count <= 0:
			objective_count = clampi(int(round(float(total_units) / 1800.0)), 3, 7)
		capture_positions = _build_contested_band_capture_positions(
			objective_count,
			map_h,
			(t1_spawn_center.y + t2_spawn_center.y) * 0.5
		)
		print("[ProvingGround] Adaptive contact enabled: gap=%.1fm, objectives=%d, contact_sla=%.1fs" % [
			spawn_gap, objective_count, _scenario_config.contact_sla_sec
		])

	if _renderer and _renderer.has_method("set_startup_focus"):
		var focus := (t1_spawn_center + t2_spawn_center) * 0.5
		_renderer.set_startup_focus(Vector3(focus.x, 0.0, focus.z))

	var t0: int = Time.get_ticks_usec()

	if not _preflight_unit_capacity(tc1.units, tc2.units, "ProvingGround"):
		return

	# Build role distribution arrays from fractions
	var t1_roles: Array[int] = _build_role_distribution(tc1.roles, tc1.units)
	var t2_roles: Array[int] = _build_role_distribution(tc2.roles, tc2.units)
	var t1_squad_roles: Array[String] = _infer_squad_roles(t1_roles, tc1.units, tc1.squad_size)
	var t2_squad_roles: Array[String] = _infer_squad_roles(t2_roles, tc2.units, tc2.squad_size)
	var t1_pers: Array[int] = _build_personality_distribution(tc1.personalities, tc1.units)
	var t2_pers: Array[int] = _build_personality_distribution(tc2.personalities, tc2.units)

	# Spawn teams as squad clusters in a compact square grid (same layout as standard mode).
	# Grid centered on spawn_center, depth rows extend AWAY from enemy.
	var t1_num_squads: int = maxi(ceili(float(tc1.units) / float(tc1.squad_size)), 1)
	var t2_num_squads: int = maxi(ceili(float(tc2.units) / float(tc2.squad_size)), 1)
	if not _configure_squad_id_layout(t1_num_squads, t2_num_squads, "ProvingGround"):
		return

	# Shared grid parameters (match standard path)
	var half_w: float = map_w * 0.5
	var half_h: float = map_h * 0.5

	# --- Team 1 spawn ---
	var t1_sq_side: int = maxi(ceili(sqrt(float(tc1.squad_size))), 2)
	var t1_unit_spacing: float = 2.5
	var t1_squad_gap: float = 5.0
	var t1_squad_footprint: float = float(t1_sq_side) * t1_unit_spacing
	var t1_squad_pitch: float = t1_squad_footprint + t1_squad_gap
	var t1_squad_cols: int = maxi(ceili(sqrt(float(t1_num_squads))), 1)
	var t1_squad_rows: int = ceili(float(t1_num_squads) / float(t1_squad_cols))
	# Clamp spacing if grid exceeds map bounds
	var t1_grid_z: float = float(t1_squad_rows) * t1_squad_pitch
	if t1_grid_z > half_h * 0.9 and half_h > 0.0:
		var sf: float = (half_h * 0.9) / t1_grid_z
		t1_unit_spacing *= sf; t1_squad_gap *= sf
		t1_squad_footprint = float(t1_sq_side) * t1_unit_spacing
		t1_squad_pitch = t1_squad_footprint + t1_squad_gap
		t1_grid_z = float(t1_squad_rows) * t1_squad_pitch
	var t1_grid_z_start: float = t1_spawn_center.z - t1_grid_z * 0.5
	# advance_dir sign: +1 means T1 faces +X, depth rows extend -X
	var t1_depth_sign: float = -signf(tc1.advance_dir) if tc1.advance_dir != 0.0 else -1.0
	for i: int in range(tc1.units):
		var sq_id: int = i / tc1.squad_size
		var local_idx: int = i % tc1.squad_size
		var m_col: int = local_idx % t1_sq_side
		var m_row: int = local_idx / t1_sq_side
		var sq_col: int = sq_id % t1_squad_cols
		var sq_row: int = sq_id / t1_squad_cols
		var sq_x: float = t1_spawn_center.x + float(sq_col) * t1_squad_pitch * t1_depth_sign
		var sq_z: float = t1_grid_z_start + float(sq_row) * t1_squad_pitch
		var pos = Vector3(
			sq_x + float(m_col) * t1_unit_spacing * t1_depth_sign,
			spawn_y,
			sq_z + float(m_row) * t1_unit_spacing
		)
		var uid: int = _sim.spawn_unit(pos, 1, t1_roles[i], sq_id)
		_sim.set_unit_personality(uid, t1_pers[i])

	# --- Team 2 spawn ---
	var t2_sq_side: int = maxi(ceili(sqrt(float(tc2.squad_size))), 2)
	var t2_unit_spacing: float = 2.5
	var t2_squad_gap: float = 5.0
	var t2_squad_footprint: float = float(t2_sq_side) * t2_unit_spacing
	var t2_squad_pitch: float = t2_squad_footprint + t2_squad_gap
	var t2_squad_cols: int = maxi(ceili(sqrt(float(t2_num_squads))), 1)
	var t2_squad_rows: int = ceili(float(t2_num_squads) / float(t2_squad_cols))
	var t2_grid_z: float = float(t2_squad_rows) * t2_squad_pitch
	if t2_grid_z > half_h * 0.9 and half_h > 0.0:
		var sf: float = (half_h * 0.9) / t2_grid_z
		t2_unit_spacing *= sf; t2_squad_gap *= sf
		t2_squad_footprint = float(t2_sq_side) * t2_unit_spacing
		t2_squad_pitch = t2_squad_footprint + t2_squad_gap
		t2_grid_z = float(t2_squad_rows) * t2_squad_pitch
	var t2_grid_z_start: float = t2_spawn_center.z - t2_grid_z * 0.5
	var t2_depth_sign: float = -signf(tc2.advance_dir) if tc2.advance_dir != 0.0 else 1.0
	for i: int in range(tc2.units):
		var sq_id: int = _team2_sim_squad_id(i / tc2.squad_size)
		var local_sq: int = i / tc2.squad_size
		var local_idx: int = i % tc2.squad_size
		var m_col: int = local_idx % t2_sq_side
		var m_row: int = local_idx / t2_sq_side
		var sq_col: int = local_sq % t2_squad_cols
		var sq_row: int = local_sq / t2_squad_cols
		var sq_x: float = t2_spawn_center.x + float(sq_col) * t2_squad_pitch * t2_depth_sign
		var sq_z: float = t2_grid_z_start + float(sq_row) * t2_squad_pitch
		var pos = Vector3(
			sq_x + float(m_col) * t2_unit_spacing * t2_depth_sign,
			t2_spawn_center.y,
			sq_z + float(m_row) * t2_unit_spacing
		)
		var uid: int = _sim.spawn_unit(pos, 2, t2_roles[i], sq_id)
		_sim.set_unit_personality(uid, t2_pers[i])

	var elapsed: int = Time.get_ticks_usec() - t0
	print("[ProvingGround] Spawned %d units in %d us" % [_sim.get_unit_count(), elapsed])

	# Register capture points from scenario
	_active_capture_positions.assign(capture_positions)
	for cp_pos: Vector3 in _active_capture_positions:
		_sim.add_capture_point(cp_pos)

	# Set squad rally points and advance directions (matching new square grid layout)
	_squad_max_advance.clear()
	var t1_squads: int = t1_num_squads
	var t2_squads: int = t2_num_squads
	_t1_squad_count = t1_squads
	_t2_squad_count = t2_squads

	# Team 1: rally at each squad's grid position, advance toward center/capture
	var t1_target: Vector3 = capture_positions[0] if capture_positions.size() > 0 else Vector3.ZERO
	for sq: int in range(t1_squads):
		var sq_col: int = sq % t1_squad_cols
		var sq_row: int = sq / t1_squad_cols
		var rx: float = t1_spawn_center.x + float(sq_col) * t1_squad_pitch * t1_depth_sign
		var rz: float = t1_grid_z_start + float(sq_row) * t1_squad_pitch
		var rally: Vector3 = Vector3(rx, spawn_y, rz)
		var advance := Vector3(tc1.advance_dir, 0, 0).normalized()
		if capture_positions.size() > 0:
			var to_target := Vector3(t1_target.x - rally.x, 0.0, t1_target.z - rally.z)
			if to_target.length_squared() > 1.0:
				advance = to_target.normalized()
		_sim.set_squad_rally(sq, rally, advance)
		_squad_max_advance[sq] = 0.0  # Colony AI takes over within 1-2s

	# Team 2: same pattern
	var t2_target: Vector3 = capture_positions[0] if capture_positions.size() > 0 else Vector3.ZERO
	for sq: int in range(t2_squads):
		var sq_col: int = sq % t2_squad_cols
		var sq_row: int = sq / t2_squad_cols
		var rx: float = t2_spawn_center.x + float(sq_col) * t2_squad_pitch * t2_depth_sign
		var rz: float = t2_grid_z_start + float(sq_row) * t2_squad_pitch
		var rally: Vector3 = Vector3(rx, t2_spawn_center.y, rz)
		var advance := Vector3(tc2.advance_dir, 0, 0).normalized()
		if capture_positions.size() > 0:
			var to_target := Vector3(t2_target.x - rally.x, 0.0, t2_target.z - rally.z)
			if to_target.length_squared() > 1.0:
				advance = to_target.normalized()
		var sim_sq: int = _team2_sim_squad_id(sq)
		_sim.set_squad_rally(sim_sq, rally, advance)
		_squad_max_advance[sim_sq] = 0.0  # Colony AI takes over within 1-2s

	# Set formations
	for sq: int in range(t1_squads):
		_sim.set_squad_formation(sq, tc1.formation)
	for sq: int in range(t2_squads):
		_sim.set_squad_formation(_team2_sim_squad_id(sq), tc2.formation)

	# Give orders: follow squad
	for i: int in range(tc1.units):
		_sim.set_order(i, SimulationServer.ORDER_FOLLOW_SQUAD, Vector3.ZERO)
	for i: int in range(tc1.units, tc1.units + tc2.units):
		_sim.set_order(i, SimulationServer.ORDER_FOLLOW_SQUAD, Vector3.ZERO)

	# Visual rendering (skip in headless)
	if not _headless:
		_create_sim_multimesh()
		_create_capture_markers()

		# Debug stats label — skip if BattleCommandUI replaces it
		if not _battle_ui:
			_sim_hud_label = Label.new()
			_sim_hud_label.position = Vector2(10, 10)
			_sim_hud_label.add_theme_font_size_override("font_size", 16)
			_sim_hud_label.add_theme_color_override("font_color", Color.WHITE)
			get_parent().add_child(_sim_hud_label)

		_create_squad_hud()
		_create_centroid_markers()
		_create_minimap()

	# Theater Commander (optional per scenario)
	if _scenario_config.enable_theater_commander:
		# Team 1
		_theater_commander = TheaterCommander.new()
		_theater_commander.setup(1, map_w, map_h)
		if _influence_map:
			_theater_commander.set_influence_map(_influence_map)
		# Team 2
		_theater_commander_t2 = TheaterCommander.new()
		_theater_commander_t2.setup(2, map_w, map_h)
		if _influence_map:
			_theater_commander_t2.set_influence_map(_influence_map)
		print("[ProvingGround] Theater Commanders initialized (both teams)")

	# Colony AI (optional per scenario)
	if _scenario_config.enable_colony_ai:
		# Team 1
		_colony_ai_cpp = ColonyAICPP.new()
		_colony_ai_cpp.setup(1, map_w, map_h, t1_squads)
		if _influence_map:
			_colony_ai_cpp.set_influence_map(_influence_map)
		_colony_ai_cpp.set_push_direction(tc1.advance_dir)
		_colony_ai_cpp.set_base_x(t1_spawn_center.x)
		for sq_i: int in range(t1_squads):
			_colony_ai_cpp.set_squad_sim_id(sq_i, sq_i)
			_colony_ai_cpp.set_squad_role(sq_i, t1_squad_roles[sq_i])
		# Team 2
		_colony_ai_cpp_t2 = ColonyAICPP.new()
		_colony_ai_cpp_t2.setup(2, map_w, map_h, t2_squads)
		if _influence_map:
			_colony_ai_cpp_t2.set_influence_map(_influence_map)
		_colony_ai_cpp_t2.set_push_direction(tc2.advance_dir)
		_colony_ai_cpp_t2.set_base_x(t2_spawn_center.x)
		for sq_i: int in range(t2_squads):
			_colony_ai_cpp_t2.set_squad_sim_id(sq_i, _team2_sim_squad_id(sq_i))
			_colony_ai_cpp_t2.set_squad_role(sq_i, t2_squad_roles[sq_i])
		print("[ProvingGround] ColonyAICPP initialized (T1: %d squads, T2: %d squads)" % [t1_squads, t2_squads])

	_apply_diag_tune_overrides_for("theater", "ProvingGround")
	_apply_diag_tune_overrides_for("colony", "ProvingGround")

	# LLM integration (after both Theater + Colony are ready)
	if _scenario_config.enable_theater_commander:
		_setup_llm_advisors(map_w, map_h)

	_sim_running = true

	# Update BattleCommandUI with sim + AI references
	if _battle_ui:
		_battle_ui.sim = _sim
		_battle_ui.theater_t1 = _theater_commander
		_battle_ui.theater_t2 = _theater_commander_t2
		_battle_ui.colony_t1 = _colony_ai_cpp
		_battle_ui.colony_t2 = _colony_ai_cpp_t2

	print("[ProvingGround] Simulation started — %d alive | T1: %s (%d) | T2: %s (%d)" % [
		_sim.get_alive_count(),
		FORMATION_NAMES[tc1.formation], tc1.units,
		FORMATION_NAMES[tc2.formation], tc2.units,
	])


func _build_role_distribution(role_fractions: Dictionary, total: int) -> Array[int]:
	## Convert {"rifleman": 0.75, "mg": 0.125, ...} to Array[int] of role enums.
	var result: Array[int] = []
	result.resize(total)
	var idx = 0
	for role_name: String in role_fractions:
		var count: int = roundi(float(role_fractions[role_name]) * total)
		var role_enum: int = ScenarioLoader.resolve_role_id(role_name)
		for _j: int in range(count):
			if idx < total:
				result[idx] = role_enum
				idx += 1
	# Fill remainder with riflemen
	while idx < total:
		result[idx] = SimulationServer.ROLE_RIFLEMAN
		idx += 1
	return result


func _infer_squad_roles(role_distribution: Array[int], total_units: int, squad_size: int) -> Array[String]:
	## Derive ColonyAICPP squad archetypes from per-unit combat roles.
	## This keeps strategic goals (e.g., fire_mission) aligned with spawned rosters.
	var squad_count: int = maxi(ceili(float(total_units) / float(maxi(squad_size, 1))), 1)
	var result: Array[String] = []
	result.resize(squad_count)
	for sq_i: int in range(squad_count):
		var start_idx: int = sq_i * squad_size
		var end_idx: int = mini(start_idx + squad_size, total_units)
		var mortar_count = 0
		var mg_count = 0
		var marksman_count = 0
		for unit_idx: int in range(start_idx, end_idx):
			if unit_idx >= role_distribution.size():
				break
			var role_enum: int = role_distribution[unit_idx]
			if role_enum == SimulationServer.ROLE_MORTAR:
				mortar_count += 1
			elif role_enum == SimulationServer.ROLE_MG:
				mg_count += 1
			elif role_enum == SimulationServer.ROLE_MARKSMAN:
				marksman_count += 1

		if mortar_count > 0:
			result[sq_i] = "mortar"
		elif marksman_count >= 2:
			result[sq_i] = "sniper"
		elif mg_count >= 2:
			result[sq_i] = "defend"
		else:
			result[sq_i] = "assault"
	return result


func _build_personality_distribution(pers_fractions: Dictionary, total: int) -> Array[int]:
	## Convert {"steady": 0.75, "berserker": 0.10, ...} to Array[int] of personality enums.
	var result: Array[int] = []
	result.resize(total)
	var idx = 0
	for pers_name: String in pers_fractions:
		var count: int = roundi(float(pers_fractions[pers_name]) * total)
		var pers_enum: int = ScenarioLoader._personality_map().get(pers_name, SimulationServer.PERS_STEADY)
		for _j: int in range(count):
			if idx < total:
				result[idx] = pers_enum
				idx += 1
	# Fill remainder with steady
	while idx < total:
		result[idx] = SimulationServer.PERS_STEADY
		idx += 1
	return result


func _check_scenario_end() -> void:
	if _scenario_ended:
		return

	# Grace period: don't check end conditions for the first 3 seconds
	# (allows units to spawn, settle, and begin engaging)
	if _scenario_elapsed < 3.0:
		return

	var done = false
	var reason = ""

	# Team eliminated (check first — more specific than duration)
	if _scenario_config.end_on_team_eliminated:
		var t1_alive: int = _sim.get_alive_count_for_team(1)
		var t2_alive: int = _sim.get_alive_count_for_team(2)
		if t1_alive == 0:
			done = true
			reason = "team1_eliminated"
		elif t2_alive == 0:
			done = true
			reason = "team2_eliminated"

	# All capture points owned by one team
	if not done and _scenario_config.end_on_all_captured and _active_capture_positions.size() > 0:
		var cap: Dictionary = _sim.get_capture_data()
		var owners = cap.get("owners", PackedInt32Array())
		var total_cp: int = owners.size()
		if total_cp > 0:
			var t1_caps = 0
			var t2_caps = 0
			for o: int in owners:
				if o == 1: t1_caps += 1
				elif o == 2: t2_caps += 1
			if t1_caps == total_cp or t2_caps == total_cp:
				done = true
				reason = "all_captured_by_team%d" % (1 if t1_caps == total_cp else 2)

	# Duration exceeded (check last — least interesting end condition)
	if not done and _scenario_elapsed >= _scenario_config.duration_sec:
		done = true
		reason = "duration_reached"

	if done:
		_scenario_ended = true
		print("[ProvingGround] END TRIGGER: %s at %.1fs | T1 alive: %d | T2 alive: %d" % [
			reason, _scenario_elapsed,
			_sim.get_alive_count_for_team(1), _sim.get_alive_count_for_team(2)])
		_finalize_scenario(reason)


func _finalize_scenario(end_reason: String) -> void:
	print("[ProvingGround] Scenario ended: %s (%.1fs elapsed)" % [end_reason, _scenario_elapsed])

	# Pause simulation
	_sim_paused = true

	var tc1: ScenarioLoader.TeamConfig = _scenario_config.teams[0]
	var tc2: ScenarioLoader.TeamConfig = _scenario_config.teams[1]
	var aggregates: Dictionary = _kpi_collector.compute_aggregates(tc1.units, tc2.units)
	aggregates["end_reason"] = end_reason

	var summary: Dictionary = RunSummary.evaluate(
		_scenario_config, aggregates, _scenario_seed, _scenario_elapsed)

	# Print summary
	var pass_str: String = "PASS" if summary["pass"] else "FAIL"
	print("[ProvingGround] Result: %s" % pass_str)
	if summary.has("fail_reasons"):
		for fr: Variant in summary["fail_reasons"]:
			print("[ProvingGround]   - %s" % str(fr))

	# Print key stats
	print("[ProvingGround] Tick p50=%.2fms p95=%.2fms p99=%.2fms max=%.2fms" % [
		aggregates.get("tick_p50_ms", 0), aggregates.get("tick_p95_ms", 0),
		aggregates.get("tick_p99_ms", 0), aggregates.get("tick_max_ms", 0)])
	print("[ProvingGround] Alive: T1=%d/%d  T2=%d/%d" % [
		aggregates.get("final_alive_team1", 0), tc1.units,
		aggregates.get("final_alive_team2", 0), tc2.units])

	# Fog-of-war stats
	print("[ProvingGround] === FOG OF WAR ===")
	print("[ProvingGround] Avg visible: T1=%.1f  T2=%.1f" % [
		aggregates.get("avg_vis_team1", 0), aggregates.get("avg_vis_team2", 0)])
	print("[ProvingGround] Vis checks/tick: %.0f  Hit rate: %.1f%%" % [
		aggregates.get("avg_vis_checks", 0), aggregates.get("vis_hit_rate", 0)])
	print("[ProvingGround] Targets skipped (FOW)/tick: %.0f  Suppressive/tick: %.1f" % [
		aggregates.get("avg_fow_targets_skipped", 0), aggregates.get("avg_fow_suppressive", 0)])
	print("[ProvingGround] Engagements visible/tick: %.1f  Suppressive: %.1f  Wall-blocked: %.1f" % [
		aggregates.get("avg_engagements_visible", 0), aggregates.get("avg_engagements_suppressive", 0),
		aggregates.get("avg_wall_blocked", 0)])
	print("[ProvingGround] Influence filtered/tick: %.0f" % aggregates.get("avg_influence_filtered", 0))
	print("[ProvingGround] Cumulative: vis_checks=%d  hits=%d  skipped=%d  suppressive=%d" % [
		aggregates.get("total_vis_checks", 0), aggregates.get("total_vis_hits", 0),
		aggregates.get("total_fow_skipped", 0), aggregates.get("total_suppressive", 0)])

	# Location tracking
	print("[ProvingGround] === LOCATION TRACKING ===")
	print("[ProvingGround] Avg dist to slot: T1=%.1fm  T2=%.1fm" % [
		aggregates.get("avg_dist_slot_t1", 0), aggregates.get("avg_dist_slot_t2", 0)])
	print("[ProvingGround] Peak dist to slot: T1=%.1fm  T2=%.1fm" % [
		aggregates.get("peak_dist_slot_t1", 0), aggregates.get("peak_dist_slot_t2", 0)])
	print("[ProvingGround] Avg squad spread: %.1fm  Peak units >20m off: %d" % [
		aggregates.get("avg_squad_spread", 0), aggregates.get("peak_units_beyond_20m", 0)])
	print("[ProvingGround] Dist by state: IDLE=%.1f MOVE=%.1f ENGAGE=%.1f COVER=%.1f FLANK=%.1f SUPP=%.1f" % [
		aggregates.get("avg_dist_idle", 0), aggregates.get("avg_dist_moving", 0),
		aggregates.get("avg_dist_engaging", 0), aggregates.get("avg_dist_in_cover", 0),
		aggregates.get("avg_dist_flanking", 0), aggregates.get("avg_dist_suppressing", 0)])
	print("[ProvingGround] Movement: formation=%.2f m/s  flow=%.2f m/s  threat=%.2f m/s" % [
		aggregates.get("avg_formation_pull", 0), aggregates.get("avg_flow_push", 0),
		aggregates.get("avg_threat_push", 0)])
	print("[ProvingGround] Advance offset: avg=%.1fm  peak=%.1fm" % [
		aggregates.get("avg_advance_offset", 0), aggregates.get("peak_advance_offset", 0)])
	print("[ProvingGround] Avg climbing: %.1f  Avg falling: %.1f" % [
		aggregates.get("avg_climbing", 0), aggregates.get("avg_falling", 0)])
	var dur: float = maxf(_scenario_elapsed, 1.0)
	var total_climbs: int = aggregates.get("total_climb_events", 0)
	var total_falls: int = aggregates.get("total_fall_events", 0)
	print("[ProvingGround] Climb events: %d (%.1f/sec)  Fall events: %d (%.1f/sec)  Fall damage: %d" % [
		total_climbs, float(total_climbs) / dur,
		total_falls, float(total_falls) / dur,
		aggregates.get("total_fall_damage_events", 0)])
	print("[ProvingGround] Goal reassignments: %d (%.1f/sec)" % [
		_goal_change_count, float(_goal_change_count) / dur])

	# Include per-second snapshot timeseries for temporal analysis
	summary["snapshots"] = _kpi_collector.get_snapshots()

	# Export JSON
	var err: Error = RunSummary.export_json(summary, _output_path)
	if err != OK:
		printerr("[ProvingGround] Failed to write output: %s" % _output_path)
	else:
		print("[ProvingGround] Results written to: %s" % _output_path)

	if _headless or _batch_mode:
		# Headless/batch CI mode — exit with code
		var exit_code: int = summary.get("exit_code", 0)
		get_tree().quit(exit_code)
	else:
		# Interactive mode — show results overlay, let user fly around or return to menu
		_show_results_overlay(summary, aggregates, tc1.units, tc2.units, end_reason)


func _show_results_overlay(summary: Dictionary, aggregates: Dictionary,
		t1_total: int, t2_total: int, end_reason: String) -> void:
	# Release mouse so user can click buttons
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_mouse_captured = false

	_results_overlay = CanvasLayer.new()
	_results_overlay.layer = 100
	get_parent().add_child(_results_overlay)

	# Semi-transparent background
	var bg = ColorRect.new()
	bg.color = Color(0.0, 0.0, 0.0, 0.6)
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_results_overlay.add_child(bg)

	# Center panel
	var panel = PanelContainer.new()
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.08, 0.09, 0.14, 0.95)
	style.corner_radius_top_left = 12
	style.corner_radius_top_right = 12
	style.corner_radius_bottom_left = 12
	style.corner_radius_bottom_right = 12
	style.content_margin_left = 32
	style.content_margin_right = 32
	style.content_margin_top = 24
	style.content_margin_bottom = 24
	style.border_width_left = 2
	style.border_width_right = 2
	style.border_width_top = 2
	style.border_width_bottom = 2
	style.border_color = Color(0.3, 0.4, 0.6, 0.8)
	panel.add_theme_stylebox_override("panel", style)
	panel.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	panel.custom_minimum_size = Vector2(520, 0)
	_results_overlay.add_child(panel)

	var vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	panel.add_child(vbox)

	# Header
	var pass_fail: bool = summary.get("pass", false)
	var header = Label.new()
	header.text = "SCENARIO COMPLETE"
	header.add_theme_font_size_override("font_size", 28)
	header.add_theme_color_override("font_color", Color(0.9, 0.95, 1.0))
	header.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(header)

	# Result badge
	var result_label = Label.new()
	result_label.text = "PASS" if pass_fail else "FAIL"
	result_label.add_theme_font_size_override("font_size", 22)
	result_label.add_theme_color_override("font_color",
		Color(0.2, 0.9, 0.3) if pass_fail else Color(1.0, 0.3, 0.3))
	result_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(result_label)

	var sep = HSeparator.new()
	vbox.add_child(sep)

	# Scenario info
	_add_result_row(vbox, "Scenario", _scenario_config.name)
	_add_result_row(vbox, "End Reason", end_reason.replace("_", " ").capitalize())
	_add_result_row(vbox, "Duration", "%.1fs / %.0fs" % [_scenario_elapsed, _scenario_config.duration_sec])
	_add_result_row(vbox, "Seed", str(_scenario_seed))

	var sep2 = HSeparator.new()
	vbox.add_child(sep2)

	# Casualties
	var t1_alive: int = aggregates.get("final_alive_team1", 0)
	var t2_alive: int = aggregates.get("final_alive_team2", 0)
	_add_result_row(vbox, "Blue Team", "%d / %d alive (%.0f%%)" % [
		t1_alive, t1_total, (float(t1_alive) / maxf(t1_total, 1)) * 100.0],
		Color(0.4, 0.6, 1.0))
	_add_result_row(vbox, "Red Team", "%d / %d alive (%.0f%%)" % [
		t2_alive, t2_total, (float(t2_alive) / maxf(t2_total, 1)) * 100.0],
		Color(1.0, 0.4, 0.4))

	var sep3 = HSeparator.new()
	vbox.add_child(sep3)

	# Performance
	_add_result_row(vbox, "Tick p95", "%.2f ms" % aggregates.get("tick_p95_ms", 0))
	_add_result_row(vbox, "Tick max", "%.2f ms" % aggregates.get("tick_max_ms", 0))

	# Fail reasons
	var fail_reasons: Array = summary.get("fail_reasons", [])
	if not fail_reasons.is_empty():
		var sep4 = HSeparator.new()
		vbox.add_child(sep4)
		var fail_header = Label.new()
		fail_header.text = "FAILURES"
		fail_header.add_theme_font_size_override("font_size", 16)
		fail_header.add_theme_color_override("font_color", Color(1.0, 0.4, 0.3))
		vbox.add_child(fail_header)
		for fr: Variant in fail_reasons:
			var fl = Label.new()
			fl.text = "  - %s" % str(fr)
			fl.add_theme_font_size_override("font_size", 13)
			fl.add_theme_color_override("font_color", Color(1.0, 0.6, 0.5))
			fl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			vbox.add_child(fl)

	var sep5 = HSeparator.new()
	vbox.add_child(sep5)

	# Buttons
	var btn_row = HBoxContainer.new()
	btn_row.alignment = BoxContainer.ALIGNMENT_CENTER
	btn_row.add_theme_constant_override("separation", 16)
	vbox.add_child(btn_row)

	var menu_btn = Button.new()
	menu_btn.text = "RETURN TO MENU"
	menu_btn.custom_minimum_size = Vector2(180, 40)
	menu_btn.add_theme_font_size_override("font_size", 16)
	menu_btn.pressed.connect(_on_return_to_menu)
	btn_row.add_child(menu_btn)

	var continue_btn = Button.new()
	continue_btn.text = "KEEP WATCHING"
	continue_btn.custom_minimum_size = Vector2(180, 40)
	continue_btn.add_theme_font_size_override("font_size", 16)
	continue_btn.pressed.connect(_on_dismiss_results)
	btn_row.add_child(continue_btn)

	# Hint
	var hint = Label.new()
	hint.text = "You can still fly around the battlefield"
	hint.add_theme_font_size_override("font_size", 12)
	hint.add_theme_color_override("font_color", Color(0.4, 0.45, 0.5))
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(hint)


func _add_result_row(parent: VBoxContainer, label_text: String, value_text: String,
		value_color: Color = Color(0.8, 0.85, 0.9)) -> void:
	var row = HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	parent.add_child(row)

	var lbl = Label.new()
	lbl.text = label_text + ":"
	lbl.add_theme_font_size_override("font_size", 15)
	lbl.add_theme_color_override("font_color", Color(0.5, 0.55, 0.6))
	lbl.custom_minimum_size = Vector2(120, 0)
	row.add_child(lbl)

	var val = Label.new()
	val.text = value_text
	val.add_theme_font_size_override("font_size", 15)
	val.add_theme_color_override("font_color", value_color)
	val.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(val)


func _on_return_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/main_menu.tscn")


func _on_dismiss_results() -> void:
	if _results_overlay:
		_results_overlay.queue_free()
		_results_overlay = null
	# Re-capture mouse for camera control
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	_mouse_captured = true
	# Unpause so user can keep watching (sim continues from frozen state)
	_sim_paused = false


func _toggle_pheromone_overlay() -> void:
	if not _sim or not _sim_running:
		print("[Debug] Pheromone HUD requires running simulation")
		return

	if _phero_hud:
		_phero_hud.queue_free()
		_phero_hud = null
		print("[Debug] Pheromone HUD: OFF")
		return

	var hud_script = load("res://ui/pheromone_debug_hud.gd")
	if not hud_script:
		print("[Debug] ERROR: Could not load pheromone_debug_hud.gd")
		return

	var phero_map = _sim.get_pheromone_map(_phero_overlay_team)
	if not phero_map:
		print("[Debug] ERROR: No pheromone map for team %d" % _phero_overlay_team)
		return

	_phero_hud = CanvasLayer.new()
	_phero_hud.set_script(hud_script)
	_phero_hud.pheromone_map = phero_map
	_phero_hud.sim = _sim
	_phero_hud.active_channel = 0
	_phero_hud.active_team = _phero_overlay_team
	get_parent().add_child(_phero_hud)
	print("[Debug] Pheromone HUD: ON (team %d, ch 0: DANGER)" % _phero_overlay_team)
	print("  Keys: ,/. = channel, T = team, F6 = close")

func _toggle_help() -> void:
	_show_help = not _show_help
	if _show_help and not _sim_help_label:
		_sim_help_label = Label.new()
		_sim_help_label.position = Vector2(10, 40)
		_sim_help_label.add_theme_font_size_override("font_size", 14)
		_sim_help_label.add_theme_color_override("font_color", Color(1, 1, 0.7))
		get_parent().add_child(_sim_help_label)
	if _sim_help_label:
		_sim_help_label.visible = _show_help
		var pause_str: String = "PAUSED" if _sim_paused else "Running"
		var tod_str: String = ""
		if _time_of_day:
			tod_str = " | %s (%s)" % [_time_of_day.get_time_string(), _time_of_day.get_period_name()]
		_sim_help_label.text = (
			"[H] Toggle help  [U] Start/Restart sim  [Tab] Top-down\n" +
			"[5] Pause (%s)  [ [ ] [ ] ] Speed: %.2fx\n" % [pause_str, _sim_time_scale] +
			"[+/-] Units/team: %d  [F] Destroy voxels\n" % _sim_units_per_team +
			"[1-4] Team1 form: %s  [Shift+1-4] Team2 form: %s\n" % [FORMATION_NAMES[_sim_team1_formation], FORMATION_NAMES[_sim_team2_formation]] +
			"  1=LINE 2=WEDGE 3=COLUMN 4=CIRCLE\n" +
			"[F2] Overlay  [F3] Cycle layer  [F4] Lighting  [F6] Pheromone HUD  [G] Cover  [P] GPU test\n" +
			"[T] Day/Night cycle  [Shift+T] +2 hours  [R] Rain%s\n" % tod_str +
			"[LMB] Place threat  [C] Clear  [Esc] Mouse\n" +
			"[L] Squad HUD + markers | Pheromone HUD: ,/. = channels"
		)


func _assign_squad_role(member_index: int, sq_size: int) -> int:
	## Assign a role based on position within the squad.
	## Member 0 = leader, 1 = medic, 2 = MG, rest = rifleman with some specialists.
	if member_index == 0:
		return SimulationServer.ROLE_LEADER
	elif member_index == 1:
		return SimulationServer.ROLE_MEDIC
	elif member_index == 2:
		return SimulationServer.ROLE_MG
	elif member_index == sq_size - 1 and sq_size >= 6:
		return SimulationServer.ROLE_MARKSMAN
	elif member_index == sq_size - 2 and sq_size >= 8:
		return SimulationServer.ROLE_GRENADIER
	return SimulationServer.ROLE_RIFLEMAN


func _compute_capture_positions(mw: float, mh: float) -> Array[Vector3]:
	## Generate capture points as a symmetric pattern scaled to map size.
	var cp_x: float = (mw * 0.5) * ScenarioState.capture_x_fraction
	var cp_z: float = (mh * 0.5) * ScenarioState.capture_z_fraction
	return [
		Vector3(0.0, 0.0, 0.0),        # Alpha — center
		Vector3(-cp_x, 0.0, -cp_z),     # Bravo — SW
		Vector3(-cp_x, 0.0,  cp_z),     # Charlie — NW
		Vector3( cp_x, 0.0, -cp_z),     # Delta — SE
		Vector3( cp_x, 0.0,  cp_z),     # Echo — NE
	]


func _build_contested_band_capture_positions(count: int, map_h: float, y: float) -> Array[Vector3]:
	## Generate objectives in a central contested band to encourage early contact.
	var out: Array[Vector3] = []
	var objective_count: int = maxi(1, count)
	var span_z: float = clampf(map_h * 0.35, 80.0, 320.0)
	var half_span: float = span_z * 0.5
	for i: int in range(objective_count):
		var t: float = 0.0 if objective_count <= 1 else float(i) / float(objective_count - 1)
		var z: float = lerpf(-half_span, half_span, t)
		var x: float = 0.0
		if objective_count > 3 and (i % 2) == 1:
			x = 18.0
		out.append(Vector3(x, y, z))
	return out


func _test_simulation_server() -> void:
	if _sim_running:
		print("[SimTest] Already running")
		return

	if not _world or not _world.is_initialized():
		print("[SimTest] World not ready")
		return

	var map_w: float = float(_world.get_world_size_x()) * _world.get_voxel_scale()
	var map_h: float = float(_world.get_world_size_z()) * _world.get_voxel_scale()
	_fixed_step_accum = 0.0

	# Create SimulationServer
	_sim = SimulationServer.new()
	_sim.setup(map_w, map_h)

	# Apply CLI tune overrides (sim namespace or unprefixed)
	_apply_diag_tune_overrides_for("sim", "SimTest")

	# Create GPU pressure map if not yet, then wire it
	if not _gpu_map:
		_gpu_map = GpuTacticalMap.new()
		_gpu_map.setup(_world, map_w, map_h)
		if _gpu_map.is_gpu_available():
			print("[SimTest] GPU tactical map created")
		else:
			print("[SimTest] GPU compute not available — no flow avoidance")
			_gpu_map = null
	if _gpu_map:
		_sim.set_gpu_tactical_map(_gpu_map)

	# --- Map-relative spawn layout ---
	var half_w: float = map_w * 0.5
	var half_h: float = map_h * 0.5
	var spawn_frac: float = ScenarioState.spawn_x_fraction
	var squad_sz: int = ScenarioState.squad_size
	var units_per_team: int = _sim_units_per_team
	var spawn_y: float = 5.0
	if not _preflight_unit_capacity(units_per_team, units_per_team, "SimTest"):
		return

	# Symmetric spawn centers — fraction of map half-width from center
	var t1_spawn_x: float = -half_w * spawn_frac  # e.g. -120 on 600m, -60 on 300m
	var t2_spawn_x: float =  half_w * spawn_frac  # e.g. +120 on 600m, +60 on 300m

	# Squad grid layout — squads arranged in a grid, members clustered within each squad
	var num_squads: int = maxi(ceili(float(units_per_team) / float(squad_sz)), 1)
	if not _configure_squad_id_layout(num_squads, num_squads, "SimTest"):
		return
	var sq_side: int = maxi(ceili(sqrt(float(squad_sz))), 2)  # members per squad row (e.g. 4 for 10)
	var unit_spacing: float = 2.5  # meters between members within a squad
	var squad_gap: float = 5.0     # meters gap between squad clusters
	var squad_footprint: float = float(sq_side) * unit_spacing  # e.g. 10m for 4×2.5
	var squad_pitch: float = squad_footprint + squad_gap  # center-to-center spacing

	# Arrange squads in a grid (cols × rows of squads)
	var squad_cols: int = maxi(ceili(sqrt(float(num_squads))), 1)
	var squad_rows: int = ceili(float(num_squads) / float(squad_cols))

	# Clamp spacing if total grid exceeds map bounds
	var grid_x_total: float = float(squad_cols) * squad_pitch
	var grid_z_total: float = float(squad_rows) * squad_pitch
	var x_budget: float = half_w * spawn_frac  # space from spawn to center
	if grid_x_total > x_budget and x_budget > 0.0:
		var scale_factor: float = x_budget / grid_x_total
		unit_spacing *= scale_factor
		squad_gap *= scale_factor
		squad_footprint = float(sq_side) * unit_spacing
		squad_pitch = squad_footprint + squad_gap
		grid_x_total = float(squad_cols) * squad_pitch
		grid_z_total = float(squad_rows) * squad_pitch
	if grid_z_total > half_h * 0.9 and half_h > 0.0:
		var scale_factor: float = (half_h * 0.9) / grid_z_total
		unit_spacing *= scale_factor
		squad_gap *= scale_factor
		squad_footprint = float(sq_side) * unit_spacing
		squad_pitch = squad_footprint + squad_gap
		grid_z_total = float(squad_rows) * squad_pitch

	var grid_z_start: float = -grid_z_total * 0.5  # center grid on Z=0

	var t0: int = Time.get_ticks_usec()

	# Team 1 (west) — spawn squads as clusters, grid extends toward center (+x)
	for sq: int in range(num_squads):
		var sq_col: int = sq % squad_cols
		var sq_row: int = sq / squad_cols
		var sq_origin_x: float = t1_spawn_x + float(sq_col) * squad_pitch
		var sq_origin_z: float = grid_z_start + float(sq_row) * squad_pitch
		var members: int = mini(squad_sz, units_per_team - sq * squad_sz)
		for m: int in range(members):
			var m_col: int = m % sq_side
			var m_row: int = m / sq_side
			var x: float = sq_origin_x + float(m_col) * unit_spacing
			var z: float = sq_origin_z + float(m_row) * unit_spacing
			var role: int = _assign_squad_role(m, squad_sz)
			var uid: int = _sim.spawn_unit(Vector3(x, spawn_y, z), 1, role, sq)
			_assign_random_personality(uid)

	# Team 2 (east) — mirror, grid extends toward center (-x)
	for sq: int in range(num_squads):
		var sq_col: int = sq % squad_cols
		var sq_row: int = sq / squad_cols
		var sq_origin_x: float = t2_spawn_x - float(sq_col) * squad_pitch
		var sq_origin_z: float = grid_z_start + float(sq_row) * squad_pitch
		var members: int = mini(squad_sz, units_per_team - sq * squad_sz)
		for m: int in range(members):
			var m_col: int = m % sq_side
			var m_row: int = m / sq_side
			var x: float = sq_origin_x - float(m_col) * unit_spacing  # mirror: extend inward
			var z: float = sq_origin_z + float(m_row) * unit_spacing
			var role: int = _assign_squad_role(m, squad_sz)
			var uid: int = _sim.spawn_unit(Vector3(x, spawn_y, z), 2, role, _team2_sim_squad_id(sq))
			_assign_random_personality(uid)

	var elapsed: int = Time.get_ticks_usec() - t0
	print("[SimTest] Spawned %d units in %d us" % [_sim.get_unit_count(), elapsed])

	# Register capture points (map-relative)
	_active_capture_positions = _compute_capture_positions(map_w, map_h)
	for cp_pos: Vector3 in _active_capture_positions:
		_sim.add_capture_point(cp_pos)

	# Compute squad counts
	_t1_squad_count = num_squads
	_t2_squad_count = num_squads

	# Assign squads to different objectives (with max advance cap)
	_squad_max_advance.clear()

	# Rally points at spawn centers — Colony AI takes over within 1-2s
	var t1_rally = Vector3(t1_spawn_x, spawn_y, 0.0)
	var t2_rally = Vector3(t2_spawn_x, spawn_y, 0.0)

	# Team 1: rally at spawn, no initial advance
	for sq: int in range(_t1_squad_count):
		_sim.set_squad_rally(sq, t1_rally, Vector3(1.0, 0.0, 0.0))
		_squad_max_advance[sq] = 0.0

	# Team 2: rally at spawn, no initial advance
	for sq: int in range(_t2_squad_count):
		var sim_sq: int = _team2_sim_squad_id(sq)
		_sim.set_squad_rally(sim_sq, t2_rally, Vector3(-1.0, 0.0, 0.0))
		_squad_max_advance[sim_sq] = 0.0

	# Set formations from config
	for sq: int in range(_t1_squad_count):
		_sim.set_squad_formation(sq, _sim_team1_formation)
	for sq: int in range(_t2_squad_count):
		_sim.set_squad_formation(_team2_sim_squad_id(sq), _sim_team2_formation)

	# Give orders: follow squad (formation-based movement)
	for i: int in range(units_per_team):
		_sim.set_order(i, SimulationServer.ORDER_FOLLOW_SQUAD, Vector3.ZERO)
	for i: int in range(units_per_team, units_per_team * 2):
		_sim.set_order(i, SimulationServer.ORDER_FOLLOW_SQUAD, Vector3.ZERO)

	# Create MultiMesh rendering
	_create_sim_multimesh()

	# Create capture point markers
	_create_capture_markers()

	# Create HUD label
	_sim_hud_label = Label.new()
	_sim_hud_label.position = Vector2(10, 10)
	_sim_hud_label.add_theme_font_size_override("font_size", 16)
	_sim_hud_label.add_theme_color_override("font_color", Color.WHITE)
	get_parent().add_child(_sim_hud_label)

	# Create Theater Commanders (strategic AI — Tier 1, both teams)
	_theater_commander = TheaterCommander.new()
	_theater_commander.setup(1, map_w, map_h)
	if _influence_map:
		_theater_commander.set_influence_map(_influence_map)
	_theater_commander_t2 = TheaterCommander.new()
	_theater_commander_t2.setup(2, map_w, map_h)
	if _influence_map:
		_theater_commander_t2.set_influence_map(_influence_map)
	print("[SimTest] Theater Commanders initialized (both teams, %.0fx%.0f)" % [map_w, map_h])

	# Create Colony AI C++ (auction/scoring — Tier 2)
	_colony_ai_cpp = ColonyAICPP.new()
	_colony_ai_cpp.setup(1, map_w, map_h, _t1_squad_count)
	if _influence_map:
		_colony_ai_cpp.set_influence_map(_influence_map)
	_colony_ai_cpp.set_push_direction(1.0)
	_colony_ai_cpp.set_base_x(t1_spawn_x)
	for sq_i: int in range(_t1_squad_count):
		_colony_ai_cpp.set_squad_sim_id(sq_i, sq_i)
		_colony_ai_cpp.set_squad_role(sq_i, "assault")

	_colony_ai_cpp_t2 = ColonyAICPP.new()
	_colony_ai_cpp_t2.setup(2, map_w, map_h, _t2_squad_count)
	if _influence_map:
		_colony_ai_cpp_t2.set_influence_map(_influence_map)
	_colony_ai_cpp_t2.set_push_direction(-1.0)
	_colony_ai_cpp_t2.set_base_x(t2_spawn_x)
	for sq_i: int in range(_t2_squad_count):
		_colony_ai_cpp_t2.set_squad_sim_id(sq_i, _team2_sim_squad_id(sq_i))
		_colony_ai_cpp_t2.set_squad_role(sq_i, "assault")

	print("[SimTest] ColonyAICPP initialized (T1: %d squads, T2: %d squads)" % [_t1_squad_count, _t2_squad_count])
	_apply_diag_tune_overrides_for("theater", "SimTest")
	_apply_diag_tune_overrides_for("colony", "SimTest")

	# LLM integration (after both Theater + Colony are ready)
	_setup_llm_advisors(map_w, map_h)

	# Create visual debug systems
	_create_squad_hud()
	_create_centroid_markers()
	_create_minimap()

	_sim_running = true

	# Update BattleCommandUI with sim + AI references
	if _battle_ui:
		_battle_ui.sim = _sim
		_battle_ui.theater_t1 = _theater_commander
		_battle_ui.theater_t2 = _theater_commander_t2
		_battle_ui.colony_t1 = _colony_ai_cpp
		_battle_ui.colony_t2 = _colony_ai_cpp_t2

	print("[SimTest] Simulation started — %d alive | T1: %s | T2: %s | %d capture points" % [
		_sim.get_alive_count(),
		FORMATION_NAMES[_sim_team1_formation],
		FORMATION_NAMES[_sim_team2_formation],
		_active_capture_positions.size()
	])


func _create_sim_multimesh() -> void:
	var mesh = _build_soldier_mesh()

	# Generate VAT pose texture from rest-pose vertices
	_generate_vat_texture()

	# VAT ShaderMaterial — replaces StandardMaterial3D for animated units
	var shader = Shader.new()
	shader.code = _get_vat_shader_code()
	var vat_mat = ShaderMaterial.new()
	vat_mat.shader = shader
	vat_mat.set_shader_parameter("vat_texture", _vat_texture)
	vat_mat.set_shader_parameter("ambient_boost", 0.12)
	mesh.surface_set_material(0, vat_mat)

	# Team 1 (blue) — use_custom_data enables INSTANCE_CUSTOM in shader
	var mm1 = MultiMesh.new()
	mm1.transform_format = MultiMesh.TRANSFORM_3D
	mm1.use_colors = true
	mm1.use_custom_data = true
	mm1.mesh = mesh
	mm1.instance_count = 0

	_mm_team1 = MultiMeshInstance3D.new()
	_mm_team1.multimesh = mm1
	get_parent().add_child(_mm_team1)

	# Team 2 (red) — different mesh (beret + SMG)
	var mesh_t2 = _build_soldier_mesh_team2()
	var vat_mat_t2 = ShaderMaterial.new()
	vat_mat_t2.shader = shader
	vat_mat_t2.set_shader_parameter("vat_texture", _vat_texture)
	vat_mat_t2.set_shader_parameter("ambient_boost", 0.35)
	mesh_t2.surface_set_material(0, vat_mat_t2)

	var mm2 = MultiMesh.new()
	mm2.transform_format = MultiMesh.TRANSFORM_3D
	mm2.use_colors = true
	mm2.use_custom_data = true
	mm2.mesh = mesh_t2
	mm2.instance_count = 0

	_mm_team2 = MultiMeshInstance3D.new()
	_mm_team2.multimesh = mm2
	get_parent().add_child(_mm_team2)

	# Corpses — use StandardMaterial3D (no VAT needed, they lie flat)
	var corpse_mat = StandardMaterial3D.new()
	corpse_mat.vertex_color_use_as_albedo = true
	corpse_mat.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
	corpse_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	corpse_mat.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
	corpse_mat.roughness = 0.7
	corpse_mat.emission_enabled = true
	corpse_mat.emission_energy_multiplier = 0.1
	var corpse_mesh = mesh.duplicate()
	corpse_mesh.surface_set_material(0, corpse_mat)

	var mm_c = MultiMesh.new()
	mm_c.transform_format = MultiMesh.TRANSFORM_3D
	mm_c.use_colors = true
	mm_c.mesh = corpse_mesh
	mm_c.instance_count = 0

	_mm_corpses = MultiMeshInstance3D.new()
	_mm_corpses.multimesh = mm_c
	get_parent().add_child(_mm_corpses)

	# Tracers
	var tracer_mesh = _build_tracer_mesh()
	var mm_t = MultiMesh.new()
	mm_t.transform_format = MultiMesh.TRANSFORM_3D
	mm_t.use_colors = true
	mm_t.mesh = tracer_mesh
	mm_t.instance_count = 0

	_mm_tracers = MultiMeshInstance3D.new()
	_mm_tracers.multimesh = mm_t
	get_parent().add_child(_mm_tracers)

	# Grenade projectiles — larger, brighter mesh
	var grenade_mesh = _build_grenade_mesh()
	var mm_g = MultiMesh.new()
	mm_g.transform_format = MultiMesh.TRANSFORM_3D
	mm_g.use_colors = true
	mm_g.mesh = grenade_mesh
	mm_g.instance_count = 0

	_mm_grenades = MultiMeshInstance3D.new()
	_mm_grenades.multimesh = mm_g
	get_parent().add_child(_mm_grenades)


func _create_capture_markers() -> void:
	_capture_markers.clear()
	_capture_spheres.clear()
	_capture_labels.clear()
	_capture_rings.clear()

	for i: int in range(_active_capture_positions.size()):
		var cp_pos = _active_capture_positions[i]
		# Get terrain height at capture point position
		var terrain_y: float = 5.0
		if _world and _world.is_initialized():
			var vpos: Vector3i = _world.world_to_voxel(cp_pos)
			for vy: int in range(mini(vpos.y + 10, _world.get_world_size_y() - 1), 0, -1):
				if _world.get_voxel(vpos.x, vy, vpos.z) != 0:
					terrain_y = float(vy + 1) * _world.get_voxel_scale()
					break

		var root = Node3D.new()
		root.position = Vector3(cp_pos.x, terrain_y, cp_pos.z)
		get_parent().add_child(root)

		# Pole (tall cylinder)
		var pole = MeshInstance3D.new()
		var pole_mesh = CylinderMesh.new()
		pole_mesh.top_radius = 0.15
		pole_mesh.bottom_radius = 0.15
		pole_mesh.height = 8.0
		pole.mesh = pole_mesh
		pole.position.y = 4.0
		var pole_mat = StandardMaterial3D.new()
		pole_mat.albedo_color = Color(0.5, 0.45, 0.4)
		pole.material_override = pole_mat
		root.add_child(pole)

		# Sphere on top (colored by ownership)
		var sphere = MeshInstance3D.new()
		var sphere_mesh = SphereMesh.new()
		sphere_mesh.radius = 1.0
		sphere_mesh.height = 2.0
		sphere.mesh = sphere_mesh
		sphere.position.y = 9.0
		var sphere_mat = StandardMaterial3D.new()
		sphere_mat.albedo_color = Color.WHITE
		sphere_mat.emission_enabled = true
		sphere_mat.emission = Color.WHITE
		sphere_mat.emission_energy_multiplier = 0.5
		sphere.material_override = sphere_mat
		root.add_child(sphere)

		# Ring on ground (capture zone indicator)
		var ring = MeshInstance3D.new()
		var ring_mesh = TorusMesh.new()
		ring_mesh.inner_radius = 11.5
		ring_mesh.outer_radius = 12.5
		ring.mesh = ring_mesh
		ring.position.y = 0.3
		var ring_mat = StandardMaterial3D.new()
		ring_mat.albedo_color = Color(1.0, 1.0, 1.0, 0.3)
		ring_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		ring.material_override = ring_mat
		root.add_child(ring)

		# Label
		var label = Label3D.new()
		var capture_name: String = CAPTURE_NAMES[i] if i < CAPTURE_NAMES.size() else ("Point %d" % (i + 1))
		label.text = capture_name
		label.font_size = 48
		label.position.y = 11.0
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.modulate = Color.WHITE
		label.outline_size = 8
		root.add_child(label)

		_capture_markers.append(root)
		_capture_spheres.append(sphere)
		_capture_labels.append(label)
		_capture_rings.append(ring)


func _build_soldier_mesh() -> ArrayMesh:
	## Build a low-poly soldier with rifle (14 boxes = 504 verts, single surface).
	## Origin at feet, facing -Z (Godot forward). Y-up.
	## Sets UV2.x = vertex index for VAT shader lookup.
	## Populates _rest_vertices with rest-pose vertex positions.
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	_rest_vertices = PackedVector3Array()
	_vat_vert_idx = 0

	# Helper: add a box at offset with given size
	# Each box = 6 faces = 12 triangles = 36 vertices
	var _add_box = func(center: Vector3, size: Vector3, color: Color) -> void:
		var hx: float = size.x * 0.5
		var hy: float = size.y * 0.5
		var hz: float = size.z * 0.5
		var corners: Array[Vector3] = [
			center + Vector3(-hx, -hy, -hz),  # 0: left bottom back
			center + Vector3( hx, -hy, -hz),  # 1: right bottom back
			center + Vector3( hx,  hy, -hz),  # 2: right top back
			center + Vector3(-hx,  hy, -hz),  # 3: left top back
			center + Vector3(-hx, -hy,  hz),  # 4: left bottom front
			center + Vector3( hx, -hy,  hz),  # 5: right bottom front
			center + Vector3( hx,  hy,  hz),  # 6: right top front
			center + Vector3(-hx,  hy,  hz),  # 7: left top front
		]
		var faces: Array[Array] = [
			[4, 5, 6, 7, Vector3(0, 0, 1)],   # front (+Z)
			[1, 0, 3, 2, Vector3(0, 0, -1)],   # back (-Z)
			[0, 4, 7, 3, Vector3(-1, 0, 0)],   # left (-X)
			[5, 1, 2, 6, Vector3(1, 0, 0)],    # right (+X)
			[7, 6, 2, 3, Vector3(0, 1, 0)],    # top (+Y)
			[0, 1, 5, 4, Vector3(0, -1, 0)],   # bottom (-Y)
		]
		for face: Array in faces:
			var v0: Vector3 = corners[face[0]]
			var v1: Vector3 = corners[face[1]]
			var v2: Vector3 = corners[face[2]]
			var v3: Vector3 = corners[face[3]]
			var n: Vector3 = face[4]
			# 2 triangles, 6 vertices — each gets unique UV2 for VAT
			var tri_verts: Array[Vector3] = [v0, v1, v2, v0, v2, v3]
			for vert: Vector3 in tri_verts:
				st.set_normal(n)
				st.set_color(color)
				st.set_uv2(Vector2(float(_vat_vert_idx), 0.0))
				st.add_vertex(vert)
				_rest_vertices.append(vert)
				_vat_vert_idx += 1

	var skin = Color(0.95, 0.85, 0.75)
	var shirt = Color(0.90, 0.90, 0.90)
	var pants = Color(0.70, 0.70, 0.70)
	var boots = Color(0.25, 0.22, 0.20)
	var gun = Color(0.30, 0.30, 0.32)
	var wood = Color(0.55, 0.40, 0.25)

	# Part order matters — vertex ranges are 36 per box:
	# 0: boot_left, 1: boot_right, 2: leg_left, 3: leg_right,
	# 4: torso, 5: head, 6: helmet,
	# 7: left_arm, 8: right_arm, 9: left_hand, 10: right_hand,
	# 11: rifle_barrel, 12: rifle_stock, 13: magazine
	_add_box.call(Vector3(-0.12, 0.06, 0.0), Vector3(0.14, 0.12, 0.22), boots)
	_add_box.call(Vector3( 0.12, 0.06, 0.0), Vector3(0.14, 0.12, 0.22), boots)
	_add_box.call(Vector3(-0.10, 0.35, 0.0), Vector3(0.16, 0.46, 0.18), pants)
	_add_box.call(Vector3( 0.10, 0.35, 0.0), Vector3(0.16, 0.46, 0.18), pants)
	_add_box.call(Vector3(0.0, 0.82, 0.0), Vector3(0.38, 0.48, 0.22), shirt)
	_add_box.call(Vector3(0.0, 1.18, 0.0), Vector3(0.22, 0.24, 0.22), skin)
	_add_box.call(Vector3(0.0, 1.32, 0.0), Vector3(0.26, 0.08, 0.26), pants)
	_add_box.call(Vector3(-0.26, 0.76, 0.0), Vector3(0.12, 0.40, 0.14), shirt)
	_add_box.call(Vector3( 0.26, 0.80, -0.08), Vector3(0.12, 0.36, 0.18), shirt)
	_add_box.call(Vector3(-0.26, 0.54, 0.0), Vector3(0.10, 0.08, 0.10), skin)
	_add_box.call(Vector3( 0.26, 0.60, -0.16), Vector3(0.10, 0.08, 0.10), skin)
	_add_box.call(Vector3( 0.20, 0.68, -0.38), Vector3(0.04, 0.04, 0.50), gun)
	_add_box.call(Vector3( 0.22, 0.72, -0.04), Vector3(0.06, 0.10, 0.18), wood)
	_add_box.call(Vector3( 0.20, 0.62, -0.22), Vector3(0.04, 0.10, 0.06), gun)

	return st.commit()


func _build_soldier_mesh_team2() -> ArrayMesh:
	## Build a Team 2 soldier variant: beret + SMG (shorter barrel, folding stock).
	## Same vertex count as Team 1 but distinct silhouette.
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)

	var _add_box = func(center: Vector3, size: Vector3, color: Color) -> void:
		var hx: float = size.x * 0.5
		var hy: float = size.y * 0.5
		var hz: float = size.z * 0.5
		var corners: Array[Vector3] = [
			center + Vector3(-hx, -hy, -hz), center + Vector3( hx, -hy, -hz),
			center + Vector3( hx,  hy, -hz), center + Vector3(-hx,  hy, -hz),
			center + Vector3(-hx, -hy,  hz), center + Vector3( hx, -hy,  hz),
			center + Vector3( hx,  hy,  hz), center + Vector3(-hx,  hy,  hz),
		]
		var faces: Array[Array] = [
			[4, 5, 6, 7, Vector3(0, 0, 1)],  [1, 0, 3, 2, Vector3(0, 0, -1)],
			[0, 4, 7, 3, Vector3(-1, 0, 0)],  [5, 1, 2, 6, Vector3(1, 0, 0)],
			[7, 6, 2, 3, Vector3(0, 1, 0)],   [0, 1, 5, 4, Vector3(0, -1, 0)],
		]
		for face: Array in faces:
			var v0: Vector3 = corners[face[0]]
			var v1: Vector3 = corners[face[1]]
			var v2: Vector3 = corners[face[2]]
			var v3: Vector3 = corners[face[3]]
			var n: Vector3 = face[4]
			st.set_normal(n); st.set_color(color); st.add_vertex(v0)
			st.set_normal(n); st.set_color(color); st.add_vertex(v1)
			st.set_normal(n); st.set_color(color); st.add_vertex(v2)
			st.set_normal(n); st.set_color(color); st.add_vertex(v0)
			st.set_normal(n); st.set_color(color); st.add_vertex(v2)
			st.set_normal(n); st.set_color(color); st.add_vertex(v3)

	var skin = Color(0.95, 0.85, 0.75)
	var shirt = Color(0.90, 0.90, 0.90)
	var pants = Color(0.70, 0.70, 0.70)
	var boots = Color(0.25, 0.22, 0.20)
	var gun = Color(0.30, 0.30, 0.32)
	var wood = Color(0.55, 0.40, 0.25)

	# Boots
	_add_box.call(Vector3(-0.12, 0.06, 0.0), Vector3(0.14, 0.12, 0.22), boots)
	_add_box.call(Vector3( 0.12, 0.06, 0.0), Vector3(0.14, 0.12, 0.22), boots)
	# Legs
	_add_box.call(Vector3(-0.10, 0.35, 0.0), Vector3(0.16, 0.46, 0.18), pants)
	_add_box.call(Vector3( 0.10, 0.35, 0.0), Vector3(0.16, 0.46, 0.18), pants)
	# Torso
	_add_box.call(Vector3(0.0, 0.82, 0.0), Vector3(0.38, 0.48, 0.22), shirt)
	# Head
	_add_box.call(Vector3(0.0, 1.18, 0.0), Vector3(0.22, 0.24, 0.22), skin)
	# Beret (tilted, taller than helmet)
	_add_box.call(Vector3(0.03, 1.34, 0.0), Vector3(0.20, 0.14, 0.20), pants)
	# Arms
	_add_box.call(Vector3(-0.26, 0.76, 0.0), Vector3(0.12, 0.40, 0.14), shirt)
	_add_box.call(Vector3( 0.26, 0.80, -0.08), Vector3(0.12, 0.36, 0.18), shirt)
	# Hands
	_add_box.call(Vector3(-0.26, 0.54, 0.0), Vector3(0.10, 0.08, 0.10), skin)
	_add_box.call(Vector3( 0.26, 0.60, -0.16), Vector3(0.10, 0.08, 0.10), skin)
	# SMG barrel (shorter, thicker)
	_add_box.call(Vector3( 0.20, 0.68, -0.28), Vector3(0.05, 0.05, 0.30), gun)
	# Folding stock (small)
	_add_box.call(Vector3( 0.22, 0.72, -0.04), Vector3(0.04, 0.06, 0.10), wood)
	# Box magazine (wider)
	_add_box.call(Vector3( 0.20, 0.62, -0.18), Vector3(0.05, 0.12, 0.06), gun)

	return st.commit()


func _generate_vat_texture() -> void:
	## Generate a 504x10 VAT texture with per-vertex deltas for 10 animation poses.
	## Legs rotate at hips, arms rotate at shoulders, child parts follow.
	const VP: int = 36   # vertices per part
	const NP: int = 14   # number of body parts
	const TV: int = NP * VP  # 504 total vertices
	const NUM_POSES: int = 10

	var img = Image.create(TV, NUM_POSES, false, Image.FORMAT_RGBAF)

	# Rotation pivots
	var hip_l = Vector3(-0.10, 0.58, 0.0)
	var hip_r = Vector3(0.10, 0.58, 0.0)
	var shoulder_l = Vector3(-0.26, 0.96, 0.0)
	var shoulder_r = Vector3(0.26, 0.98, -0.08)

	# Rest-pose endpoints that children follow
	var ankle_l = Vector3(-0.12, 0.12, 0.0)
	var ankle_r = Vector3(0.12, 0.12, 0.0)
	var wrist_l = Vector3(-0.26, 0.56, 0.0)
	var wrist_r = Vector3(0.26, 0.62, -0.16)

	# Pose definitions: [leg_l_deg, leg_r_deg, arm_l_deg, arm_r_deg, crouch_y, lean_z]
	# Positive X rotation = limb swings forward (-Z direction)
	var pose_defs: Array[Array] = [
		[0.0, 0.0, 0.0, 0.0, 0.0, 0.0],               # 0: IDLE
		[25.0, -15.0, -12.0, 8.0, 0.0, -0.02],          # 1: WALK_A (left forward)
		[-15.0, 25.0, 8.0, -12.0, 0.0, -0.02],          # 2: WALK_B (right forward)
		[0.0, 0.0, -20.0, -50.0, 0.0, -0.03],           # 3: AIM
		[20.0, 20.0, -10.0, -10.0, -0.25, -0.04],       # 4: CROUCH
		[0.0, 0.0, -5.0, -25.0, -0.05, -0.02],          # 5: RELOAD
		[15.0, -10.0, -70.0, -65.0, 0.0, 0.0],          # 6: CLIMB
		[35.0, -22.0, -18.0, 14.0, 0.0, -0.05],         # 7: SPRINT_A
		[-22.0, 35.0, 14.0, -18.0, 0.0, -0.05],         # 8: SPRINT_B
		[0.0, 0.0, -65.0, -75.0, -0.72, -0.08],          # 9: PRONE (flat, arms forward)
	]

	for pose_idx: int in NUM_POSES:
		var pd: Array = pose_defs[pose_idx]
		var leg_l_rot = Basis(Vector3.RIGHT, deg_to_rad(pd[0]))
		var leg_r_rot = Basis(Vector3.RIGHT, deg_to_rad(pd[1]))
		var arm_l_rot = Basis(Vector3.RIGHT, deg_to_rad(pd[2]))
		var arm_r_rot = Basis(Vector3.RIGHT, deg_to_rad(pd[3]))
		var crouch_y: float = pd[4]
		var lean_z: float = pd[5]
		var global_off = Vector3(0.0, crouch_y, lean_z)

		# Compute endpoint deltas for hierarchical following
		var ankle_l_delta: Vector3 = (hip_l + leg_l_rot * (ankle_l - hip_l)) - ankle_l
		var ankle_r_delta: Vector3 = (hip_r + leg_r_rot * (ankle_r - hip_r)) - ankle_r
		var wrist_l_delta: Vector3 = (shoulder_l + arm_l_rot * (wrist_l - shoulder_l)) - wrist_l
		var wrist_r_delta: Vector3 = (shoulder_r + arm_r_rot * (wrist_r - shoulder_r)) - wrist_r

		for part: int in NP:
			var start_v: int = part * VP
			for v: int in VP:
				var vi: int = start_v + v
				var rest: Vector3 = _rest_vertices[vi]
				var delta: Vector3

				match part:
					2:  # leg_left — rotates at hip
						delta = (hip_l + leg_l_rot * (rest - hip_l)) - rest + global_off
					3:  # leg_right — rotates at hip
						delta = (hip_r + leg_r_rot * (rest - hip_r)) - rest + global_off
					0:  # boot_left — follows leg ankle
						delta = ankle_l_delta + global_off
					1:  # boot_right — follows leg ankle
						delta = ankle_r_delta + global_off
					7:  # left_arm — rotates at shoulder
						delta = (shoulder_l + arm_l_rot * (rest - shoulder_l)) - rest + global_off
					8:  # right_arm — rotates at shoulder
						delta = (shoulder_r + arm_r_rot * (rest - shoulder_r)) - rest + global_off
					9:  # left_hand — follows left arm wrist
						delta = wrist_l_delta + global_off
					10:  # right_hand — follows right arm wrist
						delta = wrist_r_delta + global_off
					11, 12, 13:  # rifle parts — follow right arm wrist
						delta = wrist_r_delta + global_off
					_:  # torso, head, helmet — global offset only
						delta = global_off

				img.set_pixel(vi, pose_idx, Color(delta.x, delta.y, delta.z, 0.0))

	_vat_texture = ImageTexture.create_from_image(img)


func _get_vat_shader_code() -> String:
	return """
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D vat_texture : filter_nearest, repeat_disable;
uniform float ambient_boost : hint_range(0.0, 2.0) = 0.35;

void vertex() {
	int vid = int(UV2.x + 0.5);
	int state = clamp(int(INSTANCE_CUSTOM.g * 13.0 + 0.5), 0, 13);
	float phase = INSTANCE_CUSTOM.r;

	// State to base pose mapping
	// Poses: 0=IDLE, 1=WALK_A, 2=WALK_B, 3=AIM, 4=CROUCH, 5=RELOAD, 6=CLIMB, 7=SPRINT_A, 8=SPRINT_B
	int pose;
	bool cycles = false;

	if (state == 0 || state == 10 || state >= 11) {
		pose = 0; // IDLE (also FROZEN, DEAD, FALLING)
	} else if (state == 1 || state == 6) {
		pose = 1; cycles = true; // WALK cycle (MOVING, RETREATING)
	} else if (state == 2 || state == 4) {
		pose = 3; // AIM (ENGAGING, SUPPRESSING)
	} else if (state == 3 || state == 8) {
		pose = 4; // CROUCH (IN_COVER, DOWNED)
	} else if (state == 5 || state == 9) {
		pose = 7; cycles = true; // SPRINT cycle (FLANKING, BERSERK)
	} else if (state == 7) {
		pose = 5; // RELOAD
	} else if (state == 12) {
		pose = 6; // CLIMB
	} else {
		pose = 0;
	}

	// Posture override (B channel: 0=stand, 0.5=crouch, 1.0=prone)
	int posture = clamp(int(INSTANCE_CUSTOM.b * 2.0 + 0.5), 0, 2);
	if (posture == 1 && !cycles && pose != 6) {
		pose = 4;  // Crouch pose for stationary non-climbing states
	}
	if (posture == 2) {
		pose = 9;  // Prone pose always overrides
	}

	vec3 delta;
	if (cycles) {
		float t = sin(phase * 6.283) * 0.5 + 0.5;
		vec3 da = texelFetch(vat_texture, ivec2(vid, pose), 0).rgb;
		vec3 db = texelFetch(vat_texture, ivec2(vid, pose + 1), 0).rgb;
		delta = mix(da, db, t);
	} else {
		delta = texelFetch(vat_texture, ivec2(vid, pose), 0).rgb;
	}

	VERTEX += delta;
}

void fragment() {
	ALBEDO = COLOR.rgb;
	ROUGHNESS = 0.7;
	METALLIC = 0.0;
	EMISSION = COLOR.rgb * ambient_boost;
}
"""


func _build_tracer_mesh() -> ArrayMesh:
	## Elongated box (0.03 x 0.03 x 0.8m) for tracer rendering.
	## Oriented along -Z so Basis.looking_at(velocity) works.
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var hw: float = 0.015
	var hl: float = 0.4  # half-length along Z
	var color = Color.WHITE  # tinted per-instance
	# 8 corners of elongated box
	var corners: Array[Vector3] = [
		Vector3(-hw, -hw, -hl), Vector3( hw, -hw, -hl),
		Vector3( hw,  hw, -hl), Vector3(-hw,  hw, -hl),
		Vector3(-hw, -hw,  hl), Vector3( hw, -hw,  hl),
		Vector3( hw,  hw,  hl), Vector3(-hw,  hw,  hl),
	]
	var faces: Array[Array] = [
		[4, 5, 6, 7, Vector3(0, 0, 1)],
		[1, 0, 3, 2, Vector3(0, 0, -1)],
		[0, 4, 7, 3, Vector3(-1, 0, 0)],
		[5, 1, 2, 6, Vector3(1, 0, 0)],
		[7, 6, 2, 3, Vector3(0, 1, 0)],
		[0, 1, 5, 4, Vector3(0, -1, 0)],
	]
	for face: Array in faces:
		var v0: Vector3 = corners[face[0]]
		var v1: Vector3 = corners[face[1]]
		var v2: Vector3 = corners[face[2]]
		var v3: Vector3 = corners[face[3]]
		var n: Vector3 = face[4]
		st.set_normal(n); st.set_color(color); st.add_vertex(v0)
		st.set_normal(n); st.set_color(color); st.add_vertex(v1)
		st.set_normal(n); st.set_color(color); st.add_vertex(v2)
		st.set_normal(n); st.set_color(color); st.add_vertex(v0)
		st.set_normal(n); st.set_color(color); st.add_vertex(v2)
		st.set_normal(n); st.set_color(color); st.add_vertex(v3)

	var mesh = st.commit()
	# Emissive material — high energy for bloom pickup
	var mat = StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.emission_enabled = true
	mat.emission = Color.WHITE
	mat.emission_energy_multiplier = 8.0
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.no_depth_test = true  # tracers always visible (additive look)
	mesh.surface_set_material(0, mat)
	return mesh


func _build_grenade_mesh() -> ArrayMesh:
	## Fat glowing capsule (0.15 x 0.15 x 0.3m) for grenade projectiles.
	## Much larger and brighter than bullet tracers so the player can track them.
	var st = SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var hw: float = 0.075
	var hl: float = 0.15  # half-length along Z
	var color = Color.WHITE
	# 8 corners of fat box
	var corners: Array[Vector3] = [
		Vector3(-hw, -hw, -hl), Vector3( hw, -hw, -hl),
		Vector3( hw,  hw, -hl), Vector3(-hw,  hw, -hl),
		Vector3(-hw, -hw,  hl), Vector3( hw, -hw,  hl),
		Vector3( hw,  hw,  hl), Vector3(-hw,  hw,  hl),
	]
	var faces: Array[Array] = [
		[4, 5, 6, 7, Vector3(0, 0, 1)],
		[1, 0, 3, 2, Vector3(0, 0, -1)],
		[0, 4, 7, 3, Vector3(-1, 0, 0)],
		[5, 1, 2, 6, Vector3(1, 0, 0)],
		[7, 6, 2, 3, Vector3(0, 1, 0)],
		[0, 1, 5, 4, Vector3(0, -1, 0)],
	]
	for face: Array in faces:
		var v0: Vector3 = corners[face[0]]
		var v1: Vector3 = corners[face[1]]
		var v2: Vector3 = corners[face[2]]
		var v3: Vector3 = corners[face[3]]
		var n: Vector3 = face[4]
		st.set_normal(n); st.set_color(color); st.add_vertex(v0)
		st.set_normal(n); st.set_color(color); st.add_vertex(v1)
		st.set_normal(n); st.set_color(color); st.add_vertex(v2)
		st.set_normal(n); st.set_color(color); st.add_vertex(v0)
		st.set_normal(n); st.set_color(color); st.add_vertex(v2)
		st.set_normal(n); st.set_color(color); st.add_vertex(v3)

	var mesh = st.commit()
	var mat = StandardMaterial3D.new()
	mat.vertex_color_use_as_albedo = true
	mat.emission_enabled = true
	mat.emission = Color.WHITE
	mat.emission_energy_multiplier = 8.0  # Very bright glow
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mesh.surface_set_material(0, mat)
	return mesh


func _tick_gpu_map() -> void:
	# Build position arrays from alive units, split by team
	var positions: PackedVector3Array = _sim.get_alive_positions()
	var teams: PackedInt32Array = _sim.get_alive_teams()
	var friendlies := PackedVector3Array()
	var enemies := PackedVector3Array()
	for i in range(positions.size()):
		if teams[i] == 1:
			friendlies.append(positions[i])
		else:
			enemies.append(positions[i])
	var threats := enemies  # Use enemy positions as threat centroids
	var goals := PackedVector3Array()
	var strengths := PackedFloat32Array()
	_gpu_map.tick(friendlies, enemies, threats, goals, strengths)


func _tick_cover_map() -> void:
	if not _cover_map or not _sim:
		return
	# Gather enemy positions as threat sources for cover shadow casting
	var data1: Dictionary = _sim.get_render_data_for_team(1)
	var data2: Dictionary = _sim.get_render_data_for_team(2)
	var pos1: PackedVector3Array = data1["positions"]
	var pos2: PackedVector3Array = data2["positions"]

	var threats = PackedVector3Array()
	# Sample up to 8 threats from each team
	var step2: int = maxi(1, pos2.size() / 8)
	for i: int in range(0, pos2.size(), step2):
		threats.append(pos2[i])
		if threats.size() >= 8:
			break
	var step1: int = maxi(1, pos1.size() / 8)
	for i: int in range(0, pos1.size(), step1):
		threats.append(pos1[i])
		if threats.size() >= 16:
			break

	_cover_map.update_cover(threats)


func _apply_intent_batch(batch: Dictionary, team: int) -> void:
	var intents: Dictionary = batch.get("squad_intents", {})
	var squad_id_offset: int = _team2_squad_base if team == 2 else 0
	for sq_key in intents:
		var sq_idx: int = int(sq_key)
		var intent: Dictionary = intents[sq_key]
		var target: Vector3 = intent.get("target_pos", Vector3.ZERO)
		var action: String = str(intent.get("action", ""))
		var goal_name: String = str(intent.get("goal_name", ""))

		# Map colony squad index to SimServer squad ID.
		var sim_sq_id: int = sq_idx + squad_id_offset

		# Track goal name and count changes (per-team hysteresis)
		var goal_dict: Dictionary = _last_intent_goal_name_t1 if team == 1 else _last_intent_goal_name_t2
		var target_dict: Dictionary = _last_intent_target_t1 if team == 1 else _last_intent_target_t2
		var action_dict: Dictionary = _last_intent_action_t1 if team == 1 else _last_intent_action_t2

		var prev_goal: String = goal_dict.get(sim_sq_id, "")
		if goal_name != prev_goal and prev_goal != "":
			_goal_change_count += 1
		goal_dict[sim_sq_id] = goal_name

		# Hysteresis: skip if target hasn't moved significantly and action unchanged
		var prev_target: Vector3 = target_dict.get(sim_sq_id, Vector3(INF, 0, INF))
		var last_action: String = action_dict.get(sim_sq_id, "")
		var sim_time: float = _sim.get_game_time() if _sim else 0.0
		if team == 1:
			_last_intent_seen_t1[sim_sq_id] = sim_time
		else:
			_last_intent_seen_t2[sim_sq_id] = sim_time
		var target_xz = Vector3(target.x, 0, target.z)
		var last_xz = Vector3(prev_target.x, 0, prev_target.z)
		if action == last_action and target_xz.distance_to(last_xz) < INTENT_HYSTERESIS_DIST:
			continue

		target_dict[sim_sq_id] = target
		action_dict[sim_sq_id] = action

		if _sim.get_squad_alive_count(sim_sq_id) <= 0:
			continue

		# Get current squad centroid
		var centroid: Vector3 = _sim.get_squad_centroid(sim_sq_id)

		# Compute direction from centroid to target
		var to_target := Vector3(target.x - centroid.x, 0, target.z - centroid.z)
		var dist: float = to_target.length()
		if dist < 1.0:
			continue  # already at target
		var direction: Vector3 = to_target / dist

		# Action-specific max advance distance — no longer a tight 10m cap.
		# Squads need room to actually reach objectives.
		var max_adv: float = minf(dist, 40.0)
		match action:
			"hold_cover_arc":
				max_adv = minf(dist, 8.0)  # approach but hold near
			"retreat":
				max_adv = minf(dist, 30.0)  # retreat urgently
			_:
				pass  # assault/advance/flank/suppress use 40m cap

		# Set movement mode based on intent action (matching C++ INTENT_ACTIONS)
		# Advance uses RUSH (6 m/s) — units sprint to contact, auto-drops to COMBAT
		# on visual target acquisition via per-unit auto-switch in C++.
		var move_mode: int = SimulationServer.MMODE_COMBAT  # default
		match action:
			"advance":
				move_mode = SimulationServer.MMODE_RUSH
			"hold_cover_arc":
				move_mode = SimulationServer.MMODE_TACTICAL
			"suppress_lane":
				move_mode = SimulationServer.MMODE_COMBAT
			"flank_slot":
				move_mode = SimulationServer.MMODE_STEALTH
			"retreat":
				move_mode = SimulationServer.MMODE_RUSH

		# Suppress order: squads with TAG_SUPPRESS in provides_tags get extended
		# suppressive fire duration (coordinated with flanking elements).
		# TAG_SUPPRESS = 1 << 1 = 2
		# TODO: Implement set_squad_suppress_order() in SimulationServer C++
		#var provides_tags: int = intent.get("provides_tags", 0)
		#_sim.set_squad_suppress_order(sim_sq_id, (provides_tags & 2) != 0)

		# Rally leads toward target: "carrot on a stick" approach.
		# Rally is projected AHEAD of centroid proportional to remaining distance.
		# Self-dampening: as squads close, lead shrinks automatically.
		#   200m out → rally 50m ahead (capped)
		#   100m out → rally 40m ahead
		#    30m out → rally 12m ahead (decelerating near enemy)
		#     5m out → rally  2m ahead (in combat, minimal)
		var rally_lead := minf(dist * 0.4, 50.0)
		var rally_pos := centroid + direction * rally_lead
		_sim.set_squad_rally(sim_sq_id, rally_pos, direction)

		# Preserve existing advance progress unless direction changes significantly.
		var cur_adv: float = _sim.get_squad_advance_offset(sim_sq_id)
		var old_max: float = _squad_max_advance.get(sim_sq_id, 0.0)
		var reset_offset: bool = true
		if old_max > 0.0 and cur_adv > 2.0:
			if prev_target.x != INF:
				var old_dir := Vector3(prev_target.x - centroid.x, 0, prev_target.z - centroid.z).normalized()
				var dot: float = old_dir.dot(direction)
				if dot > 0.707:  # Within ~45 degrees
					reset_offset = false
		if reset_offset:
			_sim.set_squad_advance_offset(sim_sq_id, 0.0)
		_sim.set_squad_movement_mode(sim_sq_id, move_mode)
		_squad_max_advance[sim_sq_id] = max_adv


func _update_sim_render() -> void:
	if not _sim:
		return

	# Get per-team render data (cached for minimap reuse)
	_cached_render_data1 = _sim.get_render_data_for_team(1)
	_cached_render_data2 = _sim.get_render_data_for_team(2)

	_update_multimesh(_mm_team1, _cached_render_data1, Color(0.55, 0.70, 1.0), _selected_squad_id)
	_update_multimesh(_mm_team2, _cached_render_data2, Color(1.0, 0.55, 0.50))

	# Render corpses + tracers
	_update_corpses()
	_update_tracers()

	# Update capture point markers
	_update_capture_markers()

	# Process impact events for VFX
	_process_impacts()

	# Process muzzle flash events for VFX
	_process_muzzle_flashes()

	# Footstep dust for moving units (Phase 5B — throttled)
	_process_footstep_dust()

	# Update centroid markers + waypoint lines (if HUD visible)
	if _squad_hud_visible:
		_update_centroid_markers()

	# Update HUD
	if _sim_hud_label:
		var stats: Dictionary = _sim.get_debug_stats()
		var dead: int = _sim.get_unit_count() - stats["alive_units"]
		var collapses: int = 0
		if _world:
			collapses = _world.get_pending_collapses()
		var berserk: int = stats.get("berserk_units", 0)
		var frozen: int = stats.get("frozen_units", 0)
		var paranoid_ff: int = stats.get("paranoid_ff_units", 0)
		var wall_pen: int = stats.get("wall_pen_voxels", 0)
		var speed_str: String = "PAUSED" if _sim_paused else "%.2fx" % _sim_time_scale
		var t1_pts: int = _sim.get_capture_count_for_team(1)
		var t2_pts: int = _sim.get_capture_count_for_team(2)
		var tc_str: String = ""
		if _theater_commander:
			var tc_axes: Dictionary = _theater_commander.get_axis_values()
			var tc_debug: Dictionary = _theater_commander.get_debug_info()
			var posture: String = tc_debug.get("current_posture", "none")
			tc_str = "\nTC [%s]: Agg=%.2f Con=%.2f Tmp=%.2f Rsk=%.2f Exp=%.2f Ter=%.2f Med=%.2f Sup=%.2f Int=%.2f (%.1fms)" % [
				posture,
				tc_axes.get("aggression", 0.0), tc_axes.get("concentration", 0.0),
				tc_axes.get("tempo", 0.0), tc_axes.get("risk_tolerance", 0.0),
				tc_axes.get("exploitation", 0.0), tc_axes.get("terrain_control", 0.0),
				tc_axes.get("medical_priority", 0.0), tc_axes.get("suppression_dominance", 0.0),
				tc_axes.get("intel_coverage", 0.0),
				_theater_commander.get_last_tick_ms()
			]
		var cai_str: String = ""
		if _colony_ai_cpp:
			var cai_dbg: Dictionary = _colony_ai_cpp.get_debug_info()
			cai_str = "\nCAI: %d friendly %d enemy | %d POIs owned | plan %.2fms" % [
				cai_dbg.get("friendly_alive", 0), cai_dbg.get("enemy_alive", 0),
				cai_dbg.get("pois_owned", 0), _colony_ai_cpp.get_last_plan_ms()
			]
		# Subsystem profiling breakdown
		var sub_str: String = ""
		var sub_ema: PackedFloat64Array = stats.get("sub_ema", PackedFloat64Array())
		if sub_ema.size() > 0:
			const SUB_NAMES: PackedStringArray = [
				"Spatial", "Centroids", "Attackers", "CoverVal", "Influence",
				"Visibility", "Suppress", "Reload", "Posture", "Decisions", "Peek",
				"Combat", "Projectiles", "Morale", "Movement", "Capture"
			]
			# Sort by EMA descending, show top 5
			var pairs: Array = []
			for si: int in range(mini(sub_ema.size(), SUB_NAMES.size())):
				pairs.append([sub_ema[si], SUB_NAMES[si]])
			pairs.sort_custom(func(a, b): return a[0] > b[0])
			var parts: PackedStringArray = []
			for si: int in range(mini(5, pairs.size())):
				var us_val: float = pairs[si][0]
				var name: String = pairs[si][1]
				if us_val < 1.0:
					continue
				parts.append("%s:%.0fus" % [name, us_val])
			if parts.size() > 0:
				sub_str = "\nSubs: " + " | ".join(parts)
		var vis_t1: int = stats.get("vis_team1", 0)
		var vis_t2: int = stats.get("vis_team2", 0)
		var t1_alive: int = _sim.get_alive_count_for_team(1)
		var t2_alive: int = _sim.get_alive_count_for_team(2)
		var intel_t1: String = "%d%%" % (int(vis_t1 * 100.0 / t2_alive) if t2_alive > 0 else 100)
		var intel_t2: String = "%d%%" % (int(vis_t2 * 100.0 / t1_alive) if t1_alive > 0 else 100)
		var scenario_str: String = ""
		if _scenario_mode and _scenario_config:
			var remaining: float = maxf(0.0, _scenario_config.duration_sec - _scenario_elapsed)
			scenario_str = "\n[SCENARIO: %s | %.0fs / %.0fs | %.0fs left]" % [
				_scenario_config.name, _scenario_elapsed, _scenario_config.duration_sec, remaining]
		var llm_str: String = ""
		if _llm_sector_cmd and _llm_sector_cmd.get_stats().enabled:
			var sc_stats = _llm_sector_cmd.get_stats()
			var mem_str := ""
			if sc_stats.memory_cycles > 0:
				var score_label := "%.0f" % sc_stats.last_outcome_score
				mem_str = " | Memory:%d (last:%s)" % [sc_stats.memory_cycles, score_label]
			llm_str = "\n[LLM-Sector] Req:%d (OK:%d Fail:%d Rej:%d) | Latency:%.0fms | Interval:%.0fs | Orders:%d%s%s" % [
				sc_stats.requests, sc_stats.success, sc_stats.failures,
				sc_stats.rejected_orders, sc_stats.avg_latency_ms,
				sc_stats.interval_sec, sc_stats.active_orders,
				" [PENDING...]" if sc_stats.pending else "",
				mem_str
			]
		elif _llm_advisor and _llm_advisor.get_stats().enabled:
			var llm_stats = _llm_advisor.get_stats()
			llm_str = "\n[LLM-Bias] Req:%d (OK:%d Fail:%d) | Latency:%.0fms | Interval:%.0fs%s" % [
				llm_stats.requests, llm_stats.success, llm_stats.failures,
				llm_stats.avg_latency_ms, llm_stats.interval_sec,
				" [PENDING...]" if llm_stats.pending else ""
			]
			# Dual mode: Show local advisor stats
			if _llm_advisor_local and _llm_advisor_local.get_stats().enabled:
				var local_stats = _llm_advisor_local.get_stats()
				llm_str += " | LOCAL: %d/%d %.0fms" % [
					local_stats.success, local_stats.requests, local_stats.avg_latency_ms
				]
		_sim_hud_label.text = "Sim: %d alive %d dead | tick %.2f ms | proj %d | %s | T1: %d (%s) %dpts Intel:%s | T2: %d (%s) %dpts Intel:%s | col: %d\nBerserk: %d  Frozen: %d  Paranoid FF: %d  Wall Pen: %d%s%s%s%s%s" % [
			stats["alive_units"], dead, stats["tick_ms"], stats["active_projectiles"],
			speed_str,
			t1_alive, FORMATION_NAMES[_sim_team1_formation], t1_pts, intel_t1,
			t2_alive, FORMATION_NAMES[_sim_team2_formation], t2_pts, intel_t2,
			collapses, berserk, frozen, paranoid_ff, wall_pen, tc_str, cai_str, sub_str, scenario_str, llm_str
		]


func _update_capture_markers() -> void:
	if not _sim or _capture_spheres.is_empty():
		return
	var cap_data: Dictionary = _sim.get_capture_data()
	var owners: PackedInt32Array = cap_data["owners"]
	var prog: PackedFloat32Array = cap_data["progress"]
	var capping: PackedInt32Array = cap_data["capturing"]
	var contested: PackedInt32Array = cap_data.get("contested", PackedInt32Array())

	for i: int in range(mini(owners.size(), _capture_spheres.size())):
		var color: Color
		var status: String
		var capture_name: String = CAPTURE_NAMES[i] if i < CAPTURE_NAMES.size() else ("Point %d" % (i + 1))
		var is_contested: bool = contested.size() > i and contested[i] == 1

		if is_contested:
			# Contested: pulsing red/orange
			var pulse: float = abs(sin(_sim.get_game_time() * 3.0))
			color = Color(1.0, 0.3 + pulse * 0.3, 0.1)
			status = "CONTESTED!"
		elif owners[i] == 1:
			color = Color(0.2, 0.9, 0.3)
			status = "T1"
		elif owners[i] == 2:
			color = Color(1.0, 0.5, 0.2)
			status = "T2"
		elif capping[i] != 0:
			color = Color(1.0, 0.85, 0.0)
			status = "Cap %d%%" % int(prog[i] * 100)
		else:
			color = Color(0.9, 0.9, 0.9)
			status = "Neutral"

		var sphere_mat: StandardMaterial3D = _capture_spheres[i].material_override
		sphere_mat.albedo_color = color
		sphere_mat.emission = color

		var ring_mat: StandardMaterial3D = _capture_rings[i].material_override
		# Ring alpha scales with progress when capturing
		var ring_alpha: float = 0.3
		if capping[i] != 0 and not is_contested:
			ring_alpha = 0.3 + prog[i] * 0.5  # 0.3 → 0.8 as progress fills
		ring_mat.albedo_color = Color(color.r, color.g, color.b, ring_alpha)

		_capture_labels[i].text = "%s\n%s" % [capture_name, status]
		_capture_labels[i].modulate = color


func _update_multimesh(mmi: MultiMeshInstance3D, data: Dictionary, base_color: Color, selected_squad: int = -1) -> void:
	var count: int = data["alive_count"]
	var positions: PackedVector3Array = data["positions"]
	var facings: PackedVector3Array = data["facings"]
	var states: PackedInt32Array = data["states"]
	var anim_phases: PackedFloat32Array = data.get("anim_phases", PackedFloat32Array())
	var squad_ids: PackedInt32Array = data.get("squad_ids", PackedInt32Array())
	var has_squads: bool = squad_ids.size() == count
	var vis: PackedByteArray = data.get("visible", PackedByteArray())
	var has_vis: bool = vis.size() == count
	var game_time: float = _sim.get_game_time() if _sim else 0.0
	var vis_times: PackedFloat32Array = data.get("last_seen_times", PackedFloat32Array())
	var has_vis_times: bool = vis_times.size() == count

	var mm: MultiMesh = mmi.multimesh
	if mm.instance_count != count:
		mm.instance_count = count

	for i: int in range(count):
		var t = Transform3D()
		t.origin = positions[i]
		# Face the unit in movement direction
		if facings[i].length_squared() > 0.01:
			var fwd: Vector3 = facings[i].normalized()
			t.basis = Basis.looking_at(fwd, Vector3.UP)
		mm.set_instance_transform(i, t)

		# Color by state
		var color: Color = base_color
		var st: int = states[i]
		if st == SimulationServer.ST_ENGAGING:
			color = color.lerp(Color.YELLOW, 0.5)
		elif st == SimulationServer.ST_IN_COVER:
			color = color.lerp(Color.GREEN, 0.4)
		elif st == SimulationServer.ST_RELOADING:
			color = color.lerp(Color.GRAY, 0.5)
		elif st == SimulationServer.ST_RETREATING:
			color = color.lerp(Color.PURPLE, 0.5)
		elif st == SimulationServer.ST_FLANKING:
			color = color.lerp(Color.ORANGE, 0.5)
		elif st == SimulationServer.ST_SUPPRESSING:
			color = color.lerp(Color.CYAN, 0.4)
		elif st == SimulationServer.ST_BERSERK:
			color = color.lerp(Color.RED, 0.7)
		elif st == SimulationServer.ST_FROZEN:
			color = color.lerp(Color.WHITE, 0.6)
		elif st == SimulationServer.ST_CLIMBING:
			color = color.lerp(Color(0.0, 0.8, 1.0), 0.7)  # sky blue
		elif st == SimulationServer.ST_FALLING:
			color = color.lerp(Color.MAGENTA, 0.7)

		# Fog of war: ghost silhouette for invisible units
		if has_vis and vis[i] == 0:
			if has_vis_times:
				var time_since: float = game_time - vis_times[i]
				if time_since < 4.0:
					# Recently seen — ghost silhouette (dim + desaturated)
					color = color.lerp(Color(0.4, 0.4, 0.5), 0.7)
					color.a = clampf(1.0 - time_since / 4.0, 0.2, 0.6)
				else:
					# Fully lost — very dim ghost
					color = Color(0.3, 0.3, 0.35, 0.15)
			else:
				color = Color(0.3, 0.3, 0.35, 0.15)

		# Selection highlighting
		if selected_squad >= 0 and has_squads:
			if squad_ids[i] == selected_squad:
				color = color.lerp(Color.WHITE, 0.4)  # brighten selected
			else:
				color = color.darkened(0.25)  # dim others
		mm.set_instance_color(i, color)

		# VAT custom_data: R = anim_phase, G = state normalized, B = posture normalized
		var phase: float = anim_phases[i] if i < anim_phases.size() else 0.0
		var state_norm: float = float(st) / 13.0
		var postures: PackedByteArray = data.get("postures", PackedByteArray())
		var posture: int = postures[i] if i < postures.size() else 0
		var posture_norm: float = float(posture) / 2.0  # 0=stand, 0.5=crouch, 1.0=prone
		mm.set_instance_custom_data(i, Color(phase, state_norm, posture_norm, 0.0))


func _process_impacts() -> void:
	var impacts: Array = _sim.get_impact_events()
	for impact: Dictionary in impacts:
		var pos: Vector3 = impact["position"]
		var impact_type: int = impact.get("type", 0)
		var mat_id: int = impact.get("material", 0)

		if impact_type == 1 and _effects:
			# Explosion: full VFX pipeline
			var blast_r: float = impact.get("blast_radius", 1.5)
			_effects.spawn_explosion(pos, blast_r * 5.0)

			var histogram: PackedInt32Array = impact.get("material_histogram", PackedInt32Array())
			var debris: Array = impact.get("debris", [])
			_effects.spawn_voxel_destruction(pos, blast_r, mat_id, histogram, debris)
			_effects.spawn_destruction_fog(pos, blast_r, mat_id)

			# Camera shake (Phase 3C) — distance-attenuated
			if not _headless:
				var cam_dist: float = global_position.distance_to(pos)
				apply_camera_shake(cam_dist, blast_r)

			# Structural integrity check after explosion
			if _structural_integrity and _renderer:
				var islands: Array = _structural_integrity.detect_islands(_world, pos, blast_r + 2.0)
				for island in islands:
					_renderer.spawn_island(island)
				# Support propagation: collapse unsupported structure
				var weakened: Array = _structural_integrity.detect_weakened_voxels(_world, pos, blast_r + 2.0)
				if weakened.size() > 0:
					for v in weakened:
						var vpos: Vector3i = v["position"]
						_world.set_voxel_dirty(vpos.x, vpos.y, vpos.z, 0)
					var cascade_islands: Array = _structural_integrity.detect_islands(_world, pos, blast_r + 4.0)
					for island in cascade_islands:
						_renderer.spawn_island(island)

		elif impact_type == 2 and _effects:
			# Gas grenade: spawn persistent gas cloud VFX
			var gas_payload: int = impact.get("payload", 1)
			var cloud_r: float = impact.get("blast_radius", 5.0)
			_effects.spawn_gas_cloud(pos, cloud_r, gas_payload)

		elif impact_type == 0 and _effects:
			# Bullet impact: material-keyed VFX (Phase 1D)
			_effects.spawn_bullet_impact(pos, mat_id)


## Process muzzle flash events from SimulationServer (Phase 1B).
func _process_muzzle_flashes() -> void:
	if not _sim or not _effects:
		return
	var flashes: Array = _sim.get_muzzle_flash_events()
	for flash: Dictionary in flashes:
		var pos: Vector3 = flash["position"]
		var facing: Vector3 = flash["facing"]
		var role: int = flash.get("role", 0)
		_effects.spawn_muzzle_flash(pos, facing, role)


## Footstep dust VFX for moving units (Phase 5B).
## Throttled — only spawns every FOOTSTEP_SAMPLE_INTERVAL seconds.
## Randomly samples a subset of moving units to avoid pool exhaustion.
var _footstep_timer: float = 0.0
const FOOTSTEP_SPAWN_INTERVAL: float = 0.4
const FOOTSTEP_MAX_PER_FRAME: int = 8  ## Max dust puffs per pass

func _process_footstep_dust() -> void:
	if not _sim or not _effects or not _world:
		return
	_footstep_timer -= get_process_delta_time()
	if _footstep_timer > 0.0:
		return
	_footstep_timer = FOOTSTEP_SPAWN_INTERVAL

	# Gather moving units from both teams' cached render data
	var dust_positions: PackedVector3Array = PackedVector3Array()
	var dust_mats: PackedByteArray = PackedByteArray()
	var dust_sprint: PackedByteArray = PackedByteArray()

	for data: Dictionary in [_cached_render_data1, _cached_render_data2]:
		if data.is_empty():
			continue
		var positions: PackedVector3Array = data.get("positions", PackedVector3Array())
		var states: PackedInt32Array = data.get("states", PackedInt32Array())
		var count: int = mini(positions.size(), states.size())
		for i: int in range(count):
			# ST_MOVING=1, ST_BERSERK=9, ST_CLIMBING=12 (C++ UnitState enum)
			var st: int = states[i]
			if st == 1 or st == 9 or st == 12:
				dust_positions.append(positions[i])
				# Look up ground material below unit feet
				var foot_pos: Vector3 = positions[i]
				var foot_voxel: Vector3i = _world.world_to_voxel(foot_pos)
				var gx: int = foot_voxel.x
				var gy: int = maxi(foot_voxel.y - 1, 0)
				var gz: int = foot_voxel.z
				var ground_mat: int = _world.get_voxel(gx, gy, gz)
				dust_mats.append(ground_mat)
				dust_sprint.append(1 if st == 9 else 0)  # Berserk = sprint

	if dust_positions.is_empty():
		return

	# Random subsample to avoid pool exhaustion with 1000 units
	if dust_positions.size() > FOOTSTEP_MAX_PER_FRAME:
		var sampled_pos: PackedVector3Array = PackedVector3Array()
		var sampled_mat: PackedByteArray = PackedByteArray()
		var sampled_spr: PackedByteArray = PackedByteArray()
		for _j in FOOTSTEP_MAX_PER_FRAME:
			var idx: int = randi() % dust_positions.size()
			sampled_pos.append(dust_positions[idx])
			sampled_mat.append(dust_mats[idx])
			sampled_spr.append(dust_sprint[idx])
		dust_positions = sampled_pos
		dust_mats = sampled_mat
		dust_sprint = sampled_spr

	_effects.spawn_footstep_batch(dust_positions, dust_mats, dust_sprint)


func _renderer_half_world_x() -> float:
	if _renderer:
		return _renderer._half_world_x
	return float(_world.get_world_size_x()) * _world.get_voxel_scale() * 0.5

func _renderer_half_world_z() -> float:
	if _renderer:
		return _renderer._half_world_z
	return float(_world.get_world_size_z()) * _world.get_voxel_scale() * 0.5


func _update_corpses() -> void:
	if not _mm_corpses:
		return

	var dead_data: Dictionary = _sim.get_dead_render_data()
	var count: int = dead_data["count"]
	var positions: PackedVector3Array = dead_data["positions"]
	var facings: PackedVector3Array = dead_data["facings"]
	var teams: PackedByteArray = dead_data["teams"]

	var mm: MultiMesh = _mm_corpses.multimesh
	if mm.instance_count != count:
		mm.instance_count = count

	for i: int in range(count):
		var t = Transform3D()
		# Place corpse at ground level, rotated 90 degrees to lie flat
		t.origin = positions[i]
		# Face direction the unit was looking, then tip over (rotate -90 around local X)
		var fwd = facings[i]
		if fwd.length_squared() > 0.01:
			fwd = fwd.normalized()
			t.basis = Basis.looking_at(fwd, Vector3.UP)
		# Rotate -90 degrees around local X axis to lay flat (face down)
		t.basis = t.basis * Basis(Vector3(1, 0, 0), -PI * 0.5)
		mm.set_instance_transform(i, t)

		# Desaturated dark team color for corpses
		var color: Color
		if teams[i] == 1:
			color = Color(0.3, 0.35, 0.45)  # dark blue-gray
		else:
			color = Color(0.45, 0.3, 0.3)   # dark red-gray
		mm.set_instance_color(i, color)


func _update_tracers() -> void:
	if not _mm_tracers:
		return

	var pdata: Dictionary = _sim.get_projectile_render_data()
	var count: int = pdata["count"]
	var positions: PackedVector3Array = pdata["positions"]
	var velocities: PackedVector3Array = pdata["velocities"]
	var teams: PackedByteArray = pdata["teams"]
	var types: PackedByteArray = pdata["types"]
	var payloads: PackedByteArray = pdata["payloads"]

	# Split projectiles into bullets vs grenades/mortars
	var bullet_count: int = 0
	var grenade_count: int = 0
	for i: int in range(count):
		if types[i] == 1 or types[i] == 3:  # grenade or mortar
			grenade_count += 1
		else:
			bullet_count += 1

	# ── Bullet tracers (small, fast) ──
	var mm_b: MultiMesh = _mm_tracers.multimesh
	if mm_b.instance_count != bullet_count:
		mm_b.instance_count = bullet_count

	var cam_pos: Vector3 = global_position
	var bi: int = 0
	for i: int in range(count):
		if types[i] == 1 or types[i] == 3:
			continue
		var t = Transform3D()
		t.origin = positions[i]
		var vel: Vector3 = velocities[i]
		if vel.length_squared() > 1.0:
			t.basis = Basis.looking_at(vel.normalized(), Vector3.UP)
		mm_b.set_instance_transform(bi, t)
		# Team colors: orange-red vs cyan-blue for tactical readability
		var color: Color
		if teams[i] == 1:
			color = Color(1.0, 0.6, 0.1)  # orange-red
		else:
			color = Color(0.2, 0.7, 1.0)  # cyan-blue
		# Distance-based alpha fade (full 0-200m, fade 200-300m, invisible 300m+)
		var dist: float = cam_pos.distance_to(positions[i])
		if dist > 300.0:
			color.a = 0.0
		elif dist > 200.0:
			color.a = 1.0 - (dist - 200.0) / 100.0
		mm_b.set_instance_color(bi, color)
		bi += 1

	# ── Grenade/mortar projectiles (large, bright) ──
	if _mm_grenades:
		var mm_g: MultiMesh = _mm_grenades.multimesh
		if mm_g.instance_count != grenade_count:
			mm_g.instance_count = grenade_count

		var gi: int = 0
		for i: int in range(count):
			if types[i] != 1 and types[i] != 3:
				continue
			var t = Transform3D()
			t.origin = positions[i]
			var vel: Vector3 = velocities[i]
			if vel.length_squared() > 1.0:
				t.basis = Basis.looking_at(vel.normalized(), Vector3.UP)
			mm_g.set_instance_transform(gi, t)
			# Bright color by payload type
			var color: Color
			match payloads[i]:
				1:  # smoke
					color = Color(0.9, 0.9, 0.9)  # bright white
				2:  # tear gas
					color = Color(1.0, 1.0, 0.2)  # bright yellow
				3:  # toxic
					color = Color(0.2, 1.0, 0.2)  # bright green
				_:  # frag/kinetic
					color = Color(1.0, 0.3, 0.1)  # bright red-orange
			mm_g.set_instance_color(gi, color)
			gi += 1


# ── Goal Color Helper ──────────────────────────────────────────────

func _get_goal_color(sq_id: int) -> Color:
	var goal_dict: Dictionary = _last_intent_goal_name_t1 if sq_id < _team2_squad_base else _last_intent_goal_name_t2
	var goal_name: String = goal_dict.get(sq_id, "")
	return GOAL_COLORS.get(goal_name, GOAL_COLOR_DEFAULT)


# ── Squad Command HUD ──────────────────────────────────────────────

var _hud_style_pool: Dictionary = {}  # {Color: [normal_style, selected_style]}

func _create_squad_hud() -> void:
	# Shared CanvasLayer for all 2D debug UI (HUD + minimap)
	_debug_canvas = CanvasLayer.new()
	_debug_canvas.layer = 99
	get_parent().add_child(_debug_canvas)

	# Panel container on right side of screen
	_squad_hud_panel = PanelContainer.new()
	_squad_hud_panel.visible = false
	_squad_hud_panel.anchor_left = 1.0
	_squad_hud_panel.anchor_right = 1.0
	_squad_hud_panel.anchor_top = 0.0
	_squad_hud_panel.anchor_bottom = 1.0
	_squad_hud_panel.offset_left = -330
	_squad_hud_panel.offset_right = 0
	_squad_hud_panel.offset_top = 40
	_squad_hud_panel.offset_bottom = -10

	# Dark semi-transparent background
	var bg = StyleBoxFlat.new()
	bg.bg_color = Color(0.05, 0.05, 0.08, 0.85)
	bg.corner_radius_top_left = 6
	bg.corner_radius_top_right = 6
	bg.corner_radius_bottom_left = 6
	bg.corner_radius_bottom_right = 6
	bg.content_margin_left = 4
	bg.content_margin_right = 4
	bg.content_margin_top = 4
	bg.content_margin_bottom = 4
	_squad_hud_panel.add_theme_stylebox_override("panel", bg)
	_debug_canvas.add_child(_squad_hud_panel)

	var scroll = ScrollContainer.new()
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_squad_hud_panel.add_child(scroll)

	var vbox = VBoxContainer.new()
	vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(vbox)

	# Pre-build style pool for goal colors
	for goal_name: String in GOAL_COLORS:
		var c: Color = GOAL_COLORS[goal_name]
		var normal = StyleBoxFlat.new()
		normal.bg_color = Color(c.r, c.g, c.b, 0.10)
		normal.corner_radius_top_left = 3
		normal.corner_radius_top_right = 3
		normal.corner_radius_bottom_left = 3
		normal.corner_radius_bottom_right = 3
		var selected = StyleBoxFlat.new()
		selected.bg_color = Color(c.r, c.g, c.b, 0.35)
		selected.corner_radius_top_left = 3
		selected.corner_radius_top_right = 3
		selected.corner_radius_bottom_left = 3
		selected.corner_radius_bottom_right = 3
		_hud_style_pool[goal_name] = [normal, selected]

	# Default style (no goal assigned)
	var def_normal = StyleBoxFlat.new()
	def_normal.bg_color = Color(0.3, 0.3, 0.3, 0.10)
	def_normal.corner_radius_top_left = 3
	def_normal.corner_radius_top_right = 3
	def_normal.corner_radius_bottom_left = 3
	def_normal.corner_radius_bottom_right = 3
	var def_selected = StyleBoxFlat.new()
	def_selected.bg_color = Color(0.3, 0.3, 0.3, 0.35)
	def_selected.corner_radius_top_left = 3
	def_selected.corner_radius_top_right = 3
	def_selected.corner_radius_bottom_left = 3
	def_selected.corner_radius_bottom_right = 3
	_hud_style_pool["_default"] = [def_normal, def_selected]

	# Create button rows (one per Team 1 squad)
	_squad_hud_rows.clear()
	for sq_i: int in range(_t1_squad_count):
		var btn = Button.new()
		btn.flat = true
		btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
		btn.add_theme_font_size_override("font_size", 12)
		btn.add_theme_color_override("font_color", Color(0.85, 0.85, 0.85))
		btn.text = "Sq %02d  --" % sq_i
		btn.pressed.connect(_on_squad_row_pressed.bind(sq_i))
		vbox.add_child(btn)
		_squad_hud_rows.append(btn)


func _update_squad_hud() -> void:
	if not _sim or _squad_hud_rows.is_empty():
		return

	for sq_i: int in range(mini(_t1_squad_count, _squad_hud_rows.size())):
		var btn: Button = _squad_hud_rows[sq_i]
		var alive: int = _sim.get_squad_alive_count(sq_i)
		var goal_name: String = _last_intent_goal_name_t1.get(sq_i, "")
		var centroid: Vector3 = _sim.get_squad_centroid(sq_i)
		var is_selected: bool = (_selected_squad_id == sq_i)

		if alive <= 0:
			btn.text = "Sq %02d  DEAD" % sq_i
			btn.modulate = Color(0.4, 0.4, 0.4, 0.5)
			var styles: Array = _hud_style_pool.get("_default", [])
			if styles.size() == 2:
				btn.add_theme_stylebox_override("normal", styles[0])
			continue

		btn.modulate = Color.WHITE
		var goal_short: String = goal_name.replace("_", " ").substr(0, 14) if goal_name != "" else "---"
		btn.text = "Sq %02d  %2d alive  %-14s  (%.0f, %.0f)" % [sq_i, alive, goal_short, centroid.x, centroid.z]

		# Style based on goal color
		var style_key: String = goal_name if _hud_style_pool.has(goal_name) else "_default"
		var styles: Array = _hud_style_pool[style_key]
		if styles.size() == 2:
			btn.add_theme_stylebox_override("normal", styles[1] if is_selected else styles[0])

		# Text color: white for selected, goal-tinted otherwise
		if is_selected:
			btn.add_theme_color_override("font_color", Color.WHITE)
		else:
			var gc: Color = _get_goal_color(sq_i)
			btn.add_theme_color_override("font_color", gc.lerp(Color(0.85, 0.85, 0.85), 0.5))


func _on_squad_row_pressed(sq_id: int) -> void:
	if _selected_squad_id == sq_id:
		_selected_squad_id = -1  # deselect
	else:
		_selected_squad_id = sq_id
		_fly_to_squad(sq_id)


func _fly_to_squad(sq_id: int) -> void:
	if not _sim:
		return
	var centroid: Vector3 = _sim.get_squad_centroid(sq_id)
	if centroid == Vector3.ZERO:
		return
	global_position = centroid + Vector3(0, 20, 15)
	_pitch = -0.7
	_yaw = 0.0
	_update_rotation()


# ── Centroid Markers + Waypoint Lines ──────────────────────────────

func _create_centroid_markers() -> void:
	# Shared cone mesh for all markers
	var cone_mesh = CylinderMesh.new()
	cone_mesh.top_radius = 0.0
	cone_mesh.bottom_radius = 0.5
	cone_mesh.height = 2.0

	_centroid_markers.clear()
	_centroid_labels.clear()

	for sq_i: int in range(_t1_squad_count):
		var mi = MeshInstance3D.new()
		mi.mesh = cone_mesh
		mi.visible = false

		# Unshaded emissive material (will be recolored per-frame)
		var mat = StandardMaterial3D.new()
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		mat.albedo_color = GOAL_COLOR_DEFAULT
		mat.emission_enabled = true
		mat.emission = GOAL_COLOR_DEFAULT
		mat.emission_energy_multiplier = 1.5
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		mat.albedo_color.a = 0.8
		mi.material_override = mat
		get_parent().add_child(mi)
		_centroid_markers.append(mi)

		# Label3D child
		var label = Label3D.new()
		label.font_size = 24
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.no_depth_test = true
		label.outline_size = 6
		label.position = Vector3(0, 2.5, 0)
		label.modulate = Color.WHITE
		mi.add_child(label)
		_centroid_labels.append(label)

	# Midpoint labels for waypoint objective text (one per squad)
	_waypoint_labels.clear()
	for sq_i: int in range(_t1_squad_count):
		var wlabel = Label3D.new()
		wlabel.font_size = 20
		wlabel.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		wlabel.no_depth_test = true
		wlabel.outline_size = 8
		wlabel.modulate = Color.WHITE
		wlabel.visible = false
		get_parent().add_child(wlabel)
		_waypoint_labels.append(wlabel)

	# Single ImmediateMesh for all waypoint ribbons (thick lines via triangles)
	_waypoint_immediate = ImmediateMesh.new()
	_waypoint_mesh_inst = MeshInstance3D.new()
	_waypoint_mesh_inst.mesh = _waypoint_immediate
	_waypoint_mesh_inst.visible = false
	var line_mat = StandardMaterial3D.new()
	line_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	line_mat.vertex_color_use_as_albedo = true
	line_mat.no_depth_test = true
	line_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	line_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	_waypoint_mesh_inst.material_override = line_mat
	get_parent().add_child(_waypoint_mesh_inst)


func _update_centroid_markers() -> void:
	if not _sim:
		return

	_waypoint_immediate.clear_surfaces()
	_waypoint_immediate.surface_begin(Mesh.PRIMITIVE_TRIANGLES)

	const RIBBON_HALF_W: float = 0.35  # ribbon half-width in meters

	for sq_i: int in range(mini(_t1_squad_count, _centroid_markers.size())):
		var mi: MeshInstance3D = _centroid_markers[sq_i]
		var alive: int = _sim.get_squad_alive_count(sq_i)

		if alive <= 0:
			mi.visible = false
			if sq_i < _waypoint_labels.size():
				_waypoint_labels[sq_i].visible = false
			continue

		mi.visible = true
		var centroid: Vector3 = _sim.get_squad_centroid(sq_i)
		mi.global_position = centroid + Vector3(0, 2.0, 0)

		# Color by goal
		var gc: Color = _get_goal_color(sq_i)
		var mat: StandardMaterial3D = mi.material_override
		mat.albedo_color = Color(gc.r, gc.g, gc.b, 0.8)
		mat.emission = gc

		# Highlight selected
		if _selected_squad_id == sq_i:
			mat.emission_energy_multiplier = 3.0
		else:
			mat.emission_energy_multiplier = 1.5

		# Update centroid label
		var label: Label3D = _centroid_labels[sq_i]
		var goal_name: String = _last_intent_goal_name_t1.get(sq_i, "")
		var goal_short: String = goal_name.replace("_", " ").substr(0, 12) if goal_name != "" else "---"
		label.text = "Sq%d\n%s" % [sq_i, goal_short]
		label.modulate = gc

		# Draw waypoint ribbon from centroid to target
		var target: Vector3 = _last_intent_target_t1.get(sq_i, Vector3.ZERO)
		var wlabel: Label3D = _waypoint_labels[sq_i]
		if target != Vector3.ZERO:
			var line_start: Vector3 = centroid + Vector3(0, 1.5, 0)
			var line_end: Vector3 = Vector3(target.x, centroid.y + 1.5, target.z)

			# Vertical ribbon: offset vertices up/down by half-width
			var up = Vector3(0, RIBBON_HALF_W, 0)
			var c_start = Color(gc.r, gc.g, gc.b, 0.6)
			var c_end = Color(gc.r, gc.g, gc.b, 0.15)
			# Triangle 1: start+up, start-up, end-up
			_waypoint_immediate.surface_set_color(c_start)
			_waypoint_immediate.surface_add_vertex(line_start + up)
			_waypoint_immediate.surface_set_color(c_start)
			_waypoint_immediate.surface_add_vertex(line_start - up)
			_waypoint_immediate.surface_set_color(c_end)
			_waypoint_immediate.surface_add_vertex(line_end - up)
			# Triangle 2: start+up, end-up, end+up
			_waypoint_immediate.surface_set_color(c_start)
			_waypoint_immediate.surface_add_vertex(line_start + up)
			_waypoint_immediate.surface_set_color(c_end)
			_waypoint_immediate.surface_add_vertex(line_end - up)
			_waypoint_immediate.surface_set_color(c_end)
			_waypoint_immediate.surface_add_vertex(line_end + up)

			# Midpoint label with objective text
			var midpoint: Vector3 = (line_start + line_end) * 0.5 + Vector3(0, 1.5, 0)
			wlabel.global_position = midpoint
			wlabel.text = goal_short
			wlabel.modulate = gc
			wlabel.visible = true
		else:
			wlabel.visible = false

	_waypoint_immediate.surface_end()


func _set_centroid_markers_visible(visible: bool) -> void:
	if _waypoint_mesh_inst:
		_waypoint_mesh_inst.visible = visible
	if not visible:
		for mi: MeshInstance3D in _centroid_markers:
			mi.visible = false
		for wl: Label3D in _waypoint_labels:
			wl.visible = false


# ── Minimap ────────────────────────────────────────────────────────

func _create_minimap() -> void:
	if not _debug_canvas:
		return
	# Skip old minimap if BattleCommandUI provides one
	if _battle_ui:
		return

	var minimap_script = load("res://scenes/squad_minimap.gd")
	_minimap = minimap_script.new()
	_minimap.anchor_left = 1.0
	_minimap.anchor_right = 1.0
	_minimap.anchor_top = 1.0
	_minimap.anchor_bottom = 1.0
	_minimap.offset_left = -220
	_minimap.offset_right = -10
	_minimap.offset_top = -170
	_minimap.offset_bottom = -10
	_minimap.navigate_to.connect(_on_minimap_navigate)
	_debug_canvas.add_child(_minimap)


func _update_minimap() -> void:
	if not _minimap or not _sim:
		return

	var t1_pos: PackedVector3Array = _cached_render_data1.get("positions", PackedVector3Array())
	var t2_pos: PackedVector3Array = _cached_render_data2.get("positions", PackedVector3Array())

	# Build squad centroid + color arrays
	var sq_centroids = PackedVector3Array()
	var sq_colors = PackedColorArray()
	for sq_i: int in range(_t1_squad_count):
		if _sim.get_squad_alive_count(sq_i) > 0:
			sq_centroids.append(_sim.get_squad_centroid(sq_i))
			sq_colors.append(_get_goal_color(sq_i))

	# Capture point data
	var cap_pos = PackedVector3Array()
	var cap_colors = PackedColorArray()
	if _sim:
		var cap_data: Dictionary = _sim.get_capture_data()
		var owners: PackedInt32Array = cap_data.get("owners", PackedInt32Array())
		for ci: int in range(mini(_active_capture_positions.size(), owners.size())):
			cap_pos.append(_active_capture_positions[ci])
			if owners[ci] == 1:
				cap_colors.append(Color(0.2, 0.9, 0.3))
			elif owners[ci] == 2:
				cap_colors.append(Color(1.0, 0.5, 0.2))
			else:
				cap_colors.append(Color(0.9, 0.9, 0.9))

	_minimap.set_data(t1_pos, t2_pos, sq_centroids, sq_colors, cap_pos, cap_colors, global_position)
	_minimap.queue_redraw()


func _on_minimap_navigate(world_pos: Vector3) -> void:
	global_position = world_pos + Vector3(0, 30, 0)
	_pitch = -0.8
	_update_rotation()


# ── Lighting Adjustment Panel (F4) ────────────────────────────────

func _toggle_lighting_panel() -> void:
	if not _lighting_panel:
		_create_lighting_panel()
	_lighting_visible = not _lighting_visible
	_lighting_panel.visible = _lighting_visible
	if _lighting_visible and _mouse_captured:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_mouse_captured = false
	print("[Debug] Lighting panel: %s" % ("ON" if _lighting_visible else "OFF"))


func _create_lighting_panel() -> void:
	var canvas = CanvasLayer.new()
	canvas.layer = 100
	get_parent().add_child(canvas)

	_lighting_panel = PanelContainer.new()
	_lighting_panel.position = Vector2(10, 60)
	_lighting_panel.visible = false

	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.08, 0.08, 0.12, 0.92)
	style.corner_radius_top_left = 6
	style.corner_radius_top_right = 6
	style.corner_radius_bottom_left = 6
	style.corner_radius_bottom_right = 6
	style.content_margin_left = 12
	style.content_margin_right = 12
	style.content_margin_top = 8
	style.content_margin_bottom = 8
	_lighting_panel.add_theme_stylebox_override("panel", style)

	var vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 2)
	_lighting_panel.add_child(vbox)

	var title = Label.new()
	title.text = "Lighting (F4 to close)"
	title.add_theme_font_size_override("font_size", 16)
	title.add_theme_color_override("font_color", Color(1.0, 0.9, 0.5))
	vbox.add_child(title)

	# Define sliders: [name, min, max, step, initial_value, getter_setter_key]
	var env: Environment = _get_environment()
	var sun: DirectionalLight3D = get_parent().get_node_or_null("DirectionalLight3D")
	var fill: DirectionalLight3D = get_parent().get_node_or_null("DirectionalLight3D_Fill")

	var slider_defs: Array[Array] = [
		["Sun Energy", 0.0, 4.0, 0.05, sun.light_energy if sun else 0.6],
		["Fill Light", 0.0, 2.0, 0.05, fill.light_energy if fill else 0.35],
		["Shadow Opacity", 0.0, 1.0, 0.05, sun.shadow_opacity if sun else 0.75],
		["Ambient Energy", 0.0, 2.0, 0.05, env.ambient_light_energy if env else 0.45],
		["SSAO Intensity", 0.0, 3.0, 0.05, env.ssao_intensity if env else 1.0],
		["SSIL Intensity", 0.0, 2.0, 0.05, env.ssil_intensity if env else 0.3],
		["RC GI Intensity", 0.0, 2.0, 0.05, _rc_effect.gi_intensity if _rc_effect else 0.25],
		["Glow Intensity", 0.0, 2.0, 0.02, env.glow_intensity if env else 0.7],
		["Glow Bloom", 0.0, 0.5, 0.005, env.glow_bloom if env else 0.195],
		["Tonemap White", 1.0, 16.0, 0.1, env.tonemap_white if env else 4.0],
		["Contrast", 0.5, 2.0, 0.02, env.adjustment_contrast if env else 1.34],
		["Saturation", 0.0, 2.0, 0.02, env.adjustment_saturation if env else 0.92],
		["Fog Density", 0.0, 0.05, 0.001, env.volumetric_fog_density if env else 0.003],
	]

	for sd: Array in slider_defs:
		var sname: String = sd[0]
		var row = HBoxContainer.new()
		row.add_theme_constant_override("separation", 6)
		vbox.add_child(row)

		var lbl = Label.new()
		lbl.text = sname
		lbl.custom_minimum_size.x = 130
		lbl.add_theme_font_size_override("font_size", 13)
		lbl.add_theme_color_override("font_color", Color(0.85, 0.85, 0.85))
		row.add_child(lbl)

		var slider = HSlider.new()
		slider.min_value = sd[1]
		slider.max_value = sd[2]
		slider.step = sd[3]
		slider.value = sd[4]
		slider.custom_minimum_size.x = 180
		slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(slider)

		var val_lbl = Label.new()
		val_lbl.text = "%.3f" % sd[4]
		val_lbl.custom_minimum_size.x = 50
		val_lbl.add_theme_font_size_override("font_size", 13)
		val_lbl.add_theme_color_override("font_color", Color(0.6, 1.0, 0.6))
		row.add_child(val_lbl)

		_lighting_sliders[sname] = slider
		_lighting_labels[sname] = val_lbl
		slider.value_changed.connect(_on_lighting_slider.bind(sname))

	# Print button
	var btn = Button.new()
	btn.text = "Print Current Values"
	btn.add_theme_font_size_override("font_size", 13)
	vbox.add_child(btn)
	btn.pressed.connect(_print_lighting_values)

	canvas.add_child(_lighting_panel)


func _on_lighting_slider(value: float, sname: String) -> void:
	_lighting_labels[sname].text = "%.3f" % value

	var env: Environment = _get_environment()
	var sun: DirectionalLight3D = get_parent().get_node_or_null("DirectionalLight3D")
	var fill: DirectionalLight3D = get_parent().get_node_or_null("DirectionalLight3D_Fill")

	match sname:
		"Sun Energy":
			if sun: sun.light_energy = value
		"Fill Light":
			if fill: fill.light_energy = value
		"Shadow Opacity":
			if sun: sun.shadow_opacity = value
		"Ambient Energy":
			if env: env.ambient_light_energy = value
		"SSAO Intensity":
			if env: env.ssao_intensity = value
		"SSIL Intensity":
			if env: env.ssil_intensity = value
		"RC GI Intensity":
			if _rc_effect: _rc_effect.gi_intensity = value
		"Glow Intensity":
			if env: env.glow_intensity = value
		"Glow Bloom":
			if env: env.glow_bloom = value
		"Tonemap White":
			if env: env.tonemap_white = value
		"Contrast":
			if env: env.adjustment_contrast = value
		"Saturation":
			if env: env.adjustment_saturation = value
		"Fog Density":
			if env: env.volumetric_fog_density = value


func _print_lighting_values() -> void:
	print("\n── Current Lighting Values ──")
	for sname: String in _lighting_sliders:
		var slider: HSlider = _lighting_sliders[sname]
		print("  %s: %.3f" % [sname, slider.value])
	print("────────────────────────────\n")


func _setup_llm_advisors(map_w: float = 600.0, map_h: float = 400.0) -> void:
	"""Initialize LLM systems based on command_mode.

	Modes:
	- 'sector': LLMSectorCommander (squad orders via sector grid)
	- 'bias': LLMTheaterAdvisor (weight modifiers for Theater Commander axes)
	- 'off': No LLM integration

	Environment variables:
	- ANTHROPIC_API_KEY or OPENAI_API_KEY: Cloud providers
	- LLM_PROVIDER=ollama|local: Force local model
	- LLM_MODE=sector|bias|off: Command mode (default: sector)
	- LLM_DUAL=1: Run both cloud and local simultaneously for comparison (bias mode only)
	- OLLAMA_MODEL: Custom Ollama model (default: llama3.1:8b)
	"""
	var primary_config = LLMConfig.from_env()

	if not primary_config.enabled:
		print("[LLM] Disabled (set ANTHROPIC_API_KEY, OPENAI_API_KEY, or LLM_PROVIDER=ollama)")
		return

	if primary_config.command_mode == "off":
		print("[LLM] Disabled (LLM_MODE=off)")
		return

	if primary_config.command_mode == "sector":
		# ── Sector Command Mode ────────────────────────────────────────
		var phero_t1 = _sim.get_pheromone_map(0) if _sim else null
		var phero_t2 = _sim.get_pheromone_map(1) if _sim else null

		_llm_sector_cmd = LLMSectorCommander.new()
		_llm_sector_cmd.setup(
			_theater_commander, _colony_ai_cpp, _sim, _gpu_map, phero_t1,
			primary_config, self, 1, _t1_squad_count, map_w, map_h)
		_llm_sector_cmd.orders_received.connect(_on_sector_orders_team1)
		_llm_sector_cmd.orders_failed.connect(_on_sector_failed_team1)

		_llm_sector_cmd_t2 = LLMSectorCommander.new()
		_llm_sector_cmd_t2.setup(
			_theater_commander_t2, _colony_ai_cpp_t2, _sim, _gpu_map, phero_t2,
			primary_config, self, 2, _t2_squad_count, map_w, map_h)
		_llm_sector_cmd_t2.orders_received.connect(_on_sector_orders_team2)
		_llm_sector_cmd_t2.orders_failed.connect(_on_sector_failed_team2)
		# Stagger team 2 by ~20 seconds to avoid simultaneous API requests
		_llm_sector_cmd_t2._timer = 20.0

		print("[LLM] Sector commanders enabled (provider: %s, model: %s)" % [
			primary_config.provider, primary_config.model])
		return

	# ── Bias Mode (legacy) ─────────────────────────────────────────────
	_llm_advisor = LLMTheaterAdvisor.new()
	_llm_advisor.setup(_theater_commander, primary_config, self)
	_llm_advisor.request_completed.connect(_on_llm_weights_team1)
	_llm_advisor.request_failed.connect(_on_llm_failed_team1)

	_llm_advisor_t2 = LLMTheaterAdvisor.new()
	_llm_advisor_t2.setup(_theater_commander_t2, primary_config, self)
	_llm_advisor_t2.request_completed.connect(_on_llm_weights_team2)
	_llm_advisor_t2.request_failed.connect(_on_llm_failed_team2)

	print("[LLM] Bias advisors enabled (provider: %s, model: %s)" % [primary_config.provider, primary_config.model])

	# Dual mode: Run local model alongside cloud for comparison
	var dual_mode = OS.get_environment("LLM_DUAL") == "1"
	if dual_mode and primary_config.provider not in ["ollama", "local"]:
		var local_config = LLMConfig.ollama_config()
		var ollama_model = OS.get_environment("OLLAMA_MODEL")
		if not ollama_model.is_empty():
			local_config.model = ollama_model

		_llm_advisor_local = LLMTheaterAdvisor.new()
		_llm_advisor_local.setup(_theater_commander, local_config, self)
		_llm_advisor_local.request_completed.connect(_on_llm_weights_local_team1)
		_llm_advisor_local.request_failed.connect(_on_llm_failed_local_team1)

		_llm_advisor_local_t2 = LLMTheaterAdvisor.new()
		_llm_advisor_local_t2.setup(_theater_commander_t2, local_config, self)
		_llm_advisor_local_t2.request_completed.connect(_on_llm_weights_local_team2)
		_llm_advisor_local_t2.request_failed.connect(_on_llm_failed_local_team2)

		print("[LLM] Dual mode: Local advisors enabled (model: %s)" % local_config.model)
		print("[LLM] Running cloud (%s) vs local (%s) comparison" % [primary_config.model, local_config.model])

	if not primary_config.enabled:
		print("[LLM] Disabled (set ANTHROPIC_API_KEY, OPENAI_API_KEY, or LLM_PROVIDER=ollama)")

	# ── LLM Commentator (independent of command mode) ─────────────────────
	var commentator_config = LLMConfig.commentator_config_from_env()
	if commentator_config.enabled and _sim:
		var comment_mode := OS.get_environment("LLM_COMMENTATOR_MODE")
		var event_ticker = _battle_ui.event_ticker if _battle_ui else null

		if comment_mode == "team":
			# Per-team generals — two biased commentators
			_llm_commentator = LLMCommentator.new()
			_llm_commentator.setup(_sim, commentator_config, self, event_ticker, 0)
			_llm_commentator_t2 = LLMCommentator.new()
			_llm_commentator_t2.setup(_sim, commentator_config, self, event_ticker, 1)
			print("[LLM] Team commentators enabled (%s / %s)" % [
				commentator_config.provider, commentator_config.model])
		else:
			# Neutral observer (default)
			_llm_commentator = LLMCommentator.new()
			_llm_commentator.setup(_sim, commentator_config, self, event_ticker)
			print("[LLM] Commentator enabled (%s / %s)" % [
				commentator_config.provider, commentator_config.model])


func _on_llm_weights_team1(weights: Dictionary, reasoning: String) -> void:
	"""Apply LLM weight adjustments to Theater Commander (Team 1)"""
	if _theater_commander:
		_theater_commander.set_weight_modifiers(weights)
		print("[LLM T1] Applied: %s" % weights)
		print("[LLM T1] Reasoning: %s" % reasoning)


func _on_llm_weights_team2(weights: Dictionary, reasoning: String) -> void:
	"""Apply LLM weight adjustments to Theater Commander (Team 2)"""
	if _theater_commander_t2:
		_theater_commander_t2.set_weight_modifiers(weights)
		print("[LLM T2] Applied: %s" % weights)
		print("[LLM T2] Reasoning: %s" % reasoning.substr(0, 100))


func _on_llm_failed_team1(error: String) -> void:
	"""Handle LLM request failure (Team 1)"""
	push_warning("[LLM T1] Failed: %s" % error)


func _on_llm_failed_team2(error: String) -> void:
	"""Handle LLM request failure (Team 2)"""
	push_warning("[LLM T2] Failed: %s" % error)


func _on_llm_weights_local_team1(weights: Dictionary, reasoning: String) -> void:
	"""Apply local LLM weight adjustments (Team 1 - for comparison only, does not override)"""
	print("[LLM-Local T1] Suggested: %s" % weights)
	print("[LLM-Local T1] Reasoning: %s" % reasoning.substr(0, 100))
	# Note: In dual mode, local weights are logged but not applied (primary advisor controls)


func _on_llm_weights_local_team2(weights: Dictionary, reasoning: String) -> void:
	"""Apply local LLM weight adjustments (Team 2 - for comparison only)"""
	print("[LLM-Local T2] Suggested: %s" % weights)
	print("[LLM-Local T2] Reasoning: %s" % reasoning.substr(0, 100))


func _on_llm_failed_local_team1(error: String) -> void:
	"""Handle local LLM request failure (Team 1)"""
	push_warning("[LLM-Local T1] Failed: %s" % error)


func _on_llm_failed_local_team2(error: String) -> void:
	"""Handle local LLM request failure (Team 2)"""
	push_warning("[LLM-Local T2] Failed: %s" % error)


# ── LLM Sector Commander Callbacks ───────────────────────────────────────────

func _on_sector_orders_team1(orders: Array, reasoning: String) -> void:
	# Orders already applied to ColonyAICPP by the commander itself
	pass

func _on_sector_orders_team2(orders: Array, reasoning: String) -> void:
	pass

func _on_sector_failed_team1(error: String) -> void:
	push_warning("[LLMSectorCmd T1] Failed: %s" % error)

func _on_sector_failed_team2(error: String) -> void:
	push_warning("[LLMSectorCmd T2] Failed: %s" % error)


func _get_environment() -> Environment:
	var we: WorldEnvironment = get_parent().get_node_or_null("WorldEnvironment")
	if we:
		return we.environment
	return null
