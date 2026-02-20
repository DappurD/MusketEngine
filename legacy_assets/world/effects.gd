extends Node3D
class_name Effects
## Pooled visual effects using GPUParticles3D, Decal3D, and Labels3D.
## Track 3: Muzzle flashes, material-keyed impacts, tracers, decals.

const _DAMAGE_NUMBER_LIFETIME: float = 1.0
const _HIT_LIGHT_LIFETIME: float = 0.10
const _BLOOD_LIGHT_LIFETIME: float = 0.08
var _ambient_dust: GPUParticles3D = null

# ── Muzzle Flash Pool (Phase 1B) ───────────────────────────────────────
const MUZZLE_FLASH_POOL_SIZE: int = 32
const MUZZLE_FLASH_LIFETIME: float = 0.05  # 3 frames at 60fps

var _muzzle_particles: Array[GPUParticles3D] = []
var _muzzle_lights: Array[OmniLight3D] = []
var _muzzle_active: Array[bool] = []
var _muzzle_timers: Array[float] = []
var _muzzle_initial_energies: Array[float] = []
var _muzzle_next: int = 0
var _muzzle_initialized: bool = false

# ── Bullet Impact Pool (Phase 1D — material-keyed) ─────────────────────
const IMPACT_POOL_SIZE: int = 48
const IMPACT_LIFETIME: float = 0.8

var _impact_particles: Array[GPUParticles3D] = []
var _impact_active: Array[bool] = []
var _impact_timers: Array[float] = []
var _impact_next: int = 0
var _impact_initialized: bool = false

# ── Material color table (mirrors MATERIAL_TABLE in C++) ──────────────
const MATERIAL_COLORS: Array[Color] = [
	Color(0, 0, 0),                     # 0  AIR
	Color(0.47, 0.33, 0.22),            # 1  DIRT
	Color(0.50, 0.50, 0.50),            # 2  STONE
	Color(0.63, 0.47, 0.27),            # 3  WOOD
	Color(0.71, 0.71, 0.75),            # 4  STEEL
	Color(0.78, 0.78, 0.76),            # 5  CONCRETE
	Color(0.71, 0.31, 0.24),            # 6  BRICK
	Color(0.78, 0.86, 0.94),            # 7  GLASS
	Color(0.82, 0.75, 0.55),            # 8  SAND
	Color(0.16, 0.31, 0.78),            # 9  WATER
	Color(0.31, 0.59, 0.20),            # 10 GRASS
	Color(0.63, 0.61, 0.57),            # 11 GRAVEL
	Color(0.63, 0.57, 0.43),            # 12 SANDBAG
	Color(0.69, 0.51, 0.35),            # 13 CLAY
	Color(0.39, 0.41, 0.43),            # 14 METAL_PLATE
	Color(0.59, 0.31, 0.20),            # 15 RUST
]

# ── Hero chunk pool (RID-based, no Node overhead) ─────────────────────
const CHUNK_POOL_SIZE: int = 48
const CHUNK_LIFETIME: float = 6.0
const CHUNK_FREEZE_TIME: float = 3.0

var _chunk_body_rids: Array[RID] = []
var _chunk_mesh_rids: Array[RID] = []
var _chunk_instance_rids: Array[RID] = []
var _chunk_materials: Array = []  ## Keep StandardMaterial3D refs alive (prevent GC → null RID)
var _chunk_active: Array[bool] = []
var _chunk_timers: Array[float] = []
var _chunk_mat_ids: Array[int] = []       ## Material ID per slot (for re-solidification)
var _chunk_rest_timer: Array[float] = []  ## Seconds at rest (velocity < threshold)
var _chunk_initialized: bool = false
var _voxel_world: VoxelWorld = null        ## Reference for hero chunk re-solidification

# ── Destruction fog pool (volumetric dust/smoke at explosion sites) ────
const DESTRUCTION_FOG_POOL_SIZE: int = 12
const DESTRUCTION_FOG_LIFETIME: float = 8.0

var _fog_volumes: Array[FogVolume] = []
var _fog_materials: Array[FogMaterial] = []
var _fog_active: Array[bool] = []
var _fog_timers: Array[float] = []
var _fog_next: int = 0
var _fog_initialized: bool = false

# ── Decal Pool (Phase 3A — bullet holes, scorch marks, blood) ──────────
const DECAL_POOL_SIZE: int = 256
const DECAL_BULLET_LIFETIME: float = 45.0
const DECAL_SCORCH_LIFETIME: float = 60.0
const DECAL_BLOOD_LIFETIME: float = 40.0

var _decals: Array[Decal] = []
var _decal_active: Array[bool] = []
var _decal_timers: Array[float] = []
var _decal_fade_start: Array[float] = []   ## Lifetime at which fade-out begins
var _decal_base_alpha: Array[float] = []   ## Original alpha at spawn (for correct fade)
var _decal_next: int = 0
var _decal_initialized: bool = false

# Pre-baked procedural textures for each decal type (created once in init)
var _decal_tex_bullet: GradientTexture2D = null   ## Small sharp dot
var _decal_tex_scorch: GradientTexture2D = null    ## Radial char mark
var _decal_tex_blood: GradientTexture2D = null     ## Dark red pool


func set_voxel_world(world: VoxelWorld) -> void:
	_voxel_world = world


func _make_particle_quad(size: float, color: Color, radial_fade: bool = true) -> QuadMesh:
	var quad = QuadMesh.new()
	quad.size = Vector2(size, size)
	var mat = StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.vertex_color_use_as_albedo = true
	mat.albedo_color = color
	mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if radial_fade:
		var grad_tex = GradientTexture2D.new()
		var grad = Gradient.new()
		grad.set_color(0, Color(1, 1, 1, 1))
		grad.set_color(1, Color(1, 1, 1, 0))
		grad_tex.gradient = grad
		grad_tex.fill = GradientTexture2D.FILL_RADIAL
		grad_tex.fill_from = Vector2(0.5, 0.5)
		grad_tex.fill_to = Vector2(1.0, 0.5)
		mat.albedo_texture = grad_tex
	quad.material = mat
	return quad


func _spawn_flash_light(pos: Vector3, color: Color, energy: float, radius: float, lifetime: float) -> void:
	var light = OmniLight3D.new()
	light.light_color = color
	light.light_energy = energy
	light.omni_range = radius
	light.shadow_enabled = false
	add_child(light)
	light.global_position = pos
	var tween = create_tween()
	tween.tween_property(light, "light_energy", 0.0, lifetime).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_OUT)
	tween.tween_callback(light.queue_free)


func _spawn_impact_ring(pos: Vector3, color: Color, radius: float = 0.65, lifetime: float = 0.22) -> void:
	var ring = MeshInstance3D.new()
	var torus = TorusMesh.new()
	torus.inner_radius = radius * 0.8
	torus.outer_radius = radius
	torus.rings = 24
	torus.ring_segments = 10
	ring.mesh = torus
	ring.rotation_degrees.x = 90.0
	var ring_mat = StandardMaterial3D.new()
	ring_mat.albedo_color = Color(color.r, color.g, color.b, 0.8)
	ring_mat.emission_enabled = true
	ring_mat.emission = color
	ring_mat.emission_energy_multiplier = 2.0
	ring_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	ring_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ring.material_override = ring_mat
	add_child(ring)
	ring.global_position = pos + Vector3(0, 0.03, 0)
	var tween = create_tween()
	tween.tween_property(ring, "scale", Vector3(1.5, 1.0, 1.5), lifetime)
	tween.parallel().tween_property(ring_mat, "albedo_color:a", 0.0, lifetime)
	tween.tween_callback(ring.queue_free)


## Spawns a burst of orange spark particles at a bullet impact point.
func spawn_hit_spark(pos: Vector3) -> void:
	if not is_inside_tree():
		return
	# Core metallic spark burst
	var spark = GPUParticles3D.new()
	var spark_mat = ParticleProcessMaterial.new()
	spark_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
	spark_mat.direction = Vector3.UP
	spark_mat.spread = 85.0
	spark_mat.initial_velocity_min = 4.5
	spark_mat.initial_velocity_max = 9.0
	spark_mat.gravity = Vector3(0, -15, 0)
	spark_mat.damping_min = 0.5
	spark_mat.damping_max = 1.0
	spark_mat.color = Color(1.0, 0.84, 0.45, 1.0)
	spark_mat.scale_min = 0.05
	spark_mat.scale_max = 0.12
	spark.process_material = spark_mat
	spark.amount = 20
	spark.lifetime = 0.26
	spark.one_shot = true
	spark.explosiveness = 1.0
	spark.emitting = true
	spark.draw_pass_1 = _make_particle_quad(0.12, Color(1.0, 0.85, 0.4, 1.0))
	add_child(spark)
	spark.global_position = pos

	# Secondary dust puff so impacts read at distance.
	var dust = GPUParticles3D.new()
	var dust_mat = ParticleProcessMaterial.new()
	dust_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
	dust_mat.direction = Vector3.UP
	dust_mat.spread = 150.0
	dust_mat.initial_velocity_min = 0.8
	dust_mat.initial_velocity_max = 2.2
	dust_mat.gravity = Vector3(0, -3, 0)
	dust_mat.damping_min = 1.2
	dust_mat.damping_max = 2.0
	dust_mat.color = Color(0.6, 0.55, 0.5, 0.65)
	dust_mat.scale_min = 0.18
	dust_mat.scale_max = 0.34
	dust.process_material = dust_mat
	dust.amount = 10
	dust.lifetime = 0.45
	dust.one_shot = true
	dust.explosiveness = 1.0
	dust.emitting = true
	dust.draw_pass_1 = _make_particle_quad(0.35, Color(0.7, 0.68, 0.62, 0.65))
	add_child(dust)
	dust.global_position = pos

	_spawn_flash_light(pos + Vector3(0, 0.08, 0), Color(1.0, 0.78, 0.35), 2.6, 3.0, _HIT_LIGHT_LIFETIME)
	_spawn_impact_ring(pos, Color(1.0, 0.72, 0.32), 0.5, 0.18)

	get_tree().create_timer(1.0).timeout.connect(func():
		if is_instance_valid(spark):
			spark.queue_free()
		if is_instance_valid(dust):
			dust.queue_free()
	)


## Spawns a small burst of blood particles at a hit position.
func spawn_blood(pos: Vector3) -> void:
	if not is_inside_tree():
		return
	var p = GPUParticles3D.new()
	var mat = ParticleProcessMaterial.new()
	mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
	mat.direction = Vector3.UP
	mat.spread = 70.0
	mat.initial_velocity_min = 1.8
	mat.initial_velocity_max = 4.2
	mat.gravity = Vector3(0, -18, 0)
	mat.damping_min = 0.8
	mat.damping_max = 1.4
	mat.color = Color(0.62, 0.03, 0.03, 0.9)
	mat.scale_min = 0.08
	mat.scale_max = 0.22
	p.process_material = mat
	p.amount = 12
	p.lifetime = 0.6
	p.one_shot = true
	p.explosiveness = 1.0
	p.emitting = true
	p.draw_pass_1 = _make_particle_quad(0.14, Color(0.62, 0.03, 0.03, 0.95))
	add_child(p)
	p.global_position = pos
	_spawn_flash_light(pos + Vector3(0, 0.05, 0), Color(0.72, 0.1, 0.08), 1.1, 2.0, _BLOOD_LIGHT_LIFETIME)
	get_tree().create_timer(1.5).timeout.connect(p.queue_free)


