# Deep Think Prompt #12: UI & Controls — Throughout

> **PREREQUISITE**: Read Prompt #0 first.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §9.3 | Diegetic UI: spatial heatmaps (Alt key), Aide-de-Camp desk (Tab), no floating health bars |
| GDD | §15.1 | Input Firewall: Godot → Command Struct queue → C++ validates |
| GDD | §15.2 | Cinematic Gimbal Camera: log zoom, pitch interpolation, orbit |
| GDD | §15.3 | Marshal Controls: Total War right-click drag, ghost box, hotkeys |
| GDD | §15.4 | Architect Controls: spline drawing, magnetic snapping |
| GDD | §15.5 | Diegetic UI: visual health via bowing lines, spatial heatmaps, Aide-de-Camp desk |
| GDD | §8.4 | Cartographer's Table: seamless zoom, parchment shader, board game pawns |
| CORE_MATH | §9-10 | Transition shader, SLOD system |

## § What's Already Built
- `fly_camera.gd`: WASD + mouse free-look (placeholder — GDD specifies cinematic gimbal)
- `test_bed.gd`: keybinds (V toggle, C charge, 4-7 fire discipline, 8-0 formations)
- MacroBattalion data: everything the UI needs to display is already computed per frame

## § Design Questions
1. The Input Firewall: all inputs → Command Struct queue → C++ validates. What's the Command Struct? `{Action, EntityID, TargetX, TargetZ, Facing}`?
2. Total War right-click drag: Godot calculates facing + frontage from drag vector → sends to C++. How?
3. Cartographer's Table transition at Y=400m: camera lerps to orthographic. How does the SLOD system interact?
4. Aide-de-Camp desk (Tab): 3D parchment with LLM SitRep. Player types natural language orders?
5. Spatial heatmaps (Alt): PanicGrid + smoke + pollution projected onto terrain. Same CA data rendered as overlay?

## Deliverables
1. Command Struct → Input Firewall specification
2. Cinematic gimbal camera (replace fly_camera)
3. Total War combat controls implementation
4. Cartographer's Table transition pipeline
5. Aide-de-Camp desk specification
6. ⚠️ Traps section
