# autoload/

## What is this?
Always-on services. Like electricity in a house — every room can use it without
running special wires. These scripts are loaded once when the game starts and
stay alive forever. Any other script can talk to them by name.

## What's inside?
- **time_controller.gd** — Controls game speed. Pause, normal (1x), fast (2x),
  or very fast (3x). Every script that uses time reads `TimeController.game_delta`
  instead of raw `delta` so the whole game speeds up together.
- **spatial_grid.gd** — A fast neighbor-finder. Instead of checking every soldier
  against every other soldier (slow!), it divides the map into a grid and only
  checks nearby cells. O(1) lookup speed.
- **day_night_cycle.gd** — Tracks time of day (dawn, day, dusk, night, deep night).
  Affects visibility, flashlight behavior, and atmosphere.
- **asset_manager.gd** — Loads all 3D models (soldiers, guns, trees, buildings)
  from the asset packs at startup. Other scripts ask it for models by name.
- **sound_manager.gd** — Plays all sounds: gunshots, footsteps, explosions,
  reload clicks, death sounds, ambient music. Uses 3D spatial audio so sounds
  come from the right direction.

## How does it work?
Autoloads are registered in `project.godot` under `[autoload]`. Godot creates
them as singletons — there's exactly one of each, and any script can access
them by their name (e.g., `TimeController.game_delta` or `SpatialGrid.get_neighbors()`).

## Who talks to who?
- Every script in the project can use these services.
- `time_controller.gd` is read by almost everything (game speed).
- `spatial_grid.gd` is used by Unit (neighbor detection) and Squad (positioning).
- `day_night_cycle.gd` is used by Unit (vision) and the lighting system.
- `asset_manager.gd` is used by Unit, CoverNode, and GameController (loading models).
- `sound_manager.gd` is used by Unit, Projectile, and Effects (playing sounds).
