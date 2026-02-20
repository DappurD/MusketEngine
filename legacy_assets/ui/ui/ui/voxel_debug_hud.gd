extends CanvasLayer
class_name VoxelDebugHUD
## Screen-space debug HUD for the voxel test scene.
## Shows crosshair, voxel info at aim point, controls, and FPS.

var _camera: Camera3D
var _world: VoxelWorld
var _cover_map  # TacticalCoverMap (RefCounted)
var _influence_map  # InfluenceMapCPP (RefCounted)

var _crosshair_h: ColorRect
var _crosshair_v: ColorRect
var _info_label: Label
var _controls_label: Label
var _perf_label: Label

var _update_timer: float = 0.0
const UPDATE_INTERVAL: float = 0.1

const MATERIAL_NAMES: Array[String] = [
	"Air", "Dirt", "Stone", "Wood", "Steel", "Concrete",
	"Brick", "Glass", "Sand", "Water", "Grass", "Gravel",
	"Sandbag", "Clay", "MetalPlate", "Rust"
]


func setup(cam: Camera3D, world: VoxelWorld, cover_map, influence_map) -> void:
	_camera = cam
	_world = world
	_cover_map = cover_map
	_influence_map = influence_map


func _ready() -> void:
	layer = 100

	# Crosshair — two thin white lines
	_crosshair_h = ColorRect.new()
	_crosshair_h.color = Color(1, 1, 1, 0.7)
	_crosshair_h.size = Vector2(12, 2)
	_crosshair_h.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_crosshair_h)

	_crosshair_v = ColorRect.new()
	_crosshair_v.color = Color(1, 1, 1, 0.7)
	_crosshair_v.size = Vector2(2, 12)
	_crosshair_v.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_crosshair_v)

	# Controls label — top-left
	_controls_label = Label.new()
	_controls_label.position = Vector2(10, 10)
	_controls_label.add_theme_font_size_override("font_size", 13)
	_controls_label.add_theme_color_override("font_color", Color(0.9, 0.9, 0.9, 0.8))
	_controls_label.text = "F:Destroy | Tab:Top-down | F2:Overlay | F3:Cycle layer\nG:Cover detail | LMB:Place threat | C:Clear threats"
	_controls_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_controls_label)

	# Info label — below controls
	_info_label = Label.new()
	_info_label.position = Vector2(10, 55)
	_info_label.add_theme_font_size_override("font_size", 14)
	_info_label.add_theme_color_override("font_color", Color(1, 1, 0.85, 0.9))
	_info_label.text = "Aiming at: ..."
	_info_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_info_label)

	# FPS label — top-right
	_perf_label = Label.new()
	_perf_label.add_theme_font_size_override("font_size", 13)
	_perf_label.add_theme_color_override("font_color", Color(0.7, 1.0, 0.7, 0.8))
	_perf_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_perf_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_perf_label)


func _process(delta: float) -> void:
	# Position crosshair at screen center
	var vp_size: Vector2 = get_viewport().get_visible_rect().size
	_crosshair_h.position = Vector2(vp_size.x / 2 - 6, vp_size.y / 2 - 1)
	_crosshair_v.position = Vector2(vp_size.x / 2 - 1, vp_size.y / 2 - 6)
	_perf_label.position = Vector2(vp_size.x - 160, 10)

	_update_timer += delta
	if _update_timer >= UPDATE_INTERVAL:
		_update_timer = 0.0
		_update_info()
		_perf_label.text = "FPS: %d" % Engine.get_frames_per_second()


func _update_info() -> void:
	if not _camera or not _world or not _world.is_initialized():
		_info_label.text = "World loading..."
		return

	var forward: Vector3 = -_camera.global_transform.basis.z
	var hit: Dictionary = _world.raycast_dict(_camera.global_position, forward, 200.0)
	if hit.is_empty() or not hit.get("hit", false):
		_info_label.text = "Aiming at: sky"
		return

	var vpos: Vector3i = hit["voxel_pos"]
	var wpos: Vector3 = hit["world_pos"]
	var mat_id: int = hit.get("material", 0)
	var mat_name: String = MATERIAL_NAMES[mat_id] if mat_id < MATERIAL_NAMES.size() else "?"
	var dist: float = hit.get("distance", 0.0)

	var cover_val: float = 0.0
	if _cover_map:
		cover_val = _cover_map.get_best_cover_at(wpos)

	var threat: float = 0.0
	var cover_q: float = 0.0
	if _influence_map:
		var sec: Vector2i = _influence_map.world_to_sector(wpos)
		threat = _influence_map.get_threat(sec.x, sec.y)
		cover_q = _influence_map.get_cover_quality(sec.x, sec.y)

	_info_label.text = "Voxel [%d,%d,%d] %s\nCover: %.2f | Threat: %.1f | CQ: %.2f\nDist: %.1f m" % [
		vpos.x, vpos.y, vpos.z, mat_name, cover_val, threat, cover_q, dist
	]
