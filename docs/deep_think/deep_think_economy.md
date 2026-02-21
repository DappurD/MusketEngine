# Deep Think Prompt #3: Economy & Supply Chains — M10-M12

> **PREREQUISITE**: Read Prompt #0 (Meta-Audit) first. Requires M8 (Spatial Hash) and M9 (Citizens) to be designed.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §3 | Core Loop: Zen Mode (build town) → The March (citizens drop tools, don uniforms) → The Crucible (combat) → Recovery |
| GDD | §4.1 | 5 city types: Agricultural, Mining, Industrial, Military, Trade Hub |
| GDD | §4.2 | Black Powder Pipeline: Niter + Sulfur + Charcoal → Powder Mill → Cartridge Workshop → Ready Ammo |
| GDD | §4.3 | Physical supply lines: wagon entities on road network, interceptable by cavalry |
| GDD | §7.2 | Deep Production Chains: Arsenal, Fabric of Courage (uniforms), Burgage Plot upgrades |
| GDD | §7.3 | Complex Logistics: Byproduct Web (cattle→meat+tallow+hides), Infinite Sink (tools), Physical Packaging (barrels), Hazard Codes |
| CORE_MATH | §6 | `Citizen`(16B), `Workplace`(10B), `LogisticsJob` structs and 60Hz movement loop |
| STATE.md | M10 | "Draft system — citizen ↔ soldier mutation, economic impact" |
| STATE.md | M11 | "Supply logistics — wagon entities, road graph, depot network" |
| STATE.md | M12 | "Production chains — gunsmith, powder mill, foundry, hazard system" |

## § What's Already Built

**Components that economy touches**:
- `Position`/`Velocity` — citizens and wagons use these
- `SoldierFormationTarget` — what citizens BECOME when drafted (the bridge)
- `MusketState.ammo_count` — what the economy PRODUCES (cartridges)
- `ArtilleryBattery.ammo_roundshot/canister` — artillery ammo from the Arsenal

**The Combat Escalation Timeline** (GDD §5):
- 0-30 min: Calm, build settlement. Army = none
- 30-45 min: Bandit scouts. Army = 6 men with pitchforks
- 1.5-3 hrs: Border war. Army = 50 militia
- 3-8 hrs: Full war. Army = 200+, first artillery
- This timeline **defines the economy's production rate targets**. The player MUST be able to produce 200 muskets + ammunition in ~3 hours of gameplay.

## § GDD Production Chains (Already Designed)

```
BLACK POWDER:
  Niter Beds (pollution) ──┐
  Sulfur Mines (mountains) ─┤──→ Powder Mill (water-wheel) ──→ Cartridge Workshop → Ammo
  Charcoal Kilns (forests) ─┘

ARSENAL:
  Coal + Iron Ore → Blast Furnace → Pig Iron → Gunsmiths (Muskets)
                                              → Boring Mill (Cannons)

UNIFORMS:
  Sheep → Wool → Weaver → Tailor → Uniforms (+stiffness buff)
  + Imported Indigo → Blue Dye → Blue Uniforms (better buff)

BYPRODUCTS:
  Cattle → Slaughterhouse → Meat + Tallow + Hides
                             Tallow → Candles (Night Shifts)
                             Hides → Tannery → Boots/Saddles/Cartridge Boxes

TOOLS (Infinite Sink):
  Blacksmith: Iron + Coal → Tools
  ALL buildings consume micro-fractions of tools
  No coal → no tools → pickaxes break → mine stops → no iron → DEATH SPIRAL
```

## § Design Questions

### Production Architecture
1. `Workplace` struct (CORE_MATH §6) has `consumes_item`/`produces_item` as single bytes. But the GDD chains have MULTIPLE inputs (Powder Mill needs 3 inputs). Redesign `Workplace` for multi-input recipes?
2. Production rate: continuous float accumulator? Or discrete batch ("every 60s, if all inputs present, produce 1 output")?
3. The byproduct web: Slaughterhouse produces THREE outputs. Is each output a separate `LogisticsJob`?

### Supply Lines (GDD §4.3)
4. Wagons as entities: `Position`, `Velocity`, `CargoManifest`. What struct? They use the same spring-damper movement as soldiers?
5. Road graph: how is it represented? Weighted edge list? Flow field per destination? The GDD says "physical wagons traverse the road network"
6. Cavalry interception: enemy cavalry can burn wagons. Is this just a combat targeting check? Wagon has `IsAlive` tag, cavalry targets it?

### Hazard System (GDD §7.3)
7. Hazard Codes: "Moving Black Powder generates Volatility. Passing a Blacksmith (Spark_Risk) triggers dice roll." Is `Volatility` a spatial grid (like PanicGrid) or per-entity?
8. Explosion physics: "artillery destruction physics, leveling the city block." This is voxel destruction (`destroy_sphere()`) — does economy need voxel engine (M13)?

### The Draft Bridge (GDD §7.5)
9. When 200 men are drafted: 200 workplaces lose workers. Does each workplace auto-post a replacement `LogisticsJob`? Or does production simply stop until workers return?
10. Returning veterans: `remove<SoldierFormationTarget>` + `add<Citizen>`. Do they remember their old workplace? Or reassignment via matchmaker?

### Historical Research Context
11. **Saltpeter vexation**: niter men had legal right to dig in ANYONE's property. Citizens hate this. Gameplay: niter production = approval penalty in affected district?
12. **Horse metabolism**: cavalry horses eat 10-14kg hay/day. Standing army of 200 cavalry = 40,000 bales/season. Is horse feed a supply chain item?
13. **Field bakeries**: armies on campaign need mobile bread ovens. 18-day bread preservation. Is this a building that deploys WITH the army?

## Deliverables
1. `Workplace` struct redesign for multi-input recipes (full vision)
2. `Wagon` / `CargoManifest` component design
3. Road graph representation
4. Supply line routing algorithm
5. Production rate tuning for Combat Escalation Timeline
6. Hazard system specification
7. Draft bridge integration (who fills the empty jobs?)
8. ⚠️ Traps section
