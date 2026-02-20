class_name SectorGrid extends RefCounted
## Adaptive sector grid that partitions the map into labeled cells for LLM reasoning.
##
## Sectors are approximately 100m squares (adaptive to map size).
## Labels use column letters (A-Z) and row numbers (1-N), e.g., "A1", "C4".
## FOW-filtered: enemy data only includes units the team can see.

# ── Grid Dimensions ──────────────────────────────────────────────────────
var cols: int = 6
var rows: int = 4
var cell_w: float = 100.0
var cell_h: float = 100.0
var map_w: float = 600.0
var map_h: float = 400.0
var half_w: float = 300.0
var half_h: float = 200.0

# ── Per-Sector Data (flat arrays, index = row * cols + col) ──────────────
var friendly_count: PackedInt32Array
var enemy_count: PackedInt32Array
var threat_level: PackedFloat32Array    # GPU tactical map threat (0-10+)
var cover_quality: PackedFloat32Array   # GPU tactical map cover (0-1)
var danger_pheromone: PackedFloat32Array # CH_DANGER pheromone (0-10)
var contact_recency: PackedFloat32Array # seconds since last CONTACT pheromone > 0.1
var squad_presence: Array[PackedInt32Array]  # squad IDs present per sector
var elevation: PackedFloat32Array       # Average terrain height (meters)
var terrain_type: PackedByteArray       # Enum: TERRAIN_OPEN/URBAN/FOREST/HILL
var capture_owner: PackedByteArray      # 0=none, 1=ours, 2=theirs, 3=neutral, 4=contested
var capture_progress: PackedFloat32Array # 0-1 capture progress

# ── Terrain Classification ────────────────────────────────────────────────
enum TerrainType { TERRAIN_OPEN = 0, TERRAIN_URBAN = 1, TERRAIN_FOREST = 2, TERRAIN_HILL = 3 }
const TERRAIN_LABELS: PackedStringArray = ["OPEN", "URBAN", "FOREST", "HILL"]

# Material IDs for terrain classification (must match voxel_materials.h)
const _URBAN_MATS: PackedByteArray = [5, 6, 4, 14, 15]  # concrete, brick, steel, metal_plate, rust
const _FOREST_MATS: PackedByteArray = [3, 10]            # wood, grass

# ── Contact Recency Timer ─────────────────────────────────────────────────
var _last_contact_time: PackedFloat32Array  # game time of last CONTACT signal per sector

# ── LLM Intent Vocabulary ────────────────────────────────────────────────
const INTENT_NAMES: PackedStringArray = [
	"ATTACK", "DEFEND", "FLANK", "CAPTURE", "RECON", "HOLD", "FIRE_MISSION", "WITHDRAW"
]

# ── Setup ────────────────────────────────────────────────────────────────

func setup(p_map_w: float, p_map_h: float) -> void:
	map_w = p_map_w
	map_h = p_map_h
	half_w = map_w / 2.0
	half_h = map_h / 2.0
	cols = maxi(2, roundi(map_w / 100.0))
	rows = maxi(2, roundi(map_h / 100.0))
	cell_w = map_w / float(cols)
	cell_h = map_h / float(rows)

	var total := cols * rows
	friendly_count.resize(total)
	enemy_count.resize(total)
	threat_level.resize(total)
	cover_quality.resize(total)
	danger_pheromone.resize(total)
	contact_recency.resize(total)
	squad_presence.resize(total)
	elevation.resize(total)
	terrain_type.resize(total)
	capture_owner.resize(total)
	capture_progress.resize(total)
	_last_contact_time.resize(total)
	for i in total:
		squad_presence[i] = PackedInt32Array()
		_last_contact_time[i] = -999.0

# ── Coordinate Conversion ────────────────────────────────────────────────

func sector_label(col: int, row: int) -> String:
	# A-Z for columns, 1-N for rows (row 0 = "1", displayed bottom-up)
	var c: String = ""
	if col < 26:
		c = char(65 + col)
	else:
		c = char(65 + col / 26 - 1) + char(65 + col % 26)
	return c + str(row + 1)

