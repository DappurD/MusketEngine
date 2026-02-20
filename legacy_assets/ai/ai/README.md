# ai/

## What is this?
The brains of the game. Everything that makes soldiers think and act lives here.

## What's inside?
- **colony_ai.gd** — The General. Looks at the whole battlefield and decides
  what each squad should do ("Alpha, capture that flag! Bravo, defend the base!").
- **squad.gd** — The Sergeant. Takes the General's orders and tells individual
  soldiers where to go, what formation to use, and who to shoot.
- **pawn_ai.gd** — The soldier's instincts. A 19-state brain that decides
  moment-to-moment: "Should I shoot? Take cover? Reload? Run? Help my friend?"
- **tactical_pathfinder.gd** — The map reader. Asks the NavMesh "how do I walk
  from here to there without going through walls?" Uses corridor funneling for
  tight, smooth paths.
- **tactical_query.gd** — The scout. Evaluates positions for tactical value:
  "Is this cover good? Can I flank from here? Is this spot safe?"
- **utility_ai.gd** — The decision scorer. Gives every possible action a score
  from 0-100, and the soldier picks the highest one.
- **goap_planner.gd** — The long-term planner. Figures out multi-step plans
  like "I need to reload, then move to cover, then shoot."
- **colony_goal.gd** — Base class for strategic goals (see `goals/` folder).
- **goals/** — Each file is one type of mission: capture a flag, attack, defend,
  flank, triage wounded.

## How does it work?
The AI has five tiers, like an army chain of command:

```
LLM Weight Adjuster (optional)    async every 30-60s — shifts Theater axis weights
  ↓ weight modifiers
Theater Commander (strategic)     C++ IAUS: 8 utility axes + response curves
  ↓ bias multipliers
ColonyAICPP (operational)         C++ regret-based auction + coordination packages
  ↓ goal assignments
Squad (tactical)                  GOAP maneuvers + tactic locking
  ↓ squad orders
SimulationServer (individual)     C++ priority cascade + tactical position scoring + peek behavior
```

The bottom three tiers run entirely in C++ (`cpp/src/simulation_server.cpp`). GDScript
handles rendering, UI, and high-level coordination.

**Unit-level AI runs in C++ SimulationServer:**
- **Tactical position scoring**: 5-axis evaluation (cover + shootability + field-of-fire + height + distance) with role-specific weights. MGs/marksmen search wider (20m) for overwatch positions with clear fields of fire.
- **Peek behavior**: Units in cover physically sidestep 1m to peek and shoot, then slide back. Suppression coupling: more suppression = longer hiding, shorter peeking.
- **Height advantage**: Accuracy bonus (up to 20% tighter spread downhill), cover degradation from elevation, +15 target scoring.

## Who talks to who?
- `cpp/src/theater_commander.h` scores 8 strategic axes and outputs bias multipliers.
- `cpp/src/colony_ai_cpp.h` runs the auction, consuming Theater bias. GDScript `ai/colony_ai.gd` is a thin wrapper.
- `ai/squad.gd` executes tactical maneuvers and relays orders.
- `cpp/src/simulation_server.h` runs the per-unit tick: targeting, movement, combat, peek behavior.
- `ai/pawn_ai.gd` handles nuanced GDScript-side decision-making (utility scoring, GOAP).
- `ai/tactical_pathfinder.gd` is created by each Unit for its own pathfinding.
- `ai/tactical_query.gd` is used by Squad to evaluate tactical positions.

## Squad types
See `ai/SQUAD_TYPES.md` for the squad archetype system (assault/defend/flank/mortar/sniper/recon),
CommanderAI integration rules, and recon drone behavior contracts.
