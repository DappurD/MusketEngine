# The Musket Engine — Phased Development Plan

> Each phase builds on the last. Nothing is skipped. Each milestone (M) is a compilable, testable deliverable.

---

## Phase 1: Whitebox Math Proof — Cost: $0

> **Goal**: The game is deeply fun and strategic using only gray cubes and capsules. If the math isn't fun with programmer art, pretty textures won't save it.

### M1: The ECS Foundation
- [ ] `world_manager.cpp` — instantiate `flecs::world`, register all components from `musket_components.h`
- [ ] Expose `MusketServer` singleton to Godot via `ClassDB::bind_method`
- [ ] Create test scene (`res://scenes/test_bed.tscn`) — flat plane, fly camera
- [ ] Spawn 200 capsule soldiers via MultiMeshInstance3D
- [ ] **Verify**: Godot loads extension, capsules render, Vulkan stable

### M2: Battalion Movement Orders
- [ ] Formation slot calculator (Line, Column, Square)
- [ ] Spring-damper physics system (CORE_MATH §1)
- [ ] Player click → `MovementOrder` component → battalion marches
- [ ] Formation switching (Line↔Column↔Square) in real time
- [ ] **Verify**: Click ground, 200 capsules march in formation, reform on arrival

### M3: Volley Combat
- [ ] Inverse Sieve musket system (CORE_MATH §2)
- [ ] Two opposing battalions, auto-fire at range
- [ ] Reload timer, ammo tracking, `IsAlive` tag removal
- [ ] Dead capsules drop / disappear
- [ ] **Verify**: Two formations face off, front ranks die first (meat shield works)

### M4: Panic & Morale
- [ ] Panic CA grid — 5Hz double-buffered (CORE_MATH §4)
- [ ] Death injects panic, drummer cleanses
- [ ] Panic feeds back into spring stiffness (line bows under fire)
- [ ] Routing threshold — broken units flee
- [ ] **Verify**: Kill enough men → formation physically buckles → unit breaks and runs

### M5: Artillery
- [ ] DDA ballistic trajectory (CORE_MATH §3)
- [ ] Roundshot (penetrates formation grid) and Canister (cone shotgun)
- [ ] Ricochet on hard ground, sink in mud
- [ ] Limber/unlimber state machine
- [ ] **Verify**: Cannon fires, ball bounces through enemy line, kills multiple men

### M6: Cavalry
- [ ] Charge momentum system (CORE_MATH §3)
- [ ] Formation defense modifier (Square=0.9 bounce, Line=0.2 devastation)
- [ ] Post-charge disorder timer
- [ ] **Verify**: Cavalry charges line → devastation. Charges square → bounces off

### M7: Command Network
- [ ] Flag Bearer — formation anchor (kill = springs snap)
- [ ] Drummer — order latency + panic cleanse aura
- [ ] Officer — elevated LOS above smoke grid
- [ ] **Verify**: Kill drummer → orders slow down. Kill flag → formation disintegrates

> **Phase 1 Complete**: Full tactical combat with gray capsules. If this isn't satisfying to play, everything else is wasted.

---

## Phase 2: The Neuro-Symbolic General — Cost: $0

> **Goal**: An LLM commands the enemy army through the courier system. API latency becomes gameplay.

### M8: State Compressor (Aide-de-Camp)
- [ ] Sector grid overlay (A1-F6 with terrain tags)
- [ ] Semantic quantization (floats → adjectives)
- [ ] Fog of war raycast sieve
- [ ] Aide's Notes generator
- [ ] YAML SitRep output (~150 tokens)

### M9: LLM Integration
- [ ] `llm_api_client.gd` — async HTTP to Claude/OpenAI/Ollama
- [ ] JSON order parser → `MovementOrder`/`FireOrder` components
- [ ] Battle Commander validation (reject impossible orders)
- [ ] Feedback loop (rejected orders → next SitRep)

### M10: The Courier
- [ ] Physical courier entity spawned per order
- [ ] Courier rides to target battalion (killable!)
- [ ] Order executes on courier arrival (not on API response)
- [ ] AI Personalities (Ney/Wellington/Napoleon system prompts)

> **Phase 2 Complete**: Enemy general thinks and adapts. Couriers carry orders across the battlefield. Kill the courier = silence the general.

---

## Phase 3: The Logistics Bedrock — Cost: $0

> **Goal**: Smart Buildings, Dumb Agents. The economy that feeds the war machine.

### M11: Civilian Agents
- [ ] `Citizen` component, 16-byte state machine
- [ ] Flow Field pathfinding (pre-calculated, not per-agent A*)
- [ ] 1Hz Market Matchmaker — scan needs, assign jobs, O(1)
- [ ] 60Hz execution loop (CORE_MATH §6)

