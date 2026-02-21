# Deep Think Prompt: Diplomacy, Politics & Bureaucracy

> **QUALITY MANDATE**: EU4 has deep diplomacy but zero city-building. Manor Lords has zero diplomacy. We need BOTH — and we need the bureaucratic state itself to be a resource that limits your empire's reach. No game has ever made paperwork a strategic mechanic.

## Context
Crown & Caliber spans from a single settlement to a continental empire. The political layer must scale from "pay off the local bandit chief" to "manage a coalition war against three rival powers." Crucially, the **bureaucracy** is a physical bottleneck — your administrative capacity limits how many cities you can effectively govern.

## Engine: Flecs v4.1.4, global arrays for aggregate state.

---

## Faction Relation Matrix

Diplomatic relations between factions are continuous floats, not binary war/peace:

```
FactionRelation g_relations[MAX_FACTIONS][MAX_FACTIONS]; // symmetric matrix

struct FactionRelation {
    float opinion;      // -100 to 100
    float trust;        // 0 to 100 (built over time, lost instantly)
    float threat;       // 0 to 100 (accumulates as you expand)
    uint8_t treaty;     // NONE, TRADE, ALLIANCE, VASSAL, WAR
};
```

**Design Questions**:
1. How does opinion drift? Decay toward neutral over time? Or events shift it permanently?
2. Trust: how is it earned (keeping promises, honoring treaties) and destroyed (betrayal, surprise war)?
3. Threat: what accumulates it? (Territory conquered, army size, proximity) How does it trigger coalitions?

## Coalition Formation

When your Threat exceeds a threshold, nearby factions band together:
- Coalition = temporary multi-faction alliance against the player
- Triggered dynamically (not scripted) based on Threat accumulation
- Dissolves when Threat drops below threshold (war exhaustion, territory loss)

**Design Questions**:
4. Coalition trigger: Threat > 60? Or weighted by member opinions?
5. Coalition internal dynamics: do members coordinate attacks, or act independently?
6. Can the player break a coalition through diplomacy (bribe one member to leave)?

## War & Peace

**Declaration**: Requires Influence cost + casus belli (claim on territory, insult, treaty violation).
**Peace Treaty**: Negotiated terms — territory exchange, tribute, trade concessions, hostage exchange.

**Design Questions**:
7. War exhaustion: accumulates from casualties, supply shortages, lost battles. Forces peace at high levels?
8. Casus belli: does fabricating a claim require an espionage action?
9. Separate peace: can the player negotiate with individual coalition members?

## The Bureaucracy Resource

By the 1800s, the Treasury Building accumulated 7,000 cubic feet of records annually. Bureaucracy is the central nervous system of the state.

### Administrative Radius
- Each **Chancellery** building projects a radius on the map
- Within radius: full tax collection (100%), fast draft orders, quick supply routing
- Outside radius: tax collection drops (50%), orders delayed, corruption increases
- The Prussian model: county radius was 15-23km — distance an inhabitant could travel to and from in one day

**Design Questions**:
10. Administrative radius: fixed per Chancellery level? Or scales with assigned clerks?
11. How does "outside radius" translate to ECS? Modifier on `CityDistrict` efficiency?
12. Telegraph (late-game tech): removes distance penalty? Or just reduces delay?

### The Signature Bottleneck
Until mid-19th century, only department heads could sign official papers. This created massive backlogs.

- **Bureaucratic capacity** = number of Chancellery clerks × processing speed
- Each **action** has a paperwork cost: draft order (1), trade deal (3), tax collection (1/district)
- If actions/turn exceed capacity: backlog forms, orders are delayed

**Design Questions**:
13. Should paperwork be a visible queue the player can see? (Like a to-do list that stacks up)
14. Corruption: if backlog is high, do clerks "lose" documents? (Random chance of failed orders)
15. Reform tech: "Bureaucratic Reform" reduces per-action cost? "Vertical Filing" (1880s invention) doubles processing speed?

### War Office vs. Tax Office
Separate bureaucratic buildings for military and civilian administration:
- **War Office**: processes draft orders, army supplies, battlefield reports
- **Tax Office**: processes taxation, trade contracts, civic improvements
- Assigning clerks to one starves the other — another labor competition mechanic

**Design Questions**:
16. Is this too granular? Or does it create meaningful strategic choice?
17. Combined "Chancellery" that handles both, with internal priority slider?

## Internal Politics

Noble families in your court demand privileges:
- **Council**: 3-5 noble families advise the lord. Happy council = approval bonus. Ignored council = rebellion risk.
- **Privileges**: tax exemptions, military command positions, land grants. Granting them costs resources. Denying them costs loyalty.
- **Factions**: nobles form internal factions (military hawks, economic doves, reformists). Player must balance.

**Design Questions**:
18. Council mechanics: Crusader Kings style (vote on decisions) or Victoria style (influence slider)?
19. Rebellion trigger: what combination of factors? Low loyalty + war exhaustion + food shortage?
20. Noble families as character entities: do they have dynasty systems like the player?

## Deliverables
1. Faction relation matrix architecture
2. Coalition formation trigger math
3. War declaration and peace treaty negotiation system
4. Administrative radius and bureaucratic capacity formulas
5. Signature bottleneck mechanic specification
6. Internal politics / council system
7. Rebellion trigger conditions
8. Performance: faction matrix for 8-12 factions at 1Hz update rate
