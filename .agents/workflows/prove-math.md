---
description: Validate algorithm math in pseudocode before writing C++ implementation
---

# /prove-math — The Logic Sieve

When invoked, the agent must **NOT write C++ code**. Instead, explain the algorithm step-by-step in mathematical pseudocode.

## Steps

1. Read the relevant section of `docs/GDD.md` for the algorithm
2. Write a **step-by-step pseudocode** breakdown of the algorithm
3. List all **edge cases** explicitly:
   - Divide-by-zero scenarios
   - Boundary conditions (grid edges, empty arrays)
   - Overflow/underflow with `uint8_t` or `float`
   - What happens when entity count is 0, 1, or MAX
4. State the **Big-O complexity** of the algorithm
5. **STOP and wait** for user confirmation that the math is sound
6. Only after confirmation, write the C++ implementation

## Example Output

```
ALGORITHM: Spring-Damper Physics
INPUT: Position(x,z), Velocity(vx,vz), Target(tx,tz), Stiffness(k), Damping(b)

STEP 1: displacement = target - position
STEP 2: force = (k * displacement) - (b * velocity)
STEP 3: velocity += force * dt
STEP 4: clamp velocity to MAX_SPEED
STEP 5: position += velocity * dt

EDGE CASES:
- dt = 0: force is applied but no movement (harmless)
- displacement = 0: force = -damping * velocity (soldier decelerates to target)
- stiffness = 0: soldier drifts freely (used for ROUTING state)

COMPLEXITY: O(n) per tick, n = number of soldiers

Awaiting math approval before writing C++.
```
