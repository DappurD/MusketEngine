# THE MUSKET ENGINE
## Master Technical Game Design Document (GDD)
### A Neuro-Symbolic Napoleonic War & Economy Simulator

> *"You won the battle because you spent 3 hours optimizing a sheep farm."*

---

## 1. Vision & Engine Philosophy

A genre-defining hybrid: **Manor Lords' zen city-building** meets **Total War's visceral combat**, governed by the logistical throughput of **Factorio**, and commanded by an **LLM General** that thinks like Napoleon.

The player builds an 18th-century town — optimizing supply chains, zoning industries, and managing the social Zeitgeist — then conscripts those same citizens into a musket-era army to defend the streets they built. Every tactical blunder costs real economic output. Every industrial investment pays dividends on the battlefield.

**Engine Philosophy (The Von Neumann Solution)**:
Modern CPUs are bottlenecked by memory latency. Object-Oriented Design (OOD) scatters data, causing cache misses. This engine strictly mandates **Data-Oriented Design (DOD)** via the Flecs ECS library in C++.

- **The Brain (C++)**: All physics, pathfinding, AI, morale, and economy loops exist purely as flat, contiguous arrays (Structure of Arrays) in C++.
- **The Eyes (Godot 4)**: Godot is strictly a dumb rendering pipeline (VAT meshes, Procedural Gaussian Splats, Volumetric Fog) and UI layer. No game logic exists in GDScript.

Nothing is faked. 100,000 agents are fully simulated at 60 FPS without dropping frames.

**Target**: Godot 4 + GDExtension (`musket_engine.dll`) + Flecs ECS.

---

## 2. AI Developer Directives (The Antigravity Laws)

> These directives are **MANDATORY** for any AI coding agent reading this repository to prevent architectural drift.

### 2.1 The 4 Unbreakable Rules

1. **THE ARCHITECTURAL BOUNDARY**: ALL game logic, math, physics, pathfinding, AI, and economy state live EXCLUSIVELY in C++ via Flecs ECS. Godot (`.gd` scripts) is ONLY a visual/audio client.

2. **DATA-ORIENTED DESIGN (DOD) ONLY**: No Object-Oriented Programming (OOP) for game entities. No class hierarchies. No Godot Nodes (`CharacterBody3D`) for agents. Components MUST be Plain Old Data (POD) structs (ideally < 32 bytes). Systems iterate via `ecs.query().each()`.

3. **ANTI-HALLUCINATION PROTOCOL**: Use Godot 4.x GDExtension (`ClassDB::bind_method`). Use the Flecs C++11/17 API. Do not guess API bindings; search local headers.

4. **THE LEDGER PROTOCOL**: All completed steps, current states, and bugs MUST be logged in `STATE.md`. Before writing code, the AI must silently read `STATE.md` to establish context.

### 2.2 Agent Workflows (Slash Commands)

- `/data-first`: Output only lightweight POD structs. Calculate byte size. Wait for approval before writing systems.
- `/prove-math`: Write pseudocode algorithm proof. List edge cases. Wait for approval before writing C++.
- `/audit-bridge`: Review C++ file for correct `godot-cpp` headers and `ClassDB::bind_method` syntax.

> **When implementing core combat, physics, geometry, or ECS loops, you MUST read `CORE_MATH.md` for the exact C++ algorithms and mathematical proofs.**

---

## 3. The Core Loop

```
┌────────────────────────────────────────────────────────────┐
│  ZEN MODE (1-2 hours)                                      │
│  Build town, draw organic splines, optimize supply chains. │
│  Acoustic guitar, rain on cobblestones, diegetic UI.       │
├────────────────────────────────────────────────────────────┤
│  THE MARCH (5 minutes)                                     │
│  LLM General attacks. Industrial noise stops.              │
│  Citizens physically drop tools, don blue uniforms,        │
│  pick up muskets, and march to the Star Fort.              │
├────────────────────────────────────────────────────────────┤
│  THE CRUCIBLE (30 minutes)                                 │
│  Tactical combat. DDA Artillery math, Panic Grids.         │
│  Defend the exact streets you built. Hold the line.        │
├────────────────────────────────────────────────────────────┤
│  THE RECOVERY                                              │
│  Ambulances return wounded. Empty houses. Permanent        │
│  amputees. Haussmannize the city. Prepare for next year.   │
└────────────────────────────────────────────────────────────┘
```

