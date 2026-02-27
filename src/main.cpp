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

    auto& camera = engine.renderingServer.mainCamera;
    camera.SetPosition(glm::vec3(0.0f, -10.0f, 5.0f)); 
    camera.SetRotation(glm::vec3(0.0f, 90.0f, 0.0f)); 

    engine.Run();

    engine.Shutdown();

    return 0;
}