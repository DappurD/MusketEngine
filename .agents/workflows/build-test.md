---
description: Build and test the Musket Engine C++ extension
---
// turbo-all

# Build & Test Workflow

## ⚠️ SAFETY RULES

1. **CLOSE the Godot editor before rebuilding.** Godot locks the DLL at startup.
   Rebuilding while the editor is open creates a stale DLL that won't load.

2. **NEVER use GDExtension type annotations in GDScript.** GDScript resolves types
   at parse time — if the DLL isn't loaded yet, the script fails to parse.
   ```
   # ❌ BREAKS: @onready var server: MusketServer = $MusketServer
   # ✅ WORKS:  @onready var server = $MusketServer
   ```

## 1. Build the DLL
```
cd c:\Godot Project\MusketEngine\cpp
python -m SCons platform=windows target=template_debug
```
Expected: `scons: done building targets.` with no errors.
The only expected warning is `flecs_STATIC macro redefinition` (harmless).

## 2. Verify DLL exists
```
Test-Path "c:\Godot Project\MusketEngine\bin\musket_engine.dll"
```
Expected: `True` with a recent timestamp.

## 3. Verify GDExtension config
```
Get-Content "c:\Godot Project\MusketEngine\musket_engine.gdextension"
```
Expected: Contains `windows.debug.x86_64` pointing to `res://bin/musket_engine.dll`.

## 4. Launch Godot (editor must have been closed first)
```
& "C:\Users\denni\Godot_v4.6-stable_win64.exe" --path "c:\Godot Project\MusketEngine" --rendering-driver opengl3
```
