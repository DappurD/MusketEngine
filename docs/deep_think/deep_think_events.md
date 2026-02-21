# Deep Think Prompt #9: Event System & LLM Storyteller — NEW

> **PREREQUISITE**: Read Prompt #0 first. Requires M8 LLM General (Prompt #7) and M15 Strategic (Prompt #8).

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §3 | Core Loop defines pacing: Zen Mode → March → Crucible → Recovery |
| GDD | §5 | Combat Escalation Timeline: 0-30min calm, 30-45min bandits, 1.5hr+ real war |
| GDD | §12.4 | GDScript Event Bus: C++ emits signals, modders write GDScript listeners |
| GDD | §12.2 | LLM Personality API: external `.txt` prompts |

## § What Exists

- **LLM pipeline** (Prompt #7): State Compressor, HTTP calls, multi-provider. The event system uses the SAME pipeline for narrative generation
- **Combat Escalation Timeline** (GDD §5): already defines event pacing — bandits at 30min, border probes at 90min, full war at 3hrs
- **GDScript Event Bus** (GDD §12.4): `emit_signal("on_battalion_routed", entity_id)` — modders script event responses
- **Battle memory** (legacy `battle_memory.gd`): feedback loop — can track event outcomes too

## § Design Questions

### Event Director
1. The Combat Escalation Timeline IS the event schedule for the first 3 hours. How does the event system extend BEYOND this timeline?
2. Event selection: weighted by game state (wealth, army size, approval). Uses same signals C++ already emits?
3. Storyteller profiles: how do they modify the existing escalation timeline? Faster? More simultaneous? More political?

### LLM Narrative Layer
4. Events use the SAME LLM pipeline as the battle commander. Context window: game state snapshot (dynasty, economy, recent battles) → LLM → narrative text
5. The Gazette: reuses `LLMPromptBuilder.build_briefing()` pattern but for economic/political state instead of battlefield
6. Offline fallback: template text with variable insertion (`{lord_name} demands {thing}`)

### Integration with Modding (GDD §12.4)
7. Modders add events via JSON files? Event definitions in `res://data/events/*.json`?
8. Modders trigger events via GDScript listeners: `MusketServer.trigger_event("plague_outbreak")`

## Deliverables
1. Event director architecture (extends Combat Escalation Timeline)
2. Event selection algorithm (game state weights)
3. LLM narrative pipeline (reuse existing LLM pipeline)
4. JSON event definition format (moddable)
5. Gazette specification
6. ⚠️ Traps section
