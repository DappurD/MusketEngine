# LLM Theater Advisor - Configuration Guide

Phase 6C LLM integration supports **multiple providers** including cloud APIs and local models. You can run them **individually or simultaneously** for comparison.

## Supported Providers

### Cloud Providers

1. **Anthropic Claude** (Recommended for quality)
   - Model: `claude-3-5-sonnet-20241022`
   - Requires: `ANTHROPIC_API_KEY`
   - Cost: ~$0.003 per request (~300 tokens)

2. **OpenAI GPT-4**
   - Model: `gpt-4` or `gpt-4-turbo`
   - Requires: `OPENAI_API_KEY`
   - Cost: ~$0.01 per request

### Local Providers (Free, Offline)

3. **Ollama** (Recommended for local)
   - Models: `llama3.1:8b`, `mistral:7b`, `codellama:13b`, etc.
   - No API key needed
   - Download: https://ollama.ai
   - Default endpoint: `http://localhost:11434/v1/chat/completions`

4. **LM Studio**
   - Any GGUF model from HuggingFace
   - No API key needed
   - Download: https://lmstudio.ai
   - Default endpoint: `http://localhost:1234/v1/chat/completions`

5. **Custom OpenAI-compatible Server**
   - Any server implementing OpenAI Chat API
   - Optional API key
   - Custom endpoint via `LLMConfig.local_config(model, endpoint)`

---

## Configuration Methods

### Method 1: Environment Variables (Recommended)

**Cloud only (Anthropic):**
```bash
# Windows
set ANTHROPIC_API_KEY=sk-ant-api03-...
godot --path "C:\Godot Project\Test\ai-test-project"

# Linux/Mac
export ANTHROPIC_API_KEY=sk-ant-api03-...
godot --path ~/projects/ai-test-project
```

**Local only (Ollama):**
```bash
# Start Ollama server first
ollama pull llama3.1:8b  # Download model
ollama serve  # Start server (or it auto-starts)

# Then launch game
set LLM_PROVIDER=ollama
godot --path .
```

**Dual mode (Cloud + Local comparison):**
```bash
# Primary: Cloud (Anthropic)
set ANTHROPIC_API_KEY=sk-ant-...

# Secondary: Local (Ollama) - runs alongside for comparison
set LLM_DUAL=1
set OLLAMA_MODEL=llama3.1:8b

godot --path .
```

### Method 2: Code Configuration

Edit `ai/llm/llm_config.gd` line 95:

**Cloud:**
```gdscript
static func debug_config() -> LLMConfig:
    var config := LLMConfig.new()
    config.enabled = true
    config.provider = "anthropic"
    config.api_key = "sk-ant-YOUR-KEY-HERE"
    config.model = "claude-3-5-sonnet-20241022"
    return config
```

**Local (Ollama):**
```gdscript
static func debug_config() -> LLMConfig:
    return ollama_config("llama3.1:8b")
```

**⚠️ WARNING:** Never commit real API keys to git!

---

## Environment Variables Reference

| Variable | Values | Description |
|----------|--------|-------------|
| `ANTHROPIC_API_KEY` | `sk-ant-...` | Enables Anthropic Claude API |
| `OPENAI_API_KEY` | `sk-...` | Enables OpenAI GPT-4 API |
| `LLM_PROVIDER` | `ollama`, `local`, `anthropic`, `openai` | Force specific provider |
| `LLM_DUAL` | `1` | Run cloud + local simultaneously |
| `OLLAMA_MODEL` | `llama3.1:8b`, `mistral:7b`, etc. | Ollama model to use |
| `OLLAMA_ENDPOINT` | `http://192.168.1.100:11434/v1/chat/completions` | Custom Ollama endpoint |

---

## Local Model Setup

### Ollama (Easiest)

1. **Install Ollama**
   ```bash
   # macOS/Linux
   curl https://ollama.ai/install.sh | sh

   # Windows
   # Download installer from https://ollama.ai
   ```

2. **Download a model**
   ```bash
   ollama pull llama3.1:8b     # Fast, 4.7GB
   ollama pull mistral:7b      # Alternative, 4.1GB
   ollama pull codellama:13b   # Code-focused, 7.3GB
   ```

3. **Verify it's running**
   ```bash
   curl http://localhost:11434/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{"model": "llama3.1:8b", "messages": [{"role": "user", "content": "test"}]}'
   ```

4. **Launch game**
   ```bash
   set LLM_PROVIDER=ollama
   godot --path .
   ```

### LM Studio

1. Download from https://lmstudio.ai
2. Search for models in UI (e.g., "Llama 3.1 8B Instruct")
3. Download model
4. Start local server (tab: "Local Server")
5. Launch game with `LLM_PROVIDER=local`

---

## Dual Mode (Cloud vs Local Comparison)

Run **both cloud and local models simultaneously** to compare performance:

```bash
# Windows example
set ANTHROPIC_API_KEY=sk-ant-...
set LLM_DUAL=1
set OLLAMA_MODEL=llama3.1:8b
godot --path .
```

**What happens:**
- **Primary advisor** (cloud): Applies weight modifiers to Theater Commander
- **Local advisor** (local): Logs suggestions but doesn't apply (comparison only)

**Console output:**
```
[LLM] Primary advisors enabled (provider: anthropic, model: claude-3-5-sonnet-20241022)
[LLM] Dual mode: Local advisors enabled (model: llama3.1:8b)
[LLM] Running cloud (claude-3-5-sonnet-20241022) vs local (llama3.1:8b) comparison

[LLM T1] Applied: {"aggression": 1.4, "tempo": 1.6}
[LLM T1] Reasoning: Enemy retreating. Press advantage.

[LLM-Local T1] Suggested: {"aggression": 1.5, "exploitation": 1.8}
[LLM-Local T1] Reasoning: High morale, enemy weak. Attack now.
```

