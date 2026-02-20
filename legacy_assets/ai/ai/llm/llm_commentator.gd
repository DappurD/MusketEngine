class_name LLMCommentator
extends RefCounted
## LLM Commentator: event-driven bark system for battlefield commentary.
##
## A secondary cheap LLM that reacts to significant battlefield events with
## dramatic flavor text. Operates independently from the strategic LLM.
##
## Modes:
##   - neutral: One commentator watches both sides (sports-caster style)
##   - team: Biased toward one team's perspective (general barking at troops)
##
## USAGE:
##   var commentator = LLMCommentator.new()
##   commentator.setup(sim, config, parent_node, event_ticker, team_filter)
##   # Each frame:
##   commentator.tick(delta)

signal bark_received(text: String, category: String)

# ── Constants ────────────────────────────────────────────────────────────────
const MIN_BARK_INTERVAL: float = 10.0    ## Hard rate cap: max 1 request per 10s
const TIMEOUT_SEC: float = 8.0           ## HTTP timeout (faster than strategic LLM)
const MAX_QUEUE_SIZE: int = 8            ## Priority queue cap
const BARK_MAX_TOKENS: int = 80          ## Short responses (~1-2 sentences)
const MAX_BARKS_PER_MINUTE: int = 6      ## Budget guard
const DETECT_INTERVAL: float = 0.5       ## Event detection poll rate
const BARK_COLOR := Color(1.0, 0.85, 0.3)  ## Gold/amber — distinct from system events

## Cost per million tokens by model (input, output)
const COST_TABLE := {
	"gpt-4o-mini": [0.15, 0.60],
	"gemini-2.0-flash": [0.075, 0.30],
	"claude-3-haiku-20240307": [0.25, 1.25],
}

# ── Priority levels (higher = sent first) ────────────────────────────────────
enum BarkPriority { LOW = 0, MEDIUM = 1, HIGH = 2, CRITICAL = 3 }

# ── System prompts ───────────────────────────────────────────────────────────
const NEUTRAL_SYSTEM_PROMPT := """You are a grizzled military commentator watching a tactical battle between two AI armies. React to the event described with a single short, punchy observation (1-2 sentences, under 120 characters preferred). Be dramatic, opinionated, and colorful — shocked, impressed, contemptuous, or darkly humorous. Never be neutral or boring. No hashtags, no emojis, no questions. Just react."""

const TEAM_SYSTEM_PROMPT := """You are the commanding general of Team %d in a tactical battle. React to the event with 1-2 punchy sentences (under 120 characters preferred). Be personally invested in YOUR army — praise brave troops, berate cowards, mourn losses, gloat over enemy defeats. No hashtags, no emojis, no questions."""

# ── External references ──────────────────────────────────────────────────────
var _sim: SimulationServer
var _config: LLMConfig
var _http: HTTPRequest
var _event_ticker  # EventTicker control (may be null)
var _team_filter: int = -1  ## -1=neutral (both teams), 0=team1, 1=team2

# ── State ────────────────────────────────────────────────────────────────────
var _enabled: bool = false
var _pending: bool = false
var _cooldown: float = 0.0
var _detect_timer: float = 0.0
var _bark_queue: Array[Dictionary] = []  ## [{category, priority, context, time}]
var _request_start_time: int = 0
var _system_prompt: String = ""

# ── Per-minute rate limiter ──────────────────────────────────────────────────
var _barks_this_minute: int = 0
var _minute_timer: float = 0.0

# ── Cost tracking ────────────────────────────────────────────────────────────
var _total_input_tokens: int = 0
var _total_output_tokens: int = 0
var _total_requests: int = 0
var _total_cost_usd: float = 0.0
var _budget_limit_usd: float = 0.0  ## 0 = unlimited

# ── Event detection state (delta tracking, mirrors EventTicker pattern) ──────
var _prev_alive_t1: int = 0
var _prev_alive_t2: int = 0
var _prev_berserk: int = 0
var _prev_frozen: int = 0
var _prev_paranoid_ff: int = 0
var _prev_captures_t1: int = 0
var _prev_captures_t2: int = 0
var _prev_morale_t1: float = 1.0
var _prev_morale_t2: float = 1.0
var _first_blood_fired: bool = false
var _routed_t1_fired: bool = false
var _routed_t2_fired: bool = false
var _tracked_squads: Dictionary = {}  ## {squad_id: last_alive_count}
var _cowardice_scan_offset: int = 0   ## Amortized unit scan position


