class_name LLMConfig
extends RefCounted
## LLM provider configuration and prompt templates for Theater Commander integration.

var enabled: bool = false
var provider: String = "anthropic"  # "anthropic", "openai", "ollama", "local"
var api_key: String = ""  # Optional for local models
var model: String = "claude-3-5-sonnet-20241022"
var endpoint_url: String = ""  # Custom endpoint for local models
var max_tokens: int = 500
var temperature: float = 0.7
var command_mode: String = "sector"  # "sector" (squad orders), "bias" (weight modifiers), "off"
var vision_enabled: bool = false  # Future: send minimap screenshot to multimodal LLM

# System prompt (military advisor personality)
var system_prompt: String = """You are a military AI advisor analyzing a tactical battlefield in real-time.

Your role is to suggest strategic weight adjustments for 9 decision axes that control an AI army's behavior.

AXES (current value 0.0-1.0, you output modifier 0.0-3.0):
- aggression: Attack vs defend bias
- concentration: Mass forces vs spread out
- tempo: Maintain pressure vs wait
- risk_tolerance: Accept casualties for objectives
- exploitation: Pursue breakthroughs vs consolidate
- terrain_control: Capture points vs engage enemy
- medical_priority: Save wounded vs fight
- suppression_dominance: Suppress enemy vs advance
- intel_coverage: Scout/recon vs direct combat

INSTRUCTIONS:
1. Read the battlefield briefing (snapshot + recent events)
2. Reason about the strategic situation in 2-3 sentences
3. Output ONLY axis modifiers that should change (default is 1.0)
4. Format as JSON: {"axis_name": modifier_value}
5. Use modifiers > 1.0 to increase weight, < 1.0 to decrease

EXAMPLE RESPONSE:
The enemy is retreating after heavy casualties. We hold 2/3 capture points. Our forces are healthy but MGs are low on ammo. Recommendation: Press the advantage but don't overextend.

```json
{
  "aggression": 1.4,
  "tempo": 1.6,
  "exploitation": 1.8,
  "suppression_dominance": 0.7,
  "medical_priority": 0.8
}
```"""

# System prompt for sector command mode
var sector_system_prompt: String = """You are a battlefield commander controlling squads in a tactical combat simulation.

You see the battlefield as a grid of sectors (columns A-Z, rows 1-N). Each sector shows friendly (FR) and enemy (EN) unit counts, threat levels, and cover quality.

Your job:
1. Read the sector map and squad roster
2. Decide where each available squad should go and what they should do
3. Output orders as JSON

RULES:
- Each squad gets exactly ONE order (sector + intent)
- Valid intents: ATTACK, DEFEND, CAPTURE, FLANK, RECON, SUPPRESS, WITHDRAW, SUPPORT
- Don't send broken squads (morale < 0.3) to ATTACK — use WITHDRAW or DEFEND
- Don't leave your base completely undefended
- Consider terrain and threat levels when assigning squads
- Flanking works best from sectors adjacent to the enemy, not through them
- RECON is cheap and useful for sectors with unknown enemy presence
- Don't send more than 75% of your forces to one sector

Respond with brief reasoning (2-3 sentences) then your orders as JSON.

EXAMPLE:
Enemy concentrated at B3 contesting our capture point. Sending Alpha to assault directly while Bravo flanks through D2.

```json
{
  "orders": [
    {"squad": 0, "sector": "B3", "intent": "ATTACK"},
    {"squad": 1, "sector": "D2", "intent": "FLANK"},
    {"squad": 2, "sector": "A3", "intent": "DEFEND"}
  ]
}
```"""


func get_endpoint_url() -> String:
	# Custom endpoint takes precedence
	if not endpoint_url.is_empty():
		return endpoint_url

	match provider:
		"anthropic":
			return "https://api.anthropic.com/v1/messages"
		"openai":
			return "https://api.openai.com/v1/chat/completions"
		"ollama":
			return "http://localhost:11434/v1/chat/completions"
		"local":
			return "http://localhost:1234/v1/chat/completions"  # LM Studio default
		_:
			push_error("[LLMConfig] Unknown provider: %s" % provider)
			return ""


