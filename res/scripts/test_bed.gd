extends Node3D

## ────────────────────────────────────────────────────────────
## TestBed — M6 Battalion Rendering + Cavalry
##
## Two battalions + batteries + cavalry.
## Press V to toggle between legacy and battalion rendering.
## Press C for cavalry charge.
## ────────────────────────────────────────────────────────────

@onready var server = $MusketServer

# Legacy rendering (Strangler Fig — old path)
var legacy_mm_instance: MultiMeshInstance3D
var use_legacy := false  # Toggle with V key

# Battalion rendering (new path)
var battalion_mms: Dictionary = {}  # battalion_id -> MultiMeshInstance3D

# Projectile rendering
var proj_mm_instance: MultiMeshInstance3D

var hud_timer := 0.0
var last_f := false
var last_g := false
var last_b := false
var last_n := false
var last_l := false
var last_c := false
var last_v := false
var last_s := false
var blue_limbered := false
var scale_spawned := false

const BLUE_COUNT := 200
const RED_COUNT := 200
const CAVALRY_COUNT := 20
const BLUE_CENTER := Vector3(0, 0, -20)
const RED_CENTER := Vector3(0, 0, 20)
const BLUE_BATTERY := Vector3(-30, 0, -30)
const RED_BATTERY := Vector3(30, 0, 30)
const CAVALRY_POS := Vector3(-40, 0, -10)

# Shared mesh + material for all soldier MultiMeshes
var soldier_mesh: CapsuleMesh
var soldier_mat: ShaderMaterial


func _ready() -> void:
	# Create shared mesh with team color shader
	soldier_mesh = CapsuleMesh.new()
	soldier_mesh.radius = 0.3
	soldier_mesh.height = 1.8
	soldier_mat = ShaderMaterial.new()
	soldier_mat.shader = load("res://res/shaders/team_color.gdshader")
	soldier_mesh.material = soldier_mat

	# ── Spawn units ──
	server.spawn_test_battalion(BLUE_COUNT, BLUE_CENTER.x, BLUE_CENTER.z, 0)
	server.spawn_test_battalion(RED_COUNT, RED_CENTER.x, RED_CENTER.z, 1)
	server.spawn_test_battery(6, BLUE_BATTERY.x, BLUE_BATTERY.z, 0)
	server.spawn_test_battery(6, RED_BATTERY.x, RED_BATTERY.z, 1)
	server.spawn_test_cavalry(CAVALRY_COUNT, CAVALRY_POS.x, CAVALRY_POS.z, 0)

	# ── Legacy MultiMesh (hidden by default) ──
	legacy_mm_instance = MultiMeshInstance3D.new()
	var legacy_mm := MultiMesh.new()
	legacy_mm.transform_format = MultiMesh.TRANSFORM_3D
	legacy_mm.use_colors = true
	legacy_mm.instance_count = 0
	legacy_mm.mesh = soldier_mesh
	legacy_mm_instance.multimesh = legacy_mm
	legacy_mm_instance.visible = use_legacy
	add_child(legacy_mm_instance)

	# ── Projectile MultiMesh ──
	proj_mm_instance = MultiMeshInstance3D.new()
	var proj_mm := MultiMesh.new()
	proj_mm.transform_format = MultiMesh.TRANSFORM_3D
	proj_mm.use_colors = true
	proj_mm.instance_count = 0
	var sphere := SphereMesh.new()
	sphere.radius = 0.25
	sphere.height = 0.5
	var proj_mat := StandardMaterial3D.new()
	proj_mat.vertex_color_use_as_albedo = true
	sphere.material = proj_mat
	proj_mm.mesh = sphere
	proj_mm_instance.multimesh = proj_mm
	add_child(proj_mm_instance)

	print("═══════════════════════════════════════════════")
	print("  M6 BATTALION RENDERING + CAVALRY TEST")
	print("  [F] France muskets   [G] Russia muskets")
	print("  [B] France artillery [N] Russia artillery")
	print("  [L] Toggle limber    [C] Cavalry charge!")
	print("  [V] Toggle legacy/battalion rendering")
	print("  [1] March z+50  [2] March z-50  [3] Halt")
	print("  [S] SCALE TEST — spawn 5,000+ units")
	print("═══════════════════════════════════════════════")


