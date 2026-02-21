**Deep Think — Friendly Fire and Line-of-Sight Mathematics**

> We are refining the M7.5 Dynamic Formations geometry in our C++ Flecs ECS engine. We now have 3-rank Lines, 16x25 Attack Columns, and 4-faced Hollow Squares.
>
> **The Problem:** 
> Our `VolleyFireSystem` uses a macro-battalion centroid distance check, and then finds the nearest enemy (`best_dist_sq`) to fire at. However, nowhere in this logic do we prevent **Friendly Fire** on a macro or micro scale. 
> 
> 1. **Micro Friendly Fire (Intra-Battalion):** If the battalion is in a Hollow Square, how do we mathematically guarantee that Face A (Front) doesn't shoot at an enemy by accidentally firing its musket ball straight through Face C (Rear) standing 20 meters behind it? The `can_shoot` boolean restricts *who* fires (e.g., only the outer 2 ranks), but it does not restrict the *angle* of the shot vector.
> 2. **Macro Friendly Fire (Inter-Battalion):** If Battalion X is standing directly behind Battalion Y, and Battalion X is ordered to fire at an enemy 80m away, Battalion X will happily shoot straight through the backs of Battalion Y. 
> 
> In a traditional engine like Unity, we would do a Physics Raycast against dynamic colliders for every single musket shot (500 raycasts per volley). That violates our Data-Oriented, low-overhead mandate. We cannot do $\mathcal{O}(N^2)$ checks for every soldier to see if a friend is in the way.
>
> **What I need from you:**
> I need a purely mathematical, Data-Oriented solution to enforce **Firing Arcs** and **Friendly Fire Prevention**.
> 
> 1.  **The Firing Arc (Micro Fix):** How can we use the `SoldierFormationTarget` facing vector (or 2D dot products) to create a cheap "Firing Arc" check? For example, a soldier in a Hollow Square should only be allowed to fire if the dot product of his individual facing vector and the vector to the target is, say, $> 0.5$ (a 90-degree frontal cone). Since we now calculate geometric `dir_x` and `dir_z` for the slot, how do we plumb this into the ECS and the Volley system?
> 2.  **Macro Collision Prevention (The Battalion Raycast):** We cannot do individual soldier raycasts. Can we do a cheap $\mathcal{O}(B)$ (where B = 256 battalions) geometry check in the `VolleyFireSystem`? If a soldier in Battalion X wants to shoot at an enemy in Battalion Z, how to we check if the centroid and radius of any friendly Battalion Y intersects the line segment between the shooter and the target? Could you provide the exact 2D line-circle intersection math to reject the shot?
> 3.  **The Plumb-in:** Please write out the concrete C++ modifications to `musket_components.h` (adding facing vectors to the target slot) and `musket_systems.cpp` (the `VolleyFireSystem` dot product check and the friendly-battalion intersection check).
> 
> We need maximum Napoleonic realism: lines only shoot forward, squares only shoot outward, and you never shoot your own men in the back.
