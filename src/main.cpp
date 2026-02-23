// src/main.cpp
#include <iostream>
#include "core/Engine.hpp"
#include "controllers/VehicleController.hpp"
#include "modules/gltf/AssetLoader.hpp" 

int main(int argc, char* argv[]) {
    Crescendo::Engine engine;

    if (!engine.Initialize("Crescendo Engine - Vehicle Test", 1920, 1080)) {
        return -1;
    }

    auto& camera = engine.renderingServer.mainCamera;
    camera.SetPosition(glm::vec3(0.0f, -10.0f, 5.0f)); 
    camera.SetRotation(glm::vec3(0.0f, 90.0f, 0.0f)); 

    Crescendo::Scene* scene = &engine.scene;
 
    // 1. Load the Chassis using AssetLoader
    int chassisIndex = scene->entities.size();
    Crescendo::AssetLoader::loadModel(&engine.renderingServer, "assets/models/car/Chassis.glb", scene);
    CBaseEntity* chassis = scene->entities[chassisIndex];

    // Allocate the car dynamically so we can control exactly when it is destroyed
    Crescendo::VehicleController* playerCar = new Crescendo::VehicleController();
    CBaseEntity* wheelEntities[4];

    // 2. Load the Wheels.glb file EXACTLY ONCE
    int wheelsStartIndex = scene->entities.size();
    Crescendo::AssetLoader::loadModel(&engine.renderingServer, "assets/models/car/Wheels.glb", scene);
    
    // Safely extract the 4 tires by finding the newly added top-level entities
    int wheelIndex = 0;
    for (size_t i = wheelsStartIndex; i < scene->entities.size() && wheelIndex < 4; i++) {
        CBaseEntity* ent = scene->entities[i];
        
        // If the entity has no parent, it is a root node from the GLTF (a wheel)
        if (ent->moveParent == nullptr) {
            wheelEntities[wheelIndex] = ent;
            wheelIndex++;
        }
    }

    // Fill any missing slots with nullptr and print warnings
    for (size_t i = wheelIndex; i < 4; i++) {
        wheelEntities[i] = nullptr; 
        std::cout << "[Warning] Missing tire mesh " << i << " in Wheels.glb!" << std::endl;
    }

    // 3. Create the Physics Body
    JPH::Body* chassisBody = engine.physicsServer.CreateChassisBody(chassis->index, glm::vec3(0.0f, 0.0f, 5.0f));
    playerCar->Initialize(engine.physicsServer.physicsSystem, chassisBody);

    // 4. Give the engine access to the car, chassis, and the corrected wheels
    engine.activeVehicle = playerCar;
    engine.playerChassis = chassis; // Needed for the Chase Camera!
    for(int i = 0; i < 4; i++) engine.vehicleWheels[i] = wheelEntities[i];
    

    // Run the engine loop
    engine.Run();

    // Clean up Jolt references properly before deleting the car
    playerCar->Cleanup(engine.physicsServer.physicsSystem);
    delete playerCar;
    
    // Now it is safe to tear down Jolt and Vulkan
    engine.Shutdown();

    return 0;
}