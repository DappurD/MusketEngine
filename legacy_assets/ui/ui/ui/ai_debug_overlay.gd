extends Node3D
class_name AIDebugOverlay

var unit: Unit = null
var force_visible: bool = false

var _state_label: Label3D = null
var _lines_mesh: MeshInstance3D = null
var _line_material: StandardMaterial3D = null
var _line_immediate: ImmediateMesh = null
var _move_marker: MeshInstance3D = null
var _logged_invalid_overlay_once: bool = false

const UPDATE_INTERVAL: float = 0.30
var _update_timer: float = 0.0


func setup(p_unit: Unit) -> void:
	unit = p_unit

	_state_label = Label3D.new()
	_state_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_state_label.no_depth_test = true
	_state_label.fixed_size = false
	_state_label.font_size = 6
	_state_label.outline_size = 1
	_state_label.pixel_size = 0.004
	_state_label.position = Vector3(0, 2.9, 0)
	_state_label.modulate = Color(1.0, 0.95, 0.7, 0.9)
	add_child(_state_label)

	_line_immediate = ImmediateMesh.new()
	_lines_mesh = MeshInstance3D.new()
	_lines_mesh.mesh = _line_immediate
	_lines_mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_lines_mesh)

	_line_material = StandardMaterial3D.new()
	_line_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_line_material.vertex_color_use_as_albedo = true
	_line_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_lines_mesh.material_override = _line_material

	_move_marker = MeshInstance3D.new()
	var marker_mesh = SphereMesh.new()
	marker_mesh.radius = 0.22
	marker_mesh.height = 0.44
	_move_marker.mesh = marker_mesh
	var marker_mat = StandardMaterial3D.new()
	marker_mat.albedo_color = Color(0.0, 1.0, 1.0, 0.45)
	marker_mat.emission_enabled = true
	marker_mat.emission = Color(0.0, 1.0, 1.0)
	marker_mat.emission_energy_multiplier = 1.8
	marker_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	marker_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_move_marker.material_override = marker_mat
	add_child(_move_marker)

	_set_debug_visible(false)


func set_force_visible(enabled: bool) -> void:
	force_visible = enabled


func _process(delta: float) -> void:
	if not is_instance_valid(unit):
		queue_free()
		return

	var show_debug: bool = force_visible or unit.selected
	_set_debug_visible(show_debug)
	if not show_debug:
		return

	_update_timer -= delta
	if _update_timer <= 0.0:
		_update_timer = UPDATE_INTERVAL
		_refresh_overlay()


func _set_debug_visible(enabled: bool) -> void:
	var show_label: bool = enabled and unit != null and unit.selected
	if _state_label:
		_state_label.visible = show_label
	if _lines_mesh:
		_lines_mesh.visible = enabled
	if _move_marker:
		_move_marker.visible = false if not enabled else _move_marker.visible


