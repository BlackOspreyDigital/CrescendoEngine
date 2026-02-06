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

        // --- 1. Player Car Logic (Global Script) ---
        const Uint8* state = SDL_GetKeyboardState(NULL);
        bool w = state[SDL_SCANCODE_W];
        bool s = state[SDL_SCANCODE_S];
        bool a = state[SDL_SCANCODE_A];
        bool d = state[SDL_SCANCODE_D];

        // Note: We assume you still have 'car_physics.lua' defining a global Update()
        scriptSystem.UpdateCar(carController, dt, w, s, a, d);
        carController.SyncVisuals();

        // --- 2. Generic Entity Logic (Per-Entity Scripts) ---
        // Iterate over all entities in the game world
        for (auto* entity : renderingServer.gameWorld.entityList) {
            if (entity && entity->hasScript) {
                // This runs the script attached to THIS specific entity
                scriptSystem.RunEntityScript(entity, dt);
            }
        }

        // --- 3. Physics Simulation ---
        physicsServer.Update(dt, renderingServer.gameWorld.entityList); 
    }

    void Engine::Render() {
        renderingServer.render();
    }

    void Engine::Shutdown() {
        physicsServer.Cleanup(); 
        renderingServer.shutdown();
        displayServer.shutdown();
    }
}