static func from_env() -> LLMConfig:
	"""Create config from environment variables.
	Env vars: ANTHROPIC_API_KEY, OPENAI_API_KEY, LLM_PROVIDER, LLM_MODE (sector/bias/off)"""
	var config = LLMConfig.new()

	# Command mode from env (default: sector)
	var mode_env = OS.get_environment("LLM_MODE")
	if mode_env in ["sector", "bias", "off"]:
		config.command_mode = mode_env

	# Check for local model preference first (LLM_PROVIDER env var)
	var provider_env = OS.get_environment("LLM_PROVIDER")
	if provider_env == "ollama":
		return ollama_config()
	elif provider_env == "local":
		return local_config()

	# Try Anthropic
	var env_key = OS.get_environment("ANTHROPIC_API_KEY")
	if not env_key.is_empty():
		config.enabled = true
		config.provider = "anthropic"
		config.api_key = env_key
		config.model = "claude-3-5-sonnet-20241022"
		return config

	# Try OpenAI
	env_key = OS.get_environment("OPENAI_API_KEY")
	if not env_key.is_empty():
		config.enabled = true
		config.provider = "openai"
		config.api_key = env_key
		config.model = "gpt-4"
		return config

	# Try Ollama (no API key needed)
	if _is_local_server_available("http://localhost:11434"):
		return ollama_config()

	# Try LM Studio (no API key needed)
	if _is_local_server_available("http://localhost:1234"):
		return local_config()

	# Fallback: disabled
	config.enabled = false
	return config


static func ollama_config(model: String = "llama3.1:8b") -> LLMConfig:
	"""Create config for Ollama (local, OpenAI-compatible API)"""
	var config = LLMConfig.new()
	config.enabled = true
	config.provider = "ollama"
	config.api_key = ""  # No key needed
	config.model = model
	config.endpoint_url = OS.get_environment("OLLAMA_ENDPOINT")  # Allow override
	if config.endpoint_url.is_empty():
		config.endpoint_url = "http://localhost:11434/v1/chat/completions"
	return config


static func local_config(model: String = "local-model", endpoint: String = "") -> LLMConfig:
	"""Create config for local LM Studio or other OpenAI-compatible server"""
	var config = LLMConfig.new()
	config.enabled = true
	config.provider = "local"
	config.api_key = ""  # No key needed (or use for auth if server requires)
	config.model = model
	config.endpoint_url = endpoint if not endpoint.is_empty() else "http://localhost:1234/v1/chat/completions"
	return config


static func _is_local_server_available(url: String) -> bool:
	"""Quick check if local server is running (not implemented - always returns false for safety)"""
	# TODO: Could implement a quick HTTP HEAD request, but for now assume user sets env var
	return false


func get_active_system_prompt() -> String:
	"""Return the system prompt for the active command mode."""
	if command_mode == "sector":
		return sector_system_prompt
	return system_prompt


static func commentator_config_from_env() -> LLMConfig:
	"""Create config for the LLM Commentator (cheap event-driven barks).
	Env vars: LLM_COMMENTATOR=1, LLM_COMMENTATOR_PROVIDER, LLM_COMMENTATOR_MODEL,
	LLM_COMMENTATOR_BUDGET (hourly USD), LLM_COMMENTATOR_MODE (neutral|team)"""
	var config = LLMConfig.new()

	if OS.get_environment("LLM_COMMENTATOR") != "1":
		config.enabled = false
		return config

	# Provider/model overrides (default: cheapest available)
	var prov = OS.get_environment("LLM_COMMENTATOR_PROVIDER")
	var mdl = OS.get_environment("LLM_COMMENTATOR_MODEL")

	if prov == "ollama":
		config = ollama_config(mdl if not mdl.is_empty() else "llama3.1:8b")
	elif prov == "local":
		config = local_config(mdl if not mdl.is_empty() else "local-model")
	elif not prov.is_empty():
		config.enabled = true
		config.provider = prov
		config.api_key = OS.get_environment("OPENAI_API_KEY") if prov == "openai" \
			else OS.get_environment("ANTHROPIC_API_KEY")
		config.model = mdl if not mdl.is_empty() else ("gpt-4o-mini" if prov == "openai" else "claude-3-haiku-20240307")
	else:
		# Auto-detect: use cheapest model for whatever key is available
		var oai_key = OS.get_environment("OPENAI_API_KEY")
		var ant_key = OS.get_environment("ANTHROPIC_API_KEY")
		if not oai_key.is_empty():
			config.enabled = true
			config.provider = "openai"
			config.api_key = oai_key
			config.model = "gpt-4o-mini"
		elif not ant_key.is_empty():
			config.enabled = true
			config.provider = "anthropic"
			config.api_key = ant_key
			config.model = "claude-3-haiku-20240307"
		else:
			config = ollama_config("llama3.1:8b")

	config.max_tokens = 80
	config.temperature = 0.9
	config.command_mode = "commentator"
	return config


static func debug_config() -> LLMConfig:
	"""Hardcoded config for testing (NEVER commit real keys!)"""
	var config = LLMConfig.new()
	config.enabled = true
	config.provider = "anthropic"
	config.api_key = "sk-ant-PLACEHOLDER-DO-NOT-COMMIT"  # User replaces this
	config.model = "claude-3-5-sonnet-20241022"
	return config
