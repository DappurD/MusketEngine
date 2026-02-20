class_name LLMPromptBuilder
extends RefCounted
## Builds compressed battlefield briefings from Theater Commander debug data.


static func build_briefing(theater: TheaterCommander) -> String:
	var debug = theater.get_debug_info()
	var snapshot: Dictionary = debug.get("snapshot", {})
	var axes: Dictionary = debug.get("axes", {})

	var lines: Array[String] = []

	# Header
	lines.append("## Battlefield Briefing (T+%.0fs)" % debug.get("total_elapsed", 0.0))
	lines.append("")

	# Force balance
	var friendly = snapshot.get("friendly_alive", 0)
	var enemy = snapshot.get("enemy_alive", 0)
	var ratio = snapshot.get("force_ratio", 0.5)
	lines.append("**Forces:** %d friendly vs %d enemy (ratio: %.2f)" % [friendly, enemy, ratio])

	# Morale + Casualties
	var morale = snapshot.get("avg_morale", 0.5)
	var casualty_rate = snapshot.get("casualty_rate", 0.0)
	lines.append("**Morale:** %.2f | **Casualty Rate:** %.1f%%" % [morale, casualty_rate * 100])

	# Capture points
	var poi_ratio = snapshot.get("poi_ownership", 0.0)
	lines.append("**Territory:** %.0f%% captured" % (poi_ratio * 100))

	# Medical situation
	var medical_ratio = snapshot.get("medical_ratio", 0.0)
	if medical_ratio > 0.2:
		lines.append("**Medical:** %.0f%% wounded/downed (HIGH)" % (medical_ratio * 100))

	# Enemy state
	var enemy_retreating = snapshot.get("enemy_retreating", 0.0)
	var enemy_exposed = snapshot.get("enemy_exposure", 0.0)
	if enemy_retreating > 0.3:
		lines.append("**Enemy:** %.0f%% retreating" % (enemy_retreating * 100))
	if enemy_exposed > 0.5:
		lines.append("**Enemy:** %.0f%% exposed (not in cover)" % (enemy_exposed * 100))

	# Reserves
	var reserve_ratio = snapshot.get("reserve_ratio", 0.0)
	lines.append("**Reserves:** %.0f%% of squads in reserve" % (reserve_ratio * 100))

	# Current axis values (what the AI is currently prioritizing)
	lines.append("")
	lines.append("**Current Priorities (0.0-1.0):**")
	for axis_name in axes:
		var axis_data: Dictionary = axes[axis_name]
		var score = axis_data.get("score", 0.0)
		var modifier = axis_data.get("weight_modifier", 1.0)
		if score > 0.6 or modifier != 1.0:  # Only show high values or modified axes
			lines.append("- %s: %.2f (mod: %.1fx)" % [axis_name, score, modifier])

	# Current posture
	var posture = debug.get("current_posture", "none")
	lines.append("")
	lines.append("**Active Posture:** %s" % posture)

	return "\n".join(PackedStringArray(lines))


static func build_sector_briefing(sector_grid: SectorGrid,
		sim: SimulationServer, theater: TheaterCommander, team: int,
		battle_memory: BattleMemory = null) -> String:
	## Build a sector-grid-based briefing for LLM sector command mode.
	## Combines sector map, squad roster, strategic context, and battle memory.
	var parts: PackedStringArray

	# Strategic header
	var debug = theater.get_debug_info() if theater else {}
	var snapshot: Dictionary = debug.get("snapshot", {})
	var elapsed: float = debug.get("total_elapsed", 0.0)
	var friendly: int = snapshot.get("friendly_alive", 0)
	var enemy: int = snapshot.get("enemy_alive", 0)
	var morale: float = snapshot.get("avg_morale", 0.7)
	var poi_ratio: float = snapshot.get("poi_ownership", 0.0)

	parts.append("## Situation Report (T+%.0fs)" % elapsed)
	parts.append("Forces: %d friendly vs %d enemy | Morale: %.0f%% | Territory: %.0f%%" % [
		friendly, enemy, morale * 100.0, poi_ratio * 100.0])

	# Casualty rate if significant
	var casualty_rate: float = snapshot.get("casualty_rate", 0.0)
	if casualty_rate > 0.1:
		parts.append("Casualty rate: %.0f%%" % (casualty_rate * 100.0))

	parts.append("")

	# Battle memory feedback (if available)
	if battle_memory != null:
		var memory_text := battle_memory.format_for_briefing()
		if not memory_text.is_empty():
			parts.append(memory_text)
			parts.append("")

	# Sector map + squad roster from SectorGrid
	parts.append(sector_grid.format_for_llm(team))
	parts.append("")
	parts.append(sector_grid.format_squad_roster(sim, team))

	return "\n".join(parts)
