extends Node
## Composes base+overlay doctrine stacks and applies them to SimulationServer.

const POLICY_IMMUTABLE := "immutable"
const POLICY_PHASE_GATED := "phase_gated"
const POLICY_MUTABLE := "mutable"
const VALID_CHANNELS := {
	"target_scoring": true,
	"state_priority": true,
	"movement_intent": true,
	"formation_policy": true,
	"suppression_response": true,
	"morale_response": true,
	"fire_control": true,
	"retreat_policy": true,
	"era_migration_hooks": true,
}

var _runtime_policy: String = POLICY_IMMUTABLE
var _faction_stacks: Dictionary = {}  # faction_id -> composed stack
var _compiled_cache: Dictionary = {}  # cache_key -> compiled payload
var _last_transition_report: Dictionary = {}


func _get_doctrine_registry() -> Node:
	var main_loop := Engine.get_main_loop()
	if main_loop is SceneTree and main_loop.root:
		return main_loop.root.get_node_or_null("DoctrineRegistry")
	return null


func reset_state() -> void:
	_runtime_policy = POLICY_IMMUTABLE
	_faction_stacks.clear()
	_compiled_cache.clear()
	_last_transition_report.clear()


func set_runtime_policy(policy: String) -> bool:
	var p: String = policy.strip_edges().to_lower()
	if p != POLICY_IMMUTABLE and p != POLICY_PHASE_GATED and p != POLICY_MUTABLE:
		return false
	_runtime_policy = p
	return true


func get_runtime_policy() -> String:
	return _runtime_policy


func compose_stack(base_doctrine_id: String, overlays: Array = [], category_stacks: Dictionary = {}) -> Dictionary:
	var registry: Node = _get_doctrine_registry()
	if not registry:
		return {"ok": false, "errors": ["DoctrineRegistry not found"], "stack": {}}

	var errors: Array[String] = []
	var base: Dictionary = registry.get_doctrine(base_doctrine_id)
	if base.is_empty():
		errors.append("Base doctrine not found: %s" % base_doctrine_id)

	var merged_channels: Dictionary = base.get("channels", {}).duplicate(true)
	var merged_override_map: Dictionary = base.get("override_map", {}).duplicate(true)
	var era_transitions: Dictionary = base.get("era_transitions", {}).duplicate(true)
	var ordered_sources: Array = [base_doctrine_id]

	for overlay_id_v: Variant in overlays:
		var overlay_id: String = str(overlay_id_v)
		var overlay: Dictionary = registry.get_doctrine(overlay_id)
		if overlay.is_empty():
			errors.append("Overlay doctrine not found: %s" % overlay_id)
			continue
		ordered_sources.append(overlay_id)
		var overlay_channels: Dictionary = overlay.get("channels", {})
		for channel_key: String in overlay_channels:
			merged_channels[channel_key] = overlay_channels[channel_key]
		var overlay_override_map: Dictionary = overlay.get("override_map", {})
		for channel_key2: String in overlay_override_map:
			merged_override_map[channel_key2] = overlay_override_map[channel_key2]
		var overlay_transitions: Dictionary = overlay.get("era_transitions", {})
		for transition_id: String in overlay_transitions:
			era_transitions[transition_id] = overlay_transitions[transition_id]

	var stack: Dictionary = {
		"base_doctrine": base_doctrine_id,
		"overlays": overlays.duplicate(),
		"channels": merged_channels,
		"category_stacks": category_stacks.duplicate(true),
		"override_order": ordered_sources,
		"override_map": merged_override_map,
		"era_transitions": era_transitions,
		"runtime_policy": _runtime_policy,
	}

	return {
		"ok": errors.is_empty(),
		"errors": errors,
		"stack": stack,
	}


func apply_stack_to_sim(sim: SimulationServer, faction_id: int, stack: Dictionary) -> Dictionary:
	if not sim:
		return {"ok": false, "errors": ["SimulationServer is null"]}

	var runtime_stack: Dictionary = stack.duplicate(true)
	if ClassDB.class_exists("DoctrineCompiler"):
		var compiler := DoctrineCompiler.new()
		var compiled_res: Dictionary = compiler.compile_stack(runtime_stack)
		if not bool(compiled_res.get("ok", false)):
			return {"ok": false, "errors": compiled_res.get("errors", ["Doctrine compile failed"])}
		var compiled_payload: Dictionary = compiled_res.get("compiled", {})
		runtime_stack["compiled"] = compiled_payload
		var cache_key: String = str(compiled_payload.get("cache_key", ""))
		if not cache_key.is_empty():
			_compiled_cache[cache_key] = compiled_payload

	var validation: Dictionary = sim.validate_doctrine_stack(runtime_stack)
	if not bool(validation.get("ok", false)):
		return {"ok": false, "errors": validation.get("errors", [])}

	if not sim.set_doctrine_runtime_policy(_runtime_policy):
		return {"ok": false, "errors": ["Invalid runtime policy: %s" % _runtime_policy]}

	if not sim.set_faction_doctrine_stack(faction_id, runtime_stack):
		return {"ok": false, "errors": ["SimulationServer rejected doctrine stack"]}

	_faction_stacks[faction_id] = runtime_stack
	return {"ok": true, "errors": []}


func get_faction_stack(faction_id: int) -> Dictionary:
	return _faction_stacks.get(faction_id, {})


func get_compiled_payload(cache_key: String) -> Dictionary:
	return _compiled_cache.get(cache_key, {})


func get_last_transition_report() -> Dictionary:
	return _last_transition_report.duplicate(true)


