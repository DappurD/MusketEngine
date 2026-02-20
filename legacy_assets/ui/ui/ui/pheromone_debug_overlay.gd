extends Node3D

## Pheromone Debug Visualization Overlay
##
## Renders PheromoneMapCPP channels as colored heat maps in 3D space.
## Toggle channels with number keys (1-9, 0, F1-F5), adjust opacity with +/-.

var pheromone_map: PheromoneMapCPP = null
@export var enabled: bool = true
@export var opacity: float = 0.6  ## Heat map opacity (0.0-1.0)
@export var height_offset: float = 0.5  ## How high above ground to render (meters)
@export var active_channel: int = 0  ## Currently displayed channel

# Multi-material mesh for heat map rendering
var _mesh_instance: MeshInstance3D = null
var _material: StandardMaterial3D = null
var _mesh: PlaneMesh = null
var _texture: ImageTexture = null
var _image: Image = null
var _update_timer: float = 0.0
var _display_max: float = 1.0  # Smoothly-tracked normalization max
const UPDATE_INTERVAL := 0.1  # Update visualization 10x/sec

# Channel color profiles (channel_id → color gradient)
# Combat channels 0-7, Economy channels 8-14
const CHANNEL_COLORS := {
	0: Color(1.0, 0.0, 0.0),    # Red — DANGER
	1: Color(1.0, 0.5, 0.0),    # Orange — SUPPRESSION
	2: Color(1.0, 1.0, 0.0),    # Yellow — CONTACT
	3: Color(0.0, 1.0, 0.0),    # Green — RALLY
	4: Color(0.0, 0.5, 1.0),    # Blue — FEAR
	5: Color(0.5, 0.0, 1.0),    # Purple — COURAGE
	6: Color(1.0, 0.0, 1.0),    # Magenta — SAFE_ROUTE
	7: Color(0.0, 1.0, 1.0),    # Cyan — FLANK_OPP
	8: Color(0.7, 0.7, 0.7),    # Silver — METAL
	9: Color(0.4, 0.8, 1.0),    # Light blue — CRYSTAL
	10: Color(1.0, 1.0, 0.3),   # Bright yellow — ENERGY
	11: Color(0.8, 0.3, 0.3),   # Dark red — CONGESTION
	12: Color(1.0, 0.6, 0.0),   # Amber — BUILD_URGENCY
	13: Color(0.3, 0.8, 0.3),   # Soft green — EXPLORED
	14: Color(0.5, 0.5, 0.5),   # Grey — SPARE
}

const CHANNEL_NAMES := {
	0: "Danger", 1: "Suppression", 2: "Contact", 3: "Rally",
	4: "Fear", 5: "Courage", 6: "Safe Route", 7: "Flank Opp",
	8: "Metal", 9: "Crystal", 10: "Energy", 11: "Congestion",
	12: "Build Urgency", 13: "Explored", 14: "Spare",
}

func _ready():
	if not pheromone_map:
		# Will lazy-init in _process when pheromone_map is assigned
		return

	_create_visualization_mesh()
	var ch_name = CHANNEL_NAMES.get(active_channel, "?")
	print("PheromoneDebugOverlay: Ready (Ch %d: %s, Opacity %.2f)" % [active_channel, ch_name, opacity])
	print("  Controls: 1-9=ch0-8, 0=ch9, F1-F5=ch10-14, +/-=opacity, V=toggle")

func _create_visualization_mesh():
	# Create mesh instance
	_mesh_instance = MeshInstance3D.new()
	add_child(_mesh_instance)

	# Get grid dimensions
	var width = pheromone_map.get_width()
	var height = pheromone_map.get_height()
	var cell_size = pheromone_map.get_cell_size()
	var world_origin = pheromone_map.get_world_origin()

	# Create plane mesh matching grid dimensions
	_mesh = PlaneMesh.new()
	_mesh.size = Vector2(width * cell_size, height * cell_size)
	_mesh.subdivide_width = width - 1
	_mesh.subdivide_depth = height - 1
	_mesh.orientation = PlaneMesh.FACE_Y  # Horizontal plane

	# Position mesh (centered on grid, elevated by height_offset)
	var center_offset = Vector3(
		width * cell_size * 0.5,
		height_offset,
		height * cell_size * 0.5
	)
	_mesh_instance.global_position = world_origin + center_offset

	# Create image texture for heat map (R8 format, per-pixel value)
	_image = Image.create(width, height, false, Image.FORMAT_RGBA8)
	_texture = ImageTexture.create_from_image(_image)

	# Create material with texture
	_material = StandardMaterial3D.new()
	_material.albedo_texture = _texture
	_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED  # Emissive heat map
	_material.cull_mode = BaseMaterial3D.CULL_DISABLED  # Visible from both sides
	_material.albedo_color = Color(1, 1, 1, opacity)

	_mesh_instance.mesh = _mesh
	_mesh_instance.material_override = _material

