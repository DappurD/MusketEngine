#include "register_types.h"
#include "tactical_query.h"
#include "combat_los.h"
#include "utility_evaluator.h"
#include "influence_map.h"
#include "tactical_cover_map.h"
#include "gpu_tactical_map.h"
#include "simulation_server.h"
#include "theater_commander.h"
#include "colony_ai_cpp.h"
#include "economy_state.h"
#include "pheromone_map_cpp.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_tactical_ai_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    ClassDB::register_class<TacticalQueryCPP>();
    ClassDB::register_class<CombatLOS>();
    ClassDB::register_class<UtilityEvaluatorCPP>();
    ClassDB::register_class<InfluenceMapCPP>();
    ClassDB::register_class<TacticalCoverMap>();
    ClassDB::register_class<GpuTacticalMap>();
    ClassDB::register_class<SimulationServer>();
    ClassDB::register_class<TheaterCommander>();
    ClassDB::register_class<ColonyAICPP>();
    ClassDB::register_class<EconomyStateCPP>();
    ClassDB::register_class<PheromoneMapCPP>();
}

void uninitialize_tactical_ai_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {
GDExtensionBool GDE_EXPORT tactical_ai_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_tactical_ai_module);
    init_obj.register_terminator(uninitialize_tactical_ai_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