func world_to_sector(pos: Vector3) -> Vector2i:
	var col := clampi(int((pos.x + half_w) / cell_w), 0, cols - 1)
	var row := clampi(int((pos.z + half_h) / cell_h), 0, rows - 1)
	return Vector2i(col, row)

func sector_center(col: int, row: int) -> Vector3:
	var x := -half_w + (col + 0.5) * cell_w
	var z := -half_h + (row + 0.5) * cell_h
	return Vector3(x, 0.0, z)

func sector_index(col: int, row: int) -> int:
	return row * cols + col

# ── Update (call each LLM cycle, ~30-60s) ────────────────────────────────

func update(sim: SimulationServer, gpu_tac: GpuTacticalMap,
			phero: PheromoneMapCPP, team: int) -> void:
	var total := cols * rows
	var game_time := sim.get_game_time()
	# Reset arrays
	for i in total:
		friendly_count[i] = 0
		enemy_count[i] = 0
		threat_level[i] = 0.0
		cover_quality[i] = 0.0
		danger_pheromone[i] = 0.0
		elevation[i] = 0.0
		terrain_type[i] = TerrainType.TERRAIN_OPEN
		capture_owner[i] = 0
		capture_progress[i] = 0.0
		squad_presence[i] = PackedInt32Array()

	var unit_count := sim.get_unit_count()
	var tracked_squads: Dictionary = {}  # sector_idx → {sq_id: true}

	# Iterate all units
	for uid in unit_count:
		if not sim.is_alive(uid):
			continue
		var u_team := sim.get_team(uid)
		var pos := sim.get_position(uid)
		var sec := world_to_sector(pos)
		var idx := sector_index(sec.x, sec.y)

		if u_team == team:
			# Friendly unit
			friendly_count[idx] += 1
			var sq_id := sim.get_squad_id(uid)
			if sq_id >= 0:
				if not tracked_squads.has(idx):
					tracked_squads[idx] = {}
				tracked_squads[idx][sq_id] = true
		else:
			# Enemy unit — FOW filter
			if sim.team_can_see(team, uid):
				enemy_count[idx] += 1

	# Copy tracked squads to packed arrays
	for idx_key in tracked_squads:
		var sq_dict: Dictionary = tracked_squads[idx_key]
		var arr := PackedInt32Array()
		for sq_id in sq_dict:
			arr.append(sq_id)
		squad_presence[idx_key] = arr

	# Sample GPU tactical map and pheromones per sector center
	for row in rows:
		for col in cols:
			var idx := sector_index(col, row)
			var center := sector_center(col, row)

			if gpu_tac:
				threat_level[idx] = gpu_tac.get_threat_at(center)
				cover_quality[idx] = gpu_tac.get_cover_at(center)
				# Elevation from height map (average of 5 samples: center + 4 offsets)
				var h := gpu_tac.get_terrain_height_m(center.x, center.z)
				var offset := cell_w * 0.3
				h += gpu_tac.get_terrain_height_m(center.x - offset, center.z)
				h += gpu_tac.get_terrain_height_m(center.x + offset, center.z)
				h += gpu_tac.get_terrain_height_m(center.x, center.z - offset)
				h += gpu_tac.get_terrain_height_m(center.x, center.z + offset)
				elevation[idx] = h / 5.0

			if phero:
				# PheromoneMapCPP is per-team; caller passes the right team's map
				danger_pheromone[idx] = phero.sample(center, SimulationServer.CH_DANGER)
				var contact_val: float = phero.sample(center, SimulationServer.CH_CONTACT)
				if contact_val > 0.1:
					_last_contact_time[idx] = game_time

	# Compute contact recency from stored timestamps
	for i in total:
		if _last_contact_time[i] > 0.0:
			contact_recency[i] = game_time - _last_contact_time[i]
		else:
			contact_recency[i] = 999.0

	# Capture point data
	_update_capture_points(sim, team)

	# Terrain classification (uses cover quality + elevation as proxy)
	_classify_terrain(gpu_tac)


