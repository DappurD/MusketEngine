# ui/

## What is this?
Everything the player sees on screen that isn't the 3D world itself — health
bars, squad information, debug overlays.

## What's inside?
- **hud.gd** — The player's heads-up display. Shows team scores, selected unit
  info, combat log, and game controls.
- **ai_debug_overlay.gd** — A floating debug panel above each soldier showing
  their AI state, health, current order, and pathfinding data. Toggled on/off
  for debugging.

## How does it work?
HUD is a child of the GameController. It reads game state and draws UI elements
on screen. The AIDebugOverlay is a child of each Unit — it floats above the
soldier's head and shows what their brain is thinking.

## Who talks to who?
- `ui/hud.gd` reads from `world/game_controller.gd` (scores, game state).
- `ui/ai_debug_overlay.gd` reads from its parent Unit and `ai/pawn_ai.gd`.
