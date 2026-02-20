extends Node
class_name TimeOfDay
## Animated day/night cycle driving sun, fill light, ambient, sky, and fog.
## Attach to scene root. Set sun_light, fill_light, and world_environment paths.
## Press T to toggle cycle, +/- (numpad) to change speed.

## Time presets (interpolated via bezier-like smoothstep)
## Fields: sun_angle, sun_energy, sun_color, fill_energy, ambient_energy,
##         ambient_color, sky_top, sky_horizon, fog_density, fog_color

const PRESETS: Array[Dictionary] = [
	{  # 0: DAWN (6:00)
		"hour": 6.0,
		"sun_angle": 10.0,
		"sun_energy": 0.8,
		"sun_color": Color(1.0, 0.7, 0.4),
		"fill_energy": 0.15,
		"ambient_energy": 0.15,
		"ambient_color": Color(0.85, 0.65, 0.5),
		"sky_top": Color(0.3, 0.25, 0.55),
		"sky_horizon": Color(0.9, 0.55, 0.35),
		"fog_density": 0.012,
		"fog_color": Color(0.85, 0.7, 0.55),
	},
	{  # 1: MORNING (9:00)
		"hour": 9.0,
		"sun_angle": 30.0,
		"sun_energy": 1.4,
		"sun_color": Color(1.0, 0.9, 0.8),
		"fill_energy": 0.25,
		"ambient_energy": 0.25,
		"ambient_color": Color(0.75, 0.78, 0.82),
		"sky_top": Color(0.25, 0.45, 0.75),
		"sky_horizon": Color(0.65, 0.75, 0.85),
		"fog_density": 0.005,
		"fog_color": Color(0.75, 0.78, 0.82),
	},
	{  # 2: NOON (12:00)
		"hour": 12.0,
		"sun_angle": 70.0,
		"sun_energy": 1.8,
		"sun_color": Color(1.0, 0.95, 0.9),
		"fill_energy": 0.35,
		"ambient_energy": 0.30,
		"ambient_color": Color(0.75, 0.78, 0.82),
		"sky_top": Color(0.2, 0.4, 0.8),
		"sky_horizon": Color(0.6, 0.75, 0.9),
		"fog_density": 0.003,
		"fog_color": Color(0.75, 0.78, 0.82),
	},
	{  # 3: AFTERNOON (15:00)
		"hour": 15.0,
		"sun_angle": 45.0,
		"sun_energy": 1.6,
		"sun_color": Color(1.0, 0.85, 0.7),
		"fill_energy": 0.3,
		"ambient_energy": 0.25,
		"ambient_color": Color(0.78, 0.75, 0.72),
		"sky_top": Color(0.22, 0.42, 0.78),
		"sky_horizon": Color(0.65, 0.72, 0.82),
		"fog_density": 0.004,
		"fog_color": Color(0.78, 0.75, 0.72),
	},
	{  # 4: GOLDEN HOUR (17:30)
		"hour": 17.5,
		"sun_angle": 15.0,
		"sun_energy": 1.2,
		"sun_color": Color(1.0, 0.6, 0.3),
		"fill_energy": 0.2,
		"ambient_energy": 0.20,
		"ambient_color": Color(0.85, 0.65, 0.45),
		"sky_top": Color(0.35, 0.3, 0.55),
		"sky_horizon": Color(0.95, 0.55, 0.3),
		"fog_density": 0.008,
		"fog_color": Color(0.9, 0.65, 0.4),
	},
	{  # 5: DUSK (19:00)
		"hour": 19.0,
		"sun_angle": 5.0,
		"sun_energy": 0.4,
		"sun_color": Color(0.9, 0.4, 0.2),
		"fill_energy": 0.1,
		"ambient_energy": 0.10,
		"ambient_color": Color(0.5, 0.35, 0.45),
		"sky_top": Color(0.15, 0.1, 0.35),
		"sky_horizon": Color(0.6, 0.3, 0.25),
		"fog_density": 0.01,
		"fog_color": Color(0.5, 0.35, 0.4),
	},
	{  # 6: NIGHT (22:00)
		"hour": 22.0,
		"sun_angle": -20.0,
		"sun_energy": 0.0,
		"sun_color": Color(0.3, 0.3, 0.5),
		"fill_energy": 0.05,
		"ambient_energy": 0.05,
		"ambient_color": Color(0.2, 0.22, 0.35),
		"sky_top": Color(0.02, 0.02, 0.08),
		"sky_horizon": Color(0.05, 0.05, 0.12),
		"fog_density": 0.002,
		"fog_color": Color(0.15, 0.15, 0.25),
	},
]

## Current time of day (0-24 hours, wraps)
var current_hour: float = 12.0
## Speed multiplier: 1.0 = real-time (1 hour per hour), 60.0 = 1 min per real sec
var time_speed: float = 0.0  # 0 = paused (manual control)
## Whether cycle is active
var cycling: bool = false

# Cached references (set by camera script)
var sun: DirectionalLight3D
var fill_light: DirectionalLight3D
var environment: Environment
var sky_material: ProceduralSkyMaterial

# Base sun rotation (azimuth stays constant, elevation changes)
var _sun_azimuth: float = 0.0  # radians


