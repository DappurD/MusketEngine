class_name LLMSectorCommander
extends RefCounted
## LLM Sector Commander: async HTTP-driven squad order system.
##
## Sends sector grid briefings to an LLM, parses squad orders from the response,
## validates them, and pushes directives into ColonyAICPP's score matrix.
##
## USAGE:
##   var cmd = LLMSectorCommander.new()
##   cmd.setup(theater, colony_ai_cpp, sim, gpu_map, phero, config, parent_node, team)
##   cmd.orders_received.connect(_on_sector_orders)
##   # Each frame:
##   cmd.tick(delta)

signal orders_received(orders: Array, reasoning: String)
signal orders_failed(error: String)

# ── External references ─────────────────────────────────────────────────────
var _theater: TheaterCommander
var _colony: ColonyAICPP
var _sim: SimulationServer
var _gpu_map: GpuTacticalMap
var _phero: PheromoneMapCPP
var _config: LLMConfig
var _http: HTTPRequest
var _team: int = 1
var _squad_count: int = 0

# ── Sector grid (created once, updated each cycle) ──────────────────────────
var _sector_grid: SectorGrid
var _base_sector: Vector2i = Vector2i(-1, -1)

# ── Battle memory (tracks order outcomes for LLM feedback) ──────────────────
var _battle_memory: BattleMemory

# ── Timing ──────────────────────────────────────────────────────────────────
var _timer: float = 0.0
var _interval: float = 45.0  # Base interval (seconds)
var _enabled: bool = false
var _pending: bool = false
var _request_start_time: int = 0

# ── Failure cooldown (interval doubles after consecutive failures) ──────────
var _consecutive_failures: int = 0
const FAILURE_COOLDOWN_THRESHOLD: int = 3    # Start doubling after this many
const MAX_INTERVAL: float = 300.0            # 5 minute max
const MIN_INTERVAL: float = 15.0             # 15 second floor

# ── Cache for graceful degradation ──────────────────────────────────────────
var _last_valid_orders: Array[Dictionary] = []
var _last_briefing: String = ""
var _last_reasoning: String = ""

# ── Stats ───────────────────────────────────────────────────────────────────
var _request_count: int = 0
var _success_count: int = 0
var _fail_count: int = 0
var _total_latency_ms: float = 0.0
var _rejected_order_count: int = 0

const TIMEOUT_SEC: float = 15.0


func setup(theater: TheaterCommander, colony: ColonyAICPP, sim: SimulationServer,
		gpu_map: GpuTacticalMap, phero: PheromoneMapCPP,
		config: LLMConfig, parent: Node, team: int, squad_count: int,
		map_w: float = 600.0, map_h: float = 400.0) -> void:
	_theater = theater
	_colony = colony
	_sim = sim
	_gpu_map = gpu_map
	_phero = phero
	_config = config
	_team = team
	_squad_count = squad_count

	# For local/ollama providers, api_key may be empty — that's OK
	_enabled = config.enabled and config.command_mode == "sector"

	# Create sector grid
	_sector_grid = SectorGrid.new()
	_sector_grid.setup(map_w, map_h)

	# Estimate base sector from team direction
	var base_x: float = -map_w * 0.4 if team == 1 else map_w * 0.4
	_base_sector = _sector_grid.world_to_sector(Vector3(base_x, 0, 0))

	# Battle memory for outcome tracking
	_battle_memory = BattleMemory.new()
	_battle_memory.setup(sim, team)

	if _enabled:
		_http = HTTPRequest.new()
		_http.timeout = TIMEOUT_SEC
		parent.add_child(_http)
		_http.request_completed.connect(_on_http_completed)
		print("[LLMSectorCmd T%d] Initialized (%s, model: %s, interval: %.0fs)" % [
			team, config.provider, config.model, _interval])
	else:
		if config.command_mode != "sector":
			print("[LLMSectorCmd T%d] Skipped (command_mode=%s)" % [team, config.command_mode])
		else:
			print("[LLMSectorCmd T%d] Disabled (not enabled in config)" % team)


func tick(delta: float) -> void:
	if not _enabled or _sim == null:
		return

	_timer -= delta
	if _timer <= 0.0 and not _pending:
		_send_request()
		_timer = _get_effective_interval()


func set_interval(seconds: float) -> void:
	_interval = clampf(seconds, MIN_INTERVAL, MAX_INTERVAL)


func get_sector_grid() -> SectorGrid:
	return _sector_grid


func get_current_orders() -> Array[Dictionary]:
	return _last_valid_orders


func get_stats() -> Dictionary:
	return {
		"enabled": _enabled,
		"command_mode": "sector",
		"team": _team,
		"requests": _request_count,
		"success": _success_count,
		"failures": _fail_count,
		"rejected_orders": _rejected_order_count,
		"avg_latency_ms": _total_latency_ms / maxf(1.0, _success_count),
		"pending": _pending,
		"interval_sec": _get_effective_interval(),
		"consecutive_failures": _consecutive_failures,
		"active_orders": _last_valid_orders.size(),
		"memory_cycles": _battle_memory._cycle_counter if _battle_memory else 0,
		"last_outcome_score": _battle_memory.get_last_cycle().outcome_score() \
			if _battle_memory and _battle_memory.get_last_cycle() != null else 0.0,
	}


