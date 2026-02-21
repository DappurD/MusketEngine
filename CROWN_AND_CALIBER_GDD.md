# Crown & Caliber — Game Design Document

**Genre**: Per-Citizen Colony Sim / Grand Strategy / Real-Time Tactics Hybrid  
**Engine**: Musket Engine (Flecs ECS + Godot 4 GDExtension + Legacy Voxel)  
**Core Philosophy**: The economy is the engine; the army is the teeth. Every soldier was once a farmer. Every musket was forged in your city. You feel the weight of empire because you built every ounce of it.

**Inspiration**: Manor Lords (per-citizen tangibility) × Europa Universalis (geopolitical scope) × Total War (tactical battles) — on a single seamless map.

---

## 1. The Player Fantasy

You are the **Sovereign**. Not a military governor. Not a detached overseer. You draw the borders, forge the cannons, draft the men, and personally direct the cavalry charge that wins the war. The game exists at the intersection of three scales simultaneously:

| Scale | What You See | What You Feel |
|-------|-------------|---------------|
| **Street Level** | Families working, smoke from foundries, wagons on roads | Manor Lords — intimate, tangible |
| **Battle Level** | 3-rank musket lines, cavalry charges, artillery barrages | Total War — tactical, visceral |
| **Continental Level** | Borders, trade routes, coalition dynamics, siege networks | EU4 / Paradox — strategic, consequential |

**Critical Design Requirement**: All three scales exist on the **same map**. You zoom seamlessly from watching a blacksmith hammer a musket barrel to viewing the continental war map. The simulation never pauses, never context-switches. This is the Manor Lords camera model, not the Total War two-game model.

---

## 2. The Per-Citizen Simulation

### 2.1 The Family Unit (Smallest Operational Unit)

Every citizen is a Flecs entity. Every family has 2-4 members. They have names, homes, occupations, and needs. When a father dies in battle, his widow and son carry on alone. You FEEL the cost of war because the casualties are YOUR people, not abstract counters.

**Citizen ECS Components:**

| Component | Fields | Purpose |
|-----------|--------|---------|
| `Position` | x, z | Physical location on the map |
| `CitizenState` | occupation, workplace_id, satisfaction | What they do, where they work, how happy they are |
| `FamilyId` | family_id, role (HEAD/SPOUSE/CHILD) | Links family members together |
| `Needs` | food, fuel, clothing (0.0–1.0) | Marketplace satisfaction drives approval |
| `MilitiaReady` | (tag) | This citizen can be drafted |

### 2.2 The Marketplace & Citizen Behavior (Two-Layer Architecture)

Manor Lords bottlenecks at 500 citizens because every citizen does full A* pathfinding to deliver goods. We solve this with two layers:

**Layer 1: The Economy (invisible, fast, 5Hz)**
Needs satisfaction is calculated via grid proximity. Every 0.2s, the `MarketplaceSystem` iterates all citizens: "Is there bread in the market? Is this citizen within range? Yes → tick their food need up." Flat O(N) iteration, no pathfinding. Cheap.

**Layer 2: The Behavior (visible, animated, 60Hz)**
Citizens *visually* act out their economic life through **daily routine state machines**. They don't pathfind — they follow 3 known waypoints using the same spring-damper physics soldiers use:

```
CitizenRoutine {
    phase: SLEEP | COMMUTE_WORK | WORKING | COMMUTE_MARKET | BROWSING | COMMUTE_HOME | EATING
    timer: float           // Time remaining in current phase
    home_pos: (x, z)       // Where they live
    work_pos: (x, z)       // Where they work  
    market_pos: (x, z)     // Nearest market square
}
```

**Daily cycle**: Wake → walk to work → work animation (hammer, plow, stir) → walk to market at midday → browse animation → walk home → eat → sleep. Each phase transition is just "set velocity toward target point." Same spring-damper physics as formation movement.

**Zoom-level rendering**:

| Zoom Level | What Renders | CPU Cost |
|------------|-------------|----------|
| **Street level** | Individual citizens walking, carrying baskets, working at forges, children playing | Full animation, ~100 visible |
| **District level** | Small figures, building smoke, wagon traffic | Simplified, instanced |
| **Region level** | Building silhouettes, production icons, road activity | No individual citizens |

**The moments players will screenshot**: A blacksmith hammering while his wife tends the garden. A line of citizens walking to market at midday. A farmer abandoning his plow when the draft horn sounds. A widow watching the militia return — one man short.

### 2.3 Approval & Migration

