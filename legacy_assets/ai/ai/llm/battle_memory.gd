class_name BattleMemory
extends RefCounted
## Tracks order outcomes across LLM decision cycles for feedback learning.
##
## Each cycle snapshot records the battlefield state at order-issue time.
## When new orders arrive, the previous cycle is finalized with outcomes.
## The LLM briefing includes a compact "last cycle results" section so the
## model can learn from its decisions within a single battle.

# ── Cycle Record ──────────────────────────────────────────────────────
## One record per LLM decision cycle.
class CycleRecord:
	var cycle_id: int = 0
	var timestamp_s: float = 0.0        # Game time at order issue
	var orders: Array[Dictionary] = []   # Validated orders issued

	# Snapshot at order-issue time
	var snap_friendly_alive: int = 0
	var snap_enemy_alive: int = 0
	var snap_territory: int = 0          # Capture points owned
	var snap_squad_alive: PackedInt32Array = PackedInt32Array()  # Per-squad alive counts

	# Outcome (filled when next cycle starts)
	var out_friendly_alive: int = 0
	var out_enemy_alive: int = 0
	var out_territory: int = 0
	var out_squad_alive: PackedInt32Array = PackedInt32Array()
	var out_elapsed_s: float = 0.0       # Time between cycles
	var finalized: bool = false

	func friendly_delta() -> int:
		return out_friendly_alive - snap_friendly_alive

	func enemy_delta() -> int:
		return out_enemy_alive - snap_enemy_alive

	func territory_delta() -> int:
		return out_territory - snap_territory

	func squad_casualties(sq_idx: int) -> int:
		if sq_idx < snap_squad_alive.size() and sq_idx < out_squad_alive.size():
			return snap_squad_alive[sq_idx] - out_squad_alive[sq_idx]
		return 0

	## Simple heuristic score: positive = good outcome, negative = bad.
	func outcome_score() -> float:
		var score := 0.0
		# Enemy killed is good (+2 per kill)
		score += float(-enemy_delta()) * 2.0
		# Friendly lost is bad (-3 per loss, weighted higher)
		score += float(friendly_delta()) * 3.0
		# Territory gained is very good (+10 per point)
		score += float(territory_delta()) * 10.0
		return score


# ── State ─────────────────────────────────────────────────────────────
var _history: Array[CycleRecord] = []
var _current_cycle: CycleRecord = null
var _cycle_counter: int = 0
var _team: int = 1
var _sim: SimulationServer = null

const MAX_HISTORY: int = 10  # Keep last N cycles (ring buffer style)


func setup(sim: SimulationServer, team: int) -> void:
	_sim = sim
	_team = team


func begin_cycle(orders: Array[Dictionary], game_time: float) -> void:
	## Called when new LLM orders are received. Finalizes previous cycle and starts new one.
	# Finalize previous cycle
	if _current_cycle != null and not _current_cycle.finalized:
		_finalize_current(game_time)

	# Start new cycle
	_cycle_counter += 1
	var rec := CycleRecord.new()
	rec.cycle_id = _cycle_counter
	rec.timestamp_s = game_time
	rec.orders = orders.duplicate()

	# Snapshot current state
	rec.snap_friendly_alive = _sim.get_alive_count_for_team(_team)
	var enemy_team := 2 if _team == 1 else 1
	rec.snap_enemy_alive = _sim.get_alive_count_for_team(enemy_team)
	rec.snap_territory = _sim.get_capture_count_for_team(_team)

	# Per-squad alive counts
	var squad_alive := PackedInt32Array()
	for order in orders:
		var sq_idx: int = order.get("squad", -1)
		if sq_idx >= 0:
			var sim_sq_id: int = sq_idx if _team == 1 else sq_idx + 64
			squad_alive.append(_sim.get_squad_alive_count(sim_sq_id))
		else:
			squad_alive.append(0)
	rec.snap_squad_alive = squad_alive

	_current_cycle = rec


func _finalize_current(game_time: float) -> void:
	if _current_cycle == null:
		return

	var enemy_team := 2 if _team == 1 else 1
	_current_cycle.out_friendly_alive = _sim.get_alive_count_for_team(_team)
	_current_cycle.out_enemy_alive = _sim.get_alive_count_for_team(enemy_team)
	_current_cycle.out_territory = _sim.get_capture_count_for_team(_team)
	_current_cycle.out_elapsed_s = game_time - _current_cycle.timestamp_s

	# Per-squad outcome
	var squad_alive := PackedInt32Array()
	for order in _current_cycle.orders:
		var sq_idx: int = order.get("squad", -1)
		if sq_idx >= 0:
			var sim_sq_id: int = sq_idx if _team == 1 else sq_idx + 64
			squad_alive.append(_sim.get_squad_alive_count(sim_sq_id))
		else:
			squad_alive.append(0)
	_current_cycle.out_squad_alive = squad_alive
	_current_cycle.finalized = true

	# Add to history (ring buffer)
	_history.append(_current_cycle)
	if _history.size() > MAX_HISTORY:
		_history.remove_at(0)


func get_last_cycle() -> CycleRecord:
	## Returns the most recently finalized cycle, or null.
	for i in range(_history.size() - 1, -1, -1):
		if _history[i].finalized:
			return _history[i]
	return null


func get_history() -> Array[CycleRecord]:
	return _history


func format_for_briefing() -> String:
	## Compact text summary of last cycle outcome for LLM briefing.
	## Returns empty string if no history yet.
	var last := get_last_cycle()
	if last == null:
		return ""

	var lines: PackedStringArray
	lines.append("LAST ORDERS OUTCOME (%.0fs ago):" % last.out_elapsed_s)

	# Force balance change
	var fr_delta := last.friendly_delta()
	var en_delta := last.enemy_delta()
	var fr_str := "%+d" % fr_delta if fr_delta != 0 else "0"
	var en_str := "%+d" % en_delta if en_delta != 0 else "0"
	lines.append("  Forces: friendly %s, enemy %s" % [fr_str, en_str])

	# Territory change
	var ter_delta := last.territory_delta()
	if ter_delta > 0:
		lines.append("  Territory: GAINED %d point(s)" % ter_delta)
	elif ter_delta < 0:
		lines.append("  Territory: LOST %d point(s)" % (-ter_delta))

	# Per-order outcomes
	for i in last.orders.size():
		var order: Dictionary = last.orders[i]
		var cas := last.squad_casualties(i)
		var label: String = order.get("sector_label", "?")
		var intent: String = order.get("intent", "?")
		var sq_idx: int = order.get("squad", -1)
		var status := "OK"
		if cas > 0:
			status = "-%d casualties" % cas
		elif cas < 0:
			status = "+%d reinforced" % (-cas)  # Shouldn't happen but handle it
		lines.append("  SQ%d %s@%s: %s" % [sq_idx, intent, label, status])

	# Overall assessment
	var score := last.outcome_score()
	if score > 5.0:
		lines.append("  Assessment: FAVORABLE")
	elif score < -5.0:
		lines.append("  Assessment: UNFAVORABLE")
	else:
		lines.append("  Assessment: NEUTRAL")

	return "\n".join(lines)


func get_stats() -> Dictionary:
	return {
		"cycles": _cycle_counter,
		"history_size": _history.size(),
		"last_score": get_last_cycle().outcome_score() if get_last_cycle() != null else 0.0,
	}