func get_battle_memory() -> BattleMemory:
	return _battle_memory


func set_enabled(p_enabled: bool) -> void:
	_enabled = p_enabled and _config.command_mode == "sector"


# ── Request Building ───────────────────────────────────────────────────────

func _send_request() -> void:
	# Update sector grid with latest battlefield data
	_sector_grid.update(_sim, _gpu_map, _phero, _team)

	# Build briefing (includes battle memory if available)
	var briefing := LLMPromptBuilder.build_sector_briefing(
		_sector_grid, _sim, _theater, _team, _battle_memory)
	_last_briefing = briefing

	var body := _build_request_body(briefing)
	var headers := _build_headers()

	_pending = true
	_request_start_time = Time.get_ticks_msec()
	_request_count += 1

	var url := _config.get_endpoint_url()
	var err := _http.request(url, headers, HTTPClient.METHOD_POST, body)

	if err != OK:
		push_error("[LLMSectorCmd T%d] HTTP request failed: %d" % [_team, err])
		_pending = false
		_on_request_failed("HTTP request error: %d" % err)


func _build_request_body(briefing: String) -> String:
	var sys_prompt := _config.get_active_system_prompt()
	var payload: Dictionary

	if _config.provider == "anthropic":
		payload = {
			"model": _config.model,
			"system": sys_prompt,
			"messages": [{"role": "user", "content": briefing}],
			"max_tokens": _config.max_tokens,
			"temperature": _config.temperature
		}
	else:
		# OpenAI-compatible (openai, ollama, local)
		payload = {
			"model": _config.model,
			"messages": [
				{"role": "system", "content": sys_prompt},
				{"role": "user", "content": briefing}
			],
			"max_tokens": _config.max_tokens,
			"temperature": _config.temperature
		}

	return JSON.stringify(payload)


func _build_headers() -> PackedStringArray:
	var headers := PackedStringArray()
	headers.append("Content-Type: application/json")

	if _config.provider == "anthropic":
		headers.append("x-api-key: %s" % _config.api_key)
		headers.append("anthropic-version: 2023-06-01")
	elif _config.provider == "openai":
		headers.append("Authorization: Bearer %s" % _config.api_key)
	elif _config.provider in ["ollama", "local"]:
		if not _config.api_key.is_empty():
			headers.append("Authorization: Bearer %s" % _config.api_key)

	return headers


# ── Response Processing ────────────────────────────────────────────────────

func _on_http_completed(result: int, response_code: int,
		_headers: PackedStringArray, body: PackedByteArray) -> void:
	_pending = false
	var latency := Time.get_ticks_msec() - _request_start_time
	_total_latency_ms += latency

	if result != HTTPRequest.RESULT_SUCCESS:
		_on_request_failed("HTTP error: %d" % result)
		return

	if response_code != 200:
		var body_str := body.get_string_from_utf8()
		_on_request_failed("API error %d: %s" % [response_code, body_str.substr(0, 200)])
		return

	var json_str := body.get_string_from_utf8()
	var json := JSON.new()
	if json.parse(json_str) != OK:
		_on_request_failed("JSON parse error")
		return

	var data: Dictionary = json.data
	var content := _extract_content(data)
	var reasoning := _extract_reasoning(content)
	var raw_orders := _extract_orders(content)

	if raw_orders.is_empty():
		_on_request_failed("No valid orders in response")
		return

	# Validate orders
	var validated := SectorOrderValidator.validate_orders(
		raw_orders, _sector_grid, _sim, _team, _squad_count, _base_sector)

	var rejected_count := raw_orders.size() - validated.size()
	_rejected_order_count += rejected_count

	if validated.is_empty():
		_on_request_failed("All %d orders rejected by validator" % raw_orders.size())
		return

	# Success — apply directives to ColonyAICPP
	_last_valid_orders = validated
	_last_reasoning = reasoning
	_success_count += 1
	_consecutive_failures = 0  # Reset cooldown

	# Record cycle in battle memory (finalizes previous, snapshots current)
	_battle_memory.begin_cycle(validated, _sim.get_game_time())

	_apply_directives(validated)

	var order_summary := _format_order_summary(validated)
	print("[LLMSectorCmd T%d] Response (%.0fms): %s" % [_team, latency, reasoning.substr(0, 100)])
	print("[LLMSectorCmd T%d] Orders: %s" % [_team, order_summary])
	if rejected_count > 0:
		print("[LLMSectorCmd T%d] Rejected %d orders" % [_team, rejected_count])

	orders_received.emit(validated, reasoning)


