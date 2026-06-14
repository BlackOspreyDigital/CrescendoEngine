#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include "BaseEntity.hpp"
#include "scene/components/ProceduralPlanetComponent.hpp"

namespace Crescendo {

    enum class SkyType {
        SolidColor = 0,
        Procedural = 1,
        HDRMap = 2
    };
   
    struct EnvironmentSettings {
        bool enableFog = false;
        SkyType skyType = SkyType::Procedural;
        
        glm::vec3 sunDirection = glm::normalize(glm::vec3(0.5f, -1.0f, -0.5f));
        glm::vec3 sunColor     = glm::vec3(1.0f, 0.95f, 0.8f);
        float     sunIntensity = 1.2f;
        
        glm::vec4 fogColor     = glm::vec4(0.5f, 0.6f, 0.7f, 0.02f); 
        glm::vec4 fogParams    = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); 
        glm::vec3 skyColor     = glm::vec3(0.5f, 0.7f, 1.0f);
        glm::vec3 groundColor  = glm::vec3(0.0f, 0.0f, 0.0f);

        float ambientIntensity = 1.0f;
        float bloomIntensity = 1.0f;
        float exposure       = 1.0f;
        float gamma          = 2.2f; 
        float shadowBiasConstant = 1.25f; 
        float shadowBiasSlope = 1.75f;
    };

    class PhysicsServer;

    class Scene {
    public:
        std::vector<CBaseEntity*> entities;
        PhysicsServer* physics = nullptr;
        EnvironmentSettings environment; 
        std::string name = "Untitled Scene"; 

        // --- THE FAST CACHES ---
        CBaseEntity* activeSun = nullptr;
        CBaseEntity* activeSky = nullptr;
        std::vector<CBaseEntity*> planets;
        std::vector<CBaseEntity*> pointLights;

        ~Scene() { Clear(); }

        // Call this ONCE at the start of your main game loop (before rendering or physics)
        void RefreshCaches() {
            activeSun = nullptr;
            activeSky = nullptr;
            planets.clear();
            pointLights.clear();

            for (CBaseEntity* ent : entities) {
                if (!ent) continue;

                if (ent->className == "light_directional") activeSun = ent;
                else if (ent->className == "env_sky") activeSky = ent;
                else if (ent->className == "light_point") pointLights.push_back(ent);
                
                // If it has the planet component, add it to the solar system list!
                if (ent->HasComponent<ProceduralPlanetComponent>()) planets.push_back(ent);
            }
        }

        CBaseEntity* CreateEntity(const std::string& className = "prop_dynamic") {
            CBaseEntity* ent = new CBaseEntity();
            ent->index = (int)entities.size();
            ent->className = className; 
            entities.push_back(ent);
            return ent;
        }

        void DeleteEntity(int index) {
            if (index >= 0 && index < entities.size()) {
                CBaseEntity* target = entities[index];
                if (!target) return; 

                for (CBaseEntity* ent : entities) {
                    if (ent && !ent->children.empty()) {
                        ent->children.erase(
                            std::remove(ent->children.begin(), ent->children.end(), target),
                            ent->children.end()
                        );
                    }
                }
                
                delete target;
                entities.erase(entities.begin() + index); 

                for (size_t i = 0; i < entities.size(); i++) {
                    entities[i]->index = i;
                }
            }
        }

        void Clear() {
            for (auto* ent : entities) if (ent) delete ent;
            entities.clear();
            RefreshCaches(); // Clear caches to prevent dangling pointers
        }
    };
}