func apply_era_transition(sim: SimulationServer, faction_id: int, transition_id: String) -> bool:
	if not sim:
		_last_transition_report = {
			"ok": false,
			"errors": ["SimulationServer is null"],
		}
		return false

	var snapshot: Dictionary = sim.get_faction_doctrine_stack(faction_id)
	var preflight: Dictionary = _preflight_transition(snapshot, transition_id)
	if not bool(preflight.get("ok", false)):
		_last_transition_report = preflight
		return false

	var preview_stack: Dictionary = preflight.get("preview_stack", {})
	var sim_validation: Dictionary = sim.validate_doctrine_stack(preview_stack)
	if not bool(sim_validation.get("ok", false)):
		_last_transition_report = {
			"ok": false,
			"errors": sim_validation.get("errors", []),
		}
		return false

	if not sim.apply_era_transition(faction_id, transition_id):
		_last_transition_report = {
			"ok": false,
			"errors": ["SimulationServer transition commit failed"],
		}
		return false

	var committed: Dictionary = sim.get_faction_doctrine_stack(faction_id)
	if str(committed.get("last_transition_id", "")) != transition_id:
		var rollback_ok: bool = _rollback_to_snapshot(sim, faction_id, snapshot)
		_last_transition_report = {
			"ok": false,
			"errors": ["Post-commit verification failed"],
			"rollback_ok": rollback_ok,
		}
		return false

	_faction_stacks[faction_id] = committed
	_last_transition_report = {
		"ok": true,
		"errors": [],
		"transition_id": transition_id,
		"faction_id": faction_id,
	}
	return true


func _preflight_transition(current_stack: Dictionary, transition_id: String) -> Dictionary:
	if current_stack.is_empty():
		return {"ok": false, "errors": ["Faction doctrine stack is empty"], "preview_stack": {}}

	var errors: Array[String] = []
	if _runtime_policy == POLICY_IMMUTABLE:
		errors.append("Runtime policy 'immutable' does not allow era transitions")

	var transitions_v: Variant = current_stack.get("era_transitions", {})
	var transitions: Dictionary = {}
	if not transitions_v is Dictionary:
		errors.append("era_transitions missing or invalid")
	else:
		transitions = transitions_v
		if not transitions.has(transition_id):
			errors.append("Unknown era transition: %s" % transition_id)

	if not errors.is_empty():
		return {"ok": false, "errors": errors, "preview_stack": {}}

	var transition: Dictionary = transitions[transition_id]
	var required_policy: String = str(transition.get("required_runtime_policy", "")).strip_edges().to_lower()
	if not required_policy.is_empty() and required_policy != _runtime_policy:
		errors.append("Transition '%s' requires policy '%s' (current: '%s')" % [transition_id, required_policy, _runtime_policy])

	var channel_overrides: Variant = transition.get("channel_overrides", {})
	if not channel_overrides is Dictionary:
		errors.append("channel_overrides must be a Dictionary")
	else:
		for channel_key_v: Variant in channel_overrides:
			var channel_key: String = str(channel_key_v)
			if not VALID_CHANNELS.has(channel_key):
				errors.append("Unknown channel override key: %s" % channel_key)

	var migration_hooks: Variant = transition.get("migration_hooks", {})
	if not migration_hooks is Dictionary:
		errors.append("migration_hooks must be a Dictionary")
	else:
		for channel_key2_v: Variant in migration_hooks:
			var channel_key2: String = str(channel_key2_v)
			if not VALID_CHANNELS.has(channel_key2):
				errors.append("Unknown migration hook key: %s" % channel_key2)

	if not errors.is_empty():
		return {"ok": false, "errors": errors, "preview_stack": {}}

	var preview_stack: Dictionary = current_stack.duplicate(true)
	var channels: Dictionary = preview_stack.get("channels", {}).duplicate(true)
	_merge_channel_patch(channels, channel_overrides)
	_merge_channel_patch(channels, migration_hooks)
	var migration_meta: Dictionary = {}
	var migration_meta_v: Variant = channels.get("era_migration_hooks", {})
	if migration_meta_v is Dictionary:
		migration_meta = migration_meta_v.duplicate(true)
	var history: Array = migration_meta.get("history", [])
	history.append({
		"transition_id": transition_id,
		"runtime_policy": _runtime_policy,
	})
	migration_meta["history"] = history
	migration_meta["last_transition_id"] = transition_id
	migration_meta["last_runtime_policy"] = _runtime_policy
	channels["era_migration_hooks"] = migration_meta
	preview_stack["channels"] = channels
	preview_stack["last_transition_id"] = transition_id

	return {"ok": true, "errors": [], "preview_stack": preview_stack}


func _merge_channel_patch(channels: Dictionary, patch: Dictionary) -> void:
	for channel_key_v: Variant in patch:
		var channel_key: String = str(channel_key_v)
		var patch_value: Variant = patch[channel_key_v]
		var current_value: Variant = channels.get(channel_key, null)
		if patch_value is Dictionary and current_value is Dictionary:
			var merged: Dictionary = current_value.duplicate(true)
			for merge_k: Variant in patch_value:
				merged[merge_k] = patch_value[merge_k]
			channels[channel_key] = merged
		else:
			channels[channel_key] = patch_value


func _rollback_to_snapshot(sim: SimulationServer, faction_id: int, snapshot: Dictionary) -> bool:
	if snapshot.is_empty():
		return false

	var stats: Dictionary = sim.get_debug_stats()
	var old_policy: String = str(stats.get("doctrine_runtime_policy", POLICY_IMMUTABLE))
	if not sim.set_doctrine_runtime_policy(POLICY_MUTABLE):
		return false
	var restored: bool = sim.set_faction_doctrine_stack(faction_id, snapshot)
	sim.set_doctrine_runtime_policy(old_policy)
	if restored:
		_faction_stacks[faction_id] = snapshot.duplicate(true)
	return restored
