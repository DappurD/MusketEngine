extends Node
## Global state for passing scenario selection between scenes.

var scenario_config: ScenarioLoader.ScenarioConfig = null
var seed_override: int = 0
var is_scenario_mode: bool = false
var free_play_terrain_type: String = "battlefield"

# Base building scenario support
var bb_scenario_path: String = ""
var is_bb_scenario: bool = false

# Game settings (configured from main menu, read by camera script)
var starting_hour: float = 12.0         ## Time of day (0-24)
var day_night_cycle: bool = false        ## Auto-cycle time
var day_night_speed: float = 120.0       ## Cycle speed (game hours per real second)
var rain_enabled: bool = false           ## Start with rain
var units_per_team: int = 500            ## Units per side
var team1_formation: int = 0             ## 0=LINE, 1=WEDGE, 2=COLUMN, 3=CIRCLE
var team2_formation: int = 1             ## 0=LINE, 1=WEDGE, 2=COLUMN, 3=CIRCLE
var sim_time_scale: float = 1.0          ## Simulation speed multiplier
var squad_size: int = 10                 ## Units per squad
var spawn_x_fraction: float = 0.4       ## How far from center each team spawns (0.5 = map edge)
var capture_x_fraction: float = 0.133   ## Capture point X spread as fraction of half_w (±40m on 600m)
var capture_z_fraction: float = 0.275   ## Capture point Z spread as fraction of half_h (±55m on 400m)

# World configuration (free play only — scenarios define their own)
var world_voxel_scale: float = 0.25      ## Voxel size in meters
var world_size_preset: String = "large"  ## "micro", "small", "medium", "large", "custom"
var world_size_x: int = 2400             ## World X dimension (voxels)
var world_size_y: int = 128              ## World Y dimension (voxels)
var world_size_z: int = 1600             ## World Z dimension (voxels)
var renderer_lod1_distance: float = 30.0 ## LOD 1 transition distance (meters)
var renderer_lod2_distance: float = 80.0 ## LOD 2 transition distance (meters)
var renderer_lod3_distance: float = 150.0 ## LOD 3 terrain mesh distance (meters)
var renderer_visibility_radius: float = 400.0 ## Visibility culling radius (meters)
var renderer_mesh_threads: int = 8       ## ChunkMeshWorker thread count
var renderer_use_instance_pool: bool = true   ## Fixed instance pool (vs Dictionary)
var renderer_use_superchunk_lod2: bool = true ## LOD 2 super-chunk grouping
var renderer_use_lod3_terrain: bool = true    ## LOD 3 heightmap terrain mesh


func _ready() -> void:
	# Headless CLI: if --scenario is in user args, pre-load config and redirect
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var scenario_id = ""
	for i: int in range(args.size()):
		if args[i] == "--scenario" and i + 1 < args.size():
			scenario_id = args[i + 1]
			break
	if scenario_id.is_empty():
		return

	# Pre-load scenario config so VoxelWorldRenderer can read it
	var path = "res://test/scenarios/%s.json" % scenario_id
	var config = ScenarioLoader.load_scenario(path)
	if config:
		scenario_config = config
		is_scenario_mode = true
		# Parse optional seed/duration overrides
		for j: int in range(args.size()):
			if args[j] == "--seed" and j + 1 < args.size():
				seed_override = int(args[j + 1])

	# Defer scene change so all autoloads finish _ready first
	call_deferred("_redirect_to_test_scene")


func _redirect_to_test_scene() -> void:
	get_tree().change_scene_to_file("res://scenes/voxel_test.tscn")