func setup(sim: SimulationServer, config: LLMConfig, parent: Node,
		event_ticker = null, team_filter: int = -1) -> void:
	_sim = sim
	_config = config
	_event_ticker = event_ticker
	_team_filter = team_filter
	_enabled = config.enabled

	# Build system prompt based on mode
	if team_filter >= 0:
		_system_prompt = TEAM_SYSTEM_PROMPT % (team_filter + 1)
	else:
		_system_prompt = NEUTRAL_SYSTEM_PROMPT

	# Budget from env
	var budget_str := OS.get_environment("LLM_COMMENTATOR_BUDGET")
	if not budget_str.is_empty():
		_budget_limit_usd = budget_str.to_float()

	if _enabled:
		_http = HTTPRequest.new()
		_http.timeout = TIMEOUT_SEC
		parent.add_child(_http)
		_http.request_completed.connect(_on_http_completed)
		print("[Commentator%s] Initialized (%s, model: %s, rate: %.0fs)" % [
			" T%d" % (team_filter + 1) if team_filter >= 0 else "",
			config.provider, config.model, MIN_BARK_INTERVAL])


func tick(delta: float) -> void:
	if not _enabled:
		return

	# Per-minute rate reset
	_minute_timer += delta
	if _minute_timer >= 60.0:
		_minute_timer = 0.0
		_barks_this_minute = 0

	# Event detection
	_detect_timer += delta
	if _detect_timer >= DETECT_INTERVAL:
		_detect_timer = 0.0
		_detect_events()

	# Drain queue
	_cooldown -= delta
	if _cooldown <= 0.0 and not _pending and not _bark_queue.is_empty():
		if _barks_this_minute < MAX_BARKS_PER_MINUTE:
			if _budget_limit_usd <= 0.0 or _total_cost_usd < _budget_limit_usd:
				var event: Dictionary = _bark_queue.pop_front()
				_send_bark(event)


# ── Event Detection ──────────────────────────────────────────────────────────

