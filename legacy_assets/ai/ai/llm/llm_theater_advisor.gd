class_name LLMTheaterAdvisor
extends RefCounted
## Signal-based async LLM integration for Theater Commander weight adjustment.
##
## USAGE:
##   var advisor = LLMTheaterAdvisor.new()
##   advisor.setup(theater_commander, llm_config, parent_node)
##   advisor.request_completed.connect(_on_llm_response)
##   # Each frame:
##   advisor.tick(delta)

signal request_completed(weights: Dictionary, reasoning: String)
signal request_failed(error: String)

var _theater: TheaterCommander
var _config: LLMConfig
var _http: HTTPRequest
var _timer: float = 0.0
var _interval: float = 45.0  # Default 45s
var _enabled: bool = false
var _pending: bool = false

# Cache for graceful degradation
var _last_valid_weights: Dictionary = {}
var _last_briefing: String = ""
var _request_count: int = 0
var _success_count: int = 0
var _fail_count: int = 0
var _total_latency_ms: float = 0.0
var _request_start_time: int = 0

const TIMEOUT_MS: int = 10000  # 10s max wait
const MAX_RETRIES: int = 2
const AXIS_NAMES: Array[String] = [
	"aggression", "concentration", "tempo", "risk_tolerance",
	"exploitation", "terrain_control", "medical_priority",
	"suppression_dominance", "intel_coverage"
]


func setup(theater: TheaterCommander, config: LLMConfig, parent: Node) -> void:
	_theater = theater
	_config = config
	_enabled = config.enabled and not config.api_key.is_empty()

	if _enabled:
		_http = HTTPRequest.new()
		_http.timeout = TIMEOUT_MS / 1000.0
		parent.add_child(_http)
		_http.request_completed.connect(_on_http_completed)
		print("[LLMTheaterAdvisor] Initialized with %s provider" % config.provider)
	else:
		print("[LLMTheaterAdvisor] Disabled (no API key or disabled in config)")


func tick(delta: float) -> void:
	if not _enabled or _theater == null:
		return

	_timer -= delta
	if _timer <= 0.0 and not _pending:
		_send_request()
		_timer = _interval


func _send_request() -> void:
	var briefing = LLMPromptBuilder.build_briefing(_theater)
	_last_briefing = briefing

	var body = _build_request_body(briefing)
	var headers = _build_headers()

	_pending = true
	_request_start_time = Time.get_ticks_msec()
	_request_count += 1

	var url = _config.get_endpoint_url()
	var err = _http.request(url, headers, HTTPClient.METHOD_POST, body)

	if err != OK:
		push_error("[LLMTheaterAdvisor] HTTP request failed: %d" % err)
		_pending = false
		_fail_count += 1
		request_failed.emit("HTTP request error: %d" % err)


func _build_request_body(briefing: String) -> String:
	var payload = {}

	# Provider-specific request formats
	if _config.provider == "anthropic":
		# Anthropic format: separate system parameter
		payload = {
			"model": _config.model,
			"system": _config.system_prompt,
			"messages": [{"role": "user", "content": briefing}],
			"max_tokens": _config.max_tokens,
			"temperature": _config.temperature
		}
	else:
		# OpenAI-compatible format (openai, ollama, local)
		payload = {
			"model": _config.model,
			"messages": [
				{"role": "system", "content": _config.system_prompt},
				{"role": "user", "content": briefing}
			],
			"max_tokens": _config.max_tokens,
			"temperature": _config.temperature
		}

	return JSON.stringify(payload)


func _build_headers() -> PackedStringArray:
	var headers = PackedStringArray()
	headers.append("Content-Type: application/json")

	if _config.provider == "anthropic":
		headers.append("x-api-key: %s" % _config.api_key)
		headers.append("anthropic-version: 2023-06-01")
	elif _config.provider == "openai":
		headers.append("Authorization: Bearer %s" % _config.api_key)
	elif _config.provider in ["ollama", "local"]:
		# Local models typically don't need auth, but support it if provided
		if not _config.api_key.is_empty():
			headers.append("Authorization: Bearer %s" % _config.api_key)

	return headers