## Spawn a directional blood spray effect at the given position.
func spawn_blood_spray(pos: Vector3, direction: Vector3, count: int) -> void:
	if not is_inside_tree():
		return
	var spray = GPUParticles3D.new()
	var spray_mat = ParticleProcessMaterial.new()
	spray_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
	var dir = direction
	dir.y = maxf(dir.y, 0.15)
	if dir.length_squared() < 0.001:
		dir = Vector3.FORWARD
	spray_mat.direction = dir.normalized()
	spray_mat.spread = 22.0
	spray_mat.initial_velocity_min = 4.0
	spray_mat.initial_velocity_max = 7.0
	spray_mat.gravity = Vector3(0, -16, 0)
	spray_mat.damping_min = 0.5
	spray_mat.damping_max = 1.0
	spray_mat.color = Color(0.7, 0.05, 0.05, 0.92)
	spray_mat.scale_min = 0.08
	spray_mat.scale_max = 0.16
	spray.process_material = spray_mat
	spray.amount = maxi(count * 6, 10)
	spray.lifetime = 0.45
	spray.one_shot = true
	spray.explosiveness = 1.0
	spray.emitting = true
	spray.draw_pass_1 = _make_particle_quad(0.13, Color(0.7, 0.05, 0.05, 0.95))
	add_child(spray)
	spray.global_position = pos
	get_tree().create_timer(1.2).timeout.connect(spray.queue_free)


## Spawn a blood pool decal on the ground. Uses pooled decals.
func spawn_blood_pool(pos: Vector3) -> void:
	spawn_decal_blood(pos)


## Spawns a death marker at the given position (currently handled by blood pool).
func spawn_death_marker(pos: Vector3) -> void:
	_spawn_impact_ring(pos + Vector3(0, 0.01, 0), Color(0.95, 0.15, 0.12), 0.85, 0.35)


## Spawns a floating "!" billboard label above a downed unit for 3 seconds.
func spawn_downed_indicator(pos: Vector3) -> void:
	if not is_inside_tree():
		return
	var label = Label3D.new()
	label.text = "!"
	label.font_size = 32
	label.modulate = Color(1.0, 0.2, 0.1)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	add_child(label)
	label.global_position = pos + Vector3(0, 2.5, 0)
	var tween = create_tween()
	tween.set_loops(3)
	tween.tween_property(label, "scale", Vector3.ONE * 1.25, 0.18).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_OUT)
	tween.tween_property(label, "scale", Vector3.ONE, 0.18).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN)
	get_tree().create_timer(3.0).timeout.connect(label.queue_free)


## Spawns a floating damage number that rises and fades out over 1 second.
func spawn_damage_number(pos: Vector3, amount: float) -> void:
	if not is_inside_tree():
		return
	var label = Label3D.new()
	label.text = str(int(amount))
	var heavy_hit: bool = amount >= 18.0
	label.font_size = 28 if heavy_hit else 24
	label.modulate = Color(1.0, 0.72, 0.22) if heavy_hit else Color(1.0, 0.3, 0.2)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	add_child(label)
	label.global_position = pos + Vector3(0, 2.0, 0)
	# Float up and fade
	var drift_x = randf_range(-0.25, 0.25)
	var drift_z = randf_range(-0.25, 0.25)
	var tween = create_tween()
	tween.tween_property(label, "global_position:y", pos.y + 3.6, _DAMAGE_NUMBER_LIFETIME)
	tween.parallel().tween_property(label, "global_position:x", pos.x + drift_x, _DAMAGE_NUMBER_LIFETIME)
	tween.parallel().tween_property(label, "global_position:z", pos.z + drift_z, _DAMAGE_NUMBER_LIFETIME)
	tween.parallel().tween_property(label, "modulate:a", 0.0, _DAMAGE_NUMBER_LIFETIME)
	tween.tween_callback(label.queue_free)


## Spawns a short-lived explosion effect with fireball, debris, flash, and scorch decal.
func spawn_explosion(pos: Vector3, radius: float = 8.0) -> void:
	if not is_inside_tree():
		return
	var fireball = GPUParticles3D.new()
	var fire_mat = ParticleProcessMaterial.new()
	fire_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	fire_mat.emission_sphere_radius = maxf(radius * 0.12, 0.6)
	fire_mat.direction = Vector3.UP
	fire_mat.spread = 180.0
	fire_mat.initial_velocity_min = maxf(radius * 1.6, 5.0)
	fire_mat.initial_velocity_max = maxf(radius * 2.6, 9.0)
	fire_mat.gravity = Vector3(0, -6.0, 0)
	fire_mat.damping_min = 0.8
	fire_mat.damping_max = 1.6
	fire_mat.scale_min = 0.35
	fire_mat.scale_max = 0.95
	fire_mat.color = Color(1.0, 0.45, 0.12, 0.9)
	fireball.process_material = fire_mat
	fireball.amount = int(clampf(radius * 6.0, 24.0, 80.0))
	fireball.lifetime = 0.42
	fireball.one_shot = true
	fireball.explosiveness = 1.0
	fireball.emitting = true
	fireball.draw_pass_1 = _make_particle_quad(maxf(radius * 0.38, 1.2), Color(1.0, 0.55, 0.2, 0.95))
	add_child(fireball)
	fireball.global_position = pos + Vector3(0, 0.2, 0)

	var debris = GPUParticles3D.new()
	var debris_mat = ParticleProcessMaterial.new()
	debris_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
	debris_mat.direction = Vector3.UP
	debris_mat.spread = 155.0
	debris_mat.initial_velocity_min = maxf(radius * 1.8, 5.5)
	debris_mat.initial_velocity_max = maxf(radius * 3.0, 11.0)
	debris_mat.gravity = Vector3(0, -22.0, 0)
	debris_mat.damping_min = 0.4
	debris_mat.damping_max = 1.0
	debris_mat.scale_min = 0.08
	debris_mat.scale_max = 0.2
	debris_mat.color = Color(0.35, 0.28, 0.22, 0.95)
	debris.process_material = debris_mat
	debris.amount = int(clampf(radius * 5.5, 20.0, 72.0))
	debris.lifetime = 0.85
	debris.one_shot = true
	debris.explosiveness = 1.0
	debris.emitting = true
	debris.draw_pass_1 = _make_particle_quad(0.18, Color(0.45, 0.36, 0.28, 0.95), false)
	add_child(debris)
	debris.global_position = pos + Vector3(0, 0.12, 0)

	# Low dust ring so large impacts read clearly from tactical zoom distances.
	var dust = GPUParticles3D.new()
	var dust_mat = ParticleProcessMaterial.new()
	dust_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	dust_mat.emission_sphere_radius = maxf(radius * 0.25, 1.0)
	dust_mat.direction = Vector3.UP
	dust_mat.spread = 170.0
	dust_mat.initial_velocity_min = maxf(radius * 0.8, 2.5)
	dust_mat.initial_velocity_max = maxf(radius * 1.5, 5.0)
	dust_mat.gravity = Vector3(0, -5.0, 0)
	dust_mat.damping_min = 0.9
	dust_mat.damping_max = 1.8
	dust_mat.scale_min = 0.5
	dust_mat.scale_max = 1.4
	dust_mat.color = Color(0.44, 0.40, 0.36, 0.72)
	dust.process_material = dust_mat
	dust.amount = int(clampf(radius * 8.0, 28.0, 110.0))
	dust.lifetime = 1.4
	dust.one_shot = true
	dust.explosiveness = 1.0
	dust.emitting = true
	dust.draw_pass_1 = _make_particle_quad(maxf(radius * 0.45, 1.1), Color(0.5, 0.46, 0.42, 0.72))
	add_child(dust)
	dust.global_position = pos + Vector3(0, 0.08, 0)

	# Flash intensity scales with blast radius (Phase 3C)
	# grenade ~1.5 → energy 6, mortar ~5 → energy 12, artillery ~10 → energy 20
	var flash_energy: float = clampf(radius * 1.5 + 4.0, 6.0, 25.0)
	var flash_range: float = clampf(radius * 1.2 + 3.0, 5.0, 18.0)
	_spawn_flash_light(pos + Vector3(0, 0.25, 0), Color(1.0, 0.58, 0.22), flash_energy, flash_range, 0.24)
	_spawn_flash_light(pos + Vector3(0, 0.18, 0), Color(1.0, 0.42, 0.14), flash_energy * 0.35, flash_range * 0.7, 0.48)
	_spawn_impact_ring(pos + Vector3(0, 0.02, 0), Color(1.0, 0.45, 0.15), maxf(radius * 0.20, 1.0), 0.28)
	_spawn_impact_ring(pos + Vector3(0, 0.03, 0), Color(0.95, 0.66, 0.24), maxf(radius * 0.32, 1.5), 0.44)
	_spawn_scorch_decal(pos, radius)

	# Rising smoke column (Phase 3C — lingers after fireball fades)
	var smoke = GPUParticles3D.new()
	var smoke_mat = ParticleProcessMaterial.new()
	smoke_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	smoke_mat.emission_sphere_radius = maxf(radius * 0.15, 0.5)
	smoke_mat.direction = Vector3.UP
	smoke_mat.spread = 20.0
	smoke_mat.initial_velocity_min = 1.2
	smoke_mat.initial_velocity_max = 2.8
	smoke_mat.gravity = Vector3(0, 0.3, 0)  # Slight buoyancy (rises)
	smoke_mat.damping_min = 0.3
	smoke_mat.damping_max = 0.8
	smoke_mat.scale_min = maxf(radius * 0.3, 0.8)
	smoke_mat.scale_max = maxf(radius * 0.6, 1.5)
	smoke_mat.color = Color(0.3, 0.28, 0.26, 0.35)
	smoke.process_material = smoke_mat
	smoke.amount = int(clampf(radius * 3.0, 8.0, 30.0))
	smoke.lifetime = 3.0
	smoke.one_shot = true
	smoke.explosiveness = 0.3  # Staggered emission for rising column
	smoke.emitting = true
	smoke.draw_pass_1 = _make_particle_quad(maxf(radius * 0.5, 1.2), Color(0.35, 0.32, 0.30, 0.35))
	add_child(smoke)
	smoke.global_position = pos + Vector3(0, 0.3, 0)

	get_tree().create_timer(1.4).timeout.connect(func() -> void:
		if is_instance_valid(fireball):
			fireball.queue_free()
		if is_instance_valid(debris):
			debris.queue_free()
		if is_instance_valid(dust):
			dust.queue_free()
	)
	get_tree().create_timer(4.0).timeout.connect(func() -> void:
		if is_instance_valid(smoke):
			smoke.queue_free()
	)


## Heavy mortar-specific impact: taller dust column + wider shock ring + deeper scorch.
func spawn_mortar_impact(pos: Vector3, radius: float = 8.0) -> void:
	if not is_inside_tree():
		return
	var tuned_radius: float = maxf(radius, 5.0)
	spawn_explosion(pos, tuned_radius * 1.12)

	var column = GPUParticles3D.new()
	var column_mat = ParticleProcessMaterial.new()
	column_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	column_mat.emission_sphere_radius = maxf(tuned_radius * 0.22, 1.0)
	column_mat.direction = Vector3.UP
	column_mat.spread = 55.0
	column_mat.initial_velocity_min = maxf(tuned_radius * 1.2, 6.0)
	column_mat.initial_velocity_max = maxf(tuned_radius * 2.0, 11.0)
	column_mat.gravity = Vector3(0, -3.0, 0)
	column_mat.damping_min = 0.6
	column_mat.damping_max = 1.3
	column_mat.scale_min = 0.7
	column_mat.scale_max = 1.9
	column_mat.color = Color(0.36, 0.33, 0.30, 0.78)
	column.process_material = column_mat
	column.amount = int(clampf(tuned_radius * 11.0, 40.0, 150.0))
	column.lifetime = 1.9
	column.one_shot = true
	column.explosiveness = 1.0
	column.emitting = true
	column.draw_pass_1 = _make_particle_quad(maxf(tuned_radius * 0.62, 1.9), Color(0.42, 0.39, 0.35, 0.8))
	add_child(column)
	column.global_position = pos + Vector3(0, 0.16, 0)

	_spawn_impact_ring(pos + Vector3(0, 0.035, 0), Color(1.0, 0.78, 0.3), maxf(tuned_radius * 0.44, 2.6), 0.52)
	_spawn_scorch_decal(pos + Vector3(randf_range(-0.12, 0.12), 0, randf_range(-0.12, 0.12)), tuned_radius * 1.35)
	_spawn_flash_light(pos + Vector3(0, 0.3, 0), Color(1.0, 0.52, 0.2), 6.0 + tuned_radius * 0.5, 8.0 + tuned_radius * 0.8, 0.42)

	get_tree().create_timer(2.3).timeout.connect(func() -> void:
		if is_instance_valid(column):
			column.queue_free()
	)


