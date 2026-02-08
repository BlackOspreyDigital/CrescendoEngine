#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include "BaseEntity.hpp"

namespace Crescendo {
    class Scene {
    public:
        std::vector<CBaseEntity*> entities;

        // FIX: Add 'className' argument with a default value
        CBaseEntity* CreateEntity(const std::string& className = "prop_dynamic") {
            CBaseEntity* ent = new CBaseEntity();
            ent->index = (int)entities.size();
            ent->className = className; // Store the type for saving later!
            entities.push_back(ent);
            return ent;
        }

        void Clear() {
            for (auto* ent : entities) if (ent) delete ent;
            entities.clear();
        }
    };
}