func _on_http_completed(result: int, response_code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	_pending = false
	var latency = Time.get_ticks_msec() - _request_start_time
	_total_latency_ms += latency

	if result != HTTPRequest.RESULT_SUCCESS:
		_on_request_failed("HTTP error: %d" % result)
		return

	if response_code != 200:
		_on_request_failed("API error: %d" % response_code)
		return

	var json_str = body.get_string_from_utf8()
	var json = JSON.new()
	var err = json.parse(json_str)

	if err != OK:
		_on_request_failed("JSON parse error")
		return

	var data: Dictionary = json.data
	var weights = _extract_weights(data)
	var reasoning = _extract_reasoning(data)

	if weights.is_empty():
		_on_request_failed("No valid weights in response")
		return

	_last_valid_weights = weights
	_success_count += 1

	print("[LLMTheaterAdvisor] Response (%.1fms): %s" % [latency, reasoning.substr(0, 80)])
	request_completed.emit(weights, reasoning)


func _extract_weights(data: Dictionary) -> Dictionary:
	var content = ""

	# Provider-specific parsing
	if _config.provider == "anthropic":
		var content_arr: Array = data.get("content", [])
		if not content_arr.is_empty():
			content = content_arr[0].get("text", "")
	elif _config.provider == "openai":
		var choices: Array = data.get("choices", [])
		if not choices.is_empty():
			content = choices[0].get("message", {}).get("content", "")

	# Extract JSON block from markdown code fence
	var weights = _parse_weights_from_content(content)
	return weights


func _parse_weights_from_content(content: String) -> Dictionary:
	# Look for JSON block in ```json ... ``` fence
	var start = content.find("```json")
	var end = content.find("```", start + 7)

	if start < 0 or end < 0:
		# Fallback: try direct JSON parse
		var json = JSON.new()
		if json.parse(content) == OK and json.data is Dictionary:
			return _validate_weights(json.data)
		return {}

	var json_str = content.substr(start + 7, end - start - 7).strip_edges()
	var json = JSON.new()

	if json.parse(json_str) != OK:
		return {}

	return _validate_weights(json.data)


func _validate_weights(data: Dictionary) -> Dictionary:
	var weights = {}

	for axis in AXIS_NAMES:
		if data.has(axis):
			var value = clampf(float(data[axis]), 0.0, 3.0)
			weights[axis] = value

	return weights


func _extract_reasoning(data: Dictionary) -> String:
	var content = ""

	if _config.provider == "anthropic":
		var content_arr: Array = data.get("content", [])
		if not content_arr.is_empty():
			content = content_arr[0].get("text", "")
	elif _config.provider == "openai":
		var choices: Array = data.get("choices", [])
		if not choices.is_empty():
			content = choices[0].get("message", {}).get("content", "")

	# Extract reasoning from markdown (everything before the JSON block)
	var json_start = content.find("```json")
	if json_start > 0:
		return content.substr(0, json_start).strip_edges()
	return content.substr(0, 200)  # First 200 chars


func _on_request_failed(error: String) -> void:
	_fail_count += 1
	push_warning("[LLMTheaterAdvisor] %s (using cached weights)" % error)

	# Fallback to cached weights
	if not _last_valid_weights.is_empty():
		request_completed.emit(_last_valid_weights, "[CACHED - API FAILED]")
	else:
		request_failed.emit(error)


func get_stats() -> Dictionary:
	return {
		"enabled": _enabled,
		"requests": _request_count,
		"success": _success_count,
		"failures": _fail_count,
		"avg_latency_ms": _total_latency_ms / max(1, _success_count),
		"pending": _pending,
		"interval_sec": _interval
	}


func set_update_interval(seconds: float) -> void:
	_interval = clampf(seconds, 15.0, 300.0)  # Min 15s, max 5min


func set_enabled(enabled: bool) -> void:
	_enabled = enabled and not _config.api_key.is_empty()
