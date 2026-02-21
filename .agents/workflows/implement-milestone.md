---
description: Implement a milestone feature with anti-drift guards, correct git workflow, and legacy code isolation
---

# /implement-milestone — The Iron Rails

> **Every milestone follows this exact flow. No shortcuts. No improvisation.**

## ⚠️ FAILURE MODES THIS PREVENTS

| Trap | Guard |
|------|-------|
| Architectural drift | Step 1 forces GDD read BEFORE touching code |
| Hack fixes | Step 2 requires struct design approval BEFORE logic |
| LLM reading old code | Step 0 explicitly scopes what to read and what to IGNORE |
| Bad git practices | Step 7 enforces atomic commits with milestone tags |
| Scope creep | Step 1 locks scope to ONE milestone |

---

## Step 0: Context Fence (CRITICAL)

**Read ONLY these files. Do NOT read legacy_assets/ unless LEGACY_MAP.md points you there.**

```
MUST READ (current architecture):
  docs/GDD.md           — The spec. If it's not here, it doesn't exist
  docs/CORE_MATH.md      — The algorithms. Translate directly to code
  STATE.md               — What's built, what's broken, what's next
  cpp/src/ecs/musket_components.h  — Current component inventory
  cpp/src/ecs/musket_systems.cpp   — Current system inventory

READ ONLY IF LEGACY_MAP.md SAYS TO:
  docs/LEGACY_MAP.md     — Check for this milestone's legacy references
  legacy_assets/...      — ONLY the specific files LEGACY_MAP.md lists
```

// turbo
**Verify context fence:**
```
Get-Content "c:\Godot Project\MusketEngine\STATE.md" | Select-String "## What Is Built" | Select-Object -First 1
```

> **FORBIDDEN**: Do NOT browse `legacy_assets/` looking for inspiration. Do NOT read old GDScript files unless LEGACY_MAP.md explicitly maps them to this milestone. The legacy code uses different patterns (OOP, GDScript, Godot Nodes) that WILL contaminate your design.

---

## Step 1: Scope Lock

1. Identify the ONE milestone you're implementing (e.g., "M8: Spatial Hash")
2. Read the relevant GDD section(s) for that milestone
3. Read the relevant CORE_MATH section(s) if any
4. Check LEGACY_MAP.md for reference code
5. **Write a 3-line scope statement:**

```
MILESTONE: M8 — Spatial Hash Grid
SCOPE: Replace O(N²) targeting in VolleyFireSystem with O(1) spatial hash lookup
NOT IN SCOPE: Flow fields, LOD, SLOD, threading (those are separate milestones)
```

> **STOP. Show the scope statement to the user. Do not proceed without approval.**

---

## Step 2: Data First (/data-first)

Before writing ANY system logic:

1. Design the new component struct(s) as POD
2. Calculate byte sizes
3. Document Read/Write/Exclude for each new system
4. **Show the struct designs to the user. Do not write systems until approved.**

```cpp
// Example scope output:
struct SpatialCell {
    uint16_t entity_indices[MAX_PER_CELL]; // Packed indices
    uint8_t count;
}; // X bytes

// System: SpatialHashRebuild
//   Reads: Position(const), TeamId(const)
//   Writes: SpatialGrid(singleton)
//   Excludes: nothing
//   Rate: 60Hz (every frame, before targeting)
```

---

## Step 3: Prove the Math (/prove-math)

If the feature involves any non-trivial algorithm:

1. Write pseudocode
2. List edge cases
3. Calculate O() complexity
4. **Show the proof to the user. Do not write C++ until the math is verified.**

---

## Step 4: Implement

Now write the C++ code. Rules:

- **ONE file at a time**. Don't scatter changes across 5 files in parallel
- After EACH file change, verify it compiles:

// turbo
```
cd "c:\Godot Project\MusketEngine\cpp"; python -m SCons platform=windows target=template_debug 2>&1 | Select-Object -Last 5
```

- If compilation fails, FIX IT NOW. Do not accumulate broken state
- Reference `CORE_MATH.md` for exact algorithm patterns
- Reference `musket_components.h` for existing struct layouts
- **NEVER add a component field "just in case"** — only add what this milestone needs
- **NEVER hardcode gameplay values** — use JSON prefab values or const that maps to data

### Anti-Hack Checklist
Before writing each function, verify:
- [ ] Does this follow DOD? (flat arrays, no pointers in components)
- [ ] Does this respect the Airgap? (no Godot includes in Flecs systems)
- [ ] Is this O(N) or better? (no nested loops over all entities)
- [ ] Are world coordinates using `double`? (Trap 10)
- [ ] Am I modifying existing components via `state_flags` bitfield, not add/remove? (Trap 6)

---

## Step 5: Build & Verify

// turbo
```
cd "c:\Godot Project\MusketEngine\cpp"; python -m SCons platform=windows target=template_debug
```

Expected: `scons: done building targets.` with zero errors.
Only acceptable warning: `flecs_STATIC macro redefinition`.

If there are errors: fix them. Do NOT proceed to Step 6 with broken code.

---

## Step 6: Update Documentation

After code compiles and works:

1. Update `STATE.md`:
   - Move milestone from "What Is NOT Built" to "What Is Built"
   - Add any new known issues
   - Record performance measurements if applicable

2. Update `musket_components.h` header comment if new components were added

3. Update `CORE_MATH.md` if new algorithms were implemented

> **Do NOT skip this step.** Documentation drift is how the next session gets confused.

---

## Step 7: Git Commit (Atomic)

**One commit per milestone. Message format:**

```
M{N}: {Brief description} — {key detail}
```

// turbo
```
cd "c:\Godot Project\MusketEngine"; git add -A; git status
```

Review the staged files. Verify:
- [ ] No build artifacts (`.obj`, `.pdb`, `.dll` in wrong places)
- [ ] No personal config files
- [ ] Only files related to THIS milestone

Then commit:
```
cd "c:\Godot Project\MusketEngine"; git commit -m "M8: Spatial hash grid - O(1) targeting replaces O(N²) VolleyFire bottleneck"
```

Then push:
```
cd "c:\Godot Project\MusketEngine"; git push origin main
```

> **NEVER batch multiple milestones into one commit.** If you need to fix a bug in M7 while working on M8, commit the M7 fix FIRST with its own message.

---

## Step 8: Checkpoint

After push, verify the world is clean:

// turbo
```
cd "c:\Godot Project\MusketEngine"; git log -1 --oneline; git status
```

Expected: clean working tree, latest commit matches your milestone.

---

## Emergency Procedures

### "I broke something that was working"
```
cd "c:\Godot Project\MusketEngine"; git diff HEAD
```
Review what changed. If the damage is widespread:
```
cd "c:\Godot Project\MusketEngine"; git stash
```
Then rebuild from the last clean commit.

### "I'm not sure if this design is right"
**STOP. Ask the user.** Do not guess. Do not "try something and see." The cost of a wrong struct in ECS is an archetype migration that destroys cache coherence for 100,000 entities.

### "The legacy code does it differently"
The legacy code is REFERENCE ONLY. It used OOP, GDScript, Godot Nodes, and class hierarchies. Our architecture uses DOD, C++, Flecs ECS, and flat arrays. **Steal the math. Rewrite everything else.**

### "I need to add a quick fix"
There are no quick fixes. Every change follows this workflow. If a "quick fix" would violate DOD, Airgap, or the Moddability Mandate — it is not a fix, it is technical debt. Find the correct solution.
