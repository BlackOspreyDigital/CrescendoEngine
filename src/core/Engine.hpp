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
        // ------------------------------------------------

        EngineState currentState = EngineState::Editor;
        EngineState previousState = EngineState::Editor;

        FPSController* activePlayer = nullptr;
        CBaseEntity* localPlayerModel = nullptr;
        bool playerSpawned = false;
        
        DisplayServer displayServer;
        
        // --- 3. THE SWAP ---
        std::unique_ptr<IRenderer> renderer;
        // -------------------
        
        PhysicsServer physicsServer;
        AudioServer audioServer;

        // SYSTEMS
        std::unique_ptr<ScriptSystem> scriptSystem;

        Scene scene;
        
    private:
        bool isRunning;
        std::unique_ptr<SceneManager> sceneManager;
    };
}