func _spawn_scorch_decal(pos: Vector3, radius: float) -> void:
	spawn_decal_scorch(pos, radius)


## Spawns a tactical smoke screen that blocks vision (visually and physically) for ~15 seconds.
func spawn_smoke_screen(pos: Vector3) -> void:
	if not is_inside_tree():
		return
	
	# Root node for the smoke effect
	var smoke_root = Node3D.new()
	smoke_root.position = pos
	smoke_root.name = "SmokeScreen"
	add_child(smoke_root)

	# 1. Volumetric Visuals (FogVolume) - White opaque cloud
	var fog = FogVolume.new()
	fog.shape = RenderingServer.FOG_VOLUME_SHAPE_ELLIPSOID
	# Size is 3D extents (diameter-ish). Radius 3.0m -> Size 6.0
	fog.size = Vector3(6.0, 6.0, 6.0)
	
	var fog_mat = FogMaterial.new()
	fog_mat.density = 0.0 # Start invisible, fade in
	fog_mat.albedo = Color(1.0, 1.0, 1.0)
	fog_mat.emission = Color(0.5, 0.5, 0.5) 
	fog_mat.edge_fade = 0.25 # Soft cloud edges
	fog.material = fog_mat
	smoke_root.add_child(fog)

	# 2. Gameplay Blocking (StaticBody3D)
	# Collision Layer 9 (Bit 8, Value 256) is "Vision Blocker"
	var blocker = StaticBody3D.new()
	blocker.collision_layer = 256 
	blocker.collision_mask = 0    # Detects nothing, only detected by rays
	
	var col = CollisionShape3D.new()
	var shape = SphereShape3D.new()
	shape.radius = 2.8 # Matches the visual radius (~3.0m)
	col.shape = shape
	blocker.add_child(col)
	smoke_root.add_child(blocker)

	# 3. Particle Effects (Core) - Keep some particles for texture/movement
	var p = GPUParticles3D.new()
	var mat = ParticleProcessMaterial.new()
	mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	mat.emission_sphere_radius = 1.0 # Start small, particles expand out
	mat.direction = Vector3(0, 1, 0)
	mat.spread = 180.0
	mat.gravity = Vector3(0, 0.2, 0) # Slight rise
	mat.initial_velocity_min = 0.5
	mat.initial_velocity_max = 1.5 # Push outward
	mat.damping_min = 0.5
	mat.damping_max = 1.0
	mat.scale_min = 2.0
	mat.scale_max = 4.0 
	mat.color = Color(1.0, 1.0, 1.0, 0.4) # Softer alpha for fluffiness
	mat.alpha_curve = _get_smoke_alpha_curve()
	
	p.process_material = mat
	p.amount = 64
	p.lifetime = 5.0
	p.explosiveness = 0.0 # Stream out, don't burst instantly
	p.emitting = true
	p.one_shot = false
	
	var quad = QuadMesh.new()
	var sms = StandardMaterial3D.new()
	sms.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	sms.vertex_color_use_as_albedo = true
	sms.albedo_color = Color(1,1,1,0.5)
	sms.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	sms.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	var grad_tex = GradientTexture2D.new()
	var grad = Gradient.new()
	grad.set_color(0, Color(1,1,1,1))
	grad.set_color(1, Color(1,1,1,0)) # Soft fade
	grad_tex.gradient = grad
	grad_tex.fill = GradientTexture2D.FILL_RADIAL
	grad_tex.fill_from = Vector2(0.5, 0.5)
	grad_tex.fill_to = Vector2(1.0, 0.5)
	sms.albedo_texture = grad_tex
	quad.material = sms
	quad.size = Vector2(3.0, 3.0)
	p.draw_pass_1 = quad
	
	smoke_root.add_child(p)
	
	# 4. Lifetime Management (Fade In / Sustain / Fade Out)
	var duration: float = 15.0
	var fade_time: float = 2.5
	
	# Fade in and expand
	var tween = create_tween()
	# Slow growth (2.5s) for "smoke coming out slowly"
	tween.tween_property(fog_mat, "density", 6.0, 2.5).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_OUT)
	
	# Start small
	fog.size = Vector3(0.1, 0.1, 0.1)
	tween.parallel().tween_property(fog, "size", Vector3(6.0, 6.0, 6.0), 2.5).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_OUT)
	
	# Sustain and Cleanup
	get_tree().create_timer(duration).timeout.connect(func():
		p.emitting = false
		var out_tween = create_tween()
		out_tween.tween_property(fog_mat, "density", 0.0, fade_time)
		out_tween.tween_callback(smoke_root.queue_free)
	)


func _get_smoke_alpha_curve() -> CurveTexture:
	var curve = Curve.new()
	curve.add_point(Vector2(0, 0))
	curve.add_point(Vector2(0.1, 1))
	curve.add_point(Vector2(0.8, 1))
	curve.add_point(Vector2(1, 0))
	var tex = CurveTexture.new()
	tex.curve = curve
	return tex


## Creates a subtle long-lived ambient dust layer over the map.
func ensure_ambient_dust(map_size: Vector2) -> void:
	if _ambient_dust != null and is_instance_valid(_ambient_dust):
		return
	_ambient_dust = GPUParticles3D.new()
	var mat = ParticleProcessMaterial.new()
	mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	mat.emission_box_extents = Vector3(maxf(map_size.x * 0.5, 60.0), 9.0, maxf(map_size.y * 0.5, 45.0))
	mat.direction = Vector3(1.0, 0.08, 0.2)
	mat.spread = 35.0
	mat.initial_velocity_min = 0.08
	mat.initial_velocity_max = 0.28
	mat.gravity = Vector3(0, 0, 0)
	mat.damping_min = 0.05
	mat.damping_max = 0.2
	mat.scale_min = 0.14
	mat.scale_max = 0.28
	mat.color = Color(0.65, 0.62, 0.56, 0.12)
	_ambient_dust.process_material = mat
	_ambient_dust.amount = 220
	_ambient_dust.lifetime = 22.0
	_ambient_dust.one_shot = false
	_ambient_dust.explosiveness = 0.0
	_ambient_dust.emitting = true
	var quad = QuadMesh.new()
	quad.size = Vector2(0.28, 0.28)
	var qmat = StandardMaterial3D.new()
	qmat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	qmat.vertex_color_use_as_albedo = true
	qmat.albedo_color = Color(1, 1, 1, 0.18)
	qmat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	qmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	quad.material = qmat
	_ambient_dust.draw_pass_1 = quad
	add_child(_ambient_dust)
	_ambient_dust.global_position = Vector3(0, 4.0, 0)


# ═══════════════════════════════════════════════════════════════════════
#  Muzzle Flash Pool (Phase 1B — pooled GPUParticles3D + OmniLight3D)
# ═══════════════════════════════════════════════════════════════════════

func _init_muzzle_pool() -> void:
	if _muzzle_initialized:
		return
	_muzzle_initialized = true
	_muzzle_particles.resize(MUZZLE_FLASH_POOL_SIZE)
	_muzzle_lights.resize(MUZZLE_FLASH_POOL_SIZE)
	_muzzle_active.resize(MUZZLE_FLASH_POOL_SIZE)
	_muzzle_timers.resize(MUZZLE_FLASH_POOL_SIZE)
	_muzzle_initial_energies.resize(MUZZLE_FLASH_POOL_SIZE)
	for i in MUZZLE_FLASH_POOL_SIZE:
		# Particle burst — hot core + outer flash
		var p = GPUParticles3D.new()
		var pmat = ParticleProcessMaterial.new()
		pmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
		pmat.direction = Vector3.FORWARD  # will be rotated by transform
		pmat.spread = 25.0
		pmat.initial_velocity_min = 6.0
		pmat.initial_velocity_max = 12.0
		pmat.gravity = Vector3.ZERO
		pmat.damping_min = 8.0
		pmat.damping_max = 14.0
		pmat.scale_min = 0.03
		pmat.scale_max = 0.08
		pmat.color = Color(1.0, 0.85, 0.4, 0.7)
		p.process_material = pmat
		p.amount = 8
		p.lifetime = 0.06
		p.one_shot = true
		p.explosiveness = 1.0
		p.emitting = false
		p.draw_pass_1 = _make_particle_quad(0.08, Color(1.0, 0.9, 0.5, 0.7))
		p.visible = false
		add_child(p)
		_muzzle_particles[i] = p

		# Point light — warm flash
		var light = OmniLight3D.new()
		light.light_color = Color(1.0, 0.88, 0.65)
		light.light_energy = 0.0
		light.omni_range = 3.0
		light.omni_attenuation = 2.0
		light.shadow_enabled = false
		light.visible = false
		add_child(light)
		_muzzle_lights[i] = light

		_muzzle_active[i] = false
		_muzzle_timers[i] = 0.0


## Spawns a muzzle flash at the weapon position.
## pos: muzzle world position, facing: unit forward direction,
## role: unit role (0-6, affects flash intensity).
func spawn_muzzle_flash(pos: Vector3, facing: Vector3, role: int = 0) -> void:
	if not is_inside_tree():
		return
	if not _muzzle_initialized:
		_init_muzzle_pool()

	# Distance cull — skip flashes beyond 200m from camera
	var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
	if cam and pos.distance_squared_to(cam.global_position) > 40000.0:
		return

	var slot: int = _muzzle_next
	_muzzle_next = (_muzzle_next + 1) % MUZZLE_FLASH_POOL_SIZE

	# Role-specific intensity: MG = 1.5x, Marksman = 1.2x, Mortar = 0 (no flash)
	var intensity: float = 1.0
	var flash_frames: float = MUZZLE_FLASH_LIFETIME
	match role:
		3:  # ROLE_MG
			intensity = 1.5
			flash_frames = 0.07  # longer burst
		4:  # ROLE_MARKSMAN
			intensity = 1.3
		6:  # ROLE_MORTAR
			return  # mortars don't have muzzle flash

	# Position particle emitter
	var p: GPUParticles3D = _muzzle_particles[slot]
	p.global_position = pos
	if facing.length_squared() > 0.01:
		p.global_transform.basis = Basis.looking_at(facing.normalized(), Vector3.UP)
	p.visible = true
	p.emitting = true

	# Light
	var light: OmniLight3D = _muzzle_lights[slot]
	light.global_position = pos
	var initial_e: float = 1.5 * intensity
	light.light_energy = initial_e
	light.visible = true

	_muzzle_active[slot] = true
	_muzzle_timers[slot] = flash_frames
	_muzzle_initial_energies[slot] = initial_e


## Called every frame from the main scene to tick muzzle flash lifetimes.
func update_muzzle_flashes(delta: float) -> void:
	if not _muzzle_initialized:
		return
	for i in MUZZLE_FLASH_POOL_SIZE:
		if not _muzzle_active[i]:
			continue
		_muzzle_timers[i] -= delta
		if _muzzle_timers[i] <= 0.0:
			_muzzle_active[i] = false
			_muzzle_particles[i].visible = false
			_muzzle_particles[i].emitting = false
			_muzzle_lights[i].visible = false
			_muzzle_lights[i].light_energy = 0.0
		else:
			# Fade light energy linearly (frame-rate independent)
			var t: float = _muzzle_timers[i] / MUZZLE_FLASH_LIFETIME
			_muzzle_lights[i].light_energy = _muzzle_initial_energies[i] * t


# ═══════════════════════════════════════════════════════════════════════
#  Material-Keyed Bullet Impact Pool (Phase 1D)
# ═══════════════════════════════════════════════════════════════════════