---

## 4. Subsumption Architecture

### 4.1 The Neuro-Symbolic Stack

```
┌─────────────────────────────────────────────────────────┐
│  LLM GENERAL (Async, 30-60s)                            │
│  "You are Napoleon. Read this YAML SitRep. Output JSON."│
└──────┬──────────────────────────────────────────▲───────┘
       │ JSON Orders (2-5s latency)               │ YAML SitRep
       ▼                                          │ (Aide-de-Camp Pipeline)
┌─────────────────────────────────────────────────┴───────┐
│  BATTLE COMMANDER (C++, 60Hz)                           │
│  Validates LLM orders. Falls back to Utility AI.        │
│  Reacts instantly (forms square when charged).          │
│  Spawns physical Courier entities to deliver orders.    │
└──────┬──────────────────────────────────────────────────┘
       │ Physical Couriers (Vulnerable to death/latency)
   ┌───┴───────────┬───────────────┐
   ▼               ▼               ▼
Infantry        Cavalry        Artillery
(ECS Flecs)     (ECS Flecs)    (ECS Flecs)
```

### 4.2 System Summary

| Layer | Name | Rate | Role |
|---|---|---|---|
| The Brain | LLM General | Async 30-60s | Creative strategy, personality |
| The Spinal Cord | Battle Commander | 60Hz | Deterministic fallback, validation |
| The Muscle | Spring-Damper ECS | 60Hz | Formation physics, O(1)/soldier |
| The Sword | Volley + Artillery | Per-event | Inverse Sieve, DDA traversal |
| The Mind | Panic CA | 5Hz | Fear diffusion, morale emergence |
| The Eyes (AI) | State Compressor | 60s | Battlefield → 150-token YAML |
| The Eyes (Player) | VAT + Proc-GS | 60Hz | Manor Lords-tier rendering |
| The Voice | Courier System | Per-order | Physical order delivery, killable |
| The Stage | Reactive Terrain | 60Hz | Dynamic mud, blood, craters |
| The Fog | Volumetric Smoke | 60Hz | Black powder → Godot VFog |

---

## 5. Combat Systems & Math

### 5.1 Phase 1: The Muscle (Formation Physics)

Every soldier is a **16-byte particle** (Position, Velocity). Formations are governed by **Spring-Damper Physics** in O(1) time.

**Uniforms Dictate Physics**: Panic directly drops `stiffness`. High panic = line physically bows backward. Blue dyed uniforms hard-cap `min_stiffness = 8.0` (line holds), civilian rags cap at `2.0` (line shatters).

**Scale Switch**:

| Headcount | Mode | Behavior |
|---|---|---|
| 20+ alive | Formation | Spring-damper, battalion orders |
| < 20 alive | Skirmish | Individual cover/target AI |
| 0 alive | Destroyed | Entity removed |

**Infantry Formations**: Line (N×3, volley fire), Column (4×N, fast march), Square (√N×√N, anti-cavalry defense=0.9). Formation changes happen in real time — cavalry hitting mid-transition is devastating.

> See `CORE_MATH.md §1` for the exact Spring-Damper Flecs system implementation.

### 5.2 Phase 2: The Sword (Lethality & Ballistics)

**Muskets (Inverse Sieve)**: Cast cone → walk target grid front-to-back → apply hit probability → delete `IsAlive` tag. The front rank physically acts as a **meat shield** for the rear.

**Misfire**: Global `humidity` float scales misfire from 5% (clear) → 70% (thunderstorm). Wet powder forces bayonet charges.

**Artillery (Bresenham DDA)**: 3D raycast steps across integer grid in microseconds. Hits voxel → height check → drops `kinetic_energy` by 1.0 per man penetrated.
- **Canister**: < 100m, cone shotgun effect. Devastates charges.
- **Ricochet**: Hard earth = bounce (range extension). Mud = ball sinks (zero ricochet).
- **Glacis Deflection**: Sloped earth deflects cannonballs upward over city walls.

> See `CORE_MATH.md §2-3` for Inverse Sieve and DDA/Ricochet implementations.

### 5.3 Phase 3: The Mind (Morale & Atmosphere)

**5Hz double-buffered Cellular Automata** grid handles psychology and smoke.

