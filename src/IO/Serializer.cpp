#include "Serializer.hpp"
#include <fstream>
#include <iostream>
#include <glm/glm.hpp>

namespace Crescendo {

    void Serializer::SaveProject(Scene* scene, const std::string& path) {
        json root;
        root["version"] = "0.3"; 
        root["entities"] = json::array();

        for (auto* ent : scene->entities) {
            if (!ent) continue;

            json entityJson;
            entityJson["name"] = ent->targetName;
            entityJson["class"] = ent->className;
            entityJson["visible"] = ent->visible;
            entityJson["modelIndex"] = ent->modelIndex;
            entityJson["textureID"] = ent->textureID;
            entityJson["script"] = ent->scriptPath;
            
            entityJson["pos"] = { ent->origin.x, ent->origin.y, ent->origin.z };
            entityJson["rot"] = { ent->angles.x, ent->angles.y, ent->angles.z }; 
            entityJson["scl"] = { ent->scale.x, ent->scale.y, ent->scale.z };
            
            entityJson["roughness"] = ent->roughness;
            entityJson["metallic"] = ent->metallic;

            root["entities"].push_back(entityJson);
        }

        std::ofstream file(path);
        if (file.is_open()) {
            file << root.dump(4);
            file.close();
            std::cout << "[Serializer] Project saved to: " << path << std::endl;
        } else {
            std::cerr << "[Serializer] Failed to open file for writing: " << path << std::endl;
        }
    }

    void Serializer::LoadProject(Scene* scene, const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Serializer] Failed to open file: " << path << std::endl;
            return;
        }

        json root;
        try {
            file >> root;
        } catch (const std::exception& e) {
            std::cerr << "[Serializer] JSON Parse Error: " << e.what() << std::endl;
            return;
        }

        // Clear existing scene
        for (auto* ent : scene->entities) {
            delete ent; 
        }
        scene->entities.clear();

        // Rebuild Scene
        if (root.contains("entities")) {
            for (const auto& j : root["entities"]) {
                std::string className = j.value("class", "prop_static");
                CBaseEntity* ent = scene->CreateEntity(className);
                
                ent->targetName = j.value("name", "Untitled");
                ent->modelIndex = j.value("modelIndex", -1);
                ent->textureID = j.value("textureID", 0);
                ent->visible = j.value("visible", true);
                
                std::string script = j.value("script", "");
                if (!script.empty()) ent->SetScript(script);

                if (j.contains("pos")) ent->origin = glm::vec3(j["pos"][0], j["pos"][1], j["pos"][2]);
                if (j.contains("rot")) ent->angles = glm::vec3(j["rot"][0], j["rot"][1], j["rot"][2]);
                if (j.contains("scl")) ent->scale  = glm::vec3(j["scl"][0], j["scl"][1], j["scl"][2]);

                ent->roughness = j.value("roughness", 0.5f);
                ent->metallic = j.value("metallic", 0.0f);
            }
        }
        
        std::cout << "[Serializer] Project loaded from: " << path << std::endl;
    }
}