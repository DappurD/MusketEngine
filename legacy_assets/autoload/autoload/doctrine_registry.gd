extends Node
## Registry for doctrine manifests loaded from content packs.

var _frozen: bool = false
var _doctrines: Dictionary = {}  # doctrine_id -> {pack_id, manifest}
var _pack_to_doctrine: Dictionary = {}  # pack_id -> doctrine_id
var _errors: Array[String] = []


func reset_registry() -> void:
	_frozen = false
	_doctrines.clear()
	_pack_to_doctrine.clear()
	_errors.clear()


func register_doctrine(pack_id: String, doctrine_manifest: Dictionary) -> bool:
	if _frozen:
		_errors.append("Registry frozen, cannot register doctrine for pack '%s'" % pack_id)
		return false

	var doctrine_id: String = str(doctrine_manifest.get("doctrine_id", "")).strip_edges()
	if doctrine_id.is_empty():
		_errors.append("Pack '%s' doctrine manifest missing doctrine_id" % pack_id)
		return false

	if _doctrines.has(doctrine_id):
		_errors.append("Duplicate doctrine_id '%s' from pack '%s'" % [doctrine_id, pack_id])
		return false

	_doctrines[doctrine_id] = {
		"pack_id": pack_id,
		"manifest": doctrine_manifest.duplicate(true),
	}
	_pack_to_doctrine[pack_id] = doctrine_id
	return true


func freeze_registry() -> void:
	_frozen = true


func has_doctrine(doctrine_id: String) -> bool:
	return _doctrines.has(doctrine_id)


func get_doctrine(doctrine_id: String) -> Dictionary:
	var entry: Dictionary = _doctrines.get(doctrine_id, {})
	return entry.get("manifest", {})


func get_pack_doctrine(pack_id: String) -> Dictionary:
	var doctrine_id: String = str(_pack_to_doctrine.get(pack_id, ""))
	if doctrine_id.is_empty():
		return {}
	return get_doctrine(doctrine_id)


func get_all_doctrines() -> Dictionary:
	var out: Dictionary = {}
	for doctrine_id: String in _doctrines:
		out[doctrine_id] = _doctrines[doctrine_id].get("manifest", {})
	return out


func get_errors() -> Array[String]:
	return _errors.duplicate()