**HUD display:**
```
[LLM] Req:5 (OK:4 Fail:1) | Latency:856ms | Interval:45.0s | LOCAL: 5/5 142ms
```

---

## Performance Comparison

| Provider | Latency | Cost | Quality | Offline |
|----------|---------|------|---------|---------|
| Claude 3.5 Sonnet | 500-1500ms | ~$0.003/req | ⭐⭐⭐⭐⭐ | ❌ |
| GPT-4 | 800-2000ms | ~$0.01/req | ⭐⭐⭐⭐ | ❌ |
| Llama 3.1 8B (local) | 100-300ms | Free | ⭐⭐⭐ | ✅ |
| Mistral 7B (local) | 80-250ms | Free | ⭐⭐⭐ | ✅ |
| CodeLlama 13B (local) | 200-500ms | Free | ⭐⭐⭐⭐ | ✅ |

**Hardware requirements for local models:**
- **8B models:** 8GB+ RAM, GPU optional
- **13B models:** 16GB+ RAM, GPU recommended
- **GPU acceleration:** NVIDIA (CUDA), Apple (Metal), AMD (ROCm)

---

## Testing

**Isolated test:**
```bash
# Cloud
set ANTHROPIC_API_KEY=sk-ant-...
godot --path . --script test/test_llm_advisor.gd

# Local (Ollama)
set LLM_PROVIDER=ollama
godot --path . --script test/test_llm_advisor.gd
```

**Integrated test:**
1. Set environment variables (see above)
2. Launch `voxel_test.tscn`
3. Start simulation (U key)
4. Watch HUD for LLM stats (appears after 45s)
5. Check console for LLM reasoning logs

---

## Troubleshooting

### "LLM Disabled (no API key found)"
- **Cloud:** Set `ANTHROPIC_API_KEY` or `OPENAI_API_KEY`
- **Local:** Set `LLM_PROVIDER=ollama` or `LLM_PROVIDER=local`

### "HTTP error: 7" (Connection refused)
- **Ollama:** Verify server is running: `ollama serve`
- **LM Studio:** Check "Local Server" tab is started
- **Custom:** Verify endpoint URL and port

### "API error: 401" (Unauthorized)
- **Cloud:** Check API key is valid and not expired
- **Local:** Most local servers don't need keys - remove `api_key` setting

### "API error: 404" (Model not found)
- **Ollama:** Run `ollama pull <model>` first
- **LM Studio:** Download model in UI first
- **Check model name:** `ollama list` to see available models

### Slow responses (local)
- **Use smaller model:** `llama3.1:8b` instead of `:70b`
- **Enable GPU:** Ollama auto-detects, check logs
- **Reduce context:** Local models are fast with short prompts (~300 tokens)

### Dual mode not working
- Requires cloud API key + `LLM_DUAL=1`
- Local server must be running (Ollama or LM Studio)
- Check console for "Dual mode" message

---

## Advanced Configuration

### Custom Local Endpoint

```gdscript
# In voxel_test_camera.gd _setup_llm_advisors()
var custom_config := LLMConfig.local_config("my-model", "http://192.168.1.100:8080/v1/chat/completions")
custom_config.api_key = "optional-auth-token"
```

### Multiple Local Models (Team 1 vs Team 2)

```gdscript
# Team 1: Llama 3.1
var t1_config := LLMConfig.ollama_config("llama3.1:8b")
_llm_advisor = LLMTheaterAdvisor.new()
_llm_advisor.setup(_theater_commander, t1_config, self)

# Team 2: Mistral
var t2_config := LLMConfig.ollama_config("mistral:7b")
_llm_advisor_t2 = LLMTheaterAdvisor.new()
_llm_advisor_t2.setup(_theater_commander_t2, t2_config, self)
```

### Adjusting Update Interval

```gdscript
# In _setup_llm_advisors()
_llm_advisor.set_update_interval(30.0)  # 30s instead of 45s
```

---

## Security Notes

- **Cloud API keys:** Store in environment variables, NOT in code
- **Local models:** No network transmission, all inference runs locally
- **Dual mode:** Primary advisor data sent to cloud, local advisor runs offline
- **Git:** Never commit API keys - add `.env` to `.gitignore`

---

## Files

- `ai/llm/llm_config.gd` - Configuration + provider setup
- `ai/llm/llm_prompt_builder.gd` - Battlefield briefing formatter
- `ai/llm/llm_theater_advisor.gd` - HTTP client + response parsing
- `test/test_llm_advisor.gd` - Isolated unit test
- `scenes/voxel_test_camera.gd` - Integration (lines 77-80, 3486-3560)

---

## Example Workflows

### Development (Local, Free)
```bash
ollama pull llama3.1:8b
set LLM_PROVIDER=ollama
godot --path .
```

### Production (Cloud, High Quality)
```bash
set ANTHROPIC_API_KEY=sk-ant-...
godot --path .
```

### Benchmarking (Cloud vs Local)
```bash
set ANTHROPIC_API_KEY=sk-ant-...
set LLM_DUAL=1
set OLLAMA_MODEL=llama3.1:8b
godot --path .
# Compare console logs: [LLM T1] vs [LLM-Local T1]
```

### Offline Demo
```bash
ollama pull mistral:7b
set LLM_PROVIDER=ollama
set OLLAMA_MODEL=mistral:7b
godot --path . --headless --scenario micro_cover_peek
```
