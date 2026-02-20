---
description: Audit GDExtension bindings for correct headers and ClassDB syntax
---

# Audit GDExtension Bridge

## CRITICAL: Path Resolution Rule
// turbo-all

**The Godot project root is `c:\Godot Project\MusketEngine\` (where `project.godot` lives).**

- `res://` = `c:\Godot Project\MusketEngine\`
- `res://bin/` = `c:\Godot Project\MusketEngine\bin\`
- `res://res/` = `c:\Godot Project\MusketEngine\res\`

**ALL paths to files inside the `res/` subdirectory MUST use `res://res/` prefix.**

Examples:
- Scripts: `res://res/scripts/test_bed.gd`
- Scenes: `res://res/scenes/test_bed.tscn`
- Data: `res://res/data/units.json`
- DLL: `res://bin/musket_engine.dll` (bin is at repo root, NOT inside res/)

## 1. Verify project.godot location
```
dir "c:\Godot Project\MusketEngine\project.godot"
```

## 2. Verify .gdextension at project root
```
type "c:\Godot Project\MusketEngine\musket_engine.gdextension"
```
Expected: `windows.debug.x86_64 = "res://bin/musket_engine.dll"`

## 3. Verify DLL exists
```
dir "c:\Godot Project\MusketEngine\bin\musket_engine.dll"
```

## 4. Check .tscn script references use res://res/ prefix
```
findstr /C:"res://scripts" "c:\Godot Project\MusketEngine\res\scenes\*.tscn"
```
If any match found WITHOUT `res://res/scripts`, they are WRONG.

## 5. Check C++ res:// paths use res://res/ for data files
```
findstr /S /C:"res://data" "c:\Godot Project\MusketEngine\cpp\src\*.cpp"
```
If any match found WITHOUT `res://res/data`, they are WRONG.
