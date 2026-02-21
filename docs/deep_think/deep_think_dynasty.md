# Deep Think Prompt #13: Dynasty, Diplomacy & Politics — M17

> **PREREQUISITE**: Read Prompt #0 first. Requires M9 (Citizens) and M15 (Strategic AI).

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §7.6 | Zeitgeist: O(1) SIMD aggregation of citizen morale. "Guild petitions via LLM Mayor" |
| GDD | §7.5 | Conscription bridge: drafting has economic + emotional cost |
| CORE_MATH | §7 | Zeitgeist query: `SocialClass`, `Morale` components → angry_artisans count |
| STATE.md | M17 | "Espionage & politics — spy entities, dynasty system, marriage" |

## § What's Already Built
- Draft bridge: `remove<Citizen>` + `add<SoldierFormationTarget>` — war costs are felt
- `Citizen` + `Workplace` components (designed in CORE_MATH §6)
- PanicGrid → can represent social unrest as CA layer

## § Design Questions (NEW DESIGN — No GDD chapter yet)
1. Character entities: lord, spouse, children, officers. What components? `CharacterState`? `DynastyId`?
2. Named leaders for battalions: officer death in battle → dynasty narrative event
3. Marriage alliances: component linking two dynasty entities? Modifies faction relations?
4. Aging system: characters age in game-time. Children grow to draftable age?
5. Espionage: spy as physical entity (like scout). Moves through enemy city gathering intel
6. Diplomacy: faction relations as a matrix? Trade treaties, alliances, wars?
7. The Zeitgeist: artisan anger, food prices, conscription resentment → feeds into event system (Prompt #9)?

## Deliverables
1. Character/Dynasty component structs
2. Marriage/alliance system
3. Spy entity specification
4. Faction relations matrix
5. Integration with Zeitgeist and event system
6. ⚠️ Traps section
