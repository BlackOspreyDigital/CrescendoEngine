#pragma once

#include <memory>
#include "controllers/FPSController.hpp"
#include "servers/display/DisplayServer.hpp"
#include "servers/audio/AudioServer.hpp"
#include "servers/physics/PhysicsServer.hpp" 

#include "servers/rendering/IRenderer.hpp" 

#include "scene/Scene.hpp"
#include "core/EngineState.hpp"

namespace Crescendo {
    // Forward declare the module to keep Engine.hpp lightweight
    namespace Modules { 
        class BladesUI; 
    }

    class ScriptSystem;

    class Engine {
    public:
        Engine();
        ~Engine();

        bool Initialize(const char* title, int width, int height);
        void Run();
        void Shutdown();

        // --- MOVED TO PUBLIC FOR EMSCRIPTEN MAIN LOOP ---
        void ProcessEvents();
        void Update();
        void Render();

        EngineState currentState = EngineState::Editor;
        EngineState previousState = EngineState::Editor;

        FPSController* activePlayer = nullptr;
        CBaseEntity* localPlayerModel = nullptr;
        bool playerSpawned = false;
        
        DisplayServer displayServer;
        
        std::unique_ptr<IRenderer> renderer;
        
        PhysicsServer physicsServer;
        AudioServer audioServer;

        // DEV SYSTEMS
        std::unique_ptr<ScriptSystem> scriptSystem;
        std::unique_ptr<Crescendo::Modules::BladesUI> bladesUI;

        Scene scene;
        
    private:
        bool isRunning;
        std::unique_ptr<SceneManager> sceneManager;
    };
}