func _create_battalion_mm(bat_id: int, instance_count: int) -> void:
	var mmi := MultiMeshInstance3D.new()
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_custom_data = true
	mm.use_colors = false  # Colors via custom_data — keeps stride at 16 floats
	mm.instance_count = instance_count
	mm.mesh = soldier_mesh
	mmi.multimesh = mm
	add_child(mmi)
	battalion_mms[bat_id] = mmi


func _process(delta: float) -> void:
	# ── Input: musket fire ──
	var f_now := Input.is_physical_key_pressed(KEY_F)
	if f_now and not last_f:
		server.order_fire(0, RED_CENTER.x, RED_CENTER.z)
	last_f = f_now

	var g_now := Input.is_physical_key_pressed(KEY_G)
	if g_now and not last_g:
		server.order_fire(1, BLUE_CENTER.x, BLUE_CENTER.z)
	last_g = g_now

	# ── Input: artillery fire ──
	var b_now := Input.is_physical_key_pressed(KEY_B)
	if b_now and not last_b:
		server.order_artillery_fire(0, RED_CENTER.x, RED_CENTER.z)
		print("[ARTILLERY] France battery firing!")
	last_b = b_now

	var n_now := Input.is_physical_key_pressed(KEY_N)
	if n_now and not last_n:
		server.order_artillery_fire(1, BLUE_CENTER.x, BLUE_CENTER.z)
		print("[ARTILLERY] Russia battery firing!")
	last_n = n_now

	# ── Input: limber toggle ──
	var l_now := Input.is_physical_key_pressed(KEY_L)
	if l_now and not last_l:
		if blue_limbered:
			server.order_unlimber(0)
			blue_limbered = false
			print("[ARTILLERY] France UNLIMBERING...")
		else:
			server.order_limber(0)
			blue_limbered = true
			print("[ARTILLERY] France LIMBERED")
	last_l = l_now

	# ── Input: cavalry charge ──
	var c_now := Input.is_physical_key_pressed(KEY_C)
	if c_now and not last_c:
		server.order_charge(0, RED_CENTER.x, RED_CENTER.z)
		print("[CAVALRY] CHARGE!!! France cavalry attacking!")
	last_c = c_now

	# ── Input: SCALE TEST ──
	var s_now := Input.is_physical_key_pressed(KEY_S)
	if s_now and not last_s and not scale_spawned:
		_spawn_scale_test()
		scale_spawned = true
	last_s = s_now

	# ── Input: toggle rendering mode ──
	var v_now := Input.is_physical_key_pressed(KEY_V)
	if v_now and not last_v:
		use_legacy = not use_legacy
		legacy_mm_instance.visible = use_legacy
		for mmi in battalion_mms.values():
			mmi.visible = not use_legacy
		print("[RENDER] Mode: %s" % ("LEGACY" if use_legacy else "BATTALION"))
	last_v = v_now

	# ── Input: march ──
	if Input.is_physical_key_pressed(KEY_1):
		server.order_march(0.0, 50.0)
	if Input.is_physical_key_pressed(KEY_2):
		server.order_march(0.0, -50.0)
	if Input.is_physical_key_pressed(KEY_3):
		server.order_march(0.0, 0.0)

	# ═══════════════════════════════════════════════════════════
	# RENDERING: Battalion path (new — O(B) where B=battalions)
	# ═══════════════════════════════════════════════════════════
	if not use_legacy:
		var active_bats = server.get_active_battalions()
		for bat_id in active_bats:
			var inst_count = server.get_battalion_instance_count(bat_id)
			if inst_count == 0:
				continue

			# Create MultiMesh if needed
			if not battalion_mms.has(bat_id):
				_create_battalion_mm(bat_id, inst_count)

			var mmi: MultiMeshInstance3D = battalion_mms[bat_id]
			var mm := mmi.multimesh

			# Resize if battalion grew
			if mm.instance_count != inst_count:
				mm.instance_count = inst_count

			# ONE bulk upload — zero-copy from C++ shadow buffer
			var buffer = server.get_battalion_buffer(bat_id)
			if buffer.size() > 0:
				RenderingServer.multimesh_set_buffer(mm.get_rid(), buffer)

	# ═══════════════════════════════════════════════════════════
	# RENDERING: Legacy path (Strangler Fig — remove after verify)
	# ═══════════════════════════════════════════════════════════
	if use_legacy:
		var buf = server.get_transform_buffer()
		var count = server.get_visible_count()

		var mm := legacy_mm_instance.multimesh
		if count != mm.instance_count:
			mm.instance_count = count

		for i in range(count):
			var o := i * 16
			if o + 15 >= buf.size():
				break
			var t := Transform3D()
			t.basis.x = Vector3(buf[o+0], buf[o+4], buf[o+8])
			t.basis.y = Vector3(buf[o+1], buf[o+5], buf[o+9])
			t.basis.z = Vector3(buf[o+2], buf[o+6], buf[o+10])
			t.origin  = Vector3(buf[o+3], buf[o+7], buf[o+11])
			mm.set_instance_transform(i, t)

			var team_id := int(buf[o + 13])
			if team_id == 0:
				mm.set_instance_color(i, Color(0.2, 0.3, 0.85))
			else:
				mm.set_instance_color(i, Color(0.85, 0.2, 0.2))

	# ── Projectile rendering ──
	var proj_buf = server.get_projectile_buffer()
	var proj_count = server.get_projectile_count()
	if proj_mm_instance != null and proj_mm_instance.multimesh != null:
		var pmm := proj_mm_instance.multimesh
		if proj_count != pmm.instance_count:
			pmm.instance_count = proj_count
		for i in range(proj_count):
			var po := i * 4
			if po + 3 >= proj_buf.size():
				break
			var pt := Transform3D()
			pt.origin = Vector3(proj_buf[po+0], proj_buf[po+1], proj_buf[po+2])
			pmm.set_instance_transform(i, pt)
			pmm.set_instance_color(i, Color(0.15, 0.12, 0.1))

	# ── HUD ──
	hud_timer += delta
	if hud_timer >= 2.0:
		hud_timer = 0.0
		var blue_alive = server.get_alive_count(0)
		var red_alive = server.get_alive_count(1)
		var total_alive = blue_alive + red_alive
		var fps := Engine.get_frames_per_second()
		var frame_ms := delta * 1000.0
		var mode := "LEGACY" if use_legacy else "BATTALION"
		print("[HUD] FPS:%d (%.1fms) | %s | Blue:%d Red:%d | Total:%d | Proj:%d" % [
			fps, frame_ms, mode, blue_alive, red_alive, total_alive, proj_count])


