#ifndef MUSKET_PREFAB_LOADER_H
#define MUSKET_PREFAB_LOADER_H

#include "../../flecs/flecs.h"

namespace musket {
    void load_all_prefabs(flecs::world& ecs);
}

#endif // MUSKET_PREFAB_LOADER_H
