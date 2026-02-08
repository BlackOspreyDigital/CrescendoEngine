#pragma once
#include "servers/display/DisplayServer.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/physics/PhysicsServer.hpp" 
#include "core/ScriptSystem.hpp"
#include "scene/CarController.hpp"
#include "scene/Scene.hpp"

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
        
        DisplayServer displayServer;
        RenderingServer renderingServer;
        PhysicsServer physicsServer;


        // SYSTEMS
        ScriptSystem scriptSystem;
        CarController carController;

    private:
        bool isRunning;
        void ProcessEvents();
        void Update();
        void Render();
    };
}