func _detect_events() -> void:
	if not _sim:
		return

	var stats: Dictionary = _sim.get_debug_stats()
	var alive1: int = _sim.get_alive_count_for_team(1)
	var alive2: int = _sim.get_alive_count_for_team(2)
	var total: int = _sim.get_unit_count() / 2
	if total <= 0:
		total = 1

	# Detect sim restart (alive jumped up)
	if alive1 > _prev_alive_t1 + 10 or alive2 > _prev_alive_t2 + 10:
		reset_tracking()
		_prev_alive_t1 = alive1
		_prev_alive_t2 = alive2
		return

	# ── First blood ──
	if not _first_blood_fired and _prev_alive_t1 > 0:
		if alive1 < _prev_alive_t1 or alive2 < _prev_alive_t2:
			_first_blood_fired = true
			var team := 2 if alive1 < _prev_alive_t1 else 1
			_enqueue_bark("FIRST_BLOOD", BarkPriority.CRITICAL, {
				"team": team, "game_time": _sim.get_game_time()})

	# ── Mass casualty (5+ deaths in one cycle) ──
	if _prev_alive_t1 > 0:
		var lost1: int = _prev_alive_t1 - alive1
		var lost2: int = _prev_alive_t2 - alive2
		if lost1 >= 5 and _passes_team_filter(0):
			_enqueue_bark("MASS_CASUALTY", BarkPriority.MEDIUM, {
				"team": 1, "count": lost1})
		if lost2 >= 5 and _passes_team_filter(1):
			_enqueue_bark("MASS_CASUALTY", BarkPriority.MEDIUM, {
				"team": 2, "count": lost2})

	# ── Enemy routed (<15% strength) ──
	var ratio1: float = float(alive1) / float(total)
	var ratio2: float = float(alive2) / float(total)
	if not _routed_t1_fired and ratio1 < 0.15 and alive1 > 0 and _prev_alive_t1 > 0:
		_routed_t1_fired = true
		_enqueue_bark("ENEMY_ROUTED", BarkPriority.CRITICAL, {
			"team": 1, "alive": alive1,
			"loss_pct": (1.0 - ratio1) * 100.0})
	if not _routed_t2_fired and ratio2 < 0.15 and alive2 > 0 and _prev_alive_t2 > 0:
		_routed_t2_fired = true
		_enqueue_bark("ENEMY_ROUTED", BarkPriority.CRITICAL, {
			"team": 2, "alive": alive2,
			"loss_pct": (1.0 - ratio2) * 100.0})

	# ── Morale collapse ──
	# Need morale data — check if BattleCommandUI cached values are accessible
	# For now, use force ratio as proxy (morale data requires UI ref)

	# ── Berserk / Frozen ──
	var berserk: int = stats.get("berserk_units", 0)
	var frozen: int = stats.get("frozen_units", 0)
	if berserk > _prev_berserk and berserk > 0:
		var delta_b: int = berserk - _prev_berserk
		_enqueue_bark("BERSERK_CHARGE", BarkPriority.MEDIUM, {
			"count": delta_b})
	if frozen > _prev_frozen and frozen > 0:
		var delta_f: int = frozen - _prev_frozen
		_enqueue_bark("MORALE_COLLAPSE", BarkPriority.HIGH, {
			"count": delta_f, "type": "frozen"})
	_prev_berserk = berserk
	_prev_frozen = frozen

	# ── Friendly fire (paranoid) ──
	var paranoid_ff: int = stats.get("paranoid_ff_units", 0)
	if paranoid_ff > _prev_paranoid_ff and paranoid_ff > 0:
		_enqueue_bark("FRIENDLY_FIRE", BarkPriority.MEDIUM, {
			"count": paranoid_ff - _prev_paranoid_ff})
	_prev_paranoid_ff = paranoid_ff

	# ── Capture point changes ──
	if _sim.has_method("get_capture_data"):
		var cap_data: Dictionary = _sim.get_capture_data()
		if cap_data.has("owner_team"):
			var owners = cap_data["owner_team"]
			var new_t1: int = 0
			var new_t2: int = 0
			for o in owners:
				if o == 0:
					new_t1 += 1
				elif o == 1:
					new_t2 += 1
			if new_t1 > _prev_captures_t1 and _passes_team_filter(0):
				_enqueue_bark("CAPTURE_POINT", BarkPriority.HIGH, {
					"team": 1, "gained": true})
			elif new_t1 < _prev_captures_t1 and _passes_team_filter(0):
				_enqueue_bark("CAPTURE_POINT", BarkPriority.HIGH, {
					"team": 1, "gained": false})
			if new_t2 > _prev_captures_t2 and _passes_team_filter(1):
				_enqueue_bark("CAPTURE_POINT", BarkPriority.HIGH, {
					"team": 2, "gained": true})
			elif new_t2 < _prev_captures_t2 and _passes_team_filter(1):
				_enqueue_bark("CAPTURE_POINT", BarkPriority.HIGH, {
					"team": 2, "gained": false})
			_prev_captures_t1 = new_t1
			_prev_captures_t2 = new_t2

	# ── Squad wipe + Heroic stand (amortized scan) ──
	_check_squad_events()

	# ── Cowardice (amortized unit scan, 50 per cycle) ──
	_check_cowardice()

	_prev_alive_t1 = alive1
	_prev_alive_t2 = alive2


func _check_squad_events() -> void:
	## Check for squad wipes and heroic stands via alive count deltas.
	var unit_count: int = _sim.get_unit_count()
	if unit_count <= 0:
		return

	# Build squad alive counts
	var squad_alive: Dictionary = {}
	for i in unit_count:
		if not _sim.get_alive(i):
			continue
		var sq: int = _sim.get_squad_id(i)
		if sq < 0:
			continue
		squad_alive[sq] = squad_alive.get(sq, 0) + 1

	# Check for transitions
	for sq_id in _tracked_squads:
		var prev_count: int = _tracked_squads[sq_id]
		var curr_count: int = squad_alive.get(sq_id, 0)

		# Squad wipe: was alive, now all dead
		if prev_count > 0 and curr_count == 0:
			_enqueue_bark("SQUAD_WIPE", BarkPriority.HIGH, {
				"squad_id": sq_id, "prev_count": prev_count})

		# Heroic stand: exactly 1 survivor in a squad that had 3+
		if curr_count == 1 and prev_count >= 3:
			_enqueue_bark("HEROIC_STAND", BarkPriority.CRITICAL, {
				"squad_id": sq_id})

	_tracked_squads = squad_alive


func _check_cowardice() -> void:
	## Amortized scan: check 50 units per cycle for healthy retreaters.
	var unit_count: int = _sim.get_unit_count()
	if unit_count <= 0:
		return

	var batch_size: int = mini(50, unit_count)
	for _j in batch_size:
		var i: int = _cowardice_scan_offset % unit_count
		_cowardice_scan_offset += 1

		if not _sim.get_alive(i):
			continue
		if _sim.get_state(i) != 6:  # ST_RETREATING = 6
			continue
		if _sim.get_health(i) < 0.7:
			continue

		# Healthy unit retreating — potential cowardice
		var team: int = _sim.get_team(i) + 1
		if _passes_team_filter(_sim.get_team(i)):
			_enqueue_bark("COWARDICE", BarkPriority.LOW, {
				"team": team, "unit_id": i,
				"health": _sim.get_health(i),
				"role_name": _role_name(_sim.get_role(i))})
			break  # One per cycle max