| Event | Panic Injection |
|---|---|
| Soldier killed | +0.4 at death position |
| Wounded (DOWNED) | +0.2/tick (stationary emitter) |
| Routing soldier | +0.05/tick (moving emitter) |
| Drummer alive | -0.2/tick (cleansing) |

**Routing**: Panic > threshold → unit breaks. Routing men sprint outward, painting fear. Running through reserves = **contagion**.

**Smoke Grid**: Same CA architecture, tracks opacity. Mapped into Godot 4 Volumetric Fog. Muzzle flashes illuminate smoke volumetrically. Wind shifts via asymmetric kernel.

> See `CORE_MATH.md §4` for the CA diffusion kernel.

### 5.4 The Command Network (18th-Century Wi-Fi)

These are **not cosmetics** — they are mechanical anchors.

| Role | Alive | Dead |
|---|---|---|
| **Flag Bearer** | Springs attached, formation holds | Springs snap, men scatter into mob |
| **Drummer** | 2s order delay, -0.2 panic/tick, speed buff | 8s order delay, silence, deaf chaos |
| **Officer** | Elevated LOS above smoke, targeting data | Blinded by own smoke, fires straight ahead |

### 5.5 Siege & Medical Corps

**Vauban Star Forts**: Player-built using procedural geometry. Glacis deflects DDA cannonballs based on angle of incidence.

**The Breach**: Artillery shatters wall voxels → debris cascades into 45° rubble ramp → C++ recalculates Flow Field → impassable wall becomes zero-cost path → enemy pours through.

**Medical Triage**: 70% of casualties enter `DOWNED` state (screaming panic emitters). **Ambulance Volante** (stretcher bearers = repurposed civilian logistics agents) cart them to Surgeon Tents.
- **If supplied** (Linen, Tools, Alcohol): Survive → `Veteran` tag (productivity) or `Amputee` tag (restricted jobs)
- **If unsupplied**: Dies on table. Permanent loss of a civilian tradesman.

### 5.6 Unit Types

**Cavalry**: Walk (4 m/s) → Charge (12 m/s, momentum builds) → Disordered (2 m/s, 10s vulnerability). Impact: `casualties = momentum × (1.0 - formation_defense)`. Square defense = 0.9 (bounce). Line = 0.2 (devastation).