| Approval | Effect |
|----------|--------|
| > 75% | 2 new families/month |
| 50-75% | 1 new family/month |
| < 50% | No immigration, risk of families leaving |
| < 25% | Unrest — workers slow, militia refuses to muster |

---

## 3. The Draft System (The Emotional Core)

You do not spawn soldiers. You **conscript citizens**.

1. You select families to draft. The blacksmith's apprentice, the farmer's eldest son, the widow's only child.
2. Their entity mutates: `remove<Occupation>` → `add<MilitiaState>` → `add<BattalionId>`.
3. Their workplace loses a worker. Production drops. Visibly. The farm goes half-tended.
4. If they die in battle, the `FamilyId` marks surviving members as bereaved. Productivity and morale crater.
5. If they survive, they return home. `remove<MilitiaState>` → `add<Occupation>`. The economy recovers.

**The weight**: Sending 200 men to war means 200 families disrupted, 200 workplaces understaffed, 30% of your powder production offline. The decision to go to war is felt in your supply chains within minutes.

---

## 4. The Economic Pillars

### 4.1 Specialized Cities

Cities aren't identical cookie-cutters. Geography forces specialization:

| City Type | Geography | Produces | Needs |
|-----------|-----------|----------|-------|
| **Agricultural** | River valleys, fertile soil | Wheat, livestock, recruits | Tools, textiles |
| **Mining** | Mountains, ore deposits | Iron, coal, stone | Food, timber |
| **Industrial** | Water-wheel rivers, forests | Muskets, powder, artillery | Iron, coal, food |
| **Military** | Frontier borders, forts | Trained soldiers, fortifications | Everything |
| **Trade Hub** | Crossroads, ports | Wealth, exotic goods | Manufactured goods |

### 4.2 The Black Powder Pipeline

The most critical supply chain in the game:

```
Niter Beds (organic waste + space) ──┐
Sulfur Mines (mountain regions) ─────┤──→ Powder Mill (water-wheel) ──→ Cartridge Workshop
Charcoal Kilns (forests) ────────────┘                                        ↓
                                                                    Ready Ammunition
Coal + Iron Ore → Blast Furnace → Foundry → { Artillery Pieces }
                                 → Gunsmith → { Muskets, Carbines }
```

**The Hazard**: Powder mills near blast furnaces risk catastrophic detonation. City zoning matters.

### 4.3 Supply Lines (Physical Wagons)

No global resource pools. Every resource is physically transported:
- Wagon Train entities traverse the road network
- Enemy cavalry can intercept and burn wagons
- Severing a supply road starves a frontier army
- On the tactical battlefield: soldiers carry 40 rounds. Ammunition caissons must be deployed behind the lines.

---

## 5. Combat Escalation Timeline

Combat is never "unlocked." The world is hostile from minute one.

| Time | Threat | Player's Army | Emotional Stakes |
|------|--------|---------------|-----------------|
| **0-30 min** | Calm. Build your settlement. Watch your citizens settle in. | None — but you're already attached. | Investment in citizens |
| **30-45 min** | **Bandit scouts spotted** near timber camp | 6 men, pitchforks, a hunting musket | "Those are my farmers holding sharp sticks" |
| **45-90 min** | Bandit raid escalates — 20 men hit your lumber camp | 15 drafted militia with basic muskets | First draft decision — who do you pull from work? |
| **1.5-3 hrs** | Neighboring lord's patrols probe your border | 50 militia in 3-rank formation, first proper volley | **First real casualties.** The gut punch. |
| **3-8 hrs** | Full border war, supply pressure | 200+ line infantry, first artillery battery | Can your economy sustain a campaign? |
| **8-15 hrs** | Multi-region expansion | Multiple armies, cavalry, specialized cities | The war machine is hungry |
| **15-30 hrs** | Coalition forms against you | Corps-level operations, star fort sieges | Continental consequences |
| **30+ hrs** | Defending hegemony on multiple fronts | Combined arms, navy, espionage | Everything you built is at stake |

---

## 6. Tactical Battlefield

All combat systems from M2-M7.5 are the foundation:

| Unit | Role | Key Mechanic | Engine Status |
|------|------|-------------|---------------|
| **Line Infantry** | Anvil — hold the line | 3-rank formation, fire discipline, reload timing | ✅ Built (M3-M7.5) |
| **Heavy Cavalry** | Hammer — shatter wavering lines | Momentum charge, stamina, locked vector | ✅ Built (M6) |
| **Dragoons** | Scalpel — raid, scout, dismount to skirmish | Component swap: `CavalryState` ↔ `SkirmisherState` | Partial (M6) |
| **Field Artillery** | God of War — suppression, terrain denial | Ballistic arc, canister, ricochet | ✅ Built (M5) |
| **Skirmishers** | Screen — harass, delay, scout | Loose formation, high mobility, low firepower | Not built |
| **Siege Artillery** | Demolisher — breach fortifications | Heavy caliber, voxel `destroy_sphere()` | Not built (needs voxel integration) |

