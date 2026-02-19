#include <iostream>
#include "core/Engine.hpp"

int main(int argc, char* argv[]) {
    Crescendo::Engine engine;

    // 1. Initialize
    if (!engine.Initialize("Crescendo Engine - Water Test", 1920, 1080)) {
        return -1;
    }

    // 2. Setup Camera (Your settings)
    auto& camera = engine.renderingServer.mainCamera;
    camera.SetPosition(glm::vec3(0.0f, -10.0f, 5.0f)); 
    camera.SetRotation(glm::vec3(0.0f, 90.0f, 0.0f)); 

    Crescendo::Scene* scene = &engine.scene;

    // 3. Spawn Water Entity
    CBaseEntity* water = scene->CreateEntity("prop_water");
    water->origin = glm::vec3(0.0f, 0.0f, 0.0f);
    water->scale = glm::vec3(1.0f); 
    
    // [CRITICAL FIX] Assign the Texture ID we loaded in Initialize
    // If this stays -1, the engine crashes. Now it reads the safe ID (0 or valid).
    water->textureID = engine.renderingServer.waterTextureID;

    // 4. Find the Water Mesh
    // This looks up the mesh named "Internal_Water" created by createWaterMesh()
    bool foundWater = false;
    for (size_t i = 0; i < engine.renderingServer.meshes.size(); i++) {
        if (engine.renderingServer.meshes[i].name == "Internal_Water") {
            water->modelIndex = i;
            foundWater = true;
            break;
        }
    }
    
    if (!foundWater) {
        std::cout << "[Main] Warning: Could not find 'Internal_Water' mesh!" << std::endl;
    }
    
    // 6. Run
    engine.Run();

    return 0;
}