# ── Queue Management ─────────────────────────────────────────────────────────

func _enqueue_bark(category: String, priority: int, context: Dictionary) -> void:
	## Insert event into priority queue. Dedup by category.
	# Skip if same category already queued
	for existing in _bark_queue:
		if existing["category"] == category:
			return

	var entry := {"category": category, "priority": priority, "context": context,
		"time": Time.get_ticks_msec()}

	# Insert sorted by priority (highest first)
	var inserted := false
	for idx in _bark_queue.size():
		if priority > _bark_queue[idx]["priority"]:
			_bark_queue.insert(idx, entry)
			inserted = true
			break
	if not inserted:
		_bark_queue.append(entry)

	# Cap queue size
	while _bark_queue.size() > MAX_QUEUE_SIZE:
		_bark_queue.pop_back()


# ── HTTP Request ─────────────────────────────────────────────────────────────

func _send_bark(event: Dictionary) -> void:
	if not _http or _pending:
		return

	var prompt := _build_bark_prompt(event)
	var body := _build_request_body(prompt)
	var headers := _build_headers()
	var url := _config.get_endpoint_url()

	_pending = true
	_request_start_time = Time.get_ticks_msec()
	_cooldown = MIN_BARK_INTERVAL

	var err := _http.request(url, headers, HTTPClient.METHOD_POST, body)
	if err != OK:
		_pending = false
		print("[Commentator] HTTP request error: %d" % err)


func _build_bark_prompt(event: Dictionary) -> String:
	var cat: String = event["category"]
	var ctx: Dictionary = event.get("context", {})

	match cat:
		"FIRST_BLOOD":
			return "EVENT: First kill of the battle. Team %d drew first blood." % [
				ctx.get("team", 1)]
		"SQUAD_WIPE":
			return "EVENT: Squad %d has been completely wiped out. All soldiers eliminated." % [
				ctx.get("squad_id", 0)]
		"HEROIC_STAND":
			return "EVENT: A lone survivor from Squad %d is the last one standing, still fighting against overwhelming odds." % [
				ctx.get("squad_id", 0)]
		"MORALE_COLLAPSE":
			return "EVENT: %d soldier(s) have broken psychologically — frozen in terror on the battlefield." % [
				ctx.get("count", 1)]
		"BERSERK_CHARGE":
			return "EVENT: %d soldier(s) went berserk — charging recklessly into the enemy, ignoring all orders." % [
				ctx.get("count", 1)]
		"FRIENDLY_FIRE":
			return "EVENT: A paranoid soldier is firing on their own allies!"
		"MASS_CASUALTY":
			return "EVENT: Team %d just lost %d soldiers in seconds." % [
				ctx.get("team", 1), ctx.get("count", 5)]
		"COWARDICE":
			return "EVENT: A %s from Team %d is retreating despite being at %.0f%% health." % [
				ctx.get("role_name", "soldier"), ctx.get("team", 1),
				ctx.get("health", 1.0) * 100.0]
		"CAPTURE_POINT":
			var action := "captured" if ctx.get("gained", true) else "lost"
			return "EVENT: Team %d %s a capture point! Territory control shifted." % [
				ctx.get("team", 1), action]
		"ENEMY_ROUTED":
			return "EVENT: Team %d is down to %d survivors (%.0f%% casualties). They are being routed." % [
				ctx.get("team", 1), ctx.get("alive", 0), ctx.get("loss_pct", 85.0)]

	return "EVENT: Something significant happened on the battlefield."


