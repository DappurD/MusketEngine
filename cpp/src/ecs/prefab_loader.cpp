#include "prefab_loader.h"
#include "../../thirdparty/json.hpp"
#include "musket_components.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using json = nlohmann::json;

namespace musket {

void load_all_prefabs(flecs::world &ecs) {
  // 1. Load Units
  godot::String units_path = "res://res/data/units.json";
  if (godot::FileAccess::file_exists(units_path)) {
    godot::String content = godot::FileAccess::get_file_as_string(units_path);
    try {
      json j = json::parse(content.utf8().get_data());

      if (j.contains("units")) {
        for (auto &unit : j["units"]) {
          std::string unit_id = unit["unit_id"].get<std::string>();
          auto prefab = ecs.prefab(unit_id.c_str());
          godot::UtilityFunctions::print("Created Unit Prefab: ",
                                         godot::String(unit_id.c_str()));

          if (unit.contains("components")) {
            auto &comps = unit["components"];

            if (comps.contains("FormationTarget")) {
              prefab.set<SoldierFormationTarget>(
                  {0.0,
                   0.0, // target_x, target_z (double)
                   comps["FormationTarget"].value("base_stiffness", 50.0f),
                   comps["FormationTarget"].value("damping_multiplier", 2.0f),
                   0.0f,
                   -1.0f,
                   true,
                   0,
                   {}});
            }
            if (comps.contains("MovementStats")) {
              prefab.set<MovementStats>(
                  {comps["MovementStats"].value("base_speed", 4.0f),
                   comps["MovementStats"].value("charge_speed", 8.0f)});
            }
            if (comps.contains("MusketState")) {
              prefab.set<MusketState>(
                  {comps["MusketState"].value("reload_timer", 15.0f),
                   comps["MusketState"].value("ammo_count", (uint8_t)60),
                   comps["MusketState"].value("misfire_chance", (uint8_t)5)});
            }
            if (comps.contains("CavalryState")) {
              prefab.set<CavalryState>(
                  {comps["CavalryState"].value("charge_momentum", 0.0f), 0.0f,
                   0.0f, 0.0f, 0u, 0u});
            }
          }
        }
      }
    } catch (json::parse_error &e) {
      godot::UtilityFunctions::printerr("Parse error in units.json: ",
                                        e.what());
    }
  } else {
    godot::UtilityFunctions::printerr(
        "Failed to find res://res/data/units.json");
  }

  // 2. Load Buildings
  godot::String build_path = "res://res/data/buildings.json";
  if (godot::FileAccess::file_exists(build_path)) {
    godot::String content = godot::FileAccess::get_file_as_string(build_path);
    try {
      json j = json::parse(content.utf8().get_data());

      if (j.contains("buildings")) {
        for (auto &bld : j["buildings"]) {
          std::string building_id = bld["building_id"].get<std::string>();
          auto prefab = ecs.prefab(building_id.c_str());
          godot::UtilityFunctions::print("Created Building Prefab: ",
                                         godot::String(building_id.c_str()));

          if (bld.contains("components") &&
              bld["components"].contains("Workplace")) {
            auto &wp = bld["components"]["Workplace"];
            Workplace w = {};
            w.consumes_item = wp.value("consumes_item", (uint8_t)0);
            w.produces_item = wp.value("produces_item", (uint8_t)0);
            w.inventory_in = wp.value("inventory_in", (int16_t)0);
            w.inventory_out = wp.value("inventory_out", (int16_t)0);
            w.tool_durability = wp.value("tool_durability", 100.0f);
            w.max_workers = wp.value("max_workers", (int16_t)4);
            w.throughput_rate = wp.value("throughput_rate", (uint32_t)1);
            prefab.set<Workplace>(w);
          }
        }
      }
    } catch (json::parse_error &e) {
      godot::UtilityFunctions::printerr("Parse error in buildings.json: ",
                                        e.what());
    }
  } else {
    godot::UtilityFunctions::printerr(
        "Failed to find res://res/data/buildings.json");
  }
}

} // namespace musket
