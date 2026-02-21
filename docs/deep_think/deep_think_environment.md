# Deep Think Prompt #11: Environment & Weather — M19-M22

> **PREREQUISITE**: Read Prompt #0 first.

---

## § Architecture Anchor

| Source | Section | What It Says |
|--------|---------|-------------|
| GDD | §8.1 | Weather: rain=misfire 5→70%, winter=river freeze+attrition, wind=smoke CA drift |
| GDD | §8.2 | Candle Economy: night LOS 800→40m, muzzle flashes=0.5s vision bursts, night shifts need candles |
| GDD | §5.3 | Smoke Grid: same CA as PanicGrid, mapped to Godot VFog |
| CORE_MATH | §4 | CA diffusion kernel (reusable for weather grids) |

## § What's Already Built
- `PanicGrid` 64×64 CA with double-buffering, chunk-skip, Von Neumann diffusion — **SAME architecture for smoke, humidity, wind grids**
- `MusketState.misfire_chance` (0-255 scaled by humidity) — weather writes this
- Terrain splatmap shader (CORE_MATH §8): `trample_count` → mud. Can extend with `wetness`

## § Design Questions
1. How many CA grids? Panic(existing) + Smoke + Humidity + Wind? Or combine into multi-channel?
2. Seasonal cycle: 4 seasons at what game-time rate? Season affects humidity, temperature, growth
3. Fire CA: buildings catch fire → spreads via CA → triggers voxel destruction?
4. Disease: garrison too long without sanitation → disease CA spreads? Siege disease?
5. Day/night: GDD says candles enable night shifts. Is this a global light state + per-building candle check?

## Deliverables
1. Multi-channel CA grid specification (reuse PanicGrid pattern)
2. Seasonal cycle system
3. Fire spread CA
4. Day/night + candle economy integration
5. ⚠️ Traps section
