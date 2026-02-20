class_name EconomyState
extends RefCounted
## Canonical resource tracking and economic state management.
##
## Maintains per-team stockpiles, production rates, and consumption tracking.
## Designed to be game-agnostic - only requires team IDs and resource type IDs.
##
## Thread Safety: Safe to call from worker threads (backed by C++ atomics).
##
## Usage:
##   var economy = EconomyState.new()
##   economy.add_resource(team_id, RES_METAL, 50)
##   if economy.can_afford(team_id, {RES_METAL: 100, RES_CRYSTAL: 25}):
##       economy.consume_resources(team_id, cost)
##
## Extraction Guide:
##   Replace ResourceType enum with your game's resources. All logic is generic
##   over resource type IDs.

## Resource types (game-specific - replace as needed)
enum ResourceType {
	RES_METAL = 0,    ## Basic construction material
	RES_CRYSTAL = 1,  ## Advanced technology resource
	RES_ENERGY = 2,   ## Power/fuel resource
	RES_COUNT         ## Total count (not a resource)
}

## Per-team stockpiles [team_id][resource_type] -> amount
var _stockpiles: Array[Dictionary] = [{}, {}]  # Team 0, Team 1

## Production rate tracking [team_id][resource_type] -> rate (per second)
var _production_rates: Array[Dictionary] = [{}, {}]

## Consumption tracking [team_id][resource_type] -> total consumed
var _total_consumed: Array[Dictionary] = [{}, {}]

## Reference to C++ backend (if available)
var _cpp_state = null  # TODO: Link to EconomyStateCPP when implemented


func _init():
	## Initialize stockpiles to zero for all resources
	for team_id in range(2):
		for res_type in ResourceType.values():
			if res_type == ResourceType.RES_COUNT:
				continue
			_stockpiles[team_id][res_type] = 0
			_production_rates[team_id][res_type] = 0.0
			_total_consumed[team_id][res_type] = 0


## Add resources to team's stockpile.
## Thread-safe if using C++ backend.
func add_resource(team_id: int, resource_type: ResourceType, amount: int) -> void:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")
	assert(resource_type >= 0 and resource_type < ResourceType.RES_COUNT, "Invalid resource type")
	assert(amount >= 0, "Cannot add negative resources")

	if _cpp_state:
		_cpp_state.add_resource(team_id, resource_type, amount)
	else:
		_stockpiles[team_id][resource_type] += amount


## Remove resources from team's stockpile.
## Returns true if successful, false if insufficient resources.
func consume_resources(team_id: int, cost: Dictionary) -> bool:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")

	# Validate affordability first
	if not can_afford(team_id, cost):
		return false

	# Deduct resources
	for res_type in cost:
		var amount: int = cost[res_type]
		if _cpp_state:
			_cpp_state.consume_resource(team_id, res_type, amount)
		else:
			_stockpiles[team_id][res_type] -= amount
			_total_consumed[team_id][res_type] += amount

	return true


## Check if team can afford a resource cost.
## Returns true if all resource requirements are met.
func can_afford(team_id: int, cost: Dictionary) -> bool:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")

	for res_type in cost:
		var required: int = cost[res_type]
		var available: int = get_resource_amount(team_id, res_type)
		if available < required:
			return false

	return true


## Get current stockpile for a team.
## Returns Dictionary { RES_METAL: 150, RES_CRYSTAL: 80, ... }
func get_stockpile(team_id: int) -> Dictionary:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")

	if _cpp_state:
		return _cpp_state.get_stockpile(team_id)
	else:
		return _stockpiles[team_id].duplicate()


## Get amount of a specific resource.
func get_resource_amount(team_id: int, resource_type: ResourceType) -> int:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")

	if _cpp_state:
		return _cpp_state.get_resource_amount(team_id, resource_type)
	else:
		return _stockpiles[team_id].get(resource_type, 0)


## Update production rate tracking (called by resource gatherers).
## rate: resources per second
func update_production_rate(team_id: int, resource_type: ResourceType, rate: float) -> void:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")
	_production_rates[team_id][resource_type] = rate


## Get current production rate for a resource.
## Returns resources per second (float).
func get_production_rate(team_id: int, resource_type: ResourceType) -> float:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")
	return _production_rates[team_id].get(resource_type, 0.0)


## Get total resources consumed (lifetime).
## Useful for KPI tracking.
func get_total_consumed(team_id: int, resource_type: ResourceType) -> int:
	assert(team_id >= 0 and team_id < 2, "Invalid team_id")

	if _cpp_state:
		return _cpp_state.get_total_consumed(team_id, resource_type)
	else:
		return _total_consumed[team_id].get(resource_type, 0)


## Get summary of economic state (for diagnostics).
## Returns Dictionary with stockpiles, production rates, consumption.
func get_state_summary(team_id: int) -> Dictionary:
	return {
		"stockpile": get_stockpile(team_id),
		"production_rates": _production_rates[team_id].duplicate(),
		"total_consumed": _total_consumed[team_id].duplicate() if not _cpp_state else {},
	}


## Reset all stockpiles to zero (for testing).
func reset() -> void:
	for team_id in range(2):
		for res_type in ResourceType.values():
			if res_type == ResourceType.RES_COUNT:
				continue
			_stockpiles[team_id][res_type] = 0
			_production_rates[team_id][res_type] = 0.0
			_total_consumed[team_id][res_type] = 0

	if _cpp_state:
		_cpp_state.reset()
