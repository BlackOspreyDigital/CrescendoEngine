#include "SceneSerializer.hpp"
#include "deps/json/json.hpp"

#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace Crescendo {

    SceneSerializer::SceneSerializer(Scene* scene) : m_Scene(scene) {
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

        // Clear the current scene before loading new entities
        m_Scene->Clear(); 

        auto entities = inData["Entities"];
        if (entities.is_array()) {
            for (auto& item : entities) {
                std::string className = item.value("ClassName", "prop_static");
                
                // Create the raw entity
                CBaseEntity* ent = m_Scene->CreateEntity(className);
                
                ent->targetName = item.value("TargetName", "Unnamed Entity");
                ent->modelIndex = item.value("ModelIndex", 0);
                ent->textureID  = item.value("TextureID", 0);

                // Transform
                if (item.contains("Transform")) {
                    auto& t = item["Transform"];
                    ent->origin = glm::vec3(t["Position"][0], t["Position"][1], t["Position"][2]);
                    ent->angles = glm::vec3(t["Rotation"][0], t["Rotation"][1], t["Rotation"][2]);
                    ent->scale  = glm::vec3(t["Scale"][0], t["Scale"][1], t["Scale"][2]);
                }

                // PBR Material
                if (item.contains("Material")) {
                    auto& m = item["Material"];
                    ent->roughness      = m.value("Roughness", 1.0f);
                    ent->metallic       = m.value("Metallic", 0.0f);
                    ent->emission       = m.value("Emission", 0.0f);
                    ent->normalStrength = m.value("NormalStrength", 0.0f);
                }

                // Volume / Glass
                if (item.contains("Volume")) {
                    auto& v = item["Volume"];
                    ent->transmission        = v.value("Transmission", 0.0f);
                    ent->thickness           = v.value("Thickness", 1.0f);
                    ent->attenuationDistance = v.value("AttDistance", 0.0f);
                    ent->ior                 = v.value("IOR", 1.0f);
                    
                    if (v.contains("AttColor")) {
                        ent->attenuationColor = glm::vec3(v["AttColor"][0], v["AttColor"][1], v["AttColor"][2]);
                    }
                }
            }
        }

        std::cout << "[Serializer] Successfully loaded scene from " << filepath << std::endl;
        return true;
    }
}