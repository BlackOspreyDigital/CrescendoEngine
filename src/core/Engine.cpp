#include "Engine.hpp"
#include <iostream>
#include <cstdarg> 
#include <Jolt/Core/IssueReporting.h>
#include <SDL2/SDL.h>
#include "core/ScriptSystem.hpp"

// Jolt Callback
static bool CustomAssertFailed(const char* inExpression, const char* inMessage, const char* inFile, unsigned int inLine) {
    std::cerr << "\n!!! JOLT ASSERTION FAILED !!!\n" << inFile << ":" << inLine << "\nExpr: " << inExpression << "\nMsg:  " << (inMessage ? inMessage : "N/A") << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
    return true; 
}

namespace JPH {
    bool (*AssertFailed)(const char*, const char*, const char*, unsigned int) = CustomAssertFailed;
}

namespace Crescendo {

    Engine::Engine() : isRunning(false) {}
    Engine::~Engine() {}

    bool Engine::Initialize(const char* title, int width, int height) {
        if (!displayServer.initialize(title, width, height)) return false;
        if (!renderingServer.initialize(&displayServer)) return false;

        scriptSystem.Initialize();
        scriptSystem.LoadScript("assets/scripts/car_physics.lua");

        physicsServer.Initialize();

        isRunning = true;
        return true;
    }

    void Engine::Run() {
        std::cout << "[Engine] Entering Main Loop..." << std::endl;
        while (isRunning) {
            // Checkpoint 1
            // std::cout << "(1) Polling..." << std::endl;
            ProcessEvents();

            // Checkpoint 2
            // std::cout << "(2) Updating..." << std::endl;
            Update();

            // Checkpoint 3
            // std::cout << "(3) Rendering..." << std::endl;
            Render();
        }
        Shutdown();
    }

    void Engine::ProcessEvents() {
        displayServer.poll_events(isRunning);
    }

    void Engine::Update() {
        float dt = 1.0f / 60.0f; 

        const Uint8* state = SDL_GetKeyboardState(NULL);
        bool w = state[SDL_SCANCODE_W];
        bool s = state[SDL_SCANCODE_S];
        bool a = state[SDL_SCANCODE_A];
        bool d = state[SDL_SCANCODE_D];

        scriptSystem.UpdateCar(carController, dt, w, s, a, d);
        carController.SyncVisuals();

        if (scene.entities.size() > 0) {
            for (auto* entity : scene.entities) {
                if (entity && entity->hasScript) {
                    scriptSystem.RunEntityScript(entity, dt);
                }
            }
            // Physics Update
            physicsServer.Update(dt, scene.entities); 
        }
    }

    void Engine::Render() {
        renderingServer.render(&this->scene);
    }

    void Engine::Shutdown() {
        physicsServer.Cleanup(); 
        renderingServer.shutdown();
        displayServer.shutdown();
    }
}