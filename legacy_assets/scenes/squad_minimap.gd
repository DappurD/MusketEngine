extends Control
class_name SquadMinimap
## 2D minimap showing units, squad centroids, capture points. Click to navigate.

signal navigate_to(world_pos: Vector3)

# World bounds: -150..150 X, -100..100 Z
const WORLD_MIN_X: float = -150.0
const WORLD_MAX_X: float = 150.0
const WORLD_MIN_Z: float = -100.0
const WORLD_MAX_Z: float = 100.0

# Data arrays (set externally each frame)
var _team1_positions: PackedVector3Array
var _team2_positions: PackedVector3Array
var _squad_centroids: PackedVector3Array
var _squad_colors: PackedColorArray
var _capture_positions: PackedVector3Array
var _capture_colors: PackedColorArray
var _camera_pos: Vector3


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	custom_minimum_size = Vector2(210, 160)


func set_data(t1: PackedVector3Array, t2: PackedVector3Array,
		sq_c: PackedVector3Array, sq_col: PackedColorArray,
		cap_p: PackedVector3Array, cap_col: PackedColorArray,
		cam: Vector3) -> void:
	_team1_positions = t1
	_team2_positions = t2
	_squad_centroids = sq_c
	_squad_colors = sq_col
	_capture_positions = cap_p
	_capture_colors = cap_col
	_camera_pos = cam


func _world_to_minimap(world: Vector3) -> Vector2:
	var nx: float = (world.x - WORLD_MIN_X) / (WORLD_MAX_X - WORLD_MIN_X)
	var nz: float = (world.z - WORLD_MIN_Z) / (WORLD_MAX_Z - WORLD_MIN_Z)
	return Vector2(nx * size.x, nz * size.y)


func _minimap_to_world(local: Vector2) -> Vector3:
	var nx: float = local.x / size.x
	var nz: float = local.y / size.y
	var wx: float = WORLD_MIN_X + nx * (WORLD_MAX_X - WORLD_MIN_X)
	var wz: float = WORLD_MIN_Z + nz * (WORLD_MAX_Z - WORLD_MIN_Z)
	return Vector3(wx, 0, wz)


func _draw() -> void:
	# Background
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.05, 0.05, 0.1, 0.8))
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.3, 0.3, 0.4, 0.5), false, 1.0)

	# Team 1 unit dots (blue)
	if _team1_positions:
		var step: int = maxi(1, _team1_positions.size() / 200)
		for i: int in range(0, _team1_positions.size(), step):
			var p: Vector2 = _world_to_minimap(_team1_positions[i])
			draw_rect(Rect2(p - Vector2(1, 1), Vector2(2, 2)), Color(0.4, 0.6, 1.0, 0.7))

	# Team 2 unit dots (orange)
	if _team2_positions:
		var step: int = maxi(1, _team2_positions.size() / 200)
		for i: int in range(0, _team2_positions.size(), step):
			var p: Vector2 = _world_to_minimap(_team2_positions[i])
			draw_rect(Rect2(p - Vector2(1, 1), Vector2(2, 2)), Color(1.0, 0.5, 0.3, 0.7))

	# Squad centroid circles (goal-colored)
	if _squad_centroids and _squad_colors:
		for i: int in range(mini(_squad_centroids.size(), _squad_colors.size())):
			var p: Vector2 = _world_to_minimap(_squad_centroids[i])
			draw_circle(p, 3.0, _squad_colors[i])

	# Capture point squares
	if _capture_positions and _capture_colors:
		for i: int in range(mini(_capture_positions.size(), _capture_colors.size())):
			var p: Vector2 = _world_to_minimap(_capture_positions[i])
			draw_rect(Rect2(p - Vector2(3, 3), Vector2(6, 6)), _capture_colors[i])
			draw_rect(Rect2(p - Vector2(3, 3), Vector2(6, 6)), Color.WHITE, false, 1.0)

	# Camera indicator (white cross)
	var cam_p: Vector2 = _world_to_minimap(_camera_pos)
	draw_line(cam_p + Vector2(-4, 0), cam_p + Vector2(4, 0), Color.WHITE, 1.5)
	draw_line(cam_p + Vector2(0, -4), cam_p + Vector2(0, 4), Color.WHITE, 1.5)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		var world_pos: Vector3 = _minimap_to_world(event.position)
		navigate_to.emit(world_pos)
		accept_event()
