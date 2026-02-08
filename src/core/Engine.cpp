#include "Engine.hpp"
#include <iostream>
#include <cstdarg> 
#include <Jolt/Core/IssueReporting.h>
#include <SDL2/SDL.h>
#include "core/ScriptSystem.hpp"

// 1. Define the callback function (The logic that runs when Jolt crashes)
static bool CustomAssertFailed(const char* inExpression, const char* inMessage, const char* inFile, unsigned int inLine) {
    std::cerr << "\n!!! JOLT ASSERTION FAILED !!!\n";
    std::cerr << "File: " << inFile << ":" << inLine << "\n";
    std::cerr << "Expr: " << inExpression << "\n";
    std::cerr << "Msg:  " << (inMessage ? inMessage : "N/A") << "\n";
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
    return true; // Return true to trigger a breakpoint
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

        // 1. Initialize Scripting
        scriptSystem.Initialize();
        scriptSystem.LoadScript("assets/scripts/car_physics.lua");

        // 2. Initialize Physics
        physicsServer.Initialize();

        isRunning = true;
        return true;

        
    }

    void Engine::Run() {
        while (isRunning) {
            ProcessEvents();
            Update();
            Render();
        }
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

        // --- UPDATED LOOP: Use 'this->scene' instead of renderingServer.gameWorld ---
        for (auto* entity : scene.entities) {
            if (entity && entity->hasScript) {
                scriptSystem.RunEntityScript(entity, dt);
            }
        }

        // Update Physics using the new scene list
        physicsServer.Update(dt, scene.entities); 
        // -----------------------------------------------------------------------------
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