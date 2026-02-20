class_name EconomyVisualizer
extends Node
## Debug visualization for Economy AI.
##
## Displays resource nodes, worker assignments, construction sites, and
## stockpile UI for debugging economic behavior.
##
## Similar to VoxelDebugOverlay for voxel rendering, but for economy data.
##
## Usage:
##   var viz = EconomyVisualizer.new()
##   add_child(viz)
##   viz.set_economy_state(economy_state)
##   viz.set_resource_nodes(resource_nodes)
##   viz.set_construction_sites(sites)

## Reference to EconomyState (for stockpile display)
var _economy_state: EconomyState = null

## Resource nodes to display
var _resource_nodes: Array = []

## Construction sites to display
var _construction_sites: Array = []

## Worker assignments { worker_id: task_position }
var _worker_assignments: Dictionary = {}

## Visualization enabled flag
var _enabled = true

## Colors for resource types
const RESOURCE_COLORS := {
	EconomyState.ResourceType.RES_METAL: Color.GRAY,
	EconomyState.ResourceType.RES_CRYSTAL: Color.CYAN,
	EconomyState.ResourceType.RES_ENERGY: Color.YELLOW,
}

## Colors for construction sites
const CONSTRUCTION_COLOR := Color.ORANGE
const CONSTRUCTION_COMPLETE_COLOR := Color.GREEN


func _ready():
	# Create immediate geometry for drawing (Godot 4.x uses ImmediateMesh)
	pass


## Update visualization (call each frame or on state change).
func _process(_delta):
	if not _enabled:
		return

	queue_redraw()  # Trigger _draw() call


## Draw debug overlays (2D in screen space).
func _draw():
	if not _enabled:
		return

	# Draw stockpile UI (top-left corner)
	_draw_stockpile_ui()


## Draw 3D debug geometry (resource nodes, worker paths, construction sites).
func _physics_process(_delta):
	if not _enabled or not is_inside_tree():
		return

	# TODO: Use DebugDraw3D or ImmediateMesh for 3D visualization
	# For now: placeholder

	# _draw_resource_nodes_3d()
	# _draw_construction_sites_3d()
	# _draw_worker_assignments_3d()


## Draw stockpile UI (2D overlay).
func _draw_stockpile_ui():
	if not _economy_state:
		return

	var team_id = 0  # TODO: Make team ID configurable
	var stockpile = _economy_state.get_stockpile(team_id)

	var pos = Vector2(10, 10)
	var line_height = 20

	# Title
	draw_string(ThemeDB.fallback_font, pos, "Stockpile (Team %d)" % team_id, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, Color.WHITE)
	pos.y += line_height

	# Resources
	for res_type in stockpile:
		var amount = stockpile[res_type]
		var color = RESOURCE_COLORS.get(res_type, Color.WHITE)
		var res_name = _get_resource_name(res_type)

		draw_string(ThemeDB.fallback_font, pos, "%s: %d" % [res_name, amount], HORIZONTAL_ALIGNMENT_LEFT, -1, 12, color)
		pos.y += line_height


## Get human-readable resource name.
func _get_resource_name(resource_type: int) -> String:
	match resource_type:
		EconomyState.ResourceType.RES_METAL:
			return "Metal"
		EconomyState.ResourceType.RES_CRYSTAL:
			return "Crystal"
		EconomyState.ResourceType.RES_ENERGY:
			return "Energy"
		_:
			return "Unknown"


## Draw resource nodes in 3D (spheres at node positions).
func _draw_resource_nodes_3d():
	# TODO: Use DebugDraw3D or ImmediateMesh
	# For each node in _resource_nodes:
	#   Draw sphere at node.position with color based on resource_type
	pass


## Draw construction sites in 3D (boxes with progress bars).
func _draw_construction_sites_3d():
	# TODO: Use DebugDraw3D or ImmediateMesh
	# For each site in _construction_sites:
	#   Draw box at site.position
	#   Color based on progress (orange -> green)
	#   Display progress percentage
	pass


## Draw worker assignment lines (worker -> task).
func _draw_worker_assignments_3d():
	# TODO: Use DebugDraw3D or ImmediateMesh
	# For each assignment in _worker_assignments:
	#   Draw line from worker position to task position
	pass


## Set economy state reference.
func set_economy_state(economy_state: EconomyState) -> void:
	_economy_state = economy_state


## Set resource nodes to visualize.
func set_resource_nodes(nodes: Array) -> void:
	_resource_nodes = nodes


## Set construction sites to visualize.
func set_construction_sites(sites: Array) -> void:
	_construction_sites = sites


## Set worker assignments to visualize.
## assignments: Dictionary { worker_id: task_position }
func set_worker_assignments(assignments: Dictionary) -> void:
	_worker_assignments = assignments


## Enable/disable visualization.
func set_enabled(enabled: bool) -> void:
	_enabled = enabled
	if not enabled:
		queue_redraw()  # Clear display


## Check if visualization is enabled.
func is_enabled() -> bool:
	return _enabled
