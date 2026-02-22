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
 
    
    // 6. Run
    engine.Run();

    return 0;
}