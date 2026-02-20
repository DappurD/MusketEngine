class_name SimBridge
extends RefCounted
## Interface to SimulationServer for economy AI.
##
## Provides economy-specific queries and order issuing to SimulationServer.
## Isolates Economy AI from direct SimulationServer coupling for reusability.
##
## This is V-SAF specific. For other games, implement IUnitRegistry interface
## with your game's unit system.
##
## Usage:
##   var bridge = SimBridge.new(sim_server)
##   var workers = bridge.get_available_workers(team_id)
##   bridge.issue_gather_order(worker_id, resource_node_pos)

## Reference to SimulationServer
var _sim_server = null  # SimulationServer

## Worker role filter (optional - which unit roles can be workers)
var _worker_role_filter: Array[int] = []  # Empty = all roles


func _init(sim_server):
	_sim_server = sim_server


## Get available workers for a team.
## Returns Array of { id: int, position: Vector3, team_id: int, role_id: int }
func get_available_workers(team_id: int) -> Array:
	if not _sim_server:
		return []

	var workers = []

	# Query SimServer for idle or gathering units
	# TODO: SimServer needs get_units_by_state() or similar API
	# For now: placeholder - will implement when SimServer adds economy support

	# Placeholder logic (replace with real SimServer query):
	# var unit_count = _sim_server.get_unit_count()
	# for i in range(unit_count):
	#     var state = _sim_server.get_unit_state(i)
	#     if state == SimulationServer.ST_IDLE and _sim_server.get_team(i) == team_id:
	#         workers.append({
	#             "id": i,
	#             "position": _sim_server.get_position(i),
	#             "team_id": team_id,
	#             "role_id": _sim_server.get_role(i)
	#         })

	return workers


## Issue gather order to a unit.
## worker_id: Unit ID
## target_position: Resource node location
func issue_gather_order(worker_id: int, target_position: Vector3) -> void:
	if not _sim_server:
		return

	# TODO: SimServer needs ORDER_GATHER or similar
	# For now: use move order as placeholder
	# _sim_server.issue_order(worker_id, SimulationServer.ORDER_GATHER, target_position)

	pass  # Placeholder


## Issue build order to a unit.
## worker_id: Unit ID
## construction_site: ConstructionSite reference
func issue_build_order(worker_id: int, construction_site_pos: Vector3) -> void:
	if not _sim_server:
		return

	# TODO: SimServer needs ORDER_BUILD
	# _sim_server.issue_order(worker_id, SimulationServer.ORDER_BUILD, construction_site_pos)

	pass  # Placeholder


## Check if unit is busy (not available for assignment).
func is_unit_busy(unit_id: int) -> bool:
	if not _sim_server:
		return true

	# TODO: Query SimServer state
	# var state = _sim_server.get_unit_state(unit_id)
	# return state != SimulationServer.ST_IDLE

	return false  # Placeholder


## Get unit position.
func get_unit_position(unit_id: int) -> Vector3:
	if not _sim_server:
		return Vector3.ZERO

	# TODO: SimServer API
	# return _sim_server.get_position(unit_id)

	return Vector3.ZERO  # Placeholder


## Set worker role filter (which roles can be economy workers).
## Empty array = all roles can be workers.
func set_worker_role_filter(role_ids: Array[int]) -> void:
	_worker_role_filter = role_ids


## Check if unit role is allowed to be a worker.
func _is_valid_worker_role(role_id: int) -> bool:
	if _worker_role_filter.is_empty():
		return true  # All roles allowed
	return _worker_role_filter.has(role_id)


## Get count of active workers for a team.
func get_active_worker_count(team_id: int) -> int:
	# TODO: Implement when SimServer supports economy orders
	return 0