func _update_capture_points(sim: SimulationServer, team: int) -> void:
	## Map capture points to sectors and set ownership status.
	var cap_data: Dictionary = sim.get_capture_data()
	var positions: PackedVector3Array = cap_data.get("positions", PackedVector3Array())
	var owners: PackedInt32Array = cap_data.get("owners", PackedInt32Array())
	var progress_arr: PackedFloat32Array = cap_data.get("progress", PackedFloat32Array())
	var contested: PackedInt32Array = cap_data.get("contested", PackedInt32Array())

	for i in positions.size():
		var sec := world_to_sector(positions[i])
		var idx := sector_index(sec.x, sec.y)
		var owner: int = owners[i] if i < owners.size() else 0

		if i < contested.size() and contested[i] == 1:
			capture_owner[idx] = 4  # CONTESTED
		elif owner == team:
			capture_owner[idx] = 1  # OURS
		elif owner == 0:
			capture_owner[idx] = 3  # NEUTRAL
		else:
			capture_owner[idx] = 2  # THEIRS

		if i < progress_arr.size():
			capture_progress[idx] = progress_arr[i]


func _classify_terrain(gpu_tac: GpuTacticalMap) -> void:
	## Classify each sector terrain using elevation variance and cover quality.
	## True material sampling would require VoxelWorld reference — instead we use
	## heuristics: high cover = URBAN, high elevation = HILL, moderate cover = FOREST.
	if gpu_tac == null:
		return

	# Compute global average elevation for relative classification
	var total := cols * rows
	var avg_elev := 0.0
	for i in total:
		avg_elev += elevation[i]
	avg_elev /= maxf(1.0, float(total))

	for i in total:
		var cover := cover_quality[i]
		var elev_diff := elevation[i] - avg_elev

		if elev_diff > 3.0:
			terrain_type[i] = TerrainType.TERRAIN_HILL
		elif cover > 0.55:
			terrain_type[i] = TerrainType.TERRAIN_URBAN
		elif cover > 0.3:
			terrain_type[i] = TerrainType.TERRAIN_FOREST
		else:
			terrain_type[i] = TerrainType.TERRAIN_OPEN

# ── LLM Formatting ───────────────────────────────────────────────────────

func format_for_llm(team: int) -> String:
	## Returns a sparse text representation of the sector grid.
	## Only sectors with activity (friendlies, enemies, or recent contact) are listed.
	var lines: PackedStringArray
	lines.append("SECTOR MAP (%dx%d, ~%.0fm cells):" % [cols, rows, cell_w])

	# Header row with column labels
	var header := "     "
	for col in cols:
		var label := char(65 + col) if col < 26 else (char(65 + col / 26 - 1) + char(65 + col % 26))
		header += label.rpad(8)
	lines.append(header)

	# Grid rows (top to bottom = highest row first)
	for row in range(rows - 1, -1, -1):
		var row_str := str(row + 1).rpad(4)
		var row_parts: PackedStringArray
		var has_content := false

		for col in cols:
			var idx := sector_index(col, row)
			var cell := ""

			if friendly_count[idx] > 0 or enemy_count[idx] > 0 or contact_recency[idx] < 30.0 \
					or capture_owner[idx] > 0:
				has_content = true
				var parts: PackedStringArray

				if friendly_count[idx] > 0:
					parts.append("FR:%d" % friendly_count[idx])
				if enemy_count[idx] > 0:
					parts.append("EN:%d" % enemy_count[idx])
				if threat_level[idx] > 0.5:
					parts.append("T:%.0f" % threat_level[idx])
				if cover_quality[idx] > 0.6:
					parts.append("C+")

				if parts.is_empty():
					cell = "  ~  "
				else:
					cell = " ".join(parts)
			else:
				cell = "  --  "

			row_parts.append(cell.rpad(8))

		if has_content:
			lines.append(row_str + "".join(row_parts))

	# Sector detail lines (non-empty sectors with rich data)
	lines.append("")
	lines.append("SECTOR DETAILS:")
	for row in range(rows - 1, -1, -1):
		for col in cols:
			var idx := sector_index(col, row)
			if friendly_count[idx] == 0 and enemy_count[idx] == 0 \
					and contact_recency[idx] >= 30.0 and capture_owner[idx] == 0:
				continue
			var label := sector_label(col, row)
			var parts: PackedStringArray
			parts.append(label)

			# Forces
			if friendly_count[idx] > 0:
				parts.append("FR:%d" % friendly_count[idx])
			if enemy_count[idx] > 0:
				parts.append("EN:%d" % enemy_count[idx])

			# Terrain + elevation
			parts.append(TERRAIN_LABELS[terrain_type[idx]])
			if elevation[idx] > 0.5:
				parts.append("elev:%.0fm" % elevation[idx])

			# Cover
			if cover_quality[idx] > 0.6:
				parts.append("cover:HIGH")
			elif cover_quality[idx] > 0.3:
				parts.append("cover:MED")
			else:
				parts.append("cover:LOW")

			# Capture point
			if capture_owner[idx] > 0:
				var cap_label: String
				match capture_owner[idx]:
					1: cap_label = "[OURS"
					2: cap_label = "[THEIRS"
					3: cap_label = "[NEUTRAL"
					4: cap_label = "[CONTESTED"
					_: cap_label = "[?"
				if capture_progress[idx] > 0.05 and capture_progress[idx] < 0.95:
					cap_label += " %.0f%%" % (capture_progress[idx] * 100.0)
				cap_label += "]"
				parts.append(cap_label)

			# Danger / contact
			if danger_pheromone[idx] > 2.0:
				parts.append("DANGER:HIGH")
			elif danger_pheromone[idx] > 0.5:
				parts.append("danger:med")
			if contact_recency[idx] < 10.0:
				parts.append("contact:NOW")
			elif contact_recency[idx] < 30.0:
				parts.append("contact:RECENT")

			lines.append("  " + ", ".join(parts))

	return "\n".join(lines)