### Morale & Routing (Already Built)
- Panic cellular automata grid (M4)
- Flag/drummer/officer command network (M7)
- Formation cohesion decay (M7.5)
- Routing state with spring-damper release (M4)

---

## 7. Grand Strategy Layer

### 7.1 Diplomacy
- **Marriage alliances**: Character entities with `DynastyId`, modifies faction relations
- **Trade treaties**: Formalizes supply routes, penalties for breaking them
- **Coalition AI**: As your Threat grows, neighbors band together
- **Espionage**: Spy entities infiltrate enemy cities, sabotage powder mills, steal tech

### 7.2 Naval (Future)
Ships as formation entities on water tiles. Same targeting/morale systems. Coastal bombardment uses artillery systems. Blockades sever trade routes.

### 7.3 Fortifications
- **Voxel-based construction**: Players design forts using voxel placement
- **Structural integrity**: Legacy `structural_integrity.cpp` — BFS checks, collapse cascades
- **Siege trenching**: `destroy_box()` along a sapping path, soldiers advance through excavated terrain
- **Star Forts**: Late-game resource sink, procedural bastion geometry

---

## 8. Engine Milestone Map

### Completed (Tactical Combat Core)
| Milestone | Status | Description |
|-----------|--------|-------------|
| M2 | ✅ | Spring-damper formation physics |
| M3 | ✅ | Musket fire, reload, smoke FX |
| M4 | ✅ | Panic CA grid, routing, morale |
| M5 | ✅ | Artillery ballistics, canister, ricochet |
| M6 | ✅ | Cavalry charge, momentum, melee |
| M7 | ✅ | Command network (officer/drummer/flag) |
| M7.5 | ✅ | Fire discipline, dynamic formations |
| Test Suite | ✅ | 13 headless doctest tests, CI-ready |

### Next Milestones (Economy + Scale)
| Milestone | Priority | Description |
|-----------|----------|-------------|
| M8 | 🔴 HIGH | **Spatial hash grid** — fix O(N²) targeting, prerequisite for everything |
| M9 | 🔴 HIGH | **Per-citizen economy** — family entities, needs, marketplace aura, approval |
| M10 | 🔴 HIGH | **Draft system** — citizen ↔ soldier mutation, economic impact |
| M11 | 🟡 MED | **Supply logistics** — wagon entities, road graph, depot network |
| M12 | 🟡 MED | **Production chains** — gunsmith, powder mill, foundry, hazard system |
| M13 | 🟡 MED | **Voxel integration** — port legacy voxel engine, LOS raycasting |
| M14 | 🟡 MED | **Fortifications** — voxel placement, structural integrity, siege |
| M15 | 🟢 LOW | **Grand strategy** — multi-region, diplomatic relations, AI lords |
| M16 | 🟢 LOW | **Naval** — ship entities, coastal bombardment, blockades |
| M17 | 🟢 LOW | **Espionage & politics** — spy entities, dynasty system, marriage |

---

## 9. Open Design Questions for Deep Think

1. **Citizen ECS layout**: How do `FamilyId` links work without pointer chasing? Parent-child relationships in flat ECS?
2. **Marketplace aura**: Grid-based proximity vs. per-citizen distance check? How does stock distribution work at 60Hz?
3. **Draft entity mutation**: What happens to the citizen's `SoldierFormationTarget` when they return from war? Do they remember their old workplace?
4. **Scale ceiling**: With per-citizen entities + soldier entities + buildings + wagons, what's the realistic entity count? 10K? 20K?
5. **Seamless zoom**: How does rendering transition from per-citizen animation at street level to battalion sprites at continental level?
6. **Voxel + ECS integration**: Should the voxel world be a Flecs singleton? How does `destroy_sphere()` emit events to the ECS?

---

## 10. The 30-Second Pitch

> *"Build a city where every citizen has a name. Forge the muskets yourself. When war comes — and it will — draft your people into 3-rank firing lines and fight with the terrifying geometry of 18th-century warfare. Every casualty is someone's father. Every victory was paid for in powder you milled and iron you smelted. You are the Sovereign. You built this empire brick by brick, and you'll defend it volley by volley."*
