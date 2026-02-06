#pragma once
#include "src/scene/BaseEntity.hpp"
#include "src/entities/GridEntity.hpp"
#include <vector>
#include <string>
#include <iostream>

class GameWorld {
public:
    // The Global Entity List
    std::vector<CBaseEntity*> entityList;

    ~GameWorld() {
        Clear();
    }

    // --- The Entity Factory ---
    // This solves the "Hardcoding" problem.
    CBaseEntity* CreateEntity(const std::string& className) {
        CBaseEntity* newEnt = nullptr;

        // Factory Logic
        if (className == "prop_grid") {
            newEnt = new CGridEntity();
        }
        else {
            newEnt = new CBaseEntity(); // Generic fallback
        }

        // Setup
        newEnt->className = className;
        newEnt->index = entityList.size();
        entityList.push_back(newEnt);
        
        // Trigger Spawn Logic
        newEnt->Spawn();

        return newEnt;
    }

    // --- Update Loop ---
    void Update(float deltaTime) {
        for (auto* ent : entityList) {
            if (ent) ent->Think(deltaTime);
        }
    }

    void RemoveEntity(int indexInList) {
        if (indexInList < 0 || (size_t)indexInList >= entityList.size()) return;

        delete entityList[indexInList];

        entityList.erase(entityList.begin() + indexInList);

        for (size_t i = 0; i < entityList.size(); i++) {
            entityList[i]->index = (int)i;
        }
    }

    // --- Cleanup ---
    void Clear() {
        for (auto* ent : entityList) {
            delete ent;
        }
        entityList.clear();
    }
};