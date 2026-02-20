extends Node

## Economy AI Constants — Pheromone Channels & Parameters
##
## Unified 15-channel pheromone system shared with Combat AI:
## - Channels 0-7: Combat (SimulationServer writes, ColonyAI reads)
## - Channels 8-14: Economy (ColonyAI writes, SimulationServer reads)

# ========================================
# UNIFIED PHEROMONE CHANNEL LAYOUT
# ========================================

## Combat channels (SimulationServer owns)
enum CombatChannel {
	CH_DANGER = 0,        # Death zones, killzones
	CH_SUPPRESSION = 1,   # Active fire zones
	CH_CONTACT = 2,       # Enemy intel/sightings
	CH_RALLY = 3,         # Cohesion beacons
	CH_FEAR = 4,          # Panic contagion
	CH_COURAGE = 5,       # Leadership aura
	CH_SAFE_ROUTE = 6,    # Proven paths (ACO)
	CH_FLANK_OPP = 7,     # Tactical openings
}

## Economy channels (ColonyAI owns)
enum EconomyChannel {
	CH_METAL = 8,         # Metal ore resource trails
	CH_CRYSTAL = 9,       # Crystal resource trails
	CH_ENERGY = 10,       # Energy resource trails
	CH_CONGESTION = 11,   # Worker traffic density
	CH_BUILD_URGENCY = 12,# Construction priority
	CH_EXPLORED = 13,     # Frontier exploration
	CH_STRATEGIC = 14,    # LLM stigmergic command channel
}

# ========================================
# PHEROMONE PARAMETERS (EVAP/DIFFUSION)
# ========================================

## Resource trail channels: persistent paths (slow evap, low diffusion)
const METAL_EVAP_RATE := 0.98      # ~50s half-life
const METAL_DIFFUSION := 0.05      # Low spread (sharp trails)

const CRYSTAL_EVAP_RATE := 0.98
const CRYSTAL_DIFFUSION := 0.05

const ENERGY_EVAP_RATE := 0.98
const ENERGY_DIFFUSION := 0.05

## Congestion: real-time load balancing (fast evap, medium diffusion)
const CONGESTION_EVAP_RATE := 0.85  # ~6.5s half-life
const CONGESTION_DIFFUSION := 0.20  # Spreads to show "busy area"

## Build urgency: spreads need signal (medium evap, high diffusion)
const BUILD_EVAP_RATE := 0.92       # ~13s half-life
const BUILD_DIFFUSION := 0.30       # High spread (attracts distant workers)

## Explored: frontier memory (very slow evap, very low diffusion)
const EXPLORED_EVAP_RATE := 0.99    # ~100s half-life
const EXPLORED_DIFFUSION := 0.02    # Minimal spread (marks specific cells)

# ========================================
# DEPOSITION STRENGTHS
# ========================================

## Resource trail deposition (per meter traveled while carrying)
const METAL_TRAIL_STRENGTH := 3.0
const CRYSTAL_TRAIL_STRENGTH := 3.0
const ENERGY_TRAIL_STRENGTH := 3.0

## Trail reinforcement multiplier (successful return trip)
const TRAIL_SUCCESS_MULTIPLIER := 2.0

## Congestion deposition (when worker starts task)
const CONGESTION_DEPOSIT_STRENGTH := 20.0
const CONGESTION_RADIUS := 8.0  # meters

## Build urgency deposition (per construction site)
const BUILD_URGENCY_BASE := 15.0
const BUILD_URGENCY_RADIUS := 12.0  # meters

## Exploration deposition (idle worker scouting)
const EXPLORED_STRENGTH := 5.0

# ========================================
# SAMPLING THRESHOLDS
# ========================================

## Congestion thresholds for task allocation
const CONGESTION_LOW := 0.2
const CONGESTION_MEDIUM := 0.4
const CONGESTION_HIGH := 0.6

## Danger thresholds (read from Combat CH_DANGER)
const DANGER_CAUTION := 0.3    # Workers slow down
const DANGER_FLEE := 0.5       # Workers abandon task and flee

## Build urgency thresholds
const BUILD_URGENT := 0.5
const BUILD_CRITICAL := 0.8