func setup(p_sun: DirectionalLight3D, p_fill: DirectionalLight3D, p_env: Environment) -> void:
	sun = p_sun
	fill_light = p_fill
	environment = p_env

	if sun:
		# Extract base azimuth from current sun orientation
		var fwd: Vector3 = -sun.global_transform.basis.z
		_sun_azimuth = atan2(fwd.x, fwd.z)

	# Create procedural sky if not already present
	if environment and not environment.sky:
		var sky = Sky.new()
		sky_material = ProceduralSkyMaterial.new()
		sky_material.sky_top_color = Color(0.2, 0.4, 0.8)
		sky_material.sky_horizon_color = Color(0.6, 0.75, 0.9)
		sky_material.ground_bottom_color = Color(0.1, 0.08, 0.06)
		sky_material.ground_horizon_color = Color(0.4, 0.38, 0.35)
		sky_material.sun_angle_max = 30.0
		sky_material.sun_curve = 0.15
		sky.sky_material = sky_material
		environment.sky = sky
		environment.background_mode = Environment.BG_SKY

	# Apply initial state
	apply_time(current_hour)


func set_time(hour: float) -> void:
	current_hour = fmod(hour, 24.0)
	if current_hour < 0:
		current_hour += 24.0
	apply_time(current_hour)


func tick(delta: float) -> void:
	if not cycling or time_speed <= 0.0:
		return
	# time_speed hours per real-time second (e.g. 60 = 1 minute game = 1 second real)
	current_hour += (time_speed / 3600.0) * delta
	if current_hour >= 24.0:
		current_hour -= 24.0
	apply_time(current_hour)


func apply_time(hour: float) -> void:
	# Find the two surrounding presets to interpolate
	var a_idx: int = -1
	var b_idx: int = -1
	for i in range(PRESETS.size()):
		if hour < PRESETS[i]["hour"]:
			b_idx = i
			a_idx = i - 1 if i > 0 else PRESETS.size() - 1
			break
	if b_idx == -1:
		# Past last preset — wrap to first
		a_idx = PRESETS.size() - 1
		b_idx = 0

	var a: Dictionary = PRESETS[a_idx]
	var b: Dictionary = PRESETS[b_idx]

	# Calculate interpolation factor
	var a_hour: float = a["hour"]
	var b_hour: float = b["hour"]
	if b_hour <= a_hour:
		b_hour += 24.0  # Wrap around midnight
	var span: float = b_hour - a_hour
	var local_hour: float = hour
	if local_hour < a_hour:
		local_hour += 24.0
	var t: float = clampf((local_hour - a_hour) / maxf(span, 0.01), 0.0, 1.0)
	# Smoothstep for natural transitions
	t = t * t * (3.0 - 2.0 * t)

	# Interpolate all parameters
	var sun_angle: float = lerpf(a["sun_angle"], b["sun_angle"], t)
	var sun_energy: float = lerpf(a["sun_energy"], b["sun_energy"], t)
	var sun_color: Color = a["sun_color"].lerp(b["sun_color"], t)
	var fill_energy: float = lerpf(a["fill_energy"], b["fill_energy"], t)
	var ambient_energy: float = lerpf(a["ambient_energy"], b["ambient_energy"], t)
	var ambient_color: Color = a["ambient_color"].lerp(b["ambient_color"], t)
	var sky_top: Color = a["sky_top"].lerp(b["sky_top"], t)
	var sky_horizon: Color = a["sky_horizon"].lerp(b["sky_horizon"], t)
	var fog_density: float = lerpf(a["fog_density"], b["fog_density"], t)
	var fog_color: Color = a["fog_color"].lerp(b["fog_color"], t)

	# Apply to sun
	if sun:
		var elevation_rad: float = deg_to_rad(sun_angle)
		var dir = Vector3(
			cos(elevation_rad) * sin(_sun_azimuth),
			-sin(elevation_rad),
			cos(elevation_rad) * cos(_sun_azimuth)
		).normalized()
		if dir.length_squared() > 0.001:
			sun.look_at(sun.global_position + dir, Vector3.UP)
		sun.light_energy = sun_energy
		sun.light_color = sun_color
		sun.visible = sun_energy > 0.01

	# Apply to fill light
	if fill_light:
		fill_light.light_energy = fill_energy

	# Apply to environment
	if environment:
		environment.ambient_light_energy = ambient_energy
		environment.ambient_light_color = ambient_color
		environment.volumetric_fog_density = fog_density
		environment.volumetric_fog_albedo = fog_color
		# Aerial perspective (Phase 5C): fog light color shifts cool at distance
		# Near camera = warm (fog_color), far away = cooler tint
		var aerial_tint: Color = fog_color.lerp(Color(0.6, 0.65, 0.75), 0.3)
		environment.volumetric_fog_emission = aerial_tint * 0.05

	# Apply to sky
	if sky_material:
		sky_material.sky_top_color = sky_top
		sky_material.sky_horizon_color = sky_horizon
		# Ground follows sky but darker
		sky_material.ground_horizon_color = sky_horizon.darkened(0.3)
		sky_material.ground_bottom_color = sky_top.darkened(0.5)


func get_time_string() -> String:
	var h: int = int(current_hour)
	var m: int = int(fmod(current_hour, 1.0) * 60.0)
	return "%02d:%02d" % [h, m]


func get_period_name() -> String:
	if current_hour < 6.0: return "Night"
	if current_hour < 9.0: return "Dawn"
	if current_hour < 12.0: return "Morning"
	if current_hour < 15.0: return "Noon"
	if current_hour < 17.5: return "Afternoon"
	if current_hour < 19.0: return "Golden Hour"
	if current_hour < 22.0: return "Dusk"
	return "Night"