## Impact profile per material — controls particle behavior and appearance.
func _get_impact_profile(mat_id: int) -> Dictionary:
	match mat_id:
		1, 10, 13:  # DIRT, GRASS, CLAY
			return {
				"count": 6, "vel_min": 1.5, "vel_max": 4.0,
				"gravity": -14.0, "spread": 120.0,
				"color": Color(0.45, 0.35, 0.22, 0.8),
				"size": 0.1, "lifetime": 0.5,
				"dust_count": 8, "dust_color": Color(0.4, 0.33, 0.22, 0.5),
				"dust_size": 0.25, "dust_lifetime": 0.7,
				"has_sparks": false
			}
		2:  # STONE
			return {
				"count": 10, "vel_min": 3.0, "vel_max": 8.0,
				"gravity": -18.0, "spread": 100.0,
				"color": Color(0.55, 0.53, 0.50, 0.9),
				"size": 0.08, "lifetime": 0.4,
				"dust_count": 6, "dust_color": Color(0.55, 0.53, 0.50, 0.5),
				"dust_size": 0.3, "dust_lifetime": 0.6,
				"has_sparks": true
			}
		3:  # WOOD
			return {
				"count": 5, "vel_min": 2.0, "vel_max": 5.0,
				"gravity": -12.0, "spread": 110.0,
				"color": Color(0.55, 0.42, 0.25, 0.85),
				"size": 0.12, "lifetime": 0.5,
				"dust_count": 4, "dust_color": Color(0.5, 0.4, 0.28, 0.4),
				"dust_size": 0.2, "dust_lifetime": 0.5,
				"has_sparks": false
			}
		4, 14:  # STEEL, METAL_PLATE
			return {
				"count": 8, "vel_min": 5.0, "vel_max": 12.0,
				"gravity": -8.0, "spread": 80.0,
				"color": Color(1.0, 0.9, 0.6, 1.0),
				"size": 0.05, "lifetime": 0.3,
				"dust_count": 2, "dust_color": Color(0.4, 0.4, 0.42, 0.3),
				"dust_size": 0.15, "dust_lifetime": 0.3,
				"has_sparks": true
			}
		5:  # CONCRETE
			return {
				"count": 8, "vel_min": 2.5, "vel_max": 6.0,
				"gravity": -16.0, "spread": 110.0,
				"color": Color(0.7, 0.68, 0.65, 0.85),
				"size": 0.09, "lifetime": 0.45,
				"dust_count": 10, "dust_color": Color(0.65, 0.63, 0.60, 0.6),
				"dust_size": 0.35, "dust_lifetime": 0.8,
				"has_sparks": false
			}
		6:  # BRICK
			return {
				"count": 8, "vel_min": 2.5, "vel_max": 6.0,
				"gravity": -16.0, "spread": 110.0,
				"color": Color(0.65, 0.35, 0.25, 0.9),
				"size": 0.09, "lifetime": 0.45,
				"dust_count": 8, "dust_color": Color(0.55, 0.38, 0.30, 0.5),
				"dust_size": 0.3, "dust_lifetime": 0.7,
				"has_sparks": false
			}
		7:  # GLASS
			return {
				"count": 14, "vel_min": 5.0, "vel_max": 14.0,
				"gravity": -10.0, "spread": 160.0,
				"color": Color(0.9, 0.95, 1.0, 1.0),
				"size": 0.04, "lifetime": 0.5,
				"dust_count": 0, "dust_color": Color.WHITE,
				"dust_size": 0.0, "dust_lifetime": 0.0,
				"has_sparks": true
			}
		8, 11:  # SAND, GRAVEL
			return {
				"count": 8, "vel_min": 1.5, "vel_max": 4.5,
				"gravity": -18.0, "spread": 140.0,
				"color": Color(0.7, 0.62, 0.45, 0.75),
				"size": 0.08, "lifetime": 0.4,
				"dust_count": 10, "dust_color": Color(0.65, 0.58, 0.42, 0.5),
				"dust_size": 0.3, "dust_lifetime": 0.8,
				"has_sparks": false
			}
		12:  # SANDBAG
			return {
				"count": 6, "vel_min": 1.0, "vel_max": 3.0,
				"gravity": -16.0, "spread": 130.0,
				"color": Color(0.55, 0.48, 0.35, 0.75),
				"size": 0.1, "lifetime": 0.5,
				"dust_count": 6, "dust_color": Color(0.55, 0.50, 0.38, 0.4),
				"dust_size": 0.25, "dust_lifetime": 0.6,
				"has_sparks": false
			}
		15:  # RUST
			return {
				"count": 6, "vel_min": 4.0, "vel_max": 9.0,
				"gravity": -12.0, "spread": 90.0,
				"color": Color(0.8, 0.5, 0.25, 1.0),
				"size": 0.06, "lifetime": 0.35,
				"dust_count": 3, "dust_color": Color(0.5, 0.35, 0.25, 0.4),
				"dust_size": 0.2, "dust_lifetime": 0.4,
				"has_sparks": true
			}
		_:
			return {
				"count": 6, "vel_min": 2.0, "vel_max": 5.0,
				"gravity": -16.0, "spread": 110.0,
				"color": Color(0.5, 0.48, 0.44, 0.8),
				"size": 0.08, "lifetime": 0.4,
				"dust_count": 6, "dust_color": Color(0.5, 0.48, 0.44, 0.4),
				"dust_size": 0.25, "dust_lifetime": 0.6,
				"has_sparks": false
			}


func _init_impact_pool() -> void:
	if _impact_initialized:
		return
	_impact_initialized = true
	_impact_particles.resize(IMPACT_POOL_SIZE)
	_impact_active.resize(IMPACT_POOL_SIZE)
	_impact_timers.resize(IMPACT_POOL_SIZE)
	for i in IMPACT_POOL_SIZE:
		var p = GPUParticles3D.new()
		var pmat = ParticleProcessMaterial.new()
		pmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
		pmat.direction = Vector3.UP
		pmat.spread = 110.0
		pmat.initial_velocity_min = 3.0
		pmat.initial_velocity_max = 6.0
		pmat.gravity = Vector3(0, -16, 0)
		pmat.damping_min = 0.5
		pmat.damping_max = 1.2
		pmat.scale_min = 0.06
		pmat.scale_max = 0.12
		pmat.color = Color(0.5, 0.5, 0.5, 0.8)
		p.process_material = pmat
		p.amount = 8
		p.lifetime = 0.5
		p.one_shot = true
		p.explosiveness = 1.0
		p.emitting = false
		p.visible = false
		p.draw_pass_1 = _make_particle_quad(0.1, Color.WHITE, false)
		add_child(p)
		_impact_particles[i] = p
		_impact_active[i] = false
		_impact_timers[i] = 0.0


## Spawns a material-appropriate bullet impact effect at the given position.
## mat_id: voxel material that was hit (0-15).
func spawn_bullet_impact(pos: Vector3, mat_id: int) -> void:
	if not is_inside_tree():
		return
	if not _impact_initialized:
		_init_impact_pool()

	# Distance cull — skip beyond 150m
	var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
	if cam and pos.distance_squared_to(cam.global_position) > 22500.0:
		return

	var slot: int = _impact_next
	_impact_next = (_impact_next + 1) % IMPACT_POOL_SIZE

	var prof: Dictionary = _get_impact_profile(mat_id)
	var mat_color: Color = MATERIAL_COLORS[clampi(mat_id, 0, MATERIAL_COLORS.size() - 1)]

	# Configure the pooled particle emitter for this material
	var p: GPUParticles3D = _impact_particles[slot]
	var pmat: ParticleProcessMaterial = p.process_material as ParticleProcessMaterial
	pmat.initial_velocity_min = prof["vel_min"]
	pmat.initial_velocity_max = prof["vel_max"]
	pmat.gravity = Vector3(0, prof["gravity"], 0)
	pmat.spread = prof["spread"]
	pmat.color = prof["color"]
	pmat.scale_min = prof["size"] * 0.7
	pmat.scale_max = prof["size"] * 1.3
	p.amount = prof["count"]
	p.lifetime = prof["lifetime"]
	p.global_position = pos
	p.visible = true
	p.emitting = true

	# Update draw pass color to match material
	p.draw_pass_1 = _make_particle_quad(prof["size"] * 1.5, prof["color"], false)

	_impact_active[slot] = true
	_impact_timers[slot] = IMPACT_LIFETIME

	# Dust puff (reuse the pooled particle by spawning a temporary one for now)
	var dust_count: int = prof["dust_count"]
	if dust_count > 0:
		var dust = GPUParticles3D.new()
		var dmat = ParticleProcessMaterial.new()
		dmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
		dmat.direction = Vector3.UP
		dmat.spread = 160.0
		dmat.initial_velocity_min = 0.5
		dmat.initial_velocity_max = 1.8
		dmat.gravity = Vector3(0, -2.5, 0)
		dmat.damping_min = 1.0
		dmat.damping_max = 2.0
		dmat.scale_min = prof["dust_size"] * 0.6
		dmat.scale_max = prof["dust_size"] * 1.4
		dmat.color = prof["dust_color"]
		dust.process_material = dmat
		dust.amount = dust_count
		dust.lifetime = prof["dust_lifetime"]
		dust.one_shot = true
		dust.explosiveness = 0.8
		dust.emitting = true
		dust.draw_pass_1 = _make_particle_quad(prof["dust_size"] * 2.0, prof["dust_color"])
		add_child(dust)
		dust.global_position = pos + Vector3(0, 0.05, 0)
		get_tree().create_timer(prof["dust_lifetime"] + 0.3).timeout.connect(dust.queue_free)

	# Sparks for metallic/stone/glass materials
	if prof["has_sparks"]:
		_spawn_flash_light(pos + Vector3(0, 0.05, 0),
			Color(1.0, 0.85, 0.5), 2.0, 2.5, 0.06)

	# Small impact ring (subtle, material-colored)
	_spawn_impact_ring(pos, mat_color.lightened(0.3), 0.3, 0.14)

	# Bullet hole decal (pooled)
	spawn_decal_bullet_hole(pos, mat_id)


## Tick impact pool lifetimes.
func update_impact_pool(delta: float) -> void:
	if not _impact_initialized:
		return
	for i in IMPACT_POOL_SIZE:
		if not _impact_active[i]:
			continue
		_impact_timers[i] -= delta
		if _impact_timers[i] <= 0.0:
			_impact_active[i] = false
			_impact_particles[i].visible = false
			_impact_particles[i].emitting = false


# ═══════════════════════════════════════════════════════════════════════
#  Hero Chunk Pool (Tier 3 — RID-based flying debris)
# ═══════════════════════════════════════════════════════════════════════

func _init_chunk_pool() -> void:
	if _chunk_initialized:
		return
	_chunk_initialized = true
	var scenario = get_world_3d().scenario
	var space = get_world_3d().space
	_chunk_body_rids.resize(CHUNK_POOL_SIZE)
	_chunk_mesh_rids.resize(CHUNK_POOL_SIZE)
	_chunk_instance_rids.resize(CHUNK_POOL_SIZE)
	_chunk_materials.resize(CHUNK_POOL_SIZE)
	_chunk_active.resize(CHUNK_POOL_SIZE)
	_chunk_timers.resize(CHUNK_POOL_SIZE)
	_chunk_mat_ids.resize(CHUNK_POOL_SIZE)
	_chunk_rest_timer.resize(CHUNK_POOL_SIZE)
	for i in CHUNK_POOL_SIZE:
		# Physics body
		var body = PhysicsServer3D.body_create()
		PhysicsServer3D.body_set_mode(body, PhysicsServer3D.BODY_MODE_RIGID)
		PhysicsServer3D.body_set_space(body, space)
		var shape = PhysicsServer3D.box_shape_create()
		PhysicsServer3D.shape_set_data(shape, Vector3(0.2, 0.2, 0.2))
		PhysicsServer3D.body_add_shape(body, shape)
		PhysicsServer3D.body_set_collision_layer(body, 0)  # no collision layer
		PhysicsServer3D.body_set_collision_mask(body, 1 | 2)  # collide with world + islands
		PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM,
			Transform3D(Basis(), Vector3(0, -100, 0)))  # hide below world
		_chunk_body_rids[i] = body

		# Mesh
		var mesh = RenderingServer.mesh_create()
		var box = BoxMesh.new()
		box.size = Vector3(0.4, 0.4, 0.4)
		var arrays = box.surface_get_arrays(0)
		RenderingServer.mesh_add_surface_from_arrays(mesh, RenderingServer.PRIMITIVE_TRIANGLES, arrays)
		var default_mat = StandardMaterial3D.new()
		default_mat.albedo_color = Color(0.5, 0.5, 0.5)
		RenderingServer.mesh_surface_set_material(mesh, 0, default_mat.get_rid())
		_chunk_materials[i] = default_mat  # prevent GC — RID dies if Resource is freed
		_chunk_mesh_rids[i] = mesh

		# Instance
		var inst = RenderingServer.instance_create()
		RenderingServer.instance_set_base(inst, mesh)
		RenderingServer.instance_set_scenario(inst, scenario)
		RenderingServer.instance_set_visible(inst, false)
		_chunk_instance_rids[i] = inst

		_chunk_active[i] = false
		_chunk_timers[i] = 0.0
		_chunk_mat_ids[i] = 0
		_chunk_rest_timer[i] = 0.0


