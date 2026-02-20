---
description: Run scaled stress tests and capture performance benchmarks
---
// turbo-all

# /stress-test — Scale Verification

Run the Musket Engine with configurable agent counts to validate
performance at scale. Captures FPS and alive counts.

## 1. Build the DLL (if needed)
```
cd c:\Godot Project\MusketEngine\cpp
python -m SCons platform=windows target=template_debug
```

## 2. Launch with batch mode
```
cd "c:\Godot Project\MusketEngine"
"C:\Users\denni\Godot_v4.6-stable_win64.exe" --path . --rendering-driver opengl3 -- --batch --duration 30
```
Runs for 30 seconds with GPU active, then auto-quits.
Use `--headless` instead of `--batch` for pure CPU (no rendering).

## 3. Scale Tiers
Modify `test_bed.gd` constants to test at these tiers:

| Tier | Soldiers | Cavalry | Batteries | Expected FPS |
|------|----------|---------|-----------|-------------|
| S    | 400      | 0       | 2         | 60+ (proven) |
| M    | 2,000    | 100     | 4         | 60+ (target) |
| L    | 10,000   | 500     | 10        | 30+ (target) |
| XL   | 50,000   | 1,000   | 20        | 15+ (target) |
| XXL  | 100,000  | 2,000   | 40        | ??? (claim)  |

## 4. Read FPS from HUD
The 2-second HUD print in `test_bed.gd` should include FPS:
```
Engine.get_frames_per_second()
```

## 5. Capture screenshot
Add to `test_bed.gd`:
```gdscript
func _capture_screenshot(label: String) -> void:
    var img := get_viewport().get_texture().get_image()
    var path := "user://benchmark_%s_%s.jpg" % [label, Time.get_datetime_string_from_system()]
    img.save_jpg(path, 0.85)
    print("[Benchmark] Screenshot: %s" % path)
```

## Expected Results
- Record FPS at each tier for 30 seconds
- Note any hitches or frame spikes
- If FPS < 30 at any tier, investigate:
  - ECS tick time (Flecs built-in stats)
  - MultiMesh buffer packing time
  - Godot rendering overhead
