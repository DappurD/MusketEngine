# Deep Think: Scalable Test Suite Architecture

## Context

You are designing a **scalable automated test suite** for a real-time Napoleonic warfare simulation engine built as a Godot 4 GDExtension (C++17, Flecs ECS, MSVC/Windows). The engine currently has **~850 lines of ECS systems** and **~700 lines of world manager** across these milestones:

| Milestone | Systems |
|-----------|---------|
| M2 | Spring-damper movement, march orders |
| M3 | Musket reload tick, volley fire (hitscan) |
| M4 | Panic CA diffusion (5Hz), routing behavior, death→panic injection |
| M5 | Artillery ballistics, ricochet, canister, limber/unlimber |
| M6 | Battalion rendering, cavalry charge/impact/disorder |
| M7 | Command network (flag/drummer/officer), order latency pipeline |
| M7.5 | 3-rank formations, fire discipline (Hold/AtWill/ByRank/MassVolley), formation shapes (Line/Column/Square), hoisted O(B²) targeting, OBB friendly fire, distributed drummer aura |

### Architecture Constraints
- **No Godot in tests**: The ECS runs in a standalone Flecs world. Tests must NOT depend on `godot-cpp` headers, `ClassDB`, `UtilityFunctions`, or the rendering pipeline.
- **Unity Build**: All `.cpp` files are `#include`d into `musket_master.cpp`. Tests need their own compilation unit.
- **Deterministic**: The engine uses hash-based RNG seeded from `entity_id ^ world_time`. Tests can control time via `ecs.progress(dt)`.
- **Global state**: `g_macro_battalions[MAX_BATTALIONS]` and `g_pending_orders[MAX_BATTALIONS]` are extern globals.
- **No threads**: Single-threaded ECS pipeline. Safe to test synchronously.

### Key Data Structures
```cpp
// 64-byte cache-aligned per-soldier formation slot
struct alignas(64) SoldierFormationTarget {
  double target_x, target_z;
  float base_stiffness, damping_multiplier;
  float face_dir_x, face_dir_z;
  bool can_shoot;
  uint8_t rank_index; // 0=Front, 1=Mid, 2=Rear
  uint8_t pad[6];
};

// Per-battalion macro state (persistent across frames)
struct MacroBattalion {
  float cx, cz;           // Centroid (zeroed each frame)
  int alive_count;         // Zeroed each frame
  uint32_t team_id;
  bool flag_alive, drummer_alive, officer_alive;
  float flag_cohesion;     // Persistent: decays 16s → 0.2 floor
  FireDiscipline fire_discipline;
  uint8_t active_firing_rank;
  float volley_timer;
  float dir_x, dir_z;     // OBB facing
  float ext_w, ext_d;     // OBB half-extents
  int target_bat_id;       // Hoisted macro targeting result
};
```

### Current Test Output (Visual, from today's session)
```
Battalion #0 spawned: 200 soldiers (3-rank line, 67 files wide).
Blue: 200→67 over ~60s (one-sided casualties, Red≈200)
Flag died → cohesion 1.0→0.2 (16s decay, floor at 0.2)
Formation changes: Line(136), Column(136), Square(129)
Scale test: 10,632 entities in 6ms, 28-35 FPS
```

## Your Task

Design a **test suite architecture** that:

1. **Runs without Godot** — pure C++ test binary using Flecs directly
2. **Scales with the engine** — adding M8, M9, M10 shouldn't require rewriting tests
3. **Catches regressions** — especially the numbered Traps (1-29) we've documented
4. **Runs fast** — entire suite < 5 seconds on a modern CPU
5. **Is CI-friendly** — exit code 0/1, no GUI, machine-parseable output

### Test Categories to Design

#### Category 1: Invariant Tests (must ALWAYS hold)
- `sizeof(SoldierFormationTarget) == 64`
- `alignof(SoldierFormationTarget) == 64`
- Centroid pass zeros transients but preserves persistent fields (Trap 23)
- Dead entities don't accumulate in centroid (no ghost positions)
- `flag_cohesion` floors at 0.2, caps at 1.0
- Routing soldiers have `base_stiffness == 0.0`
- `target_bat_id == -1` when no enemies exist

#### Category 2: Formation Geometry Tests
- 3-rank line: N soldiers → ceil(N/3) columns
- Column: 16-wide, only front rank `can_shoot`
- Square: 4-face symmetry, only outermost rank fires
- Face vectors are unit-length after rotation
- OBB extents match formation dimensions + 2m buffer
- No two soldiers share the same slot position (within epsilon)

#### Category 3: Fire Discipline Tests
- HOLD: zero shots fired over 100 ticks
- AT_WILL: soldiers fire independently when reloaded
- BY_RANK: only `active_firing_rank` soldiers fire each 3s window
- MASS_VOLLEY: all fire within 0.5s window, then auto→HOLD
- Dead officer → forced AT_WILL (never BY_RANK or MASS_VOLLEY)
- Routing soldiers never fire (Trap 27)

#### Category 4: Panic & Morale Tests
- Death injection: exactly +0.20 at kill cell
- Contagion: routing soldier emits +0.10*dt
- Route threshold: panic > 0.65 → Routing tag
- Recovery: panic < 0.25 → Routing removed
- Drummer aura: -0.015/s per alive soldier (only when drummer alive)
- Drummer death → aura stops immediately

#### Category 5: Friendly Fire / Targeting Tests
- `target_bat_id` points to nearest ENEMY (never friendly)
- OBB blocking: friendly battalion between shooter and target → `target_bat_id == -1`
- Firing arc: dot < 0.5 → no shot (>60° off-axis)
- `hit_chance *= dot` — angled shots are less accurate

#### Category 6: Performance Regression Tests
- Spawn 10,000 entities: < 50ms
- `ecs.progress(dt)` with 10,000 entities: < 16ms per tick
- `order_formation()` on 500-soldier battalion: < 1ms
- Centroid pass with 25 battalions: < 2ms

#### Category 7: Integration / Scenario Tests
- **"Waterloo"**: 2 battalions face each other, advance, fire → both lose soldiers
- **"Broken Officer"**: Kill officer → discipline degrades to AT_WILL
- **"Drummer Down"**: Kill drummer → panic no longer cleansed → eventual rout
- **"Friendly Shield"**: 3 battalions in line → middle blocks flanks' targeting
- **"Column Charge"**: Switch to Column → march → only front rank fires
- **"Last Stand Square"**: Form square while surrounded → all 4 faces fire

### Questions for Deep Think

1. **Test framework**: Should we use a lightweight C++ test framework (doctest, Catch2, or bare `assert()` + custom runner)? Consider that we need sub-test timing for perf regression.

2. **ECS test harness**: How do we create isolated Flecs worlds per test without leaking state between tests through the `g_macro_battalions` global? Should we wrap the globals in a context struct?

3. **Deterministic time control**: The fire system uses `world_time_total * 100000.0` for RNG seeds. How do we ensure test reproducibility across runs?

4. **CI pipeline**: We use MSVC on Windows. What's the minimal CI setup (GitHub Actions? Local script?) to run the test binary after each build?

5. **Milestone extensibility**: When we add M8 (Terrain + Flow Fields), M9 (Economy), M10 (Weather), how should the test categories expand? Should each milestone own a test file?

6. **Coverage gaps**: What critical behaviors am I NOT testing that could silently break?

### Deliverables Expected
- File structure for the test suite
- Example test implementations for 2-3 tests per category
- Build system integration (SCons addition)
- CI script skeleton
- Guidelines for writing new tests as milestones are added