func _process(delta):
	if not enabled or not pheromone_map:
		return

	# Lazy initialization if pheromone_map was assigned after _ready()
	if not _mesh_instance:
		_create_visualization_mesh()
		var ch_name = CHANNEL_NAMES.get(active_channel, "?")
		print("PheromoneDebugOverlay: Late init (Ch %d: %s, Opacity %.2f)" % [active_channel, ch_name, opacity])
		print("  Controls: 1-9=ch0-8, 0=ch9, F1-F5=ch10-14, +/-=opacity, V=toggle")

	# Throttle updates to avoid GDScript per-pixel bottleneck
	_update_timer += delta
	if _update_timer < UPDATE_INTERVAL:
		return
	_update_timer = 0.0

	_update_heat_map()

func _update_heat_map():
	# Get channel data
	var channel_data = pheromone_map.get_channel_data(active_channel)
	if channel_data.size() == 0:
		return

	# Smooth-tracked max: rises instantly on new deposit, decays slowly
	# This prevents old spots from vanishing when you click a new one
	var current_max = pheromone_map.get_max_value(active_channel)
	if current_max > _display_max:
		_display_max = current_max  # Instant rise
	else:
		_display_max = lerp(_display_max, current_max, 0.1)  # Slow decay
	_display_max = max(_display_max, 0.1)  # Never zero

	# Get grid dimensions
	var width = pheromone_map.get_width()
	var height = pheromone_map.get_height()

	# Get channel color
	var base_color = CHANNEL_COLORS.get(active_channel, Color.WHITE)

	# Update image pixels
	for gy in range(height):
		for gx in range(width):
			var idx = gy * width + gx
			var value = channel_data[idx]
			var normalized = clamp(value / _display_max, 0.0, 1.0)

			# Gamma boost (pow 0.4) makes diffused low values clearly visible
			var intensity = pow(normalized, 0.4)
			var pixel_color = base_color * intensity
			pixel_color.a = intensity * opacity

			_image.set_pixel(gx, gy, pixel_color)

	# Upload to GPU
	_texture.update(_image)

func _input(event):
	if not event is InputEventKey or not event.pressed or not pheromone_map:
		return

	# Channel selection: 1-9 → ch 0-8, 0 → ch 9, F1-F5 → ch 10-14
	var new_channel = -1
	if event.keycode >= KEY_1 and event.keycode <= KEY_9:
		new_channel = event.keycode - KEY_1
	elif event.keycode == KEY_0:
		new_channel = 9
	elif event.keycode >= KEY_F1 and event.keycode <= KEY_F5:
		new_channel = 10 + (event.keycode - KEY_F1)

	if new_channel >= 0 and new_channel < pheromone_map.get_channel_count():
		active_channel = new_channel
		_display_max = 1.0  # Reset normalization on channel switch
		var ch_name = CHANNEL_NAMES.get(new_channel, "?")
		print("PheromoneDebugOverlay: Channel %d (%s)" % [active_channel, ch_name])

	# Opacity adjustment (+/-)
	elif event.keycode == KEY_EQUAL or event.keycode == KEY_PLUS:
		opacity = clamp(opacity + 0.1, 0.0, 1.0)
		_material.albedo_color.a = opacity
		print("PheromoneDebugOverlay: Opacity = %.2f" % opacity)

	elif event.keycode == KEY_MINUS:
		opacity = clamp(opacity - 0.1, 0.0, 1.0)
		_material.albedo_color.a = opacity
		print("PheromoneDebugOverlay: Opacity = %.2f" % opacity)

	# Toggle visibility (V)
	elif event.keycode == KEY_V:
		enabled = not enabled
		_mesh_instance.visible = enabled
		print("PheromoneDebugOverlay: %s" % ("Enabled" if enabled else "Disabled"))

# --- Public API for external control ---

func set_active_channel(channel: int):
	if channel >= 0 and channel < pheromone_map.get_channel_count():
		active_channel = channel

func set_opacity(value: float):
	opacity = clamp(value, 0.0, 1.0)
	if _material:
		_material.albedo_color.a = opacity

func toggle_visibility():
	enabled = not enabled
	if _mesh_instance:
		_mesh_instance.visible = enabled
