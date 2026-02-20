class_name SectorOrderValidator
extends RefCounted
## Validates and sanitizes LLM sector orders before passing to ColonyAICPP.
##
## Applies rejection rules from the design spec (Section 6.1):
## - Invalid sector/squad/intent → ignored
## - Suicide orders → downgraded to RECON
## - Overcommit → spread excess to adjacent sectors
## - Broken squads → forced to WITHDRAW
## - Duplicate assignments → first order wins
##
## Returns a filtered Array of validated order Dictionaries.

# ── Intent Constants (must match ColonyAICPP::_intent_to_goal mapping) ──────
const VALID_INTENTS: PackedStringArray = [
	"ATTACK", "DEFEND", "FLANK", "CAPTURE", "RECON", "HOLD", "SUPPRESS", "WITHDRAW"
]

# Intent name → C++ intent index mapping
const INTENT_MAP: Dictionary = {
	"ATTACK": 0, "DEFEND": 1, "FLANK": 2, "CAPTURE": 3,
	"RECON": 4, "HOLD": 5, "SUPPRESS": 6, "WITHDRAW": 7
}

# Intents that require combat strength (not safe for broken/weak squads)
const AGGRESSIVE_INTENTS: PackedStringArray = ["ATTACK", "FLANK", "CAPTURE", "SUPPRESS"]

# ── Tunable Thresholds ──────────────────────────────────────────────────────
const SUICIDE_THREAT_THRESHOLD: float = 8.0  # Sector threat level considered extreme
const SUICIDE_MIN_UNITS: int = 3             # Min units to send into extreme threat
const OVERCOMMIT_RATIO: float = 0.75         # Max fraction of squads in one sector
const BROKEN_MORALE_THRESHOLD: float = 0.2   # Below this, force WITHDRAW
const BASE_DEFEND_DISTANCE: int = 2          # Must keep 1 squad within this many sectors of base


static func validate_orders(raw_orders: Array, sector_grid: SectorGrid,
		sim: SimulationServer, team: int, squad_count: int,
		base_sector: Vector2i = Vector2i(-1, -1)) -> Array[Dictionary]:
	## Validate and filter LLM orders. Returns clean orders ready for ColonyAICPP.
	var validated: Array[Dictionary] = []
	var assigned_squads: Dictionary = {}  # sq_idx → true (dedup)
	var sector_squad_counts: Dictionary = {}  # "A1" → count of squads assigned

	for raw in raw_orders:
		if not raw is Dictionary:
			continue

		var order: Dictionary = raw

		# ── 1. Validate squad index ───────────────────────────────────────
		var sq_idx: int = -1
		if order.has("squad"):
			sq_idx = int(order["squad"])
		if sq_idx < 0 or sq_idx >= squad_count:
			_log("Rejected: invalid squad index %d" % sq_idx)
			continue

		# ── 2. Deduplicate ────────────────────────────────────────────────
		if assigned_squads.has(sq_idx):
			_log("Rejected: squad %d already assigned (duplicate)" % sq_idx)
			continue

		# ── 3. Validate sector label ──────────────────────────────────────
		var sector_str: String = str(order.get("sector", ""))
		var sector_pos := _parse_sector_label(sector_str, sector_grid)
		if sector_pos.x < 0:
			_log("Rejected: invalid sector '%s' for squad %d" % [sector_str, sq_idx])
			continue

		# ── 4. Validate intent ────────────────────────────────────────────
		var intent_str: String = str(order.get("intent", "")).to_upper()
		if intent_str not in VALID_INTENTS:
			_log("Rejected: invalid intent '%s' for squad %d" % [intent_str, sq_idx])
			continue

		# ── 5. Broken squad override ──────────────────────────────────────
		var squad_morale := _get_squad_avg_morale(sim, team, sq_idx)
		if squad_morale < BROKEN_MORALE_THRESHOLD and intent_str in AGGRESSIVE_INTENTS:
			_log("Override: squad %d morale %.2f too low for %s → WITHDRAW" % [
				sq_idx, squad_morale, intent_str])
			intent_str = "WITHDRAW"

		# ── 6. Suicide order detection ────────────────────────────────────
		var sec_idx := sector_grid.sector_index(sector_pos.x, sector_pos.y)
		var threat := sector_grid.threat_level[sec_idx] if sec_idx < sector_grid.threat_level.size() else 0.0
		var squad_alive := _get_squad_alive_count(sim, team, sq_idx)

		if threat > SUICIDE_THREAT_THRESHOLD and squad_alive < SUICIDE_MIN_UNITS \
				and intent_str in AGGRESSIVE_INTENTS:
			_log("Override: squad %d (%d units) into extreme threat sector %s → RECON" % [
				sq_idx, squad_alive, sector_str])
			intent_str = "RECON"

		# ── 7. Track for overcommit check ─────────────────────────────────
		if not sector_squad_counts.has(sector_str):
			sector_squad_counts[sector_str] = 0
		sector_squad_counts[sector_str] += 1

		# Build validated order
		var valid_order: Dictionary = {
			"squad": sq_idx,
			"sector_col": sector_pos.x,
			"sector_row": sector_pos.y,
			"sector_label": sector_str,
			"intent": intent_str,
			"intent_idx": INTENT_MAP.get(intent_str, -1),
			"confidence": 0.9  # Default high confidence for explicit LLM orders
		}
		validated.append(valid_order)
		assigned_squads[sq_idx] = true

	# ── 8. Overcommit check ───────────────────────────────────────────────
	var max_per_sector: int = maxi(1, int(ceil(squad_count * OVERCOMMIT_RATIO)))
	for sector_label_key in sector_squad_counts:
		if sector_squad_counts[sector_label_key] > max_per_sector:
			_log("Warning: %d squads assigned to sector %s (max %d) — excess will use lower confidence" % [
				sector_squad_counts[sector_label_key], sector_label_key, max_per_sector])
			# Reduce confidence of excess squads rather than removing them
			var count := 0
			for i in range(validated.size() - 1, -1, -1):
				if validated[i]["sector_label"] == sector_label_key:
					count += 1
					if count > max_per_sector:
						validated[i]["confidence"] = 0.3  # Low confidence = weak suggestion

	# ── 9. Base defense check ─────────────────────────────────────────────
	if base_sector.x >= 0:
		var has_defender := false
		for order in validated:
			var dist := absi(order["sector_col"] - base_sector.x) + absi(order["sector_row"] - base_sector.y)
			if dist <= BASE_DEFEND_DISTANCE:
				has_defender = true
				break
		if not has_defender and not validated.is_empty():
			# Find the nearest unassigned squad or the nearest assigned squad
			_log("Warning: no squads defending near base sector %s — consider assigning one" % [
				sector_grid.sector_label(base_sector.x, base_sector.y)])

	return validated


