#include <iostream>
#include "core/Engine.hpp"

int main(int argc, char* argv[]) {
    Crescendo::Engine engine;

    if (!engine.Initialize("Crescendo Engine - Physics Test", 1280, 720)) {
        return -1;
    }

    CBaseEntity* cube = engine.renderingServer.gameWorld.CreateEntity("prop_cube");
    if (cube) {
        // Set Visuals
        cube->modelIndex = 0; // Assuming 0 is a cube/box mesh
        cube->origin = glm::vec3(5.0f, 2.0f, 0.0f);
        cube->scale = glm::vec3(1.0f, 1.0f, 1.0f);
        
        // ATTACH SCRIPT
        cube->SetScript("assets/scripts/rotator.lua");
    }
    
    

    //
    // Now we cna use the following
    // CBaseEntity* cube = engine.renderingServer.gameWorld.CreateEntity("prop_cube");
    // cube->SetScript("assets/scripts/rotator.lua");
    //
    // will work on later

    std::cout << "Starting Simulation..." << std::endl;
    engine.Run();
    engine.Shutdown();
    return 0;
}