func _extract_content(data: Dictionary) -> String:
	if _config.provider == "anthropic":
		var content_arr: Array = data.get("content", [])
		if not content_arr.is_empty():
			return str(content_arr[0].get("text", ""))
	else:
		# OpenAI-compatible
		var choices: Array = data.get("choices", [])
		if not choices.is_empty():
			return str(choices[0].get("message", {}).get("content", ""))
	return ""


func _extract_reasoning(content: String) -> String:
	# Everything before the JSON block is reasoning
	var json_start := content.find("```json")
	if json_start > 0:
		return content.substr(0, json_start).strip_edges()
	# Try finding raw JSON object
	var brace_start := content.find("{")
	if brace_start > 10:  # Some reasoning before JSON
		return content.substr(0, brace_start).strip_edges()
	return content.substr(0, 200)


func _extract_orders(content: String) -> Array:
	## Parse orders from LLM response content.
	## Supports: ```json { "orders": [...] } ``` or raw JSON.
	var json_str := ""

	# Try markdown code fence first
	var fence_start := content.find("```json")
	if fence_start >= 0:
		var fence_end := content.find("```", fence_start + 7)
		if fence_end > fence_start:
			json_str = content.substr(fence_start + 7, fence_end - fence_start - 7).strip_edges()

	# Fallback: find raw JSON object
	if json_str.is_empty():
		var brace_start := content.find("{")
		var brace_end := content.rfind("}")
		if brace_start >= 0 and brace_end > brace_start:
			json_str = content.substr(brace_start, brace_end - brace_start + 1)

	if json_str.is_empty():
		return []

	var json := JSON.new()
	if json.parse(json_str) != OK:
		push_warning("[LLMSectorCmd T%d] Failed to parse JSON: %s" % [_team, json_str.substr(0, 100)])
		return []

	var parsed = json.data
	if parsed is Dictionary:
		# Expected format: { "orders": [...] }
		var orders = parsed.get("orders", [])
		if orders is Array:
			return orders
		# Maybe the dict itself is a single order
		if parsed.has("squad") and parsed.has("sector"):
			return [parsed]
	elif parsed is Array:
		return parsed

	return []


func _apply_directives(validated_orders: Array[Dictionary]) -> void:
	## Push validated orders into ColonyAICPP via set_llm_directive().
	if _colony == null:
		return

	# Clear previous directives for this team's squads
	_colony.clear_all_llm_directives()

	for order in validated_orders:
		var sq_idx: int = order["squad"]
		var sector_col: int = order["sector_col"]
		var sector_row: int = order["sector_row"]
		var intent_idx: int = order["intent_idx"]
		var confidence: float = order["confidence"]

		_colony.set_llm_directive(sq_idx, sector_col, sector_row, intent_idx, confidence)

	# Also deposit CH_STRATEGIC pheromone at ordered sector centers
	_deposit_strategic_pheromones(validated_orders)


func _deposit_strategic_pheromones(orders: Array[Dictionary]) -> void:
	## Deposit CH_STRATEGIC pheromone at ordered sector centers.
	## Units follow this gradient via context steering.
	## _phero is already the per-team PheromoneMapCPP — no team param needed.
	if _phero == null:
		return

	for order in orders:
		var center := _sector_grid.sector_center(order["sector_col"], order["sector_row"])
		# Deposit strength proportional to confidence and intent aggressiveness
		var strength: float = order["confidence"] * 8.0  # Base strength
		var intent_str: String = order.get("intent", "HOLD")
		if intent_str in ["ATTACK", "CAPTURE", "FLANK"]:
			strength *= 1.5  # Stronger pull for offensive intents
		_phero.deposit(center, SimulationServer.CH_STRATEGIC, strength)


func _on_request_failed(error: String) -> void:
	_fail_count += 1
	_consecutive_failures += 1

	push_warning("[LLMSectorCmd T%d] %s (failures: %d)" % [_team, error, _consecutive_failures])

	# Use cached orders if available
	if not _last_valid_orders.is_empty():
		_apply_directives(_last_valid_orders)
		orders_received.emit(_last_valid_orders, "[CACHED - %s]" % error)
	else:
		orders_failed.emit(error)


func _get_effective_interval() -> float:
	## Returns the effective interval, accounting for failure cooldown.
	if _consecutive_failures < FAILURE_COOLDOWN_THRESHOLD:
		return _interval

	# Double the interval for each failure beyond threshold (capped at MAX_INTERVAL)
	var doublings: int = _consecutive_failures - FAILURE_COOLDOWN_THRESHOLD
	var cooldown_interval: float = _interval * pow(2.0, mini(doublings, 6))
	return minf(cooldown_interval, MAX_INTERVAL)


func _format_order_summary(orders: Array[Dictionary]) -> String:
	var parts: PackedStringArray
	for order in orders:
		parts.append("SQ%d→%s:%s" % [
			order["squad"], order["sector_label"], order["intent"]])
	return ", ".join(parts)