static func _parse_sector_label(label: String, grid: SectorGrid) -> Vector2i:
	## Parse "A1", "B3", etc. into (col, row). Returns (-1, -1) on failure.
	label = label.strip_edges().to_upper()
	if label.is_empty():
		return Vector2i(-1, -1)

	# Parse column letter(s)
	var col := -1
	var num_start := 0
	if label.length() >= 2 and label.unicode_at(0) >= 65 and label.unicode_at(0) <= 90:
		if label.length() >= 3 and label.unicode_at(1) >= 65 and label.unicode_at(1) <= 90:
			# Two-letter column (AA=26, AB=27, etc.)
			col = (label.unicode_at(0) - 65 + 1) * 26 + (label.unicode_at(1) - 65)
			num_start = 2
		else:
			col = label.unicode_at(0) - 65
			num_start = 1

	if col < 0 or col >= grid.cols:
		return Vector2i(-1, -1)

	# Parse row number
	var row_str := label.substr(num_start)
	if not row_str.is_valid_int():
		return Vector2i(-1, -1)
	var row := int(row_str) - 1  # Labels are 1-based, internal is 0-based
	if row < 0 or row >= grid.rows:
		return Vector2i(-1, -1)

	return Vector2i(col, row)


static func _get_squad_avg_morale(sim: SimulationServer, team: int, sq_idx: int) -> float:
	## Get average morale for a squad. Returns 1.0 if no data available.
	var total_morale := 0.0
	var count := 0
	var unit_count := sim.get_unit_count()
	var sim_sq_id := sq_idx if team == 1 else sq_idx + 64
	for uid in unit_count:
		if not sim.is_alive(uid):
			continue
		if sim.get_team(uid) != team:
			continue
		if sim.get_squad_id(uid) == sim_sq_id:
			total_morale += sim.get_morale(uid)
			count += 1
	return total_morale / float(count) if count > 0 else 1.0


static func _get_squad_alive_count(sim: SimulationServer, team: int, sq_idx: int) -> int:
	## Count alive units in a squad.
	var count := 0
	var unit_count := sim.get_unit_count()
	var sim_sq_id := sq_idx if team == 1 else sq_idx + 64
	for uid in unit_count:
		if not sim.is_alive(uid):
			continue
		if sim.get_team(uid) != team:
			continue
		if sim.get_squad_id(uid) == sim_sq_id:
			count += 1
	return count


static func _log(msg: String) -> void:
	print("[SectorOrderValidator] %s" % msg)