func _get_free_chunk_slot() -> int:
	for i in CHUNK_POOL_SIZE:
		if not _chunk_active[i]:
			return i
	return -1


func _spawn_hero_chunk(pos: Vector3, mat_id: int, blast_dir: Vector3) -> void:
	if not _chunk_initialized:
		_init_chunk_pool()
	var slot = _get_free_chunk_slot()
	if slot < 0:
		return

	var color = MATERIAL_COLORS[clampi(mat_id, 0, MATERIAL_COLORS.size() - 1)]

	# Set material color on the mesh surface (store ref to prevent GC → null RID)
	var mat = StandardMaterial3D.new()
	mat.albedo_color = color
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
	RenderingServer.mesh_surface_set_material(_chunk_mesh_rids[slot], 0, mat.get_rid())
	_chunk_materials[slot] = mat

	# Random size variation — big enough to see at gameplay distance
	var size_scale = randf_range(0.25, 0.6)
	var xform = Transform3D(Basis().scaled(Vector3(size_scale, size_scale, size_scale)), pos)
	PhysicsServer3D.body_set_state(_chunk_body_rids[slot],
		PhysicsServer3D.BODY_STATE_TRANSFORM, xform)
	RenderingServer.instance_set_transform(_chunk_instance_rids[slot], xform)
	RenderingServer.instance_set_visible(_chunk_instance_rids[slot], true)

	# Apply outward impulse biased upward — dramatic arcs
	var impulse = blast_dir.normalized() * randf_range(5.0, 12.0)
	impulse.y = absf(impulse.y) + randf_range(3.0, 8.0)
	PhysicsServer3D.body_apply_central_impulse(_chunk_body_rids[slot], impulse)

	# Random angular velocity for tumble
	var ang = Vector3(randf_range(-10, 10), randf_range(-10, 10), randf_range(-10, 10))
	PhysicsServer3D.body_set_state(_chunk_body_rids[slot],
		PhysicsServer3D.BODY_STATE_ANGULAR_VELOCITY, ang)

	_chunk_active[slot] = true
	_chunk_timers[slot] = CHUNK_LIFETIME
	_chunk_mat_ids[slot] = mat_id
	_chunk_rest_timer[slot] = 0.0


func update_hero_chunks(delta: float) -> void:
	if not _chunk_initialized:
		return
	for i in CHUNK_POOL_SIZE:
		if not _chunk_active[i]:
			continue
		_chunk_timers[i] -= delta
		if _chunk_timers[i] <= 0.0:
			_reclaim_chunk(i)
			continue

		# Sync render instance to physics body transform
		var xform: Transform3D = PhysicsServer3D.body_get_state(
			_chunk_body_rids[i], PhysicsServer3D.BODY_STATE_TRANSFORM)
		RenderingServer.instance_set_transform(_chunk_instance_rids[i], xform)

		# Rest detection: re-solidify when chunk settles
		var vel: Vector3 = PhysicsServer3D.body_get_state(
			_chunk_body_rids[i], PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY)
		if vel.length() < 0.5:
			_chunk_rest_timer[i] += delta
			if _chunk_rest_timer[i] >= CHUNK_FREEZE_TIME:
				_resolidify_hero_chunk(i)
				_reclaim_chunk(i)
		else:
			_chunk_rest_timer[i] = 0.0


func _resolidify_hero_chunk(slot: int) -> void:
	if not _voxel_world or not _voxel_world.is_initialized():
		return
	var mat_id: int = _chunk_mat_ids[slot]
	if mat_id == 0:
		return  # AIR, skip

	var xform: Transform3D = PhysicsServer3D.body_get_state(
		_chunk_body_rids[slot], PhysicsServer3D.BODY_STATE_TRANSFORM)
	var voxel_pos: Vector3i = _voxel_world.world_to_voxel(xform.origin)

	# Bounds check
	if voxel_pos.x < 0 or voxel_pos.x >= _voxel_world.get_world_size_x(): return
	if voxel_pos.y < 0 or voxel_pos.y >= _voxel_world.get_world_size_y(): return
	if voxel_pos.z < 0 or voxel_pos.z >= _voxel_world.get_world_size_z(): return

	# Only write into air (don't overwrite existing solid)
	if _voxel_world.get_voxel(voxel_pos.x, voxel_pos.y, voxel_pos.z) != 0:
		return

	_voxel_world.set_voxel_dirty(voxel_pos.x, voxel_pos.y, voxel_pos.z, mat_id)


func _reclaim_chunk(slot: int) -> void:
	_chunk_active[slot] = false
	_chunk_mat_ids[slot] = 0
	_chunk_rest_timer[slot] = 0.0
	RenderingServer.instance_set_visible(_chunk_instance_rids[slot], false)
	PhysicsServer3D.body_set_state(_chunk_body_rids[slot],
		PhysicsServer3D.BODY_STATE_TRANSFORM,
		Transform3D(Basis(), Vector3(0, -100, 0)))
	PhysicsServer3D.body_set_state(_chunk_body_rids[slot],
		PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY, Vector3.ZERO)
	PhysicsServer3D.body_set_state(_chunk_body_rids[slot],
		PhysicsServer3D.BODY_STATE_ANGULAR_VELOCITY, Vector3.ZERO)


# ═══════════════════════════════════════════════════════════════════════
#  Material-Specific Destruction VFX (Tier 4)
# ═══════════════════════════════════════════════════════════════════════

func _get_destruction_profile(mat_id: int) -> Dictionary:
	match mat_id:
		1, 10, 13:  # DIRT, GRASS, CLAY
			return {
				"fragment_vel": Vector2(2.0, 5.0),
				"fragment_count_mult": 0.8,
				"fragment_gravity": -18.0,
				"dust_mult": 1.5,
				"dust_color": Color(0.35, 0.28, 0.20, 0.6),
				"dust_lifetime": 2.0,
				"chunk_count": 4,
				"has_sparks": false, "has_embers": false, "has_sparkle": false,
			}
		2:  # STONE
			return {
				"fragment_vel": Vector2(3.0, 7.0),
				"fragment_count_mult": 1.0,
				"fragment_gravity": -20.0,
				"dust_mult": 2.0,
				"dust_color": Color(0.55, 0.53, 0.50, 0.65),
				"dust_lifetime": 2.5,
				"chunk_count": 6,
				"has_sparks": true, "has_embers": false, "has_sparkle": false,
			}
		3:  # WOOD
			return {
				"fragment_vel": Vector2(2.5, 6.0),
				"fragment_count_mult": 0.9,
				"fragment_gravity": -14.0,
				"dust_mult": 0.8,
				"dust_color": Color(0.42, 0.35, 0.25, 0.5),
				"dust_lifetime": 1.5,
				"chunk_count": 5,
				"has_sparks": false, "has_embers": true, "has_sparkle": false,
			}
		4, 14:  # STEEL, METAL_PLATE
			return {
				"fragment_vel": Vector2(4.0, 9.0),
				"fragment_count_mult": 0.5,
				"fragment_gravity": -22.0,
				"dust_mult": 0.3,
				"dust_color": Color(0.4, 0.4, 0.42, 0.3),
				"dust_lifetime": 0.8,
				"chunk_count": 3,
				"has_sparks": true, "has_embers": false, "has_sparkle": false,
			}
		5:  # CONCRETE
			return {
				"fragment_vel": Vector2(3.0, 6.5),
				"fragment_count_mult": 1.2,
				"fragment_gravity": -18.0,
				"dust_mult": 2.5,
				"dust_color": Color(0.65, 0.63, 0.60, 0.7),
				"dust_lifetime": 3.0,
				"chunk_count": 6,
				"has_sparks": false, "has_embers": false, "has_sparkle": false,
			}
		6:  # BRICK
			return {
				"fragment_vel": Vector2(2.5, 5.5),
				"fragment_count_mult": 1.1,
				"fragment_gravity": -17.0,
				"dust_mult": 1.8,
				"dust_color": Color(0.52, 0.35, 0.30, 0.6),
				"dust_lifetime": 2.2,
				"chunk_count": 5,
				"has_sparks": false, "has_embers": false, "has_sparkle": false,
			}
		7:  # GLASS
			return {
				"fragment_vel": Vector2(5.0, 12.0),
				"fragment_count_mult": 2.0,
				"fragment_gravity": -12.0,
				"dust_mult": 0.1,
				"dust_color": Color(0.8, 0.85, 0.9, 0.2),
				"dust_lifetime": 0.5,
				"chunk_count": 2,
				"has_sparks": false, "has_embers": false, "has_sparkle": true,
			}
		8, 11:  # SAND, GRAVEL
			return {
				"fragment_vel": Vector2(2.0, 4.0),
				"fragment_count_mult": 1.5,
				"fragment_gravity": -20.0,
				"dust_mult": 2.0,
				"dust_color": Color(0.65, 0.58, 0.42, 0.6),
				"dust_lifetime": 2.5,
				"chunk_count": 3,
				"has_sparks": false, "has_embers": false, "has_sparkle": false,
			}
		_:
			return {
				"fragment_vel": Vector2(3.0, 6.0),
				"fragment_count_mult": 1.0,
				"fragment_gravity": -18.0,
				"dust_mult": 1.0,
				"dust_color": Color(0.5, 0.48, 0.44, 0.5),
				"dust_lifetime": 2.0,
				"chunk_count": 4,
				"has_sparks": false, "has_embers": false, "has_sparkle": false,
			}