func format_squad_roster(sim: SimulationServer, team: int) -> String:
	## Returns a compact squad roster for the LLM briefing.
	## Includes unit count, sector position, morale, and combat status.
	var lines: PackedStringArray
	lines.append("SQUAD ROSTER:")

	# Collect per-squad data
	var squad_ids: Dictionary = {}
	var unit_count := sim.get_unit_count()
	for uid in unit_count:
		if not sim.is_alive(uid):
			continue
		if sim.get_team(uid) != team:
			continue
		var sq_id := sim.get_squad_id(uid)
		if sq_id >= 0:
			if not squad_ids.has(sq_id):
				squad_ids[sq_id] = {
					"count": 0, "pos": Vector3.ZERO,
					"morale_sum": 0.0, "suppressed": 0, "in_combat": 0
				}
			var info: Dictionary = squad_ids[sq_id]
			info["count"] += 1
			info["pos"] += sim.get_position(uid)
			info["morale_sum"] += sim.get_morale(uid)
			if sim.get_suppression(uid) > 0.3:
				info["suppressed"] += 1
			# Check if unit has a target (in combat)
			if sim.get_target(uid) >= 0:
				info["in_combat"] += 1

	# Format each squad
	var sorted_ids := squad_ids.keys()
	sorted_ids.sort()
	for sq_id in sorted_ids:
		var info: Dictionary = squad_ids[sq_id]
		var count: int = info["count"]
		var avg_pos: Vector3 = info["pos"] / float(count)
		var sec := world_to_sector(avg_pos)
		var label := sector_label(sec.x, sec.y)
		var avg_morale: float = info["morale_sum"] / float(count)

		# Determine status
		var status := "READY"
		if info["suppressed"] > count / 2:
			status = "PINNED"
		elif info["in_combat"] > 0:
			status = "CONTACT"
		elif avg_morale < 0.3:
			status = "BROKEN"

		# Check if squad is in contact range of enemies
		var sim_sq_id: int = sq_id
		if sim.is_squad_in_contact(sim_sq_id, 60.0):
			if status == "READY":
				status = "CONTACT"

		lines.append("  SQ%d: %d units @ %s | morale:%.0f%% | %s" % [
			sq_id, count, label, avg_morale * 100.0, status])

	return "\n".join(lines)

func format_briefing(sim: SimulationServer, gpu_tac: GpuTacticalMap,
					 phero: PheromoneMapCPP, team: int) -> String:
	## Full briefing for LLM: sector map + squad roster.
	## Call update() first to refresh data.
	var parts: PackedStringArray
	parts.append(format_for_llm(team))
	parts.append("")
	parts.append(format_squad_roster(sim, team))
	return "\n".join(parts)
