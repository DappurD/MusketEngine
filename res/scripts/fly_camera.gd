extends Camera3D
## Free-look fly camera — WASD + mouse.
## Attach to a Camera3D node in the scene.

@export var move_speed: float = 10.0
@export var fast_multiplier: float = 3.0
@export var sensitivity: float = 0.002

var _yaw: float = 0.0
var _pitch: float = 0.0


func _ready() -> void:
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_yaw -= event.relative.x * sensitivity
		_pitch -= event.relative.y * sensitivity
		_pitch = clampf(_pitch, -PI * 0.49, PI * 0.49)
		rotation = Vector3(_pitch, _yaw, 0.0)

	if event is InputEventKey:
		if event.keycode == KEY_ESCAPE and event.pressed:
			Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
		if event.keycode == KEY_F1 and event.pressed:
			Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)


func _process(delta: float) -> void:
	var speed := move_speed
	if Input.is_key_pressed(KEY_SHIFT):
		speed *= fast_multiplier

	var dir := Vector3.ZERO
	if Input.is_key_pressed(KEY_W):
		dir -= transform.basis.z
	if Input.is_key_pressed(KEY_S):
		dir += transform.basis.z
	if Input.is_key_pressed(KEY_A):
		dir -= transform.basis.x
	if Input.is_key_pressed(KEY_D):
		dir += transform.basis.x
	if Input.is_key_pressed(KEY_E):
		dir += Vector3.UP
	if Input.is_key_pressed(KEY_Q):
		dir -= Vector3.UP

	if dir.length_squared() > 0.001:
		dir = dir.normalized()
	global_position += dir * speed * delta
