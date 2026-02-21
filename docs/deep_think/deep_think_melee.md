# Deep Think Prompt #6: Melee & Bayonet Charge — Extends M6

> **PREREQUISITE**: Read Prompt #0 first. Builds directly on M6 cavalry and M7.5 formations.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §5.1 | Scale Switch: 20+ = formation mode, <20 = skirmish mode |
| GDD | §5.2 | Misfire: humidity 5→70%. Wet powder forces bayonet charges |
| GDD | §5.3 | Panic CA: death = +0.4 panic, routing = +0.05/tick |
| CROWN_AND_CALIBER_GDD | §6.2 | 4-phase bayonet charge (already written in the new GDD) |
| CROWN_AND_CALIBER_GDD | §6.3 | Butcher's Bill wound system |
| CORE_MATH | §1 | Spring-damper. MeleeScrum REPLACES springs with micro-flocking boids |

## § What's Already Built

**Components melee builds on**:
- `CavalryState` (24B): `charge_momentum`, `is_charging`, `lock_dir_x/z`. **Melee reuses the momentum concept**
- `FormationDefense` (4B): `defense` float. Square=0.9, Line=0.2. **Bayonet charge reads this**
- `Disordered` (tag): cavalry post-impact state of vulnerability. **Also triggered by melee aftermath**
- `ChargeOrder` (tag): triggers charge behavior. **Same tag triggers bayonet charge for infantry**
- `SoldierFormationTarget.can_shoot` (bool): set to `false` during charge (fixes bayonets)
- `PanicGrid` 64×64: melee injects MASSIVE panic (+0.8 per death, double combat rate)

**The cavalry charge pipeline** is the TEMPLATE for bayonet charge:
```
ChargeOrder tag → CavalryBallistics system → locked vector → impact → Disordered
```
Bayonet charge:
```
ChargeOrder tag → ??? (new system) → bow-wave fear → collision → MeleeScrum → Disordered
```

## § GDD Spec: 4-Phase Bayonet (from CROWN_AND_CALIBER_GDD §6.2)

### Phase 1: Bow-Wave (Approach)
- `can_shoot = false` (fixes bayonets)
- Movement = dead sprint (max velocity, burns stamina)
- Battalion becomes forward-facing **Fear Emitter** on PanicGrid

### Phase 2: Collision (Snapping Springs)
- Front-rank: `remove<SoldierFormationTarget>` + `add<MeleeScrum>`
- **MeleeScrum** = micro-flocking boids: Seek(nearest enemy) + Repel(friendly shoulders)
- Rear ranks KEEP `SoldierFormationTarget` — press forward (column crush)

### Phase 3: Meat Grinder (O(1) Lethality)
- No 1v1 fencing. Kill probability = local_density × momentum × (1 - fatigue)
- 50 men die in 10 seconds. Each death = +0.8 panic (double normal)
- Scrum is a ticking bomb — compounding death + exhaustion guarantees one side breaks in 15-30s

### Phase 4: Rout
- Losing side hits panic > 0.65 → mass routing
- Winners get `Disordered` tag (10s vulnerability, no fire, no formation)

## § Design Questions

1. `MeleeScrum` component: what's the struct? It replaces `SoldierFormationTarget`. Needs: target_enemy_entity, boid seek/repel vectors? Or just velocity overrides?
2. The bow-wave: charging battalion injects panic into PanicGrid cells AHEAD of it. What's the injection pattern? Cone ahead of battalion centroid?
3. O(1) lethality: "local density × momentum × (1 - fatigue)". How is local density calculated? Spatial hash cell population count?
4. When does MeleeScrum END? Both sides broke? Timer? Density drops below threshold?
5. Mixed melee: what happens when cavalry charges INTO a melee scrum already in progress?
6. **Butcher's Bill**: 70% of casualties enter DOWNED state (GDD §5.5). Wounded soldiers become `Veteran` or `Amputee`. Component design for these persistent tags?

## Deliverables
1. `MeleeScrum` component struct (POD, byte-counted)
2. Bow-wave fear emitter specification (PanicGrid injection pattern)
3. O(1) lethality system (Flecs system signature, math)
4. Phase transitions (when does each phase start/end?)
5. Integration with existing `CavalryState`, `Disordered`, `ChargeOrder`
6. Butcher's Bill wound component (`Veteran`/`Amputee` tags + `Downed` state)
7. ⚠️ Traps section
