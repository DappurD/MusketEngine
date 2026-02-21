# Deep Think Audit — Cavalry Impact + Death Buffer

## Overall Verdict: ✅ APPROVED with 1 Critical Adaption

The math is sound. The DOD patterns are correct. One architectural mismatch must be resolved before implementation.

---

## System 1: Cavalry Impact — ✅ PASS

| Design Choice | Verdict | Notes |
|---|---|---|
| **Quadratic momentum (t²)** | ✅ Excellent | Physically intuitive lurch. 3s ramp is perfect for gameplay pacing |
| **Sequential micro-collision** | ✅ Correct | Solves overshoot elegantly — surplus cavalry ride through gaps |
| **Atomic CAS on `is_alive`** | ✅ Future-proof | Our systems are single-threaded now, but this is free insurance for multi-threaded Flecs later |
| **1.8m contact radius** | ✅ Validated | At 12m/s (0.2m/frame), perfectly captures front rank without tunneling |
| **Partial momentum depletion** | ✅ Elegant | `cost = 0.25 / (1.0 - defense)` makes Square (0.9) cost 2.5 per kill vs Line (0.2) costing 0.3125 |

### Adaptions Needed

1. **Flecs v4 syntax**: Deep Think used `.iter()` (v3). Our codebase uses `.each()`. Math identical, just lambda signature change
2. **SpatialHash doesn't exist yet**: For M6, we use brute-force query iteration (same as volley fire). We add spatial hash at M8+ when scale demands it. At 400 agents + 20 cavalry, brute-force is sub-millisecond
3. **`state_flags` as uint32_t**: Good consolidation vs our current `bool is_charging`. Adopt this — one field for walk/charging/disordered

---

## System 2: Death Buffer — ✅ PASS with 1 CRITICAL fix

| Design Choice | Verdict | Notes |
|---|---|---|
| **Fixed 4096 × double buffer** | ✅ Perfect | 65KB fits L2. No realloc mid-artillery-strike |
| **Atomic pointer swap** | ✅ Lock-free | C++ writes A, Godot reads B. Zero mutex |
| **4 floats per event** | ✅ Minimal | mm_idx, cause, impulse_x, impulse_z |
| **2s ragdoll → frozen corpse** | ✅ Zero ongoing cost | Shader clamps `TIME - start_time` to [0, 2] |
| **GPU parabolic arc in vertex shader** | ✅ Brilliant | `VERTEX.y += impulse*t - 9.81*t²`. Zero CPU ragdoll physics |

### 🚨 CRITICAL: MultiMesh Index Instability

> [!CAUTION]
> Our current `sync_transforms()` **repacks alive entities sequentially every frame**. When Entity A (MM index 0) dies, Entity B shifts from MM index 1 → MM index 0. If the death buffer says "MM index 0 died," next frame it incorrectly points to Entity B.

**Deep Think assumes stable MM indices. Our rendering bridge doesn't provide them.**

#### Resolution: Stable Slot Allocation

Change `sync_transforms()` to pack **ALL entities** (alive + dead), not just alive ones. Each entity gets a permanent MM slot at spawn time (store as `uint32_t mm_slot` component). Dead entities stay in the buffer but their `custom_data` tells the shader to play the ragdoll animation instead of the alive animation.

```diff
 // Old: only alive entities
-auto q = ecs.query_builder<...>().with<IsAlive>().build();
 
 // New: ALL entities (alive + dead get permanent slots)
+auto q = ecs.query_builder<...>().build();
+// custom_data[12] encodes: 0.0 = alive, 1.0+ = death_cause
```

This means:
- **Spawn**: Assign `mm_slot = next_slot++` permanently
- **Alive**: Shader renders walk/idle animation
- **Dead**: Shader renders ragdoll VAT (cause + start_time from custom_data)
- **Corpse**: After 2s, shader freezes on final ragdoll frame (static corpse)
- MultiMesh `instance_count` only grows, never shrinks mid-battle

> [!NOTE]
> This is actually *simpler* than the current repacking approach — we eliminate the per-frame O(n) sort and just do direct slot writes.

---

## Recommended Implementation Order

1. **Refactor rendering bridge** → stable MM slots (unlocks death buffer)
2. **Death buffer** → double-buffered DeathEvent ring (shared by all kill systems)
3. **Cavalry systems** → momentum, impact, disorder (uses death buffer for ragdoll)
4. **VAT shader** → GPU parabolic arc (vertex shader only)
5. **Backport** → musket + artillery kills also push to death buffer
6. **Stress test** → validate at tiers S through L
