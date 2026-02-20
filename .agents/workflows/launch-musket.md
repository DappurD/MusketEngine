---
description: Launch Godot editor with OpenGL3 for the musket test project
---

# Launch Musket Test (OpenGL3)

Vulkan has a known heap corruption crash (`0xC0000374`) with RTX 5090 + Godot 4.6.
Use OpenGL3 as a workaround.

// turbo-all

1. Launch the Godot editor with OpenGL3 renderer:
```powershell
& "C:\Users\denni\Downloads\Godot_v4.6-stable_win64.exe\Godot_v4.6-stable_win64_console.exe" --path "c:\Godot Project\Test\ai-test-project" -e --rendering-driver opengl3
```

2. In the editor, open `test/musket_test_bed.tscn` and press **F6** to run the scene.
