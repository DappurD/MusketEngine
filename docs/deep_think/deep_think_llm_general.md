# Deep Think Prompt #7: LLM General — M8

> **PREREQUISITE**: Read Prompt #0 first. This is M8 alongside the spatial hash.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §4.1 | Neuro-Symbolic Stack: LLM General (async 30-60s) → Battle Commander (60Hz) → physical couriers → ECS |
| GDD | §4.2 | System Summary: Brain=LLM(30-60s), Spinal Cord=BattleCommander(60Hz), Eyes=StateCompressor(60s) |
| GDD | §6.1 | State Compressor: 4-step pipeline → ~150 YAML tokens (~$0.05/hr) |
| GDD | §6.2 | Orders: strict ENUM conditions, physical courier delivery |
| GDD | §6.3 | AI Personalities: Ney (aggressive), Wellington (defensive), Napoleon (balanced) |
| GDD | §12.2 | LLM Personality API: external `.txt` prompts in `res://data/ai_prompts/` |
| CORE_MATH | §9 | State Compressor: `SectorInfo` struct, semantic quantization thresholds, aide's notes |
| STATE.md | M8 | "LLM General (Battle Commander + State Compressor)" |

## § What's Already Built (Current Engine)

- `PendingOrder` struct, `OrderType` enum (M7): order delay pipeline
- `MacroBattalion` (128B): centroid, alive_count, has_flag, has_drummer, cohesion — **this IS the state the compressor reads**
- `PanicGrid`: avg panic per cell — **feeds into YAML SitRep**
- `napoleon.txt`: `res/data/ai_prompts/napoleon.txt` — personality prompt already exists

## § Legacy Code Reference (FULLY BUILT — Port This)

| File | What It Does |
|------|-------------|
| `ai/llm/llm_sector_commander.gd` (425L) | Sends sector briefings → parses `{"orders": [...]}` → calls `set_llm_directive()` |
| `ai/llm/llm_theater_advisor.gd` (266L) | 9-axis weight modifier (aggression, tempo, exploitation...) |
| `ai/llm/llm_prompt_builder.gd` (107L) | Compresses battlefield → ~150 tokens: force ratio, morale, territory |
| `ai/llm/llm_config.gd` | Multi-provider: Anthropic, OpenAI, Ollama, LM Studio |
| `ai/llm/llm_commentator.gd` | Battle narrative generation |
| `cpp_src/colony_ai_cpp.h` (L200-219) | `LLMDirective` struct: sector_col/row, intent, confidence, 90s expiry |
| `cpp_src/colony_ai_cpp.cpp` (L1071-1180) | `_apply_llm_directives()`: confidence-decayed floor scoring |

### Key Legacy Design Decisions
1. **Two LLM layers**: Theater Advisor (strategic weights) + Sector Commander (per-squad orders)
2. **Confidence-decayed directives**: floor=75, soft decay at 60s, hard expiry at 90s
3. **Deterministic fallback**: auction scoring runs standalone without LLM
4. **Physical couriers**: orders propagate via killable courier entities
5. **Battle memory**: LLM gets feedback on whether previous orders succeeded

## § Design Questions

### State Compressor (CORE_MATH §9)
1. The `SectorInfo` struct exists in CORE_MATH. In the new engine, sectors are defined by... what? Grid cells from spatial hash? Hardcoded regions?
2. Semantic quantization: Pristine/Degraded/Decimated, Eager/Wavering/Routing. These thresholds map to which component fields? `MacroBattalion.alive_count` and `PanicGrid.avg_panic`?
3. Fog of War sieve: DDA LOS raycasts (from M5 artillery) filter what the LLM "sees". How does this interact with voxel terrain (M13)?

### Battle Commander (60Hz)
4. The Battle Commander validates LLM orders before executing. What validation? "Squad 3 doesn't exist" → reject. "Move to sector off-map" → reject. "Charge while routing" → reject
5. How does the Battle Commander map from legacy `ColonyAICPP` goals (CAPTURE_POI, DEFEND_POI, ASSAULT_ENEMY...) to Crown & Caliber orders (MARCH, FIRE, CHARGE, REFORM)?
6. Utility fallback: when LLM is offline, what decision tree runs? The `ColonyAICPP` auction system?

### Physical Courier System (GDD §6.2)
7. Courier = entity with `Position`, `Velocity`, `CourierOrder`. Rides from HQ to battalion. If killed, order is lost
8. Latency: API response time (2-5s) becomes GAMEPLAY — courier gallop time. How does this map?
9. Can the player intercept enemy couriers? (Cavalry screening mission)

### Port Architecture
10. Legacy LLM was GDScript calling HTTP. New engine mandates C++ for game logic (GDD §2.1 Rule 1). Where does the HTTP call live? Exception: LLM HTTP is I/O, not game logic — stays in GDScript?
11. Response parsing (JSON → directives): GDScript or C++? Legacy uses GDScript JSON parsing

## Deliverables
1. State compressor pipeline (MacroBattalion + PanicGrid → YAML)
2. Battle Commander validation system
3. `LLMDirective` port to new ECS (component struct)
4. Courier entity specification
5. Multi-provider config (port from legacy)
6. Deterministic fallback system
7. ⚠️ Traps section
