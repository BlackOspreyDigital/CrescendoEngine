#include "Engine.hpp"
#include <iostream>
#include <cstdarg> 
#include <Jolt/Core/IssueReporting.h>
#include <SDL2/SDL_image.h> // [FIX] Required for IMG_Init

// 1. Jolt Callback
static bool CustomAssertFailed(const char* inExpression, const char* inMessage, const char* inFile, unsigned int inLine) {
    std::cerr << "\n!!! JOLT ASSERTION FAILED !!!\n";
    std::cerr << "File: " << inFile << ":" << inLine << "\n";
    std::cerr << "Expr: " << inExpression << "\n";
    std::cerr << "Msg:  " << (inMessage ? inMessage : "N/A") << "\n";
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" << std::endl;
    return true; 
}

namespace Crescendo {

    Engine::Engine() : isRunning(false) {}
    Engine::~Engine() {}

    bool Engine::Initialize(const char* title, int width, int height) {
        // Manually Hook Jolt here
        JPH::AssertFailed = CustomAssertFailed;

        // 1. Initialize Display (SDL_Init happens here)
        if (!displayServer.initialize(title, width, height)) return false;

        // 2. [FIX] Initialize SDL_Image
        // This must happen AFTER SDL_Init (DisplayServer) but BEFORE RenderingServer tries to load textures.
        int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
        if (!(IMG_Init(imgFlags) & imgFlags)) {
            std::cerr << "[Error] SDL_image could not initialize! IMG_Error: " << IMG_GetError() << std::endl;
            return false;
        }

        // 3. Initialize Rendering
        if (!renderingServer.initialize(&displayServer)) return false;

        // 4. Initialize Physics
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
        Shutdown(); // Shutdown is called automatically when loop ends
    }

    void Engine::ProcessEvents() {
        displayServer.poll_events(isRunning);
    }

    void Engine::Update() {
        float dt = 1.0f / 60.0f; 
        // Note: Make sure renderingServer.gameWorld.entityList matches your new Scene structure
        // If you switched to 'scene.entities', update this line accordingly.
        physicsServer.Update(dt, renderingServer.gameWorld.entityList); 
    }

    void Engine::Render() {
        // [FIX] Pass the address of the scene member
        renderingServer.render(&scene); 
    }

    void Engine::Shutdown() {
        physicsServer.Cleanup(); 
        renderingServer.shutdown();
        displayServer.shutdown();
        
        IMG_Quit(); 
    }
}