func _build_request_body(prompt: String) -> String:
	var payload: Dictionary

	if _config.provider == "anthropic":
		payload = {
			"model": _config.model,
			"system": _system_prompt,
			"messages": [{"role": "user", "content": prompt}],
			"max_tokens": _config.max_tokens,
			"temperature": _config.temperature
		}
	else:
		# OpenAI-compatible (openai, ollama, local)
		payload = {
			"model": _config.model,
			"messages": [
				{"role": "system", "content": _system_prompt},
				{"role": "user", "content": prompt}
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


# ── Response Handling ────────────────────────────────────────────────────────

func _on_http_completed(result: int, response_code: int,
		_headers: PackedStringArray, body: PackedByteArray) -> void:
	_pending = false
	var latency := Time.get_ticks_msec() - _request_start_time

	if result != HTTPRequest.RESULT_SUCCESS or response_code != 200:
		return  # Silently drop failed barks (they're ephemeral)

	var json_str := body.get_string_from_utf8()
	var json := JSON.new()
	if json.parse(json_str) != OK:
		return

	var data: Dictionary = json.data
	var content := _extract_content(data)
	if content.is_empty():
		return

	# Track token usage
	_track_usage(data)

	_total_requests += 1
	_barks_this_minute += 1

	_deliver_bark(content)


func _extract_content(data: Dictionary) -> String:
	if _config.provider == "anthropic":
		var content_arr: Array = data.get("content", [])
		if not content_arr.is_empty():
			return str(content_arr[0].get("text", ""))
	else:
		var choices: Array = data.get("choices", [])
		if not choices.is_empty():
			return str(choices[0].get("message", {}).get("content", ""))
	return ""


func _track_usage(data: Dictionary) -> void:
	var input_t: int = 0
	var output_t: int = 0

	if _config.provider == "anthropic":
		var u: Dictionary = data.get("usage", {})
		input_t = u.get("input_tokens", 0)
		output_t = u.get("output_tokens", 0)
	else:
		var u: Dictionary = data.get("usage", {})
		input_t = u.get("prompt_tokens", 0)
		output_t = u.get("completion_tokens", 0)

	_total_input_tokens += input_t
	_total_output_tokens += output_t

	# Estimate cost
	var model_key := _config.model
	if COST_TABLE.has(model_key):
		var rates: Array = COST_TABLE[model_key]
		_total_cost_usd += (float(input_t) / 1_000_000.0) * rates[0]
		_total_cost_usd += (float(output_t) / 1_000_000.0) * rates[1]


func _deliver_bark(text: String) -> void:
	# Clean up: strip surrounding quotes, limit length
	text = text.strip_edges()
	if text.begins_with("\"") and text.ends_with("\""):
		text = text.substr(1, text.length() - 2)
	if text.length() > 200:
		text = text.substr(0, 197) + "..."

	# Output to EventTicker
	if _event_ticker and _event_ticker.has_method("add_event"):
		var prefix := "[CMD] " if _team_filter < 0 else "[GEN T%d] " % (_team_filter + 1)
		_event_ticker.add_event(prefix + text, BARK_COLOR)

	bark_received.emit(text, "")


# ── Helpers ──────────────────────────────────────────────────────────────────

func _passes_team_filter(team_idx: int) -> bool:
	## Returns true if this event is relevant to our perspective.
	return _team_filter < 0 or _team_filter == team_idx


func _role_name(role: int) -> String:
	match role:
		0: return "Rifleman"
		1: return "MG Gunner"
		2: return "Marksman"
		3: return "Grenadier"
		4: return "Medic"
		5: return "Leader"
		6: return "Mortar"
	return "soldier"


# ── Public API ───────────────────────────────────────────────────────────────

func get_stats() -> Dictionary:
	return {
		"enabled": _enabled,
		"team_filter": _team_filter,
		"total_requests": _total_requests,
		"total_input_tokens": _total_input_tokens,
		"total_output_tokens": _total_output_tokens,
		"total_cost_usd": _total_cost_usd,
		"budget_limit_usd": _budget_limit_usd,
		"budget_remaining": _budget_limit_usd - _total_cost_usd if _budget_limit_usd > 0 else -1.0,
		"barks_this_minute": _barks_this_minute,
		"queue_size": _bark_queue.size(),
		"pending": _pending,
		"cooldown": maxf(0.0, _cooldown),
	}


func reset_tracking() -> void:
	_prev_alive_t1 = 0
	_prev_alive_t2 = 0
	_prev_berserk = 0
	_prev_frozen = 0
	_prev_paranoid_ff = 0
	_prev_captures_t1 = 0
	_prev_captures_t2 = 0
	_prev_morale_t1 = 1.0
	_prev_morale_t2 = 1.0
	_first_blood_fired = false
	_routed_t1_fired = false
	_routed_t2_fired = false
	_tracked_squads.clear()
	_cowardice_scan_offset = 0
	_bark_queue.clear()
	_barks_this_minute = 0
	_minute_timer = 0.0
	_cooldown = 0.0
	_pending = false