# ========================================
# ACO PATHFINDING PARAMETERS
# ========================================

## Ant Colony Optimization - probabilistic trail following
## P(choose path) = (pheromone^α * heuristic^β) / Σ(all neighbors)

const ACO_ALPHA := 1.5      # Pheromone importance (higher = trust trails more)
const ACO_BETA := 2.0       # Greedy bias (higher = prefer closer resources)

## Heuristic = 1 / distance_to_goal
## Neighbors = 8 cardinal + diagonal directions

# ========================================
# UTILITY FUNCTIONS
# ========================================

## Configure all economy channels on a PheromoneMapCPP instance
static func configure_economy_channels(pheromone_map: PheromoneMapCPP) -> void:
	if not pheromone_map:
		push_error("configure_economy_channels: null pheromone_map")
		return

	# Resource trails (slow evap, low diffusion)
	pheromone_map.set_channel_params(EconomyChannel.CH_METAL, METAL_EVAP_RATE, METAL_DIFFUSION)
	pheromone_map.set_channel_params(EconomyChannel.CH_CRYSTAL, CRYSTAL_EVAP_RATE, CRYSTAL_DIFFUSION)
	pheromone_map.set_channel_params(EconomyChannel.CH_ENERGY, ENERGY_EVAP_RATE, ENERGY_DIFFUSION)

	# Congestion (fast evap, medium diffusion)
	pheromone_map.set_channel_params(EconomyChannel.CH_CONGESTION, CONGESTION_EVAP_RATE, CONGESTION_DIFFUSION)

	# Build urgency (medium evap, high diffusion)
	pheromone_map.set_channel_params(EconomyChannel.CH_BUILD_URGENCY, BUILD_EVAP_RATE, BUILD_DIFFUSION)

	# Explored (very slow evap, very low diffusion)
	pheromone_map.set_channel_params(EconomyChannel.CH_EXPLORED, EXPLORED_EVAP_RATE, EXPLORED_DIFFUSION)

	# Strategic channel (LLM stigmergic command — slow evap, slight spread)
	pheromone_map.set_channel_params(EconomyChannel.CH_STRATEGIC, 0.98, 0.05)

	print("Economy channels configured (8-14)")

## Get pheromone strength for a resource type at a world position
static func sample_resource_trail(pheromone_map: PheromoneMapCPP, world_pos: Vector3, resource_type: int) -> float:
	match resource_type:
		0: return pheromone_map.sample(world_pos, EconomyChannel.CH_METAL)
		1: return pheromone_map.sample(world_pos, EconomyChannel.CH_CRYSTAL)
		2: return pheromone_map.sample(world_pos, EconomyChannel.CH_ENERGY)
		_: return 0.0

## Get pheromone gradient for a resource type (direction toward strongest trail)
static func get_resource_gradient(pheromone_map: PheromoneMapCPP, world_pos: Vector3, resource_type: int) -> Vector3:
	match resource_type:
		0: return pheromone_map.gradient(world_pos, EconomyChannel.CH_METAL)
		1: return pheromone_map.gradient(world_pos, EconomyChannel.CH_CRYSTAL)
		2: return pheromone_map.gradient(world_pos, EconomyChannel.CH_ENERGY)
		_: return Vector3.ZERO

## Deposit resource trail (called while worker moving with resource)
static func deposit_resource_trail(pheromone_map: PheromoneMapCPP, from_pos: Vector3, to_pos: Vector3, resource_type: int, success: bool = false) -> void:
	var channel = 0
	var strength = 0.0

	match resource_type:
		0: channel = EconomyChannel.CH_METAL; strength = METAL_TRAIL_STRENGTH
		1: channel = EconomyChannel.CH_CRYSTAL; strength = CRYSTAL_TRAIL_STRENGTH
		2: channel = EconomyChannel.CH_ENERGY; strength = ENERGY_TRAIL_STRENGTH
		_: return

	# Reinforcement on successful return trip
	if success:
		strength *= TRAIL_SUCCESS_MULTIPLIER

	pheromone_map.deposit_trail(from_pos, to_pos, channel, strength)
