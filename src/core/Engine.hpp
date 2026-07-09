#pragma once

#include <memory>
#include "controllers/FPSController.hpp"
#include "servers/display/DisplayServer.hpp"
#include "servers/audio/AudioServer.hpp"
#include "servers/physics/PhysicsServer.hpp" 

#include "servers/rendering/IRenderer.hpp" 

#include "scene/Scene.hpp"
#include "core/EngineState.hpp"
// [INJECTION 1] Include Crescendo OS Supervisor
#include "core/CrescendoOS.hpp"

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

        // [INJECTION 2] Public getter for servers/renderers to access OS routing
        Core::CrescendoOS* GetOS() const { return crescendoOS.get(); }

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
        // [INJECTION 3] Upgraded from standalone BladesUI to Master OS Supervisor!
        std::unique_ptr<Core::CrescendoOS> crescendoOS;

        Scene scene;
        
    private:
        bool isRunning;
        std::unique_ptr<SceneManager> sceneManager;
    };
}