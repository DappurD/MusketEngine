# Deep Think Prompt: Grand Strategy Campaign

> **QUALITY MANDATE**: The biggest unsolved problem in strategy gaming is the Total War split — where the campaign map and the battle map are two separate games bolted together. We want ONE seamless map where you zoom from watching a blacksmith to viewing the continental war. No loading screens. No context switches. One simulation, one world.

## Context
Crown & Caliber scales from a single settlement (hour 1) to continental empire (hour 50+). The strategic layer manages multiple cities, AI rival lords, army logistics, and diplomatic relations. All of this exists on the SAME map as the tactical battles.

## Engine: Flecs v4.1.4, Godot 4, voxel terrain, seamless camera zoom (already designed in GDD).

---

## Map Scale & Regions

**Design Questions**:
1. How many regions on the map? 20? 50? How large is a "region"? (Manor Lords: ~3km². EU4: abstract provinces)
2. Fixed hand-crafted map or procedurally generated? Historical Napoleonic Europe or fictional?
3. Region boundaries: political (borders shift with conquest) or geographic (rivers, mountains are fixed)?
4. Map size in world units: if the battle scale is 300m×300m for 1K soldiers, how big is the full continent?

## AI Lords (Rival Players)

AI lords must feel like they're playing the same game as the player:
- Build cities, assign workers, raise armies, trade, forge alliances
- Make strategic decisions: when to attack, when to defend, when to trade

**Design Questions**:
5. Does the AI lord simulate a full city with per-citizen entities? Or use an abstracted "city simulator" for performance?
6. AI army composition: does the AI draft citizens like the player? Or spawn armies from a budget?
7. AI personality types: aggressive, mercantile, defensive, expansionist — how many archetypes?
8. Can the player visit an AI lord's city? If so, it must be physically rendered.

## Era Progression

Four historical eras. Each transforms the tactical META, economy, and unit roster:

| Era | Period | Unit Paradigm | Key Weapon | Formation | Economy |
|-----|--------|--------------|-----------|-----------|---------|
| 1: Pike & Shot | 1520-1650 | Tercio squares: pike blocks protect musketeers | Matchlock arquebus/musket (forked rest) | Bastioned square, 80% pike | Saltpeter men, artisan workshops, bronze foundry |
| 2: Linear Warfare | 1650-1780 | Homogeneous line infantry, pike eliminated | Flintlock musket (Brown Bess/Charleville) | 3-rank line, countermarch, volley fire | Standardized production, regimental bands |
| 3: Napoleonic | 1780-1830 | Ordre mixte: line + column + skirmisher screen | Improved flintlock, socket bayonet | Line/column/square, combined arms | Industrial bakeries, bureaucratic state |
| 4: Rifled Musket | 1830-1860 | Trench warfare begins, massed charges suicidal | Percussion cap, Minié ball (400+ yard range) | Skirmish order, field entrenchments | Factories, railroads, telegraph |

### Critical Tech Unlocks Per Era

| Unlock | Era | Effect | What It Kills |
|--------|-----|--------|---------------|
| **Socket bayonet** | 1→2 | Every musketeer is their own pikeman | Pikemen eliminated entirely |
| **Flintlock** | 1→2 | Reliable ignition, 3 rounds/min | Matchlock obsolete (no more lit wick) |
| **Standardized caliber** | 2 | Universal ammo, interchangeable parts | Regional weapon incompatibility |
| **Percussion cap** | 3→4 | No misfires in rain, faster lock time | Flintlock obsolete |
| **Minié ball + rifling** | 4 | Effective range 100yd → 400yd | Massed frontal charges, tight formations |
| **Breech-loading** | 4 (late) | 7-15 rounds/min from cover | Muzzle-loading, standing fire |

### Unit Roster Per Era

