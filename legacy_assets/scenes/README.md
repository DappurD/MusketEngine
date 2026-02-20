# scenes/

## What is this?
The starting points of the game. When you press Play, Godot loads the scene
listed in `project.godot` — that's `main_menu.tscn`.

## What's inside?
- **main_menu.tscn** — Current main entry scene. Lets you pick scenario/free play.
- **voxel_test.tscn** — Active gameplay/simulation scene using voxel world + C++ sim.
- **main_3d.tscn** — Legacy non-voxel entry flow kept for reference.

## How does it work?
Think of a scene like a blueprint. The current flow is:
`main_menu.tscn` -> `voxel_test.tscn` -> `voxel_test_camera.gd` orchestration
with `SimulationServer` and `ColonyAICPP`.

## Who talks to who?
- `project.godot` points to `main_menu.tscn` as the main scene.
- `main_menu.gd` launches `voxel_test.tscn` for free play and scenarios.
- `voxel_test_camera.gd` drives scenario setup, C++ sim, and result output.
- `main_3d.tscn` -> `world/game_controller.gd` is legacy/reference.
