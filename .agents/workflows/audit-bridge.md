---
description: Audit GDExtension bindings for correct headers and ClassDB syntax
---

# /audit-bridge — The GDExtension Enforcer

When invoked, review the C++ file just written and verify the Godot ↔ C++ bridge is correct.

## Checklist

1. **Headers**: Are we including the correct `<godot_cpp/classes/...>` headers?
   - `MultiMeshInstance3D` → `<godot_cpp/classes/multi_mesh_instance3d.hpp>`
   - `Node3D` → `<godot_cpp/classes/node3d.hpp>`
   - Do NOT use Godot 3 `<Godot.hpp>` or `<Reference.hpp>` (deprecated)

2. **ClassDB bindings**: Is `ClassDB::bind_method` syntax correct in `register_types.cpp`?
   - Method: `ClassDB::bind_method(D_METHOD("method_name", "arg1", "arg2"), &ClassName::method_name);`
   - Property: `ClassDB::bind_method(D_METHOD("get_x"), &C::get_x); ClassDB::bind_method(D_METHOD("set_x", "value"), &C::set_x); ADD_PROPERTY(...)`

3. **Type safety**:
   - Heavy math types (`Transform3D`, `PackedFloat32Array`) passed by reference
   - `String` not `std::string` in Godot-facing APIs
   - `PackedByteArray` / `PackedFloat32Array` for buffer transfers

4. **Build verification**:
   - Does `scons` compile without warnings?
   - Does the `.gdextension` file point to the correct DLL path?

## If Errors Found
- Fix them immediately
- If unsure about the correct Godot 4 API, search local `godot-cpp/include/` headers
- Do NOT guess. Ask user or search.