| Era 1 | Era 2 | Era 3 | Era 4 |
|-------|-------|-------|-------|
| Arquebusier (low range) | Line Infantry (standard) | Line Infantry (versatile) | Rifled Infantry (precision) |
| Pikeman (anti-cavalry) | Grenadier (elite shock) | Grenadier (assault) | Sharpshooter (long range) |
| — | — | Voltigeur (skirmisher) | Skirmisher (loose order) |
| Heavy Cavalry | Cuirassier + Hussar | Full cavalry roster | Mounted Infantry (dismount to fight) |
| Bronze Cannon | Field Artillery | Horse Artillery (mobile) | Rifled Artillery (precision) |

**Design Questions**:
9. Era transitions: global (entire map advances) or per-city (your industrial city coexists with your agricultural village)?
10. Tech tree within eras: linear progression or branching choices?
11. Does the AI advance through eras independently? Can an AI lord reach era 4 before you?
12. The pike-to-bayonet transition: is this a single tech unlock that REMOVES pikemen from your roster? Or gradual phase-out?
13. Era 4 crisis: when rifled muskets appear, does the AI adapt tactics (spread out, use cover)? Or keep charging in lines?

## Administrative Radius

Prussian county model: max 15-23km from administrative center = one day's travel.
- Chancellery projects governance radius
- Outside radius: taxes reduced, orders delayed, corruption rises
- Late-game tech (telegraph, railroad) extends reach

**Design Questions**:
12. Administrative radius: how does it interact with region boundaries? Per-region or per-building?
13. Corruption: abstract modifier or physical mechanic (clerks "lose" tax revenue)?
14. Railroad: transportation entity on fixed track? Or just a radius multiplier?

## Army Movement (Strategic Scale)

Armies appear as blocks on the zoomed-out map. When they collide, the camera CAN zoom into tactical battle.

**Design Questions**:
15. Army movement: roads only? Or cross-country with speed penalty?
16. Forced march: faster movement but troops arrive exhausted (fatigue pre-set)?
17. When two armies meet: auto-resolve option or mandatory tactical battle? Player choice?
18. Multi-army engagements: can reinforcements arrive mid-battle from the strategic map?

## Siege Mechanics

Star fort + attacking army = siege. This is the strategic-tactical interface:
1. Attacking army arrives at fortified city
2. Player can: assault immediately (costly), encircle (siege, starve them out), or sap (trench slowly toward walls)
3. Sapping uses `destroy_box()` from voxel engine
4. Breach walls with heavy artillery using `destroy_sphere()`
5. Storm through the breach — tactical bayonet charge through the gap

**Design Questions**:
19. Siege duration: proportional to defender supplies / attacker army size?
20. Sortie: can the defender send troops out to attack siege works?
21. Disease during siege: both sides accumulate miasma over time?

## Campaign Victory

Multiple paths prevent the "30-hour wall":

| Victory | Condition |
|---------|-----------|
| Conquest | Control all regions |
| Economic | Accumulate X wealth + all trade routes |
| Diplomatic | Form alliance covering 75% of the map |
| Cultural | Build all prestige buildings + max dynasty renown |

**Design Questions**:
22. Are these mutually exclusive or can the player pursue multiple?
23. Sandbox mode with no victory condition? (Manor Lords "Rise to Prosperity")
24. Campaign length: target hours for each victory type?

## Save/Load Architecture

A full game state includes: 10K+ entities + voxel chunks + faction relations + dynasty trees.

**Design Questions**:
25. Serialization: Flecs native serialize? Custom binary format? JSON for debugging?
26. Voxel world: save only modified chunks (delta compression)?
27. Target save file size: < 50MB? Load time: < 5 seconds?

## Deliverables
1. Map scale and region specification
2. AI lord architecture (full sim vs. abstracted)
3. Era progression system design
4. Administrative radius + bureaucracy integration
5. Strategic army movement rules
6. Siege mechanics pipeline (encircle → sap → breach → storm)
7. Victory condition system
8. Save/load architecture for 10K+ entities + voxel world
