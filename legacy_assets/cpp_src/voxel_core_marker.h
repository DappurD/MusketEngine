#ifndef VOXEL_CORE_MARKER_H
#define VOXEL_CORE_MARKER_H

#include <godot_cpp/classes/object.hpp>

namespace godot {

class VoxelCoreMarker : public Object {
    GDCLASS(VoxelCoreMarker, Object)

protected:
    static void _bind_methods();
};

} // namespace godot

#endif // VOXEL_CORE_MARKER_H
