---
description: Force data-first design - output ECS component structs before any logic
---

# /data-first — The Memory Architect

When invoked, the agent must **STOP writing game logic**. The ONLY task is to output the Flecs ECS Component structs required for the requested feature.

## Steps

1. Read the relevant section of `docs/GDD.md` for the feature being implemented
2. Output **only** the C++ component structs as Plain Old Data (POD)
3. For each struct:
   - Use only primitive types (`float`, `uint8_t`, `int`, `bool`, `flecs::entity`)
   - Calculate and state the **byte size** in a comment
   - No `std::vector`, no `std::string`, no pointers inside components
   - Keep under 32 bytes per component
4. **STOP and wait** for user approval of the memory layout
5. Only after approval, proceed to write the `flecs::system` that iterates over the components

## Example Output

```cpp
// --- Position (8 bytes) ---
struct Position {
    float x, z;  // World coordinates (Y from terrain)
};

// --- Velocity (8 bytes) ---
struct Velocity {
    float vx, vz;
};

// Awaiting approval before writing systems.
```
