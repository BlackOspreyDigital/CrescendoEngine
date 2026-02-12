#pragma once
#include "BaseEntity.hpp"
#include <vector>
#include <string>
#include <iostream>
#include <glm/glm.hpp>

// Struct to hold Level Settings
struct EnvironmentSettings {
    // Sun / Lighting
    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.5f, -1.0f, -0.5f));
    glm::vec3 sunColor     = glm::vec3(1.0f, 0.95f, 0.8f);
    float     sunIntensity = 1.2f;

    // Post Processing
    float bloomIntensity = 1.0f;
    float exposure       = 1.0f;
    float gamma          = 2.2f; 
};

class GameWorld {
public:
    EnvironmentSettings environment;

    // The Global Entity List
    std::vector<CBaseEntity*> entityList;
    std::vector<CBaseEntity*>& entities = entityList; 

    ~GameWorld() {
        Clear();
    }
    
    CBaseEntity* CreateEntity(const std::string& className) {
        CBaseEntity* newEnt = nullptr;
        
        newEnt = new CBaseEntity(); 
        
        newEnt->className = className;
        newEnt->index = entityList.size();
        entityList.push_back(newEnt);
        newEnt->Spawn();
        return newEnt;
    }

    void Update(float deltaTime) {
        for (auto* ent : entityList) { if (ent) ent->Think(deltaTime); }
    }

    void RemoveEntity(int indexInList) {
        if (indexInList < 0 || (size_t)indexInList >= entityList.size()) return;
        delete entityList[indexInList];
        entityList.erase(entityList.begin() + indexInList);
        for (size_t i = 0; i < entityList.size(); i++) entityList[i]->index = (int)i;
    }

    void Clear() {
        for (auto* ent : entityList) delete ent;
        entityList.clear();
    }
};