## Master destruction VFX method — orchestrates all tiers based on material.
func spawn_voxel_destruction(center: Vector3, blast_radius: float,
		dominant_mat: int, histogram: PackedInt32Array,
		debris_data: Array) -> void:
	if not is_inside_tree():
		return

	var prof = _get_destruction_profile(dominant_mat)
	var mat_color = MATERIAL_COLORS[clampi(dominant_mat, 0, MATERIAL_COLORS.size() - 1)]

	# Tier 3: Hero chunks from debris samples
	for d in debris_data:
		var dpos: Vector3 = d.get("position", center)
		var dmat: int = d.get("material", dominant_mat)
		var blast_dir = (dpos - center)
		if blast_dir.length_squared() < 0.01:
			blast_dir = Vector3(randf_range(-1, 1), 1, randf_range(-1, 1))
		_spawn_hero_chunk(dpos, dmat, blast_dir)

	# Tier 4: Material-colored fragment particles
	var frag = GPUParticles3D.new()
	var frag_mat = ParticleProcessMaterial.new()
	frag_mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	frag_mat.emission_sphere_radius = maxf(blast_radius * 0.3, 0.3)
	frag_mat.direction = Vector3.UP
	frag_mat.spread = 170.0
	var vel: Vector2 = prof["fragment_vel"]
	frag_mat.initial_velocity_min = vel.x
	frag_mat.initial_velocity_max = vel.y
	frag_mat.gravity = Vector3(0, prof["fragment_gravity"], 0)
	frag_mat.damping_min = 0.5
	frag_mat.damping_max = 1.2
	frag_mat.scale_min = 0.06
	frag_mat.scale_max = 0.15
	frag_mat.color = Color(mat_color.r, mat_color.g, mat_color.b, 0.95)
	frag.process_material = frag_mat
	frag.amount = int(clampf(120.0 * prof["fragment_count_mult"], 40.0, 500.0))
	frag.lifetime = 1.0
	frag.one_shot = true
	frag.explosiveness = 1.0
	frag.emitting = true
	frag.draw_pass_1 = _make_particle_quad(0.12, mat_color, false)
	add_child(frag)
	frag.global_position = center

	# Lingering dust cloud
	var dust_mult: float = prof["dust_mult"]
	if dust_mult > 0.1:
		var dust = GPUParticles3D.new()
		var dust_pmat = ParticleProcessMaterial.new()
		dust_pmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
		dust_pmat.emission_sphere_radius = maxf(blast_radius * 0.5, 0.5)
		dust_pmat.direction = Vector3.UP
		dust_pmat.spread = 180.0
		dust_pmat.initial_velocity_min = 0.3
		dust_pmat.initial_velocity_max = 1.2
		dust_pmat.gravity = Vector3(0, -1.0, 0)
		dust_pmat.damping_min = 1.0
		dust_pmat.damping_max = 2.0
		dust_pmat.scale_min = 0.4
		dust_pmat.scale_max = 1.2
		var dc: Color = prof["dust_color"]
		dust_pmat.color = dc
		dust.process_material = dust_pmat
		dust.amount = int(clampf(60.0 * dust_mult, 20.0, 250.0))
		dust.lifetime = prof["dust_lifetime"]
		dust.one_shot = true
		dust.explosiveness = 0.6
		dust.emitting = true
		dust.draw_pass_1 = _make_particle_quad(
			maxf(blast_radius * 0.4, 0.8),
			Color(dc.r, dc.g, dc.b, dc.a * 0.8))
		add_child(dust)
		dust.global_position = center + Vector3(0, 0.1, 0)

		var dust_life: float = prof["dust_lifetime"]
		get_tree().create_timer(dust_life + 0.5).timeout.connect(func():
			if is_instance_valid(dust):
				dust.queue_free()
		)

	# Special effects based on material
	if prof["has_sparks"]:
		_spawn_spark_burst(center, mat_color)
	if prof["has_embers"]:
		_spawn_ember_trails(center)
	if prof["has_sparkle"]:
		_spawn_glass_sparkle(center, blast_radius)

	# Fire smoke for flammable materials (Phase 5C)
	if dominant_mat == 3 or dominant_mat == 10:  # WOOD or GRASS
		spawn_fire_smoke(center, clampf(blast_radius * 0.4, 0.5, 2.0))

	# Cleanup fragments
	get_tree().create_timer(1.5).timeout.connect(func():
		if is_instance_valid(frag):
			frag.queue_free()
	)


func _spawn_spark_burst(pos: Vector3, color: Color) -> void:
	var sparks = GPUParticles3D.new()
	var smat = ParticleProcessMaterial.new()
	smat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_POINT
	smat.direction = Vector3.UP
	smat.spread = 180.0
	smat.initial_velocity_min = 6.0
	smat.initial_velocity_max = 14.0
	smat.gravity = Vector3(0, -12.0, 0)
	smat.damping_min = 0.3
	smat.damping_max = 0.8
	smat.scale_min = 0.03
	smat.scale_max = 0.08
	smat.color = Color(1.0, 0.9, 0.6, 1.0)
	sparks.process_material = smat
	sparks.amount = 60
	sparks.lifetime = 0.5
	sparks.one_shot = true
	sparks.explosiveness = 1.0
	sparks.emitting = true
	sparks.draw_pass_1 = _make_particle_quad(0.06, Color(1.0, 0.85, 0.4, 1.0))
	add_child(sparks)
	sparks.global_position = pos
	get_tree().create_timer(0.8).timeout.connect(func():
		if is_instance_valid(sparks):
			sparks.queue_free()
	)


func _spawn_ember_trails(pos: Vector3) -> void:
	var embers = GPUParticles3D.new()
	var emat = ParticleProcessMaterial.new()
	emat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	emat.emission_sphere_radius = 0.3
	emat.direction = Vector3.UP
	emat.spread = 120.0
	emat.initial_velocity_min = 1.5
	emat.initial_velocity_max = 4.0
	emat.gravity = Vector3(0, -4.0, 0)
	emat.damping_min = 0.2
	emat.damping_max = 0.5
	emat.scale_min = 0.04
	emat.scale_max = 0.1
	emat.color = Color(1.0, 0.5, 0.1, 0.9)
	embers.process_material = emat
	embers.amount = 30
	embers.lifetime = 1.5
	embers.one_shot = true
	embers.explosiveness = 0.8
	embers.emitting = true
	embers.draw_pass_1 = _make_particle_quad(0.08, Color(1.0, 0.6, 0.15, 0.9))
	add_child(embers)
	embers.global_position = pos
	get_tree().create_timer(2.0).timeout.connect(func():
		if is_instance_valid(embers):
			embers.queue_free()
	)


func _spawn_glass_sparkle(pos: Vector3, radius: float) -> void:
	var sparkle = GPUParticles3D.new()
	var gmat = ParticleProcessMaterial.new()
	gmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
	gmat.emission_sphere_radius = maxf(radius * 0.4, 0.3)
	gmat.direction = Vector3.UP
	gmat.spread = 180.0
	gmat.initial_velocity_min = 5.0
	gmat.initial_velocity_max = 12.0
	gmat.gravity = Vector3(0, -10.0, 0)
	gmat.damping_min = 0.2
	gmat.damping_max = 0.6
	gmat.scale_min = 0.02
	gmat.scale_max = 0.06
	gmat.color = Color(0.9, 0.95, 1.0, 1.0)
	sparkle.process_material = gmat
	sparkle.amount = 120
	sparkle.lifetime = 0.7
	sparkle.one_shot = true
	sparkle.explosiveness = 1.0
	sparkle.emitting = true
	sparkle.draw_pass_1 = _make_particle_quad(0.04, Color(1.0, 1.0, 1.0, 1.0))
	add_child(sparkle)
	sparkle.global_position = pos
	# Brief flash for glass shatter
	_spawn_flash_light(pos, Color(0.8, 0.9, 1.0), 4.0, 3.0, 0.12)
	get_tree().create_timer(1.0).timeout.connect(func():
		if is_instance_valid(sparkle):
			sparkle.queue_free()
	)


func cleanup_chunk_pool() -> void:
	for i in _chunk_body_rids.size():
		if _chunk_body_rids[i].is_valid():
			PhysicsServer3D.free_rid(_chunk_body_rids[i])
		if _chunk_instance_rids[i].is_valid():
			RenderingServer.free_rid(_chunk_instance_rids[i])
		if _chunk_mesh_rids[i].is_valid():
			RenderingServer.free_rid(_chunk_mesh_rids[i])
	_chunk_body_rids.clear()
	_chunk_mesh_rids.clear()
	_chunk_instance_rids.clear()
	_chunk_active.clear()
	_chunk_initialized = false


# ═══════════════════════════════════════════════════════════════════════
#  Destruction Fog Pool (volumetric dust/smoke at explosion sites)
# ═══════════════════════════════════════════════════════════════════════

func _init_fog_pool() -> void:
	if _fog_initialized:
		return
	_fog_initialized = true
	_fog_volumes.resize(DESTRUCTION_FOG_POOL_SIZE)
	_fog_materials.resize(DESTRUCTION_FOG_POOL_SIZE)
	_fog_active.resize(DESTRUCTION_FOG_POOL_SIZE)
	_fog_timers.resize(DESTRUCTION_FOG_POOL_SIZE)
	for i in DESTRUCTION_FOG_POOL_SIZE:
		var fog = FogVolume.new()
		fog.shape = RenderingServer.FOG_VOLUME_SHAPE_ELLIPSOID
		fog.size = Vector3(1, 1, 1)
		fog.visible = false
		var fog_mat = FogMaterial.new()
		fog_mat.density = 0.0
		fog_mat.albedo = Color.WHITE
		fog_mat.emission = Color(0.3, 0.3, 0.3)
		fog_mat.edge_fade = 0.3
		fog.material = fog_mat
		add_child(fog)
		_fog_volumes[i] = fog
		_fog_materials[i] = fog_mat
		_fog_active[i] = false
		_fog_timers[i] = 0.0


func _get_fog_profile(mat_id: int) -> Dictionary:
	## Returns fog density and color based on destroyed material type.
	match mat_id:
		1, 10, 13:  # DIRT, GRASS, CLAY
			return {"density": 3.5, "color": Color(0.45, 0.38, 0.28), "emission": Color(0.2, 0.17, 0.12)}
		2:  # STONE
			return {"density": 4.0, "color": Color(0.6, 0.58, 0.55), "emission": Color(0.25, 0.24, 0.22)}
		3:  # WOOD
			return {"density": 3.0, "color": Color(0.45, 0.38, 0.30), "emission": Color(0.18, 0.14, 0.1)}
		4, 14:  # STEEL, METAL_PLATE
			return {"density": 2.0, "color": Color(0.4, 0.4, 0.42), "emission": Color(0.15, 0.15, 0.16)}
		5:  # CONCRETE
			return {"density": 4.5, "color": Color(0.65, 0.63, 0.60), "emission": Color(0.28, 0.27, 0.25)}
		6:  # BRICK
			return {"density": 3.8, "color": Color(0.55, 0.38, 0.32), "emission": Color(0.22, 0.15, 0.12)}
		7:  # GLASS
			return {"density": 1.0, "color": Color(0.8, 0.85, 0.92), "emission": Color(0.35, 0.38, 0.42)}
		8, 11:  # SAND, GRAVEL
			return {"density": 3.5, "color": Color(0.62, 0.55, 0.40), "emission": Color(0.25, 0.22, 0.16)}
		12:  # SANDBAG
			return {"density": 3.0, "color": Color(0.55, 0.50, 0.38), "emission": Color(0.22, 0.2, 0.15)}
		15:  # RUST
			return {"density": 2.5, "color": Color(0.5, 0.35, 0.25), "emission": Color(0.2, 0.14, 0.1)}
		_:
			return {"density": 3.0, "color": Color(0.5, 0.48, 0.44), "emission": Color(0.2, 0.19, 0.17)}


func spawn_destruction_fog(pos: Vector3, blast_radius: float, dominant_mat: int) -> void:
	## Spawns a lingering volumetric dust/smoke cloud at a destruction site.
	if not is_inside_tree():
		return
	if not _fog_initialized:
		_init_fog_pool()

	# Round-robin slot selection
	var slot: int = _fog_next
	_fog_next = (_fog_next + 1) % DESTRUCTION_FOG_POOL_SIZE

	# Reclaim if active (cancel existing tween by overwriting)
	var fog: FogVolume = _fog_volumes[slot]
	var fog_mat: FogMaterial = _fog_materials[slot]

	# Get material-dependent fog profile
	var prof: Dictionary = _get_fog_profile(dominant_mat)
	var target_density: float = prof["density"]
	var fog_color: Color = prof["color"]
	var fog_emission: Color = prof["emission"]

	# Configure fog volume
	var fog_size = Vector3(
		blast_radius * 2.5,
		blast_radius * 1.5,
		blast_radius * 2.5
	)
	fog.size = Vector3(0.1, 0.1, 0.1)  # Start tiny, expand
	fog.global_position = pos + Vector3(0, blast_radius * 0.3, 0)
	fog.visible = true

	# Configure material
	fog_mat.density = 0.0
	fog_mat.albedo = fog_color
	fog_mat.emission = fog_emission

	_fog_active[slot] = true
	_fog_timers[slot] = DESTRUCTION_FOG_LIFETIME

	# Animate: expand + fade in, then fade out
	var tween = create_tween()
	# Phase 1: Rapid expansion + density ramp (0.4s)
	tween.tween_property(fog_mat, "density", target_density, 0.4).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tween.parallel().tween_property(fog, "size", fog_size, 0.5).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	# Phase 2: Slow drift upward + gentle expansion (sustain period)
	var drift_size = fog_size * 1.3
	drift_size.y *= 1.5  # Rise more vertically
	tween.tween_property(fog, "size", drift_size, DESTRUCTION_FOG_LIFETIME - 2.0).set_trans(Tween.TRANS_LINEAR)
	tween.parallel().tween_property(fog, "position:y", fog.position.y + blast_radius * 0.8, DESTRUCTION_FOG_LIFETIME - 2.0)
	# Phase 3: Fade out (last 2s)
	tween.tween_property(fog_mat, "density", 0.0, 2.0).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN)
	tween.tween_callback(func():
		fog.visible = false
		_fog_active[slot] = false
	)

	# Phase 3D: Light shaft enhancement — spawn a second, taller, thinner fog
	# that lingers 20s and catches directional light through destroyed ceilings/walls.
	if blast_radius >= 2.0:
		_spawn_light_shaft_fog(pos, blast_radius)


