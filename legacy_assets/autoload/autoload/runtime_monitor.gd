extends Node

const SERVER_URL := "ws://127.0.0.1:6800"
const RECONNECT_INTERVAL := 1.0
const CAPTURE_INTERVAL := 0.5  # 2 FPS — sufficient for debugging, low overhead
const MAX_WIDTH := 400  # Keep frames small for WebSocket transport
const JPEG_QUALITY := 0.6  # 60% quality — good enough for debugging, ~30KB per frame

var _ws := WebSocketPeer.new()
var _connected := false
var _time_since_last_capture := 0.0
var _reconnect_timer := 0.0

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_connect_to_server()

func _process(delta: float) -> void:
	_ws.poll()
	var state = _ws.get_ready_state()

	if state == WebSocketPeer.STATE_OPEN:
		if not _connected:
			_connected = true
			print("[RuntimeMonitor] Connected to Antigravity plugin")

		# Handle incoming packets
		while _ws.get_available_packet_count() > 0:
			var packet = _ws.get_packet()
			var packet_text = packet.get_string_from_utf8()
			if packet_text:
				var json = JSON.new()
				if json.parse(packet_text) == OK:
					_handle_packet(json.data)

		_time_since_last_capture += delta
		if _time_since_last_capture >= CAPTURE_INTERVAL:
			_time_since_last_capture = 0.0
			_capture_and_send()

	elif state == WebSocketPeer.STATE_CLOSED or state == WebSocketPeer.STATE_CLOSING:
		if _connected:
			_connected = false
			print("[RuntimeMonitor] Disconnected from Antigravity plugin")

		_reconnect_timer += delta
		if _reconnect_timer >= RECONNECT_INTERVAL:
			_reconnect_timer = 0.0
			_connect_to_server()

func _handle_packet(data: Dictionary) -> void:
	var method = data.get("method", "")
	var params = data.get("params", {})
	
	match method:
		"set_camera":
			_set_camera(params)


func _set_camera(params: Dictionary) -> void:
	var cam = get_viewport().get_camera_3d()
	if not cam:
		return
		
	# Check if it's our RTS camera script
	if "voxel_test_camera.gd" in cam.get_script().resource_path:
		if "height" in params:
			cam._rts_height = float(params["height"])
			cam._rts_height_target = cam._rts_height
			
		if "yaw" in params:
			cam._rts_yaw = float(params["yaw"])
			
		if "focus" in params:
			var f = params["focus"]
			if f is Dictionary:
				cam._rts_focus = Vector3(f.get("x", 0), f.get("y", 0), f.get("z", 0))
				cam._rts_drag_start_focus = cam._rts_focus

func _connect_to_server() -> void:
	if _ws.get_ready_state() == WebSocketPeer.STATE_CLOSED:
		# Set large outbound buffer before connecting (4 MB — fits several frames)
		_ws.outbound_buffer_size = 1 << 22
		_ws.connect_to_url(SERVER_URL)

func _collect_metrics() -> Dictionary:
	var cam = get_viewport().get_camera_3d()
	if not cam or not cam.get_script():
		return {}
	if not "voxel_test_camera.gd" in cam.get_script().resource_path:
		return {}
	if not cam.has_method("get") and not ("_sim" in cam):
		return {}
	var sim = cam.get("_sim")
	if not sim or not sim.has_method("get_debug_stats"):
		return {}

	var stats: Dictionary = sim.get_debug_stats()
	var total_units: int = sim.get_unit_count() if sim.has_method("get_unit_count") else 0
	var alive: int = stats.get("alive_units", 0)
	var t1_pts: int = sim.get_capture_count_for_team(1) if sim.has_method("get_capture_count_for_team") else 0
	var t2_pts: int = sim.get_capture_count_for_team(2) if sim.has_method("get_capture_count_for_team") else 0
	var t1_alive: int = sim.get_alive_count_for_team(1) if sim.has_method("get_alive_count_for_team") else 0
	var t2_alive: int = sim.get_alive_count_for_team(2) if sim.has_method("get_alive_count_for_team") else 0

	return {
		"alive_units": alive,
		"dead_units": total_units - alive,
		"total_units": total_units,
		"berserk": stats.get("berserk_units", 0),
		"frozen": stats.get("frozen_units", 0),
		"paranoid_ff": stats.get("paranoid_ff_units", 0),
		"wall_pen_voxels": stats.get("wall_pen_voxels", 0),
		"tick_ms": stats.get("tick_ms", 0.0),
		"active_projectiles": stats.get("active_projectiles", 0),
		"t1_alive": t1_alive,
		"t2_alive": t2_alive,
		"t1_capture_pts": t1_pts,
		"t2_capture_pts": t2_pts,
		"vis_team1": stats.get("vis_team1", 0),
		"vis_team2": stats.get("vis_team2", 0),
	}


func _capture_and_send() -> void:
	var viewport = get_viewport()
	if not viewport:
		return

	var tex = viewport.get_texture()
	if not tex:
		return

	var img = tex.get_image()
	if not img:
		return

	# Resize for bandwidth — 400px wide is plenty for debugging
	if img.get_width() > MAX_WIDTH:
		var sc = float(MAX_WIDTH) / img.get_width()
		img.resize(MAX_WIDTH, int(img.get_height() * sc))

	# JPEG is ~10x smaller than PNG for screenshots
	var jpg_bytes = img.save_jpg_to_buffer(JPEG_QUALITY)
	var base64 = Marshalls.raw_to_base64(jpg_bytes)

	var params = {
		"width": img.get_width(),
		"height": img.get_height(),
		"format": "jpeg",
		"data": base64,
		"timestamp": Time.get_unix_time_from_system()
	}

	# Piggyback sim metrics on every frame
	var metrics = _collect_metrics()
	if metrics.size() > 0:
		params["metrics"] = metrics

	var message = {
		"jsonrpc": "2.0",
		"method": "store_game_viewport",
		"params": params,
		"id": null  # Notification — no response expected
	}

	_ws.send_text(JSON.stringify(message))