### M12: Production Chain MVP
- [ ] 3 chains: Wheat→Bread, Iron→Tools, Sheep→Wool→Uniforms
- [ ] Workplace components (consumes/produces/inventory)
- [ ] Tool degradation (The Infinite Sink)
- [ ] **Verify**: Citizens autonomously harvest, carry, produce without any scripted AI

### M13: The Conscription Bridge
- [ ] `Citizen` → `SoldierFormationTarget` component swap
- [ ] Visual: citizens drop hammers, march to rally point
- [ ] Economy feels the loss (foundries go silent)

### M14: Advanced Production
- [ ] Black Powder chain (Charcoal+Sulfur+Niter→Powder Mill→Arsenal)
- [ ] Cooperage (Barrels as reusable containers)
- [ ] Hazard Codes (Spark_Risk near blacksmiths)
- [ ] Byproduct web (Cattle→Meat+Tallow+Hides)

> **Phase 3 Complete**: Working city economy. Player can watch citizens build, trade, and produce. Conscription physically drains the workforce.

---

## Phase 4: Procedural Urbanism & Siege — Cost: $0

### M15: Road Splines & Burgage Plots
- [ ] Player draws spline roads
- [ ] Voronoi/Straight-Skeleton lot subdivision
- [ ] Buildings auto-fill irregular lots

### M16: Vauban Star Forts
- [ ] Magistral Line Solver (CORE_MATH §5)
- [ ] Player draws polygon → bastions extrude
- [ ] Glacis deflection physics for cannonballs

### M17: Siege Warfare
- [ ] Wall destruction → rubble ramp → Flow Field recalc
- [ ] Infantry plug breaches
- [ ] Medical triage (Downed→Stretcher→Surgeon→Veteran/Amputee)

### M18: Haussmannization
- [ ] Demolish old forts → Macadam Boulevards
- [ ] Paved roads multiply agent speed

---

## Phase 5: The Friction of War — Cost: $0

### M19: Weather & Mud
- [ ] Global humidity → misfire scaling, mud splatmap
- [ ] Rain nullifies ricochet, slows wagons
- [ ] Winter (frozen rivers, firewood demand)

### M20: Day/Night & Candle Economy
- [ ] LOS shrinks at night (800m→40m)
- [ ] Candle production chain (Tallow→Candlemaker)
- [ ] Night Shift (double output, triple fire risk)

### M21: Acoustic Physics & Diegetic Audio
- [ ] Speed of sound delay (distance/343)
- [ ] Diegetic music tied to game state
- [ ] Trip-hammer loops, fifes/drums on march

### M22: The Cartographer's Table
- [ ] Seamless zoom (terrain→cloud→parchment shader)
- [ ] SLOD macro economy tick (CORE_MATH §11)
- [ ] Board game pawns replace 3D units at altitude
- [ ] Inter-regional logistics via trade route splines

---

## Phase 6: Vertical Slice & Polish — Cost: ~$150

### M23: Asset Integration
- [ ] Buy 1 soldier mesh + 1 building kit
- [ ] VAT bake in Blender, import to Godot
- [ ] Anti-clone shader (CORE_MATH §8)
- [ ] Reactive terrain splatmap

### M24: Visual Polish
- [ ] Volumetric black powder smoke
- [ ] SDFGI + Volumetric Fog tuning
- [ ] Diegetic UI (paper SitReps, pocket watch)
- [ ] Spatial heatmap overlays

### M25: The Pitch Video
- [ ] 90-second gameplay capture
- [ ] 5,000 agents building a town
- [ ] Seamless zoom to Cartographer's Table
- [ ] 10,000 soldiers in volley combat + volumetric smoke
- [ ] Publisher outreach (Hooded Horse, Paradox, Kickstarter)

---

## Dependency Graph

```
M1 (ECS) → M2 (Movement) → M3 (Combat) → M4 (Panic) → M5 (Artillery) → M6 (Cavalry) → M7 (Command)
                                                                                              ↓
M11 (Citizens) → M12 (Production) → M13 (Conscription) → M14 (Advanced)          M8 → M9 → M10 (LLM General)
       ↓                                                                                      ↓
M15 (Urbanism) → M16 (Vauban) → M17 (Siege) → M18 (Haussmann)                    All combat + economy
       ↓                                                                                      ↓
M19 (Weather) → M20 (Night) → M21 (Audio) → M22 (Cartographer)              M23 → M24 → M25 (Pitch)
```
