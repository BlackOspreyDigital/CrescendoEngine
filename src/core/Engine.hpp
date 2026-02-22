#pragma once

#include "servers/display/DisplayServer.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/physics/PhysicsServer.hpp" 
#include "core/ScriptSystem.hpp"
#include "controllers/VehicleController.hpp"
#include "scene/Scene.hpp"
#include "core/EngineState.hpp"

namespace Crescendo {
    class Engine {
    public:
        Engine();
        ~Engine();

        bool Initialize(const char* title, int width, int height);
        void Run();
        void Shutdown();

        // PUBLIC MEMBERS so main.cpp can access them
        Scene scene; // world manager 
        EngineState currentState = EngineState::Editor;
        EngineState previousState = EngineState::Editor;

        // for the car test
        Crescendo::VehicleController* activeVehicle = nullptr;
        CBaseEntity* vehicleWheels[4] = {nullptr, nullptr, nullptr, nullptr};
        CBaseEntity* playerChassis = nullptr; 
        
        DisplayServer displayServer;
        RenderingServer renderingServer;
        PhysicsServer physicsServer;

        // SYSTEMS
        ScriptSystem scriptSystem;
        
    private:
        bool isRunning;
        void ProcessEvents();
        void Update();
        void Render();
    };
}