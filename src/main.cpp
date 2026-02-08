#include <iostream>
#include "core/Engine.hpp"

int main(int argc, char* argv[]) {
    Crescendo::Engine engine;

    if (!engine.Initialize("Crescendo Engine - Physics Test", 1920, 1080)) {
        return -1;
    }

    // Access the camera through the rendering server
    auto& camera = engine.renderingServer.mainCamera;
    camera.SetPosition(glm::vec3(3.0f, 3.0f, 3.0f)); 
    camera.SetRotation(glm::vec3(-35.0f, 135.0f, 0.0f)); 

    // --- FIX START ---
    // 1. Get the Scene directly from the Engine (It is a public member)
    // Don't use renderingServer.GetWorld() as it likely points to garbage.
    Crescendo::Scene* scene = &engine.scene;

    // 2. Load the Duck into the Engine's scene
    engine.renderingServer.loadGLTF("assets/models/Crescenduck/CRESCENDUCK.gltf", scene);
    // --- FIX END ---
    
    // 3. Find the Duck Entity
    CBaseEntity* duckEntity = nullptr;

    if (scene) {
        for (auto* ent : scene->entities) {
            if (ent->targetName.find("Duck") != std::string::npos ||
                ent->targetName.find("duck") != std::string::npos ||
                ent->targetName.find("CRESCENDUCK") != std::string::npos) {

                    duckEntity = ent;
                    duckEntity->origin = glm::vec3(0.0f, 0.0f, 0.2f);

                    // Optional: Attach Lua script for logic
                    duckEntity->SetScript("assets/scripts/duck.lua");

                    std::cout << "[GAME] CRESCENDUCK FOUND! Preparing for launch." << std::endl;
                    break;
            }
        }
    }

    if (!duckEntity) {
        std::cout << "[GAME] Warning: Crescenduck entity not found in GLTF." << std::endl;
    }

    std::cout << "Starting Simulation..." << std::endl;
    engine.Run();

    engine.Shutdown();
    return 0;
}