// src/main.cpp
#include <iostream>
#include "core/Engine.hpp"

int main(int argc, char* argv[]) {
    Crescendo::Engine engine;

    // 1. Boot up the engine and core servers
    if (!engine.Initialize("Crescendo Engine - Editor", 1920, 1080)) {
        std::cerr << "Failed to initialize Crescendo Engine!" << std::endl;
        return -1;
    }

    // 2. Set a default camera position (great for viewing freshly loaded maps)
    auto& camera = engine.renderingServer.mainCamera;
    camera.SetPosition(glm::vec3(0.0f, -10.0f, 5.0f)); 
    camera.SetRotation(glm::vec3(0.0f, 90.0f, 0.0f)); 

    // Add character controller temp

    // 3. Run the main engine loop (UI, Rendering, Physics)
    // The SceneSerializer and ImGui now handle all entity loading and saving!
    engine.Run();

    // 4. Safely tear down Vulkan, Jolt, and SDL
    engine.Shutdown();

    return 0;
}