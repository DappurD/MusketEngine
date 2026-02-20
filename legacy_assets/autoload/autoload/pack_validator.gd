class_name PackValidator
extends RefCounted
## Validates content pack manifests and unit schemas at load time.

const VALID_UNIT_CATEGORIES: Dictionary = {
	"infantry": true,
	"drone": true,
	"vehicle_wheeled": true,
	"vehicle_tracked": true,
	"vehicle_heli": true,
}

const MANIFEST_REQUIRED_KEYS: Array[String] = [
	"pack_id",
	"version",
	"display_name",
	"engine_compat",
	"dependencies",
	"unit_files",
	"ai_profile_files",
	"asset_map_file",
]

const DOCTRINE_REQUIRED_KEYS: Array[String] = [
	"doctrine_id",
	"version",
	"kind",
	"channels",
]

const VALID_DOCTRINE_KINDS: Dictionary = {
	"base": true,
	"overlay": true,
}

const VALID_DOCTRINE_POLICIES: Dictionary = {
	"immutable": true,
	"phase_gated": true,
	"mutable": true,
}

const VALID_DOCTRINE_CHANNELS: Dictionary = {
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


static func load_json_file(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {
			"ok": false,
			"error": "File not found: %s" % path,
			"data": {},
		}

	var file := FileAccess.open(path, FileAccess.READ)
	if not file:
		return {
			"ok": false,
			"error": "Cannot open file: %s" % path,
			"data": {},
		}
	var text: String = file.get_as_text()
	file.close()

	var json := JSON.new()
	var err: Error = json.parse(text)
	if err != OK:
		return {
			"ok": false,
			"error": "JSON parse error at %s line %d: %s" % [path, json.get_error_line(), json.get_error_message()],
			"data": {},
		}
	if not json.data is Dictionary:
		return {
			"ok": false,
			"error": "JSON root must be object: %s" % path,
			"data": {},
		}

	return {
		"ok": true,
		"error": "",
		"data": json.data,
	}


static func validate_pack_manifest(pack_root: String, manifest_path: String, manifest: Dictionary) -> Array[String]:
	var errors: Array[String] = []

	for k: String in MANIFEST_REQUIRED_KEYS:
		if not manifest.has(k):
			errors.append("Missing manifest key '%s' in %s" % [k, manifest_path])

	if not errors.is_empty():
		return errors

	if str(manifest.pack_id).is_empty():
		errors.append("pack_id must be non-empty in %s" % manifest_path)
	if str(manifest.version).is_empty():
		errors.append("version must be non-empty in %s" % manifest_path)

	if not manifest.unit_files is Array:
		errors.append("unit_files must be array in %s" % manifest_path)
	if not manifest.ai_profile_files is Array:
		errors.append("ai_profile_files must be array in %s" % manifest_path)
	if not manifest.dependencies is Array:
		errors.append("dependencies must be array in %s" % manifest_path)

	var asset_map_path: String = "%s/%s" % [pack_root, str(manifest.asset_map_file)]
	if not FileAccess.file_exists(asset_map_path):
		errors.append("asset_map_file missing: %s" % asset_map_path)

	for rel_path_v: Variant in manifest.unit_files:
		var rel_path: String = str(rel_path_v)
		var abs_path: String = "%s/%s" % [pack_root, rel_path]
		if not FileAccess.file_exists(abs_path):
			errors.append("unit file missing: %s" % abs_path)

	var doctrine_rel: String = str(manifest.get("doctrine_manifest_file", ""))
	if not doctrine_rel.is_empty():
		var doctrine_path: String = "%s/%s" % [pack_root, doctrine_rel]
		if not FileAccess.file_exists(doctrine_path):
			errors.append("doctrine_manifest_file missing: %s" % doctrine_path)
		else:
			var doctrine_load: Dictionary = load_json_file(doctrine_path)
			if not doctrine_load.ok:
				errors.append(str(doctrine_load.error))
			else:
				errors.append_array(validate_doctrine_manifest(doctrine_path, doctrine_load.data))

	return errors


static func validate_doctrine_manifest(doctrine_path: String, doctrine: Dictionary) -> Array[String]:
	var errors: Array[String] = []
	for k: String in DOCTRINE_REQUIRED_KEYS:
		if not doctrine.has(k):
			errors.append("%s missing key '%s'" % [doctrine_path, k])

	if not errors.is_empty():
		return errors

	var doctrine_id: String = str(doctrine.get("doctrine_id", ""))
	if doctrine_id.is_empty():
		errors.append("%s doctrine_id must be non-empty" % doctrine_path)

	var kind: String = str(doctrine.get("kind", ""))
	if not VALID_DOCTRINE_KINDS.has(kind):
		errors.append("%s invalid kind '%s' (expected base|overlay)" % [doctrine_path, kind])

	var channels_v: Variant = doctrine.get("channels", {})
	if not channels_v is Dictionary:
		errors.append("%s channels must be Dictionary" % doctrine_path)
	else:
		var channels: Dictionary = channels_v
		for channel_key_v: Variant in channels:
			var channel_key: String = str(channel_key_v)
			if not VALID_DOCTRINE_CHANNELS.has(channel_key):
				errors.append("%s unknown channel '%s'" % [doctrine_path, channel_key])
			var channel_data: Variant = channels[channel_key_v]
			if not channel_data is Dictionary:
				errors.append("%s channel '%s' must map to Dictionary" % [doctrine_path, channel_key])

	var runtime_policy: String = str(doctrine.get("runtime_policy", ""))
	if not runtime_policy.is_empty() and not VALID_DOCTRINE_POLICIES.has(runtime_policy):
		errors.append("%s invalid runtime_policy '%s'" % [doctrine_path, runtime_policy])

	var override_map_v: Variant = doctrine.get("override_map", {})
	if not override_map_v is Dictionary:
		errors.append("%s override_map must be Dictionary" % doctrine_path)

	var category_v: Variant = doctrine.get("category_channels", {})
	if not category_v is Dictionary:
		errors.append("%s category_channels must be Dictionary" % doctrine_path)
	else:
		var category_channels: Dictionary = category_v
		for category_key_v: Variant in category_channels:
			var category_key: String = str(category_key_v)
			if category_key != "infantry" and category_key != "armor" and category_key != "air":
				errors.append("%s category_channels has unknown key '%s'" % [doctrine_path, category_key])
			if not category_channels[category_key_v] is Dictionary:
				errors.append("%s category '%s' must map to Dictionary" % [doctrine_path, category_key])

	var era_hooks_v: Variant = doctrine.get("era_transition_hooks", {})
	if not era_hooks_v is Dictionary:
		errors.append("%s era_transition_hooks must be Dictionary" % doctrine_path)

	var era_transitions_v: Variant = doctrine.get("era_transitions", {})
	if not era_transitions_v is Dictionary:
		errors.append("%s era_transitions must be Dictionary" % doctrine_path)
	else:
		var era_transitions: Dictionary = era_transitions_v
		for transition_id_v: Variant in era_transitions:
			var transition_id: String = str(transition_id_v)
			var transition: Variant = era_transitions[transition_id_v]
			if not transition is Dictionary:
				errors.append("%s era_transitions.%s must be Dictionary" % [doctrine_path, transition_id])
				continue

			var transition_dict: Dictionary = transition
			var channel_overrides_v: Variant = transition_dict.get("channel_overrides", {})
			if not channel_overrides_v is Dictionary:
				errors.append("%s era_transitions.%s.channel_overrides must be Dictionary" % [doctrine_path, transition_id])
			else:
				var channel_overrides: Dictionary = channel_overrides_v
				for channel_key_v: Variant in channel_overrides:
					var channel_key: String = str(channel_key_v)
					if not VALID_DOCTRINE_CHANNELS.has(channel_key):
						errors.append("%s era_transitions.%s unknown channel override '%s'" % [doctrine_path, transition_id, channel_key])

			var migration_hooks_v: Variant = transition_dict.get("migration_hooks", {})
			if not migration_hooks_v is Dictionary:
				errors.append("%s era_transitions.%s.migration_hooks must be Dictionary" % [doctrine_path, transition_id])
			else:
				var migration_hooks: Dictionary = migration_hooks_v
				for hook_key_v: Variant in migration_hooks:
					var hook_key: String = str(hook_key_v)
					if not VALID_DOCTRINE_CHANNELS.has(hook_key):
						errors.append("%s era_transitions.%s unknown migration hook '%s'" % [doctrine_path, transition_id, hook_key])

			var required_policy: String = str(transition_dict.get("required_runtime_policy", ""))
			if not required_policy.is_empty() and not VALID_DOCTRINE_POLICIES.has(required_policy):
				errors.append("%s era_transitions.%s invalid required_runtime_policy '%s'" % [doctrine_path, transition_id, required_policy])

	return errors


static func validate_pack_units(pack_root: String, manifest: Dictionary) -> Array[String]:
	var errors: Array[String] = []
	var asset_map: Dictionary = {}
	var asset_map_path: String = "%s/%s" % [pack_root, str(manifest.get("asset_map_file", ""))]
	var asset_load: Dictionary = load_json_file(asset_map_path)
	if asset_load.ok:
		asset_map = asset_load.data
	else:
		errors.append(str(asset_load.error))

	for rel_path_v: Variant in manifest.get("unit_files", []):
		var rel_path: String = str(rel_path_v)
		var abs_path: String = "%s/%s" % [pack_root, rel_path]
		var load_res: Dictionary = load_json_file(abs_path)
		if not load_res.ok:
			errors.append(str(load_res.error))
			continue

		var unit_data: Dictionary = load_res.data
		errors.append_array(validate_unit_schema(abs_path, unit_data, asset_map))

	return errors


static func validate_unit_schema(unit_path: String, unit_data: Dictionary, asset_map: Dictionary) -> Array[String]:
	var errors: Array[String] = []
	var required_keys: Array[String] = [
		"unit_key",
		"display_name",
		"theme",
		"version",
		"identity",
		"combat",
		"survivability",
		"ai_profile",
		"render_profile",
		"spawn_profile",
	]
	for k: String in required_keys:
		if not unit_data.has(k):
			errors.append("%s missing key '%s'" % [unit_path, k])

	if not errors.is_empty():
		return errors

	var combat: Dictionary = unit_data.combat
	var combat_required: Array[String] = [
		"attack_range_m",
		"attack_cooldown_s",
		"accuracy_0_1",
		"mag_size",
		"ballistics",
		"settle_time_s",
		"deploy_time_s",
		"detect_range_m",
	]
	for k2: String in combat_required:
		if not combat.has(k2):
			errors.append("%s combat missing '%s'" % [unit_path, k2])

	var ballistics: Dictionary = combat.get("ballistics", {})
	for bk: String in ["muzzle_vel_mps", "base_spread_rad", "base_energy", "base_damage"]:
		if not ballistics.has(bk):
			errors.append("%s combat.ballistics missing '%s'" % [unit_path, bk])

	if float(combat.get("attack_cooldown_s", -1.0)) <= 0.0:
		errors.append("%s attack_cooldown_s must be > 0" % unit_path)
	var accuracy: float = float(combat.get("accuracy_0_1", -1.0))
	if accuracy < 0.0 or accuracy > 1.0:
		errors.append("%s accuracy_0_1 must be in [0,1]" % unit_path)
	if int(combat.get("mag_size", 0)) < 1:
		errors.append("%s mag_size must be >= 1" % unit_path)

	var category: String = str(unit_data.get("unit_category", "infantry"))
	if not VALID_UNIT_CATEGORIES.has(category):
		errors.append("%s invalid unit_category '%s'" % [unit_path, category])
		return errors

	var has_posture: bool = unit_data.has("posture_profile")
	var has_drone: bool = unit_data.has("drone_profile")
	var has_vehicle: bool = unit_data.has("vehicle_profile")
	var has_heli: bool = unit_data.has("heli_profile")

	match category:
		"infantry":
			if not has_posture:
				errors.append("%s infantry requires posture_profile" % unit_path)
			if has_drone or has_vehicle or has_heli:
				errors.append("%s infantry must not include drone/vehicle/heli profile blocks" % unit_path)
		"drone":
			if not has_drone:
				errors.append("%s drone requires drone_profile" % unit_path)
			if has_posture or has_vehicle or has_heli:
				errors.append("%s drone must not include posture/vehicle/heli profile blocks" % unit_path)
		"vehicle_wheeled", "vehicle_tracked":
			if not has_vehicle:
				errors.append("%s vehicle requires vehicle_profile" % unit_path)
			if has_posture or has_drone or has_heli:
				errors.append("%s vehicle must not include posture/drone/heli profile blocks" % unit_path)
		"vehicle_heli":
			if not has_heli:
				errors.append("%s vehicle_heli requires heli_profile" % unit_path)
			if has_posture or has_drone or has_vehicle:
				errors.append("%s vehicle_heli must not include posture/drone/vehicle profile blocks" % unit_path)

	if has_posture:
		var post: Dictionary = unit_data.posture_profile
		for p_name: String in ["stand", "crouch", "prone"]:
			if not post.has(p_name):
				errors.append("%s posture_profile missing '%s'" % [unit_path, p_name])

	var render_profile: Dictionary = unit_data.render_profile
	if not render_profile.has("model_key"):
		errors.append("%s render_profile missing 'model_key'" % unit_path)
	else:
		var model_key: String = str(render_profile.model_key)
		var models: Dictionary = asset_map.get("models", {})
		if not models.has(model_key):
			errors.append("%s model_key '%s' not found in asset_map.models" % [unit_path, model_key])

	return errors
