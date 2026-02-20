#include "chunk_mesh_worker.h"
#include "gpu_chunk_culler.h"
#include "radiance_cascades.h"
#include "structural_integrity.h"
#include "svdag/svdag_renderer.h"
#include "voxel_core_marker.h"
#include "voxel_post_effects.h"
#include "voxel_world.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_voxel_core_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
  ClassDB::register_class<VoxelCoreMarker>();
  ClassDB::register_class<VoxelWorld>();
  ClassDB::register_class<ChunkMeshWorker>();
  ClassDB::register_class<GpuChunkCuller>();
  ClassDB::register_class<StructuralIntegrity>();
  ClassDB::register_class<RadianceCascadesEffect>();
  ClassDB::register_class<VoxelPostEffect>();
  ClassDB::register_class<SVDAGRenderer>();
}

void uninitialize_voxel_core_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
}

extern "C" {
GDExtensionBool GDE_EXPORT
voxel_core_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                        const GDExtensionClassLibraryPtr p_library,
                        GDExtensionInitialization *r_initialization) {
  godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                                 r_initialization);

  init_obj.register_initializer(initialize_voxel_core_module);
  init_obj.register_terminator(uninitialize_voxel_core_module);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);

  return init_obj.init();
}
}