## Spawn a tall, thin, low-density fog volume at destruction sites that catches
## directional light rays through newly created openings (Phase 3D).
func _spawn_light_shaft_fog(pos: Vector3, blast_radius: float) -> void:
	if not _fog_initialized:
		_init_fog_pool()
	var slot: int = _fog_next
	_fog_next = (_fog_next + 1) % DESTRUCTION_FOG_POOL_SIZE
	var fog: FogVolume = _fog_volumes[slot]
	var fog_mat: FogMaterial = _fog_materials[slot]

	# Tall thin column — catches directional light shafts
	var shaft_w: float = maxf(blast_radius * 0.8, 1.5)
	var shaft_h: float = maxf(blast_radius * 3.0, 6.0)
	fog.size = Vector3(0.1, 0.1, 0.1)
	fog.global_position = pos + Vector3(0, shaft_h * 0.3, 0)
	fog.visible = true

	fog_mat.density = 0.0
	fog_mat.albedo = Color(0.75, 0.72, 0.68)  # Warm dust catching light
	fog_mat.emission = Color(0.1, 0.09, 0.08)

	_fog_active[slot] = true
	_fog_timers[slot] = 20.0

	var shaft_size = Vector3(shaft_w, shaft_h, shaft_w)
	var tween = create_tween()
	tween.tween_property(fog_mat, "density", 0.02, 1.5)\
		.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tween.parallel().tween_property(fog, "size", shaft_size, 2.0)\
		.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	# Slow rise over 15s
	tween.tween_property(fog, "position:y", fog.position.y + shaft_h * 0.4, 14.0)
	# Fade out
	tween.tween_property(fog_mat, "density", 0.0, 4.0)\
		.set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN)
	tween.tween_callback(func():
		fog.visible = false
		_fog_active[slot] = false
	)


# ── Gas Cloud VFX Pool (persistent smoke/gas volumes) ─────────────────
const GAS_CLOUD_POOL_SIZE: int = 16
const GAS_CLOUD_LIFETIMES: Array[float] = [0.0, 25.0, 35.0, 50.0]  # per gas type: none, smoke, tear, toxic

var _gas_volumes: Array[FogVolume] = []
var _gas_materials: Array[FogMaterial] = []
var _gas_active: Array[bool] = []
var _gas_timers: Array[float] = []
var _gas_next: int = 0
var _gas_initialized: bool = false


func _get_gas_color(gas_type: int) -> Dictionary:
	## Returns fog profile for each gas type.
	match gas_type:
		1:  # Smoke (gray-white)
			return {"density": 2.5, "color": Color(0.8, 0.8, 0.82, 0.6), "emission": Color(0.35, 0.35, 0.36)}
		2:  # Tear gas (yellow-green)
			return {"density": 2.0, "color": Color(0.8, 0.9, 0.4, 0.5), "emission": Color(0.35, 0.4, 0.15)}
		3:  # Toxic (sickly green)
			return {"density": 1.8, "color": Color(0.4, 0.85, 0.4, 0.4), "emission": Color(0.15, 0.38, 0.15)}
		_:
			return {"density": 2.0, "color": Color(0.7, 0.7, 0.7, 0.5), "emission": Color(0.3, 0.3, 0.3)}


func _init_gas_pool() -> void:
	if _gas_initialized:
		return
	_gas_initialized = true
	_gas_volumes.resize(GAS_CLOUD_POOL_SIZE)
	_gas_materials.resize(GAS_CLOUD_POOL_SIZE)
	_gas_active.resize(GAS_CLOUD_POOL_SIZE)
	_gas_timers.resize(GAS_CLOUD_POOL_SIZE)
	for i in GAS_CLOUD_POOL_SIZE:
		var fog = FogVolume.new()
		fog.shape = RenderingServer.FOG_VOLUME_SHAPE_ELLIPSOID
		fog.size = Vector3(1, 1, 1)
		fog.visible = false
		var fog_mat = FogMaterial.new()
		fog_mat.density = 0.0
		fog_mat.albedo = Color.WHITE
		fog_mat.emission = Color(0.3, 0.3, 0.3)
		fog_mat.edge_fade = 0.5
		fog.material = fog_mat
		add_child(fog)
		_gas_volumes[i] = fog
		_gas_materials[i] = fog_mat
		_gas_active[i] = false
		_gas_timers[i] = 0.0


func spawn_gas_cloud(pos: Vector3, radius: float, gas_type: int) -> void:
	## Spawns a persistent gas cloud FogVolume at the given position.
	if not is_inside_tree():
		return
	if not _gas_initialized:
		_init_gas_pool()

	var slot: int = _gas_next
	_gas_next = (_gas_next + 1) % GAS_CLOUD_POOL_SIZE

	var fog: FogVolume = _gas_volumes[slot]
	var fog_mat: FogMaterial = _gas_materials[slot]

	# Get gas-type-specific visual profile
	var prof: Dictionary = _get_gas_color(gas_type)
	var target_density: float = prof["density"]
	var gas_color: Color = prof["color"]
	var gas_emission: Color = prof["emission"]

	var lifetime: float = GAS_CLOUD_LIFETIMES[clampi(gas_type, 0, 3)]
	if lifetime <= 0.0:
		return

	# Configure fog volume
	var fog_size = Vector3(radius * 2.5, radius * 1.5, radius * 2.5)
	fog.size = Vector3(0.5, 0.5, 0.5)  # Start small, expand
	fog.global_position = pos + Vector3(0, radius * 0.3, 0)
	fog.visible = true

	# Configure material
	fog_mat.density = 0.0
	fog_mat.albedo = gas_color
	fog_mat.emission = gas_emission
	fog_mat.edge_fade = 0.5

	_gas_active[slot] = true
	_gas_timers[slot] = lifetime

	# Animate: expand rapidly, sustain, then fade
	var tween = create_tween()
	# Phase 1: Rapid expansion (0.8s)
	tween.tween_property(fog_mat, "density", target_density, 0.8)\
		.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tween.parallel().tween_property(fog, "size", fog_size, 1.0)\
		.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	# Phase 2: Slow drift with wind (sustain)
	var drift_size = fog_size * 1.4
	drift_size.y *= 1.2
	var sustain_time = lifetime - 5.0
	if sustain_time > 0.0:
		tween.tween_property(fog, "size", drift_size, sustain_time)\
			.set_trans(Tween.TRANS_LINEAR)
		# Drift eastward (wind direction)
		tween.parallel().tween_property(fog, "position:x",
			fog.position.x + sustain_time * 0.5, sustain_time)
		# Rise slightly
		tween.parallel().tween_property(fog, "position:y",
			fog.position.y + radius * 0.5, sustain_time)
	# Phase 3: Fade out (last 5s)
	tween.tween_property(fog_mat, "density", 0.0, 5.0)\
		.set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN)
	tween.tween_callback(func():
		fog.visible = false
		_gas_active[slot] = false
	)


# ═══════════════════════════════════════════════════════════════════════
#  Decal Pool (Phase 3A — bullet holes, scorch marks, blood)
# ═══════════════════════════════════════════════════════════════════════

func _create_radial_gradient(center_color: Color, edge_color: Color, size: int = 96) -> GradientTexture2D:
	var tex = GradientTexture2D.new()
	var grad = Gradient.new()
	grad.set_color(0, center_color)
	grad.set_color(1, edge_color)
	tex.gradient = grad
	tex.fill = GradientTexture2D.FILL_RADIAL
	tex.fill_from = Vector2(0.5, 0.5)
	tex.fill_to = Vector2(1.0, 0.5)
	tex.width = size
	tex.height = size
	return tex


func _init_decal_pool() -> void:
	if _decal_initialized:
		return
	_decal_initialized = true

	# Pre-bake textures (once)
	_decal_tex_bullet = _create_radial_gradient(
		Color(0.15, 0.12, 0.10, 0.85),
		Color(0.25, 0.22, 0.18, 0.0),
		64
	)
	_decal_tex_scorch = _create_radial_gradient(
		Color(0.14, 0.11, 0.08, 0.8),
		Color(0.05, 0.04, 0.03, 0.0),
		96
	)
	_decal_tex_blood = _create_radial_gradient(
		Color(0.45, 0.04, 0.04, 0.95),
		Color(0.18, 0.0, 0.0, 0.0),
		96
	)

	_decals.resize(DECAL_POOL_SIZE)
	_decal_active.resize(DECAL_POOL_SIZE)
	_decal_timers.resize(DECAL_POOL_SIZE)
	_decal_fade_start.resize(DECAL_POOL_SIZE)
	_decal_base_alpha.resize(DECAL_POOL_SIZE)
	for i in DECAL_POOL_SIZE:
		var d = Decal.new()
		d.size = Vector3(1, 0.5, 1)
		d.cull_mask = 0xFFFFFFFF
		d.upper_fade = 0.1
		d.lower_fade = 0.2
		d.distance_fade_enabled = true
		d.distance_fade_begin = 40.0
		d.distance_fade_length = 10.0
		d.visible = false
		add_child(d)
		_decals[i] = d
		_decal_active[i] = false
		_decal_timers[i] = 0.0
		_decal_fade_start[i] = 0.0
		_decal_base_alpha[i] = 0.0


func _acquire_decal_slot() -> int:
	## Returns the next decal slot (round-robin). Recycles oldest if full.
	var slot: int = _decal_next
	_decal_next = (_decal_next + 1) % DECAL_POOL_SIZE
	return slot


## Spawns a bullet hole decal at the impact point.
## mat_id: voxel material that was hit (for color tinting).
## normal: surface normal for decal orientation.
func spawn_decal_bullet_hole(pos: Vector3, mat_id: int, normal: Vector3 = Vector3.UP) -> void:
	if not is_inside_tree():
		return
	if not _decal_initialized:
		_init_decal_pool()

	# Distance cull
	var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
	if cam and pos.distance_squared_to(cam.global_position) > 10000.0:  # 100m
		return

	var slot: int = _acquire_decal_slot()
	var d: Decal = _decals[slot]
	var hole_size: float = randf_range(0.06, 0.14)
	d.size = Vector3(hole_size, 0.4, hole_size)

	# Tint based on material — darker version of material color
	var mat_color: Color = MATERIAL_COLORS[clampi(mat_id, 0, MATERIAL_COLORS.size() - 1)]
	d.modulate = mat_color.darkened(0.6)
	d.modulate.a = randf_range(0.5, 0.8)
	d.texture_albedo = _decal_tex_bullet

	# Orient along surface normal
	d.global_position = pos + normal * 0.02
	if normal.abs().dot(Vector3.UP) > 0.9:
		d.rotation = Vector3(0, randf_range(0.0, TAU), 0)
	else:
		d.look_at(pos + normal, Vector3.UP)
		d.rotate_object_local(Vector3.RIGHT, PI * 0.5)
		d.rotate_object_local(Vector3.UP, randf_range(0.0, TAU))

	d.visible = true
	_decal_active[slot] = true
	_decal_timers[slot] = DECAL_BULLET_LIFETIME
	_decal_fade_start[slot] = 5.0  # Start fading 5s before expiry
	_decal_base_alpha[slot] = d.modulate.a


