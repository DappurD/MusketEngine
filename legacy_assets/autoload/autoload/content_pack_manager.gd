extends Node
## Discovers, validates, and registers content packs at startup.

const PACKS_ROOT := "res://packs"

var _packs: Dictionary = {}          # pack_id -> {path, manifest, unit_schemas}
var _doctrine_manifests: Dictionary = {}  # pack_id -> doctrine manifest
var _errors: Array[String] = []
var _warnings: Array[String] = []
var _valid: bool = true


func _ready() -> void:
	_bootstrap()


func _bootstrap() -> void:
	_packs.clear()
	_doctrine_manifests.clear()
	_errors.clear()
	_warnings.clear()
	_valid = true

	var registry: Node = get_node_or_null("/root/UnitRoleRegistry")
	var doctrine_registry: Node = get_node_or_null("/root/DoctrineRegistry")
	if registry:
		registry.reset_registry()
		registry.register_builtin_roles()
	else:
		_warnings.append("UnitRoleRegistry autoload not found; role registration skipped")
	if doctrine_registry and doctrine_registry.has_method("reset_registry"):
		doctrine_registry.reset_registry()
	elif not doctrine_registry:
		_warnings.append("DoctrineRegistry autoload not found; doctrine registration skipped")

	var entries: Array[String] = _discover_pack_dirs(PACKS_ROOT)
	if entries.is_empty():
		_warnings.append("No packs discovered under %s" % PACKS_ROOT)
		_finalize(registry, doctrine_registry)
		return

	for pack_dir: String in entries:
		_load_one_pack(pack_dir)

	# Register roles only from valid packs.
	if registry:
		for pack_id: String in _packs:
			var unit_schemas: Array = _packs[pack_id].get("unit_schemas", [])
			for schema_v: Variant in unit_schemas:
				var schema: Dictionary = schema_v
				var role_key: String = str(schema.get("unit_key", ""))
				if role_key.is_empty():
					continue
				registry.register_role(role_key, schema)

	if doctrine_registry and doctrine_registry.has_method("register_doctrine"):
		for pack_id: String in _packs:
			var doctrine_manifest: Dictionary = _packs[pack_id].get("doctrine_manifest", {})
			if doctrine_manifest.is_empty():
				continue
			doctrine_registry.register_doctrine(pack_id, doctrine_manifest)
			_doctrine_manifests[pack_id] = doctrine_manifest

	_finalize(registry, doctrine_registry)


func _discover_pack_dirs(root_path: String) -> Array[String]:
	var result: Array[String] = []
	var dir: DirAccess = DirAccess.open(root_path)
	if not dir:
		_warnings.append("Packs root not found: %s" % root_path)
		return result

	dir.list_dir_begin()
	while true:
		var name: String = dir.get_next()
		if name.is_empty():
			break
		if name.begins_with("."):
			continue
		var abs_path: String = "%s/%s" % [root_path, name]
		if dir.current_is_dir():
			result.append(abs_path)
	dir.list_dir_end()
	return result


func _load_one_pack(pack_dir: String) -> void:
	var manifest_path := "%s/pack_manifest.json" % pack_dir
	var manifest_load: Dictionary = PackValidator.load_json_file(manifest_path)
	if not manifest_load.ok:
		_errors.append(str(manifest_load.error))
		return
	var manifest: Dictionary = manifest_load.data

	var manifest_errors: Array[String] = PackValidator.validate_pack_manifest(pack_dir, manifest_path, manifest)
	if not manifest_errors.is_empty():
		_errors.append_array(manifest_errors)
		return

	var unit_errors: Array[String] = PackValidator.validate_pack_units(pack_dir, manifest)
	if not unit_errors.is_empty():
		_errors.append_array(unit_errors)
		return

	var unit_schemas: Array[Dictionary] = []
	for rel_path_v: Variant in manifest.get("unit_files", []):
		var unit_path: String = "%s/%s" % [pack_dir, str(rel_path_v)]
		var load_res: Dictionary = PackValidator.load_json_file(unit_path)
		if load_res.ok:
			unit_schemas.append(load_res.data)

	var doctrine_manifest: Dictionary = {}
	var doctrine_rel: String = str(manifest.get("doctrine_manifest_file", ""))
	if not doctrine_rel.is_empty():
		var doctrine_path: String = "%s/%s" % [pack_dir, doctrine_rel]
		var doctrine_load: Dictionary = PackValidator.load_json_file(doctrine_path)
		if doctrine_load.ok:
			doctrine_manifest = doctrine_load.data
		else:
			_errors.append(str(doctrine_load.error))
			return

	var pack_id: String = str(manifest.pack_id)
	if _packs.has(pack_id):
		_errors.append("Duplicate pack_id '%s' at %s" % [pack_id, pack_dir])
		return

	_packs[pack_id] = {
		"path": pack_dir,
		"manifest": manifest,
		"unit_schemas": unit_schemas,
		"doctrine_manifest": doctrine_manifest,
	}


func _finalize(registry: Node, doctrine_registry: Node) -> void:
	if not _errors.is_empty():
		_valid = false
		for e: String in _errors:
			printerr("[ContentPackManager] %s" % e)
	else:
		print("[ContentPackManager] Loaded %d pack(s)" % _packs.size())

	if not _warnings.is_empty():
		for w: String in _warnings:
			push_warning("[ContentPackManager] %s" % w)

	if registry:
		registry.freeze_registry()
	if doctrine_registry and doctrine_registry.has_method("freeze_registry"):
		doctrine_registry.freeze_registry()


func is_valid() -> bool:
	return _valid


func get_errors() -> Array[String]:
	return _errors.duplicate()


func get_warnings() -> Array[String]:
	return _warnings.duplicate()


func get_pack(pack_id: String) -> Dictionary:
	return _packs.get(pack_id, {})


func get_registered_roles() -> Dictionary:
	var registry: Node = get_node_or_null("/root/UnitRoleRegistry")
	if not registry:
		return {}
	return registry.get_registered_roles()


func get_all_pack_ids() -> Array[String]:
	return _packs.keys()


func get_doctrine_manifest(pack_id: String) -> Dictionary:
	return _doctrine_manifests.get(pack_id, {})


func get_all_doctrine_manifests() -> Dictionary:
	return _doctrine_manifests.duplicate(true)
