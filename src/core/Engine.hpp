#pragma once

#include "controllers/FPSController.hpp"
#include "servers/display/DisplayServer.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/audio/AudioServer.hpp"
#include "servers/physics/PhysicsServer.hpp" 
#include "core/ScriptSystem.hpp"

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

        FPSController* activePlayer = nullptr;
        CBaseEntity* localPlayerModel = nullptr;
        bool playerSpawned = false;
        
        DisplayServer displayServer;
        RenderingServer renderingServer;
        PhysicsServer physicsServer;
        AudioServer audioServer;

        // SYSTEMS
        ScriptSystem scriptSystem;
        
    private:
        bool isRunning;
        void ProcessEvents();
        void Update();
        void Render();
    };
}