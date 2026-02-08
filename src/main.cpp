#include <iostream>
#include "core/Engine.hpp"

int main(int argc, char* argv[]) {
    Crescendo::Engine engine;

    if (!engine.Initialize("Crescendo Engine - Physics Test", 1920, 1080)) {
        return -1;
    }

    // Access the camera through the rendering server
    auto& camera = engine.renderingServer.mainCamera;
    camera.SetPosition(glm::vec3(3.0f, 3.0f, 3.0f)); // Move back and up
    camera.SetRotation(glm::vec3(-35.0f, 135.0f, 0.0f)); // Look down at the center

    // 1. Get the World (Scene) from the Renderer
    // We use GetWorld() because Engine doesn't expose GetScene() directly
    Crescendo::Scene* scene = (Crescendo::Scene*)engine.renderingServer.GetWorld();

    // 2. Load the Duck
    // FIX: Use the 'scene' pointer we just retrieved
    engine.renderingServer.loadGLTF("assets/models/Crescenduck/CRESCENDUCK.gltf", scene);
    
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

    std::cout << "Starting Simulation..." << std::endl;
    
    engine.Run();
    
    engine.Shutdown();
    return 0;
}