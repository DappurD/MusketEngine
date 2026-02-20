extends Node
## Startup-only key -> numeric ID registry for content-pack units.
## Runtime systems should consume numeric IDs, not strings.

var _frozen: bool = false
var _next_role_id: int = 1000  # keep distance from hardcoded SimulationServer roles (0-6)

var _role_key_to_id: Dictionary = {}
var _id_to_role_key: Dictionary = {}
var _role_schemas: Dictionary = {}  # role_key -> unit schema dictionary
var _errors: Array[String] = []
const _BUILTIN_ROLES: Array[Dictionary] = [
	{"key": "rifleman", "const_name": "ROLE_RIFLEMAN", "fallback": 0},
	{"key": "leader", "const_name": "ROLE_LEADER", "fallback": 1},
	{"key": "medic", "const_name": "ROLE_MEDIC", "fallback": 2},
	{"key": "mg", "const_name": "ROLE_MG", "fallback": 3},
	{"key": "marksman", "const_name": "ROLE_MARKSMAN", "fallback": 4},
	{"key": "grenadier", "const_name": "ROLE_GRENADIER", "fallback": 5},
	{"key": "mortar", "const_name": "ROLE_MORTAR", "fallback": 6}
]


func _ready() -> void:
	register_builtin_roles()


func reset_registry() -> void:
	_frozen = false
	_next_role_id = 1000
	_role_key_to_id.clear()
	_id_to_role_key.clear()
	_role_schemas.clear()
	_errors.clear()


func register_builtin_roles() -> void:
	for defn in _BUILTIN_ROLES:
		var role_key: String = str(defn.get("key", ""))
		var const_name: String = str(defn.get("const_name", ""))
		var fallback: int = int(defn.get("fallback", -1))
		var role_id: int = _resolve_sim_constant(const_name, fallback)
		_register_builtin(role_key, role_id)


func _resolve_sim_constant(const_name: String, fallback: int) -> int:
	if ClassDB.class_exists("SimulationServer"):
		if ClassDB.class_has_integer_constant("SimulationServer", const_name):
			return int(ClassDB.class_get_integer_constant("SimulationServer", const_name))
	return fallback


func _register_builtin(role_key: String, role_id: int) -> void:
	_role_key_to_id[role_key] = role_id
	_id_to_role_key[role_id] = role_key


func register_role(role_key: String, schema: Dictionary) -> int:
	if _frozen:
		_errors.append("Registry frozen, cannot register role '%s'" % role_key)
		return -1

	var key: String = role_key.strip_edges().to_lower()
	if key.is_empty():
		_errors.append("Attempted to register empty role key")
		return -1

	if _role_key_to_id.has(key):
		# Do not reassign existing role IDs; keep first registration.
		return int(_role_key_to_id[key])

	var role_id: int = _next_role_id
	_next_role_id += 1

	_role_key_to_id[key] = role_id
	_id_to_role_key[role_id] = key
	_role_schemas[key] = schema.duplicate(true)
	return role_id


func has_role(role_key: String) -> bool:
	return _role_key_to_id.has(role_key.to_lower())


func get_role_id(role_key: String, default_id: int = -1) -> int:
	return int(_role_key_to_id.get(role_key.to_lower(), default_id))


func get_role_key(role_id: int) -> String:
	return str(_id_to_role_key.get(role_id, ""))


func get_role_schema(role_key: String) -> Dictionary:
	return _role_schemas.get(role_key.to_lower(), {})


func freeze_registry() -> void:
	_frozen = true


func is_frozen() -> bool:
	return _frozen


func get_registered_roles() -> Dictionary:
	return _role_key_to_id.duplicate(true)


func get_errors() -> Array[String]:
	return _errors.duplicate()