## Spawns a scorch mark decal from an explosion.
func spawn_decal_scorch(pos: Vector3, radius: float) -> void:
	if not is_inside_tree():
		return
	if not _decal_initialized:
		_init_decal_pool()

	var slot: int = _acquire_decal_slot()
	var d: Decal = _decals[slot]
	var scorch_size: float = clampf(radius * 0.7, 1.4, 7.0)
	d.size = Vector3(scorch_size, 0.8, scorch_size)
	d.modulate = Color(0.08, 0.07, 0.06, 0.52)
	d.texture_albedo = _decal_tex_scorch
	d.global_position = pos + Vector3(0, 0.04, 0)
	d.rotation = Vector3(0, randf_range(0.0, TAU), 0)
	d.visible = true
	_decal_active[slot] = true
	_decal_timers[slot] = DECAL_SCORCH_LIFETIME
	_decal_fade_start[slot] = 8.0
	_decal_base_alpha[slot] = d.modulate.a


## Spawns a blood pool decal on the ground at a death location.
func spawn_decal_blood(pos: Vector3) -> void:
	if not is_inside_tree():
		return
	if not _decal_initialized:
		_init_decal_pool()

	var slot: int = _acquire_decal_slot()
	var d: Decal = _decals[slot]
	var pool_size: float = randf_range(1.2, 2.1)
	d.size = Vector3(pool_size, 0.6, pool_size)
	d.modulate = Color(0.32, 0.02, 0.02, 0.68)
	d.texture_albedo = _decal_tex_blood
	d.global_position = pos + Vector3(0, 0.05, 0)
	d.rotation = Vector3(0, randf_range(0.0, TAU), 0)
	d.visible = true
	_decal_active[slot] = true
	_decal_timers[slot] = DECAL_BLOOD_LIFETIME
	_decal_fade_start[slot] = 6.0
	_decal_base_alpha[slot] = d.modulate.a


# ── Footstep Dust Pool (Phase 5B — terrain interaction VFX) ─────────────
const FOOTSTEP_POOL_SIZE: int = 32
const FOOTSTEP_LIFETIME: float = 0.6
const FOOTSTEP_SAMPLE_INTERVAL: float = 0.4  ## (unused — throttling done in camera script)
const FOOTSTEP_MAX_DIST_SQ: float = 40000.0  ## 200m squared — cull beyond this

var _footstep_particles: Array[GPUParticles3D] = []
var _footstep_active: Array[bool] = []
var _footstep_timers: Array[float] = []
var _footstep_next: int = 0
var _footstep_initialized: bool = false
var _footstep_sample_timer: float = 0.0  ## (unused — kept for potential future use in effects.gd)

## Material-keyed dust colors for footstep puffs (mirrors MATERIAL_COLORS indices).
const FOOTSTEP_DUST_COLORS: Dictionary = {
	1:  Color(0.45, 0.35, 0.22, 0.4),   # DIRT — brown
	2:  Color(0.50, 0.48, 0.45, 0.35),  # STONE — gray
	5:  Color(0.65, 0.63, 0.60, 0.35),  # CONCRETE — light gray
	8:  Color(0.72, 0.65, 0.45, 0.45),  # SAND — tan
	10: Color(0.35, 0.45, 0.28, 0.3),   # GRASS — green-gray
	11: Color(0.55, 0.52, 0.48, 0.35),  # GRAVEL — gray-brown
	13: Color(0.55, 0.42, 0.30, 0.4),   # CLAY — red-brown
}
const FOOTSTEP_DEFAULT_COLOR: Color = Color(0.50, 0.45, 0.38, 0.35)


func _init_footstep_pool() -> void:
	for i in FOOTSTEP_POOL_SIZE:
		var p = GPUParticles3D.new()
		var pmat = ParticleProcessMaterial.new()
		pmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
		pmat.emission_sphere_radius = 0.15
		pmat.direction = Vector3.UP
		pmat.spread = 160.0
		pmat.initial_velocity_min = 0.3
		pmat.initial_velocity_max = 0.8
		pmat.gravity = Vector3(0, -2.0, 0)
		pmat.damping_min = 1.0
		pmat.damping_max = 2.0
		pmat.scale_min = 0.08
		pmat.scale_max = 0.18
		pmat.color = FOOTSTEP_DEFAULT_COLOR
		p.process_material = pmat
		p.amount = 4
		p.lifetime = 0.5
		p.one_shot = true
		p.explosiveness = 1.0
		p.emitting = false
		p.visible = false
		p.draw_pass_1 = _make_particle_quad(0.18, FOOTSTEP_DEFAULT_COLOR)
		add_child(p)
		_footstep_particles.append(p)
		_footstep_active.append(false)
		_footstep_timers.append(0.0)
	_footstep_initialized = true


## Spawn footstep dust at a unit's feet. mat_id determines dust color.
func spawn_footstep_dust(pos: Vector3, mat_id: int, is_sprinting: bool = false) -> void:
	if not is_inside_tree():
		return
	if not _footstep_initialized:
		_init_footstep_pool()

	# Distance cull
	var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
	if cam and pos.distance_squared_to(cam.global_position) > FOOTSTEP_MAX_DIST_SQ:
		return

	var slot: int = _footstep_next
	_footstep_next = (_footstep_next + 1) % FOOTSTEP_POOL_SIZE

	var dust_color: Color = FOOTSTEP_DUST_COLORS.get(mat_id, FOOTSTEP_DEFAULT_COLOR)
	var p: GPUParticles3D = _footstep_particles[slot]
	var pmat: ParticleProcessMaterial = p.process_material as ParticleProcessMaterial
	pmat.color = dust_color

	# Sprint = wider, more particles
	if is_sprinting:
		pmat.initial_velocity_max = 1.5
		pmat.emission_sphere_radius = 0.25
		p.amount = 6
	else:
		pmat.initial_velocity_max = 0.8
		pmat.emission_sphere_radius = 0.15
		p.amount = 4

	p.global_position = pos
	p.visible = true
	p.emitting = true
	_footstep_active[slot] = true
	_footstep_timers[slot] = FOOTSTEP_LIFETIME


## Batch spawn footstep dust for moving units. Called from camera script.
## positions: PackedVector3Array of moving unit feet positions.
## mat_ids: PackedByteArray of ground material at each position.
## sprinting: PackedByteArray (0/1) whether unit is running.
func spawn_footstep_batch(positions: PackedVector3Array, mat_ids: PackedByteArray, sprinting: PackedByteArray) -> void:
	var count: int = mini(positions.size(), mini(mat_ids.size(), sprinting.size()))
	for i in count:
		spawn_footstep_dust(positions[i], mat_ids[i], sprinting[i] != 0)


## Tick footstep pool — hide expired particles.
func update_footstep_pool(delta: float) -> void:
	if not _footstep_initialized:
		return
	for i in FOOTSTEP_POOL_SIZE:
		if not _footstep_active[i]:
			continue
		_footstep_timers[i] -= delta
		if _footstep_timers[i] <= 0.0:
			_footstep_active[i] = false
			_footstep_particles[i].visible = false
			_footstep_particles[i].emitting = false


# ── Fire Smoke Pool (Phase 5C — rising smoke from flammable destruction) ──
const FIRE_SMOKE_POOL_SIZE: int = 16
const FIRE_SMOKE_LIFETIME: float = 20.0

var _fire_smoke_particles: Array[GPUParticles3D] = []
var _fire_smoke_active: Array[bool] = []
var _fire_smoke_timers: Array[float] = []
var _fire_smoke_next: int = 0
var _fire_smoke_initialized: bool = false


func _init_fire_smoke_pool() -> void:
	for i in FIRE_SMOKE_POOL_SIZE:
		var p = GPUParticles3D.new()
		var pmat = ParticleProcessMaterial.new()
		pmat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
		pmat.emission_sphere_radius = 0.3
		pmat.direction = Vector3.UP
		pmat.spread = 15.0
		pmat.initial_velocity_min = 0.8
		pmat.initial_velocity_max = 1.8
		pmat.gravity = Vector3(0, 0.4, 0)  # Buoyancy — smoke rises
		pmat.damping_min = 0.2
		pmat.damping_max = 0.5
		pmat.scale_min = 0.4
		pmat.scale_max = 1.0
		pmat.color = Color(0.25, 0.23, 0.20, 0.25)
		p.process_material = pmat
		p.amount = 12
		p.lifetime = 4.0
		p.one_shot = false  # Continuous emission
		p.explosiveness = 0.1
		p.emitting = false
		p.visible = false
		p.draw_pass_1 = _make_particle_quad(0.8, Color(0.28, 0.26, 0.24, 0.25))
		add_child(p)
		_fire_smoke_particles.append(p)
		_fire_smoke_active.append(false)
		_fire_smoke_timers.append(0.0)
	_fire_smoke_initialized = true


## Spawn a rising smoke column at a destruction site for flammable materials.
## Called automatically when wood/grass voxels are destroyed.
func spawn_fire_smoke(pos: Vector3, intensity: float = 1.0) -> void:
	if not is_inside_tree():
		return
	if not _fire_smoke_initialized:
		_init_fire_smoke_pool()

	# Distance cull — no smoke beyond 250m
	var cam: Camera3D = get_viewport().get_camera_3d() if get_viewport() else null
	if cam and pos.distance_squared_to(cam.global_position) > 62500.0:
		return

	var slot: int = _fire_smoke_next
	_fire_smoke_next = (_fire_smoke_next + 1) % FIRE_SMOKE_POOL_SIZE

	var p: GPUParticles3D = _fire_smoke_particles[slot]
	var pmat: ParticleProcessMaterial = p.process_material as ParticleProcessMaterial
	pmat.scale_max = 1.0 * intensity
	p.amount = int(clampf(12.0 * intensity, 6, 20))
	p.global_position = pos + Vector3(0, 0.5, 0)
	p.visible = true
	p.emitting = true
	_fire_smoke_active[slot] = true
	_fire_smoke_timers[slot] = FIRE_SMOKE_LIFETIME


## Tick fire smoke pool — fade out and stop expired emitters.
func update_fire_smoke_pool(delta: float) -> void:
	if not _fire_smoke_initialized:
		return
	for i in FIRE_SMOKE_POOL_SIZE:
		if not _fire_smoke_active[i]:
			continue
		_fire_smoke_timers[i] -= delta
		if _fire_smoke_timers[i] <= 0.0:
			_fire_smoke_active[i] = false
			_fire_smoke_particles[i].visible = false
			_fire_smoke_particles[i].emitting = false
		elif _fire_smoke_timers[i] < 5.0:
			# Fade out over last 5 seconds
			var fade_t: float = _fire_smoke_timers[i] / 5.0
			var pmat: ParticleProcessMaterial = _fire_smoke_particles[i].process_material as ParticleProcessMaterial
			pmat.color.a = 0.25 * fade_t


## Tick decal pool — fade out and recycle expired decals.
func update_decal_pool(delta: float) -> void:
	if not _decal_initialized:
		return
	for i in DECAL_POOL_SIZE:
		if not _decal_active[i]:
			continue
		_decal_timers[i] -= delta
		if _decal_timers[i] <= 0.0:
			_decal_active[i] = false
			_decals[i].visible = false
		elif _decal_timers[i] < _decal_fade_start[i]:
			# Gradual alpha fade during final seconds (uses stored base alpha)
			var fade_t: float = _decal_timers[i] / _decal_fade_start[i]
			var c: Color = _decals[i].modulate
			c.a = _decal_base_alpha[i] * fade_t
			_decals[i].modulate = c
