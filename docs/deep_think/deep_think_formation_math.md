**Deep Think — Revise Formation Dimensions and Panic Tuning**

> I am building a Napoleonic battle simulator in C++ (Flecs ECS). I just implemented the M7 Command Network (Flag bearers, Drummers, Officers) which adds cohesion, order latency, and speed buffs. However, battalions are collapsing far too fast when attacked.
>
> **The Realization (The Hole):**
> I looked at my `spawn_test_battalion` logic. I am spawning 500-man battalions with a hardcoded `cols = 20`. This means my formations are **20 men wide and 25 men deep**, essentially massive dense squares/columns. Historical line formations were typically 2 or 3 ranks deep (e.g., 166 men wide, 3 ranks deep).
>
> No wonder the math is breaking. My panic grid (Cellular Automata) and inverse sieve volley fire are tuned for thin lines, but I am simulating giant cubes of men.
>
> **Current Engine Specifications:**
> 1.  **Spawn Logic:** `row = i / cols; col = i % cols;` with `cols = 20`. Spacing is `1.5f`.
> 2.  **Panic Grid (M4):** 5Hz CA diffusion.
>     *   Death injects +0.4 fear into the dead soldier's team layer.
>     *   Contagion (Routing soldiers) injects +0.25 (0.05 * 5.0) panic/tick.
>     *   Route Threshold = 0.6. Recovery Threshold = 0.3.
>     *   Drummer cleanses -0.2 panic/tick.
> 3.  **Command Network (M7):**
>     *   Flag Bearer alive = exponential decay to 0.2 floor over 16s upon death.
>     *   Flag Bearer entity spawns at `center_x, center_z`
>     *   Drummer spawns at `center_z + 3.0f` (behind line)
>     *   Officer spawns at `center_z - 2.0f` (front of line)
>
> **What I need from you:**
> I need you to completely re-evaluate the **Formation Shapes** and **Panic Tuning** math to stop this hyper-collapse. Let's fix this at the root.
>
> 1.  **The Formation Dimensions:** If I have a 500-man battalion, what is the exact algorithm or math I should use to arrange them into a proper Napoleonic Line (3 ranks deep)? How should `cols`, `row`, and layout loop change in `spawn_test_battalion`? Keep in mind we might have 200, 500, or 1000 men.
> 2.  **Volley Fire vs Depth:** If the formation is now 166 men wide and 3 ranks deep, how does this affect my O(B) targeting and volley fire (M3)? Will the front rank shield the back 2 ranks properly given my 1.5m spacing and raycast physics?
> 3.  **Panic Math Tuning:** If they are now in a thin 3-deep line, deaths will happen spread out across a wide front. Is +0.4 fear per death still the correct value for the CA Grid if the drummer is cleansing -0.2? I suspect the 0.6 threshold is being hit instantly by a single volley. What should the new Panic Injection and Threshold constants be to make a line hold for a historically accurate amount of time against standard musketry, but break quickly to flanking cavalry or canister?
> 4.  **Command Staff Placement:** If the line is 250 meters wide (166 cols * 1.5m), spawning the Drummer at the exact center means his -0.2 panic cleanse only affects the center of the line. The flanks will route instantly! How do we solve Commander Aura distribution for wide formations? Do I need multiple drummers per battalion (e.g., 1 per company), or should the drummer's cleanse radius be larger (a modified kernel), or should panic diffuse differently?
>
> Please provide concrete C++ math and structural rulings for how to fix my Spawner and my Panic Math to support realistic, resilient Napoleonic lines.
