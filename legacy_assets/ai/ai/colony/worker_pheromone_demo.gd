extends Node
class_name WorkerPheromoneDemo

## Worker Pheromone Demo — Shows economy AI pheromone integration
##
## Demonstrates:
## - Getting unified pheromone map from SimulationServer
## - Depositing resource trails while carrying ore
## - Following pheromone gradients to find resources
## - ACO pathfinding with trail reinforcement

# Constants from economy_constants.gd
const EconomyChannel = preload("res://ai/colony/economy_constants.gd").EconomyChannel

# Simulation references
var simulation_server = null  # SimulationServer (RefCounted — set by parent scene)
var pheromone_map: PheromoneMapCPP = null
var voxel_world: VoxelWorld = null
var resource_scanner: RefCounted = null  # VoxelResourceScanner

# Worker state
var worker_pos = Vector3.ZERO
var worker_team = 0
var carrying_resource = -1  # -1 = none, 0 = metal, 1 = crystal, 2 = energy
var target_node: Dictionary = {}  # {type, pos, density}
var prev_pos = Vector3.ZERO

func _ready():
	# Get SimulationServer singleton
	simulation_server = get_node_or_null("/root/SimulationServer")
	if not simulation_server:
		push_error("WorkerPheromoneDemo: SimulationServer not found")
		return

	# Get unified pheromone map (15 channels, shared with combat)
	pheromone_map = simulation_server.get_pheromone_map(worker_team)
	if not pheromone_map:
		push_error("WorkerPheromoneDemo: Failed to get pheromone map")
		return

	print("✓ WorkerPheromoneDemo initialized")
	print("  Team %d pheromone map: %d channels" % [worker_team, pheromone_map.get_channel_count()])

## Simulate worker AI tick (called from game loop)
func tick(delta: float):
	if not pheromone_map:
		return

	if carrying_resource >= 0:
		# Worker is carrying ore — deposit trail as we move
		_deposit_resource_trail()
		_move_toward_base()
	else:
		# Worker is idle — search for resources via pheromone gradient or scanner
		_search_for_resources()

## Deposit resource trail pheromones while moving
func _deposit_resource_trail():
	# Deposit trail from previous position to current position
	# (SimulationServer does this per-tick for workers)
	var channel = _resource_type_to_channel(carrying_resource)
	var strength = 3.0  # METAL_TRAIL_STRENGTH from economy_constants.gd

	# deposit_trail() automatically interpolates between positions
	pheromone_map.deposit_trail(prev_pos, worker_pos, channel, strength)

	# On successful delivery, reinforce trail with 2x strength
	if _reached_base():
		pheromone_map.deposit_trail(prev_pos, worker_pos, channel, strength * 2.0)
		print("  ✓ Worker delivered %s — trail reinforced!" % _resource_name(carrying_resource))
		carrying_resource = -1

## Search for resources using ACO (ant colony optimization)
func _search_for_resources():
	# Sample all resource trail channels
	var metal_strength = pheromone_map.sample(worker_pos, EconomyChannel.CH_METAL)
	var crystal_strength = pheromone_map.sample(worker_pos, EconomyChannel.CH_CRYSTAL)
	var energy_strength = pheromone_map.sample(worker_pos, EconomyChannel.CH_ENERGY)

	# ACO: P(choose path) = (pheromone^α * heuristic^β) / Σ
	# If strong trail exists, follow gradient; otherwise use scanner
	var strongest = max(metal_strength, max(crystal_strength, energy_strength))

	if strongest > 0.5:  # Trail threshold
		# Follow strongest pheromone gradient
		var channel = _strongest_channel(metal_strength, crystal_strength, energy_strength)
		var gradient = pheromone_map.gradient(worker_pos, channel)

		if gradient.length() > 0.01:
			_move_along_gradient(gradient)
			print("  → Following %s trail (strength=%.2f)" % [_channel_name(channel), strongest])
			return

	# No trail found — use VoxelResourceScanner to find new resources
	if resource_scanner:
		var nodes = resource_scanner.scan_resources()
		if nodes.size() > 0:
			# Score nodes by density/distance (ACO heuristic β=2.0)
			var scored = resource_scanner.score_nodes(worker_pos, -1)  # All types
			if scored.size() > 0:
				target_node = scored[0].node
				print("  ⛏ Found new %s deposit at %s (density=%d, score=%.2f)" % [
					_resource_name(target_node.type),
					target_node.pos,
					target_node.density,
					scored[0].score
				])

## Convert resource type to economy pheromone channel
func _resource_type_to_channel(resource_type: int) -> int:
	match resource_type:
		0: return EconomyChannel.CH_METAL
		1: return EconomyChannel.CH_CRYSTAL
		2: return EconomyChannel.CH_ENERGY
		_: return -1

## Get strongest channel from three samples
func _strongest_channel(metal: float, crystal: float, energy: float) -> int:
	if metal >= crystal and metal >= energy:
		return EconomyChannel.CH_METAL
	elif crystal >= energy:
		return EconomyChannel.CH_CRYSTAL
	else:
		return EconomyChannel.CH_ENERGY

## Helper: Resource type to name
func _resource_name(type: int) -> String:
	match type:
		0: return "metal ore"
		1: return "crystal"
		2: return "energy core"
		_: return "unknown"

## Helper: Channel to name
func _channel_name(channel: int) -> String:
	match channel:
		EconomyChannel.CH_METAL: return "metal"
		EconomyChannel.CH_CRYSTAL: return "crystal"
		EconomyChannel.CH_ENERGY: return "energy"
		_: return "unknown"

## Stub: Move worker toward base (deposit point)
func _move_toward_base():
	pass  # TODO: implement pathfinding

## Stub: Check if worker reached base
func _reached_base() -> bool:
	return false  # TODO: implement

## Stub: Move along pheromone gradient
func _move_along_gradient(gradient: Vector3):
	pass  # TODO: implement pathfinding

## EXAMPLE: Direct pheromone deposit (for testing)
func example_deposit_congestion_at_position(pos: Vector3):
	# Deposit congestion pheromone when worker starts task
	# (prevents clustering - load balancing)
	pheromone_map.deposit(pos, EconomyChannel.CH_CONGESTION, 20.0)
	print("  🚧 Deposited congestion at %s" % pos)

## EXAMPLE: Direct pheromone sample (for testing)
func example_check_danger_before_mining(pos: Vector3) -> bool:
	# Check combat danger channel before sending worker
	# (economy AI reads combat pheromones for safety)
	var danger = pheromone_map.sample(pos, 0)  # CH_DANGER = 0
	if danger > 0.5:
		print("  ⚠ Position %s is dangerous (danger=%.2f) — abort mining!" % [pos, danger])
		return false
	return true