func _refresh_overlay() -> void:
	if not is_instance_valid(unit.ai):
		return
	if not unit.global_transform.origin.is_finite() or not unit.global_transform.basis.x.is_finite() or not unit.global_transform.basis.y.is_finite() or not unit.global_transform.basis.z.is_finite():
		if not _logged_invalid_overlay_once:
			_logged_invalid_overlay_once = true
			push_warning("[AIDebugOverlay] Unit transform non-finite for %s; skipping overlay draw." % unit.name)
		_line_immediate.clear_surfaces()
		_move_marker.visible = false
		return

	# Show the held action (stable, changes only on real decision switches)
	# plus a brief FSM hint when it differs meaningfully.
	var action_text: String = unit.ai.last_decision_action
	var fsm_name: String = _get_short_state_name(unit.ai.get_state_name())
	# Only show the FSM state when it's informatively different from the action.
	var show_fsm: bool = false
	match action_text:
		"engage":
			show_fsm = fsm_name != "ENGAGING" and fsm_name != "IN_COVER" and fsm_name != "PEEKING" and fsm_name != "MOVE_COVER"
		"take_cover":
			show_fsm = fsm_name != "IN_COVER" and fsm_name != "PEEKING" and fsm_name != "MOVE_COVER"
		"follow_squad":
			show_fsm = fsm_name != "MOVE_SQUAD"
		"flank":
			show_fsm = fsm_name != "FLANKING" and fsm_name != "ENGAGING"
		"retreat":
			show_fsm = fsm_name != "FALLING_BACK"
		_:
			show_fsm = false
	if show_fsm:
		_state_label.text = "%s (%s)" % [action_text, fsm_name]
	else:
		_state_label.text = action_text
	var tags: Array[String] = []
	if unit.assigned_cover_node != null and is_instance_valid(unit.assigned_cover_node):
		var slot_txt: String = "slot:%d" % unit.assigned_cover_slot_id if unit.assigned_cover_slot_id >= 0 else "slot:none"
		tags.append(slot_txt)
	if unit.squad != null and is_instance_valid(unit.squad):
		if unit.squad.has_method("get_debug_archetype_name"):
			var archetype: String = str(unit.squad.get_debug_archetype_name())
			if archetype != "":
				tags.append("arch:%s" % archetype)
		if unit.squad.has_method("is_recon_ready") and str(unit.squad.get_debug_archetype_name()) == "recon":
			tags.append("recon:%s" % ("ready" if unit.squad.is_recon_ready() else "offline"))
		var traffic: String = str(unit.squad.traffic_state)
		if traffic != "" and traffic != "idle":
			tags.append("traffic:%s" % traffic)
		if unit.squad.has_method("get_tactical_intent_action"):
			var intent_action: String = unit.squad.get_tactical_intent_action()
			if intent_action != "":
				tags.append("intent:%s" % intent_action)
		if unit.squad.has_method("is_debug_forming_up") and unit.squad.is_debug_forming_up():
			var gen: int = unit.squad.get_debug_form_generation() if unit.squad.has_method("get_debug_form_generation") else 0
			tags.append("formup:g%d" % gen)
	if not tags.is_empty():
		_state_label.text += "\n" + " | ".join(PackedStringArray(tags))
	var controller: Node = unit.get_parent()
	if controller != null and controller.has_method("get_ai_metrics_snapshot"):
		var metrics: Dictionary = controller.get_ai_metrics_snapshot()
		var gate_units: int = int(metrics.get("gate_units", 0))
		var gate_pass: bool = bool(metrics.get("gate_pass", true))
		var gate_status: String = "PASS" if gate_pass else "FAIL"
		_state_label.text += "\nAI:%d %s p%.1f e%.1f nq%d sf%.1f" % [
			gate_units,
			gate_status,
			float(metrics.get("planning_ms", 0.0)),
			float(metrics.get("unit_execution_ms", 0.0)),
			int(metrics.get("nav_query_count", 0)),
			float(metrics.get("state_flip_rate", 0.0))
		]

	_line_immediate.clear_surfaces()
	var has_line_segments: bool = false

	var start_high: Vector3 = Vector3(0, 1.45, 0)
	var start_mid: Vector3 = Vector3(0, 0.9, 0)

	# Red: current enemy target
	if is_instance_valid(unit.ai.current_target):
		var target_world: Vector3 = unit.ai.current_target.global_position + Vector3(0, 1.0, 0)
		if target_world.is_finite():
			var target_local: Vector3 = unit.to_local(target_world)
			if target_local.is_finite():
				if not has_line_segments:
					_line_immediate.surface_begin(Mesh.PRIMITIVE_LINES, _line_material)
					has_line_segments = true
				_line_immediate.surface_set_color(Color(1.0, 0.2, 0.2, 0.75))
				_line_immediate.surface_add_vertex(start_high)
				_line_immediate.surface_add_vertex(target_local)

	# Green: selected cover node
	if is_instance_valid(unit.ai.current_cover):
		var cover_world: Vector3 = unit.ai.current_cover.global_position + Vector3(0, 0.3, 0)
		if cover_world.is_finite():
			var cover_local: Vector3 = unit.to_local(cover_world)
			if cover_local.is_finite():
				if not has_line_segments:
					_line_immediate.surface_begin(Mesh.PRIMITIVE_LINES, _line_material)
					has_line_segments = true
				_line_immediate.surface_set_color(Color(0.25, 1.0, 0.25, 0.75))
				_line_immediate.surface_add_vertex(start_mid)
				_line_immediate.surface_add_vertex(cover_local)

	# Cyan: navigation next point + final destination marker
	var should_show_nav: bool = (
		unit.has_squad_order
		or unit.order == Unit.OrderType.MOVE
		or (unit.ai != null and unit.ai.current_state in [
			PawnAI.State.MOVING_TO_SQUAD_POS,
			PawnAI.State.MOVING_TO_COVER,
			PawnAI.State.FLANKING,
			PawnAI.State.FALLING_BACK,
			PawnAI.State.HELPING_ALLY,
			PawnAI.State.BOUNDING_MOVE,
			PawnAI.State.CARRYING_ALLY,
		])
	)
	if should_show_nav and unit.pathfinder != null and not unit.pathfinder.is_navigation_finished():
		var path: PackedVector3Array = unit.pathfinder.get_path()
		var path_idx: int = unit.pathfinder.get_path_index()
		var final_pos: Vector3 = unit.pathfinder.get_target_position()
		# Draw path segments from current waypoint to final target
		if path_idx < path.size():
			var next_pos: Vector3 = path[path_idx]
			if next_pos.is_finite():
				var next_local: Vector3 = unit.to_local(next_pos + Vector3(0, 0.15, 0))
				if next_local.is_finite():
					if not has_line_segments:
						_line_immediate.surface_begin(Mesh.PRIMITIVE_LINES, _line_material)
						has_line_segments = true
					_line_immediate.surface_set_color(Color(0.0, 1.0, 1.0, 0.8))
					_line_immediate.surface_add_vertex(Vector3(0, 0.35, 0))
					_line_immediate.surface_add_vertex(next_local)
			# Draw remaining path segments in faded color
			for i in range(path_idx, path.size() - 1):
				if not path[i].is_finite() or not path[i + 1].is_finite():
					continue
				var p0: Vector3 = unit.to_local(path[i] + Vector3(0, 0.15, 0))
				var p1: Vector3 = unit.to_local(path[i + 1] + Vector3(0, 0.15, 0))
				if not p0.is_finite() or not p1.is_finite():
					continue
				if not has_line_segments:
					_line_immediate.surface_begin(Mesh.PRIMITIVE_LINES, _line_material)
					has_line_segments = true
				_line_immediate.surface_set_color(Color(0.0, 1.0, 1.0, 0.35))
				_line_immediate.surface_add_vertex(p0)
				_line_immediate.surface_add_vertex(p1)

		_move_marker.visible = true
		if final_pos.is_finite():
			var final_local: Vector3 = unit.to_local(final_pos + Vector3(0, 0.2, 0))
			if final_local.is_finite():
				_move_marker.position = final_local
			else:
				_move_marker.visible = false
		else:
			_move_marker.visible = false
	else:
		_move_marker.visible = false

	if has_line_segments:
		_line_immediate.surface_end()


func _get_short_state_name(state_name: String) -> String:
	match state_name:
		"MOVING_TO_SQUAD_POS":
			return "MOVE_SQUAD"
		"MOVING_TO_COVER":
			return "MOVE_COVER"
		"INJECTING_MORPHINE":
			return "MORPHINE"
		"CARRYING_ALLY":
			return "CARRY_ALLY"
		"HELPING_ALLY":
			return "HELP_ALLY"
		"BOUNDING_HOLD":
			return "BOUND_HOLD"
		"BOUNDING_MOVE":
			return "BOUND_MOVE"
		"CAPTURING_POI":
			return "CAP_POI"
		"MG_DEPLOYED":
			return "MG_DEPLOY"
	return state_name