**Artillery**: Limbered (3 m/s, can't fire) → Unlimbering (60s transition) → Deployed (roundshot DDA or canister cone).

---

## 6. The AI General (LLM)

### 6.1 The State Compressor (Aide-de-Camp Algorithm)

**Four-step C++ pipeline** compresses 10,000 men into ~150 YAML tokens (~$0.05/hr):

1. **Spatial Discretization**: Coordinates → Sectors + Tags ("C3: Sunken Road")
2. **Semantic Quantization**: Floats → Adjectives (Pristine/Degraded/Decimated, Eager/Wavering/Routing)
3. **Fog of War Sieve**: DDA LOS raycasts remove hidden units. Generates "Stale Intel" ("Dust cloud in A3")
4. **Aide's Notes**: C++ pre-calculates emergencies ("1st Infantry will break soon")

**Feedback Loop**: Rejected orders reported back to prevent hallucinated context drift.

### 6.2 Orders, Enums & Couriers

**Strict ENUM conditions** evaluated in O(1): `SECTOR_PANIC_CRITICAL`, `SECTOR_CASUALTIES_HEAVY`, `SECTOR_ENEMY_ROUTING`, `SECTOR_ENEMY_ABSENT`, `TIMER_ELAPSED`.

**The Physical Courier**: API latency is gamified. Battle Commander validates LLM order → spawns physical Courier rider → order doesn't execute until courier reaches battalion. If sniped, the order is lost.

### 6.3 AI Personalities

| General | Style | Key Trait |
|---|---|---|
| **Marshal Ney** | Aggressive, reckless | Commits cavalry early, accepts casualties |
| **Wellington** | Defensive, terrain-focused | Holds ridgelines, lets enemy attack |
| **Napoleon** | Balanced, decisive | Concentrates force, exploits weak points |

---

## 7. The City Builder: Systematic Urbanism

### 7.1 The Paradigm: Smart Buildings, Dumb Agents

Agents do not run expensive A* scripts. Buildings post to a global C++ `LogisticsJob` array. The 1Hz Matchmaker finds the closest `IDLE` 16-byte citizen and writes the job to their ECS memory. O(1) scaling.

60Hz execution: Citizens blindly follow Flow Fields. The **Traffic Jam** is the gameplay — hand-carrying at 0.5 m/s vs horse carts at 2.0 m/s (capacity=100). Player must pave roads and build warehouses.

> See `CORE_MATH.md §6` for Citizen/Workplace/LogisticsJob structs and the 60Hz tick loop.

### 7.2 The Deep Production Chains

**Alchemy of Death (Black Powder)**: Charcoal + Sulfur + Niter Beds (massive pollution, must zone downwind) → Powder Mill (must snap to river for water-powered trip-hammers) → Arsenal. Enemy burns Powder Mill → artillery falls silent.

**The Arsenal (Heavy Industry)**: Iron + Coal → Blast Furnace → Pig Iron → splits into Gunsmiths (Muskets) and Boring Mills (Cannons).

**Fabric of Courage (Uniforms)**: Sheep → Wool → Weaver → Tailor. Adding imported Indigo → Blue Uniforms → Phase 1 Combat `stiffness` buff.

**Burgage Plot Upgrades** (Manor Lords-style): Tier 1 vegetable garden → Tier 2 backyard forge → Tier 3 stone house with apprentices.

### 7.3 Complex Logistics & Hazard Ordinances

**The Byproduct Web**: Cattle → Slaughterhouse → (Meat + Tallow + Hides). Tallow → Candles (Night Shifts). Hides → Tannery (Pollution!) → Boots (+10% speed), Saddles (Cavalry!), Cartridge Boxes.

**The Infinite Sink (Tools)**: Blacksmiths make Tools (Iron + Coal). All buildings consume micro-fractions of tools. No coal → no tools → pickaxes break → mine stops → no iron → total death spiral.

**Physical Packaging (Cooperage)**: Powder/Salt Beef require Barrels (Hardwood + Iron Bands → Cooper). Barrels are **reusable** — empty barrels circulate like blood cells.

**Hazard Codes (Richmond Ordinance)**: Moving Black Powder generates `Volatility`. Passing a Blacksmith (`Spark_Risk`) triggers a dice roll. Explosion = C++ artillery destruction physics, leveling the city block. Requires spark-proof wagons and isolated bypass roads.

### 7.4 Procedural Urbanism & The Haussmann Decision

**Burgage Plots (Organic)**: Player draws curved spline → C++ Voronoi/Straight-Skeleton → dynamic fence-lined lots.

**Magistral Line Solver (Rational)**: Player draws macro-polygon → C++ Vauban Edge Division Factor (s/6 for hexagon) → mathematically perfect, dead-zone-free Bastions.

**Haussmannization (Late Game)**: Demolish obsolete Star Forts → rubble → paved Macadam Boulevards through slums (Inner Fringe Belt). Paved roads multiply agent speed → massive industrial throughput.

> See `CORE_MATH.md §5` for Vauban Magistral Line vertex math.

### 7.5 The Conscription Bridge

```cpp
e.remove<Citizen>();              // Drops from economy loop
e.add<SoldierFormationTarget>();  // Adds to combat loop
```

Citizens physically drop hammers, don uniforms, march out. Foundries go cold — nobody left to answer the job board. The economic cost of war is instantly, painfully visualized.

### 7.6 The Zeitgeist (Social Stratification)

O(1) SIMD reduction over 100,000 citizens. Tracks artisan anger, food prices, conscription resentment. Feeds into **LLM Mayor** — diegetic guild petitions ("Governor, the Gunsmith Guild refuses to cast more 12-pounders while our sons die in your trenches").

> See `CORE_MATH.md §7` for the Zeitgeist query.

---

## 8. Weather, Environment & Acoustics

### 8.1 Meteorology & Physics

| Condition | Combat Effect | City Effect |
|---|---|---|
| **Rain** | Misfire 5%→70%, mud nullifies ricochet, wagons ×0.1 | Mud roads, slower logistics |
| **Winter** | Rivers freeze (bypass bridges!), attrition without coats | Firewood demand, riots |
| **Wind** | Smoke CA drifts directionally | Pollution/smog drifts |

### 8.2 The Candle Economy & Day/Night

Night shrinks LOS 800m → 40m. Muzzle flashes = 0.5s vision bursts. Work stops unless supplied with **Candles** (Cow → Slaughterhouse → Tallow → Candlemaker). Unlocks Night Shift (double output, triple fire risk).

### 8.3 Acoustic Physics

**Speed of Sound (343 m/s)**: `distance / 343 = audio delay`. Silent white flashes → 6 seconds silence → rolling thunder. **Diegetic Music**: Trip-hammer loops in city. Fifes/drums on march. Orchestral swell only during Panic Grid statistical extremes (40%+ routing).

> See `CORE_MATH.md §8` for VAT shader and acoustic delay GDScript.

---

## 9. Visual Art Pipeline & UI

> **The Cardinal Rule**: Voxels are for the Brain. Real meshes are for the Eyes. The player never sees a cube.

### 9.1 Rendering

- **Procedural Gaussian Splatting (Proc-GS)**: Generates highly-detailed, low-overhead 18th-century building facades fitted to irregular Voronoi plots
- **Vertex Animation Textures (VATs)**: 100,000 agents. `INSTANCE_ID` hash creates random faces, mud splatters, animation staggering. `carrying_item` byte swaps animations
- **Reactive Splatmap**: Shader mixes grass/mud based on C++ `trample_count`. Craters via `artillery_crater` events
- **Volumetric Black Powder**: Smoke CA → Godot 4 VFog. God rays through powder. Muzzle flashes illuminate from inside

### 9.2 Diegetic & Spatial UI

- **Spatial Heatmaps**: Frostpunk 2 style overlays for Spark_Risk, Pollution, Desirability projected onto CA grids
- **Diegetic Interaction**: Physical paper SitReps (LLM-generated). 3D pocket watch for time. No generic floating UI panels

---

## 10. Development Roadmap

- [ ] **Phase 1**: Combat Prototype (Marketing Demo) — Spring-Damper, Volleys, DDA Artillery, Panic CA, VATs
- [ ] **Phase 2**: The Neuro-Symbolic General — State Compressor, Battle Commander, Couriers, Personalities
- [ ] **Phase 3**: The Logistics Bedrock — 1Hz Matchmaker, Flow Fields, Tools/Barrels, Hazard zoning
- [ ] **Phase 4**: Procedural Urbanism & Siege — Voronoi lots, Vauban polygons, Glacis physics, Triage
- [ ] **Phase 5**: The Friction of War — Weather/Mud, Night shifts, Acoustic delays, Conscription bridge
- [ ] **Phase 6**: Visual Polish & Steam — Proc-GS, Volumetric Smoke, Spatial UI, Trailer

---

## 11. Technical File Plan

| File | Purpose |
|---|---|
| **Build & Setup** | |
| `SConstruct` | Links godot-cpp and flecs static libraries |
| `cpp/src/register_types.cpp` | GDExtension entry point. Binds C++ singletons to Godot |
| `STATE.md` | AI Context Ledger (tasks, bugs, current state) |
| `CORE_MATH.md` | Algorithmic reference (all C++ implementations) |
| `.agents/rules.md` | Global AI instructions (DOD only, no Godot 3) |
| **C++ Core (Flecs)** | |
| `cpp/src/ecs/world_manager.cpp` | Instantiates `flecs::world`, runs 60Hz and 1Hz ticks |
| `cpp/src/combat/musket_systems.cpp` | Spring-damper physics, Volley Sieve, DDA Artillery |
| `cpp/src/combat/panic_ca.cpp` | 5Hz double-buffered Voxel Grid arrays |
| `cpp/src/economy/logistics.cpp` | 1Hz Matchmaker, 60Hz flow-field movement, Job Boards |
| `cpp/src/generation/urban_math.cpp` | Voronoi slicing, Vauban Magistral line generation |
| `cpp/src/ai/battle_commander.cpp` | Validation of LLM JSON, utility fallbacks, Courier dispatch |
| **Godot (Shaders / UI)** | |
| `res://shaders/soldier_vat.gdshader` | VAT vertex math, instancing hash, gear toggles |
| `res://shaders/terrain_splatmap.gdshader` | Reads C++ trample_count and wetness |
| `res://ui/diegetic_sitrep.gd` | Parses C++ YAML and displays paper texture UI |
| `res://ai/llm_api_client.gd` | Handles async HTTP requests to Claude/OpenAI |
