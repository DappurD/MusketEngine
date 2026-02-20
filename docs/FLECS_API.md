# Flecs v4.1.4 — Locked API Cheat Sheet

> **AI DIRECTIVE**: We are using **Flecs v4.1.4** (vendored at `cpp/flecs/flecs.h`).
> Do NOT use syntax from Flecs v3 or earlier. If unsure, **search the header** — do not guess.

---

## ❌ DEPRECATED / REMOVED (Do NOT Use)

| Pattern | Why It's Wrong |
|---|---|
| `ecs.filter<T>()` | **Removed in v4**. Filters merged into queries. |
| `ecs.filter_builder<T>()` | **Removed in v4**. Use `query_builder`. |
| `.iter([](flecs::iter &it, T *ptr) { for (int i : it) ... })` on **system_builder** | **Removed in v4.1.4**. `system_builder` only has `.each()`. |
| `.iter([](flecs::iter &it, T *ptr) { ... })` on built **queries** | **Signature mismatch** in v4. Use `.each()` with references instead. |
| `ecs.count<flecs::Prefab>()` | `flecs::Prefab` is a built-in entity tag, not a component type. Cannot template on it. |
| `e.get<T>()` returning `const T*` | **Changed in v4**: `.get<T>()` returns `const T&` (reference), not a pointer. |

---

## ✅ CORRECT Patterns (v4.1.4)

### Register a Component
```cpp
ecs.component<Position>("Position");
```

### Create an Entity
```cpp
ecs.entity()
    .set<Position>({10.0f, 20.0f})
    .set<Velocity>({0.0f, 0.0f})
    .add<IsAlive>();
```

### Create a Prefab
```cpp
auto prefab = ecs.prefab("line_infantry")
    .set<Position>({0.0f, 0.0f})
    .set<MovementStats>({4.0f, 8.0f});
```

### Query with .each() (Reference-Based — PREFERRED)
```cpp
auto q = ecs.query_builder<const Position, const Velocity>()
             .with<IsAlive>()   // Extra filter term (no data)
             .build();

// O(1) count from archetype tables
int count = q.count();

// Single-pass iteration with references
q.each([&](const Position &p, const Velocity &v) {
    // p and v are references, not pointers
    float x = p.x;
    float vx = v.vx;
});
```

### Register a System (Runs Every Frame)
```cpp
ecs.system<Position, Velocity, const SoldierFormationTarget>(
       "SpringDamperPhysics")
    .with<IsAlive>()     // Extra filter term (no data — NOT in lambda args)
    .each([](flecs::entity e, Position &p, Velocity &v,
             const SoldierFormationTarget &target) {
        float dt = e.world().delta_time();
        // Reference-based access: p.x, v.vx, target.base_stiffness
    });
```

> **CRITICAL**: In Flecs v4.1.4, `system_builder` only has `.each()` — there is
> NO `.iter()` method. `.each()` gives you `flecs::entity` + references.
> Get `delta_time` via `e.world().delta_time()`.
> **CORE_MATH.md §1 shows v3 `.iter()` syntax** — the math is correct,
> but the lambda signature must be translated to `.each()` references.

### Register an Observer (Event-Driven)
```cpp
ecs.observer<Position>("OnDeath")
    .event(flecs::OnRemove)
    .with<IsAlive>()
    .each([](flecs::iter &it, size_t i, Position &pos) {
        // Triggered when IsAlive is removed from an entity with Position
    });
```

### World Tick
```cpp
ecs.progress(delta);  // Runs all registered systems
```

---

## API Quick Reference

| Operation | v4.1.4 Syntax |
|---|---|
| Create query | `ecs.query_builder<A, B>().with<Tag>().build()` |
| Count entities | `query.count()` — O(1) from archetype tables |
| Iterate query | `query.each([](const A &a, const B &b) { ... })` |
| Register system | `ecs.system<A, B>("Name").with<Tag>().each([](flecs::entity e, A &a, B &b) { ... })` |
| Get delta_time | `e.world().delta_time()` (inside `.each()` lambda) |
| Register observer | `ecs.observer<A>("Name").event(flecs::OnRemove).each(...)` |
| Create entity | `ecs.entity().set<A>({...}).add<Tag>()` |
| Get component | `e.get<T>()` — returns `const T&` (reference, NOT pointer) |
| Create prefab | `ecs.prefab("name").set<A>({...})` |
| Register component | `ecs.component<A>("A")` |
| Singleton | `ecs.set<Config>({...})` / `ecs.get<Config>()` |

---

## Common Pitfalls

1. **`.each()` only**: Both systems and queries use `.each()` with reference-based lambdas. There is NO `.iter()` on `system_builder`.
2. **`flecs::entity` first arg**: System `.each()` lambdas take `flecs::entity e` as the first argument, then component references.
3. **`e.world().delta_time()`**: Get frame delta from the entity's world reference.
4. **`.with<Tag>()`**: Adds a filter term with no data. Lambda args must NOT include a parameter for it.
5. **`e.get<T>()`**: Returns `const T&` (reference), NOT `const T*` (pointer). Use dot access, not arrow.
6. **Query caching**: `query_builder<>().build()` creates a cached query. Avoid building the same query repeatedly.
7. **Tags**: `struct IsAlive {};` — empty struct. Use `.add<IsAlive>()` / `.remove<IsAlive>()`.