## ── Scale Test: spawn 5,000+ units ──────────────────────
func _spawn_scale_test() -> void:
	print("═══════════════════════════════════════════════")
	print("  SCALE TEST — spawning 5,000+ entities")
	print("═══════════════════════════════════════════════")

	var t0 := Time.get_ticks_msec()

	# 10 battalions per side, 500 soldiers each = 5,000 per team
	var bat_size := 500
	var bats_per_team := 10
	var spacing := 30.0  # meters between battalion centers

	for i in range(bats_per_team):
		var row := i / 5
		var col := i % 5
		var bx := (col - 2) * spacing
		var bz_blue := -50.0 - row * spacing
		var bz_red := 50.0 + row * spacing

		server.spawn_test_battalion(bat_size, bx, bz_blue, 0)
		server.spawn_test_battalion(bat_size, bx, bz_red, 1)

	# Extra cavalry per side
	server.spawn_test_cavalry(100, -60.0, -40.0, 0)
	server.spawn_test_cavalry(100, 60.0, 40.0, 1)

	# Extra batteries
	server.spawn_test_battery(6, -50.0, -60.0, 0)
	server.spawn_test_battery(6, 50.0, 60.0, 1)

	var elapsed := Time.get_ticks_msec() - t0
	var total := bats_per_team * bat_size * 2 + 200 + 12 + 420  # scale + existing
	print("═══════════════════════════════════════════════")
	print("  SCALE TEST COMPLETE: ~%d entities in %dms" % [total, elapsed])
	print("  Watch FPS in HUD to verify performance")
	print("═══════════════════════════════════════════════")

