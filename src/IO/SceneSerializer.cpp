#include "SceneSerializer.hpp"
#include "scene/Scene.hpp"
#include "modules/gltf/AssetLoader.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/physics/PhysicsServer.hpp"
#include "deps/json/json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace Crescendo {

    SceneSerializer::SceneSerializer(Scene* scene, RenderingServer* renderer) 
        : m_Scene(scene), m_Renderer(renderer) {
    }

    bool SceneSerializer::Serialize(const std::string& filepath) {
        json outData;
        outData["SceneName"] = "Untitled Surf Map"; // You can make this dynamic later
        
        json entitiesArray = json::array();

        for (auto* ent : m_Scene->entities) {
            if (!ent) continue;

            json entityJson;
            entityJson["TargetName"]                    = ent->targetName;
            entityJson["ClassName"]                     = ent->className;
            entityJson["ModelIndex"]                    = ent->modelIndex;
            entityJson["TextureID"]                     = ent->textureID;

            // Transform
            entityJson["Transform"]["Position"]         = { ent->origin.x, ent->origin.y, ent->origin.z };
            entityJson["Transform"]["Rotation"]         = { ent->angles.x, ent->angles.y, ent->angles.z };
            entityJson["Transform"]["Scale"]            = { ent->scale.x, ent->scale.y, ent->scale.z };

            // PBR Material
            entityJson["Material"]["Roughness"]         = ent->roughness;
            entityJson["Material"]["Metallic"]          = ent->metallic;
            entityJson["Material"]["Emission"]          = ent->emission;
            entityJson["Material"]["NormalStrength"]    = ent->normalStrength;

            // Volume / Glass   
            entityJson["Volume"]["Transmission"]        = ent->transmission;
            entityJson["Volume"]["Thickness"]           = ent->thickness;
            entityJson["Volume"]["AttDistance"]         = ent->attenuationDistance;
            entityJson["Volume"]["IOR"]                 = ent->ior;
            entityJson["Volume"]["AttColor"]            = { ent->attenuationColor.r, ent->attenuationColor.g, ent->attenuationColor.b };

            entitiesArray.push_back(entityJson);
        }

        outData["Entities"] = entitiesArray;

        // Write to file with pretty-printing (4 spaces)
        std::ofstream file(filepath);
        if (file.is_open()) {
            file << outData.dump(4);
            file.close();
            std::cout << "[Serializer] Successfully saved scene to " << filepath << std::endl;
            return true;
        }

        std::cerr << "[Serializer] Failed to open " << filepath << " for writing!" << std::endl;
        return false;
    }

    bool SceneSerializer::Deserialize(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Serializer] Failed to open " << filepath << " for reading!" << std::endl;
        return false;
    }

    json inData;
    try {
        file >> inData;
    } catch (json::parse_error& e) {
        std::cerr << "[Serializer] Parse Error: " << e.what() << std::endl;
        return false;
    }

    // 1. Clear the current scene
    m_Scene->Clear(); 

    auto entities = inData["Entities"];
    if (entities.is_array()) {
        for (auto& item : entities) {
            // 2. Extract the model path and class info
            std::string className = item.value("ClassName", "prop_static");
            std::string modelPath = item.value("ModelPath", ""); // You must add 'modelPath' to Serialize()!
            
            CBaseEntity* ent = nullptr;

            // 3. If it's a mesh-based prop, we MUST reload it via AssetLoader 
            // This ensures Jolt generates the Mesh Collider for surfing!
            if (!modelPath.empty()) {
                // AssetLoader handles: mesh upload, texture binding, and Jolt collider creation
                AssetLoader::loadModel(m_Renderer, modelPath, m_Scene); 
                ent = m_Scene->entities.back(); // Grab the one we just loaded
            } else {
                // If it's a light or empty, just create it
                ent = m_Scene->CreateEntity(className);
            }

            if (!ent) continue;

            // 4. Restore the Metadata
            ent->targetName = item.value("TargetName", "Unnamed Entity");
            ent->textureID  = item.value("TextureID", 0);

            // 5. Restore the Transform (Safely)
            if (item.contains("Transform")) {
                auto& t = item["Transform"];
                if (t.contains("Position") && t["Position"].is_array()) {
                    ent->origin = glm::vec3(t["Position"][0], t["Position"][1], t["Position"][2]);
                }
                if (t.contains("Rotation") && t["Rotation"].is_array()) {
                    ent->angles = glm::vec3(t["Rotation"][0], t["Rotation"][1], t["Rotation"][2]);
                }
                if (t.contains("Scale") && t["Scale"].is_array()) {
                    ent->scale  = glm::vec3(t["Scale"][0], t["Scale"][1], t["Scale"][2]);
                }
            }

            // 6. Restore PBR Material properties (Safely)
            if (item.contains("Material")) {
                auto& m = item["Material"];
                
                // Only try to read Albedo if it actually exists in the file!
                if (m.contains("Albedo") && m["Albedo"].is_array()) {
                    ent->albedoColor = glm::vec3(m["Albedo"][0], m["Albedo"][1], m["Albedo"][2]);
                }
                
                ent->roughness      = m.value("Roughness", 1.0f);
                ent->metallic       = m.value("Metallic", 0.0f);
                ent->emission       = m.value("Emission", 0.0f);
                ent->normalStrength = m.value("NormalStrength", 0.0f);
            }

            // 7. Restore Volume / Glass (Safely)
            if (item.contains("Volume")) {
                auto& v = item["Volume"];
                ent->transmission        = v.value("Transmission", 0.0f);
                ent->thickness           = v.value("Thickness", 1.0f);
                ent->attenuationDistance = v.value("AttDistance", 1.0f);
                ent->ior                 = v.value("IOR", 1.5f);
                
                if (v.contains("AttColor") && v["AttColor"].is_array()) {
                    ent->attenuationColor = glm::vec3(v["AttColor"][0], v["AttColor"][1], v["AttColor"][2]);
                }
            }

            // 8. CRITICAL: Update the Physics Server
            // This teleports the Jolt body to the saved position/rotation
            if (m_Scene->physics) {
                m_Scene->physics->ResetBody(ent->index, ent->origin, ent->angles);
            }
        }
    }

    std::cout << "[Serializer] Successfully loaded " << entities.size() << " entities." << std::endl;
    return true;
}
}