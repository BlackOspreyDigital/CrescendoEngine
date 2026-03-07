#pragma once
#include <vector>
#include <string>

#include "BaseEntity.hpp"

namespace Crescendo {

    enum class SkyType {
        SolidColor = 0,
        Procedural = 1,
        HDRMap = 2
    };
   
    struct EnvironmentSettings {
        bool enableFog = false;
        SkyType skyType = SkyType::Procedural; // Default
        
        // Sun / Lighting
        glm::vec3 sunDirection = glm::normalize(glm::vec3(0.5f, -1.0f, -0.5f));
        glm::vec3 sunColor     = glm::vec3(1.0f, 0.95f, 0.8f);
        float     sunIntensity = 1.2f;
        
        // GI and Fog
            glm::vec4 fogColor     = glm::vec4(0.5f, 0.6f, 0.7f, 0.02f); 
            glm::vec4 fogParams    = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); 
            glm::vec3 skyColor     = glm::vec3(0.5f, 0.7f, 1.0f);
            glm::vec3 groundColor  = glm::vec3(0.0f, 0.0f, 0.0f);
        
        // Post Processing
        float bloomIntensity = 1.0f;
        float exposure       = 1.0f;
        float gamma          = 2.2f; 
        
        // Cascaded Shadows
        float shadowBiasConstant = 1.25f;
        float shadowBiasSlope = 1.75;
    };

    class PhysicsServer;

    class Scene {
    public:
        std::vector<CBaseEntity*> entities;
        PhysicsServer* physics = nullptr;

        EnvironmentSettings environment; 

        CBaseEntity* CreateEntity(const std::string& className = "prop_dynamic") {
            CBaseEntity* ent = new CBaseEntity();
            ent->index = (int)entities.size();
            ent->className = className; // Store the type for saving later!
            entities.push_back(ent);
            return ent;
        }

        void DeleteEntity(int index) {
            if (index >= 0 && index < entities.size()) {
                delete entities[index];
                entities.erase(entities.begin() + index); 

                // Re-sync
                for (int i = 0; i < entities.size(); i++) {
                    entities[i]->index = i;
                }
            }
        }

        void Clear() {
            for (auto* ent : entities) if (ent) delete ent;
            entities.clear();
        }

    };
}