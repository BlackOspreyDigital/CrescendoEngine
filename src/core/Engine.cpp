#include "Engine.hpp"
#include <iostream>

#include <Jolt/Core/IssueReporting.h>

#include "core/Input.hpp"

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
        JPH::AssertFailed = CustomAssertFailed;
        
        // 1. MUST BE FIRST: Create the Window
        if (!displayServer.initialize(title, width, height)) return false;
        
        // 2. MUST BE SECOND: Start Vulkan and the VMA Allocator
        if (!renderingServer.initialize(&displayServer)) return false;
        
        // 3. NOW it is safe to create the floor and load glTF models!
        renderingServer.createDefaultGround(&scene);
        
        // (If you have any AssetLoader::loadModel calls, put them here too!)
        
        // 4. Start Physics 
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
        Shutdown(); 
    }

    void Engine::ProcessEvents() {
        displayServer.poll_events(isRunning);
    }

    void Engine::Update() {
        // 1. Calculate Delta Time (Fixed to 60fps for now, can be made dynamic later)
        float dt = 1.0f / 60.0f; 

        // 2. Poll Inputs (Keyboard/Mouse)
        Input::Update();
       
        // 3. Free Flying Camera Mode
        auto& cam = renderingServer.mainCamera;
        
        // Keyboard Movement (WASD + Q/E for Up/Down)
        float speed = 10.0f * dt;
        if (Input::IsKeyDown(SDL_SCANCODE_LSHIFT)) speed *= 4.0f; // Sprint
 
        if (Input::IsKeyDown(SDL_SCANCODE_W)) cam.Position += cam.Front * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_S)) cam.Position -= cam.Front * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_A)) cam.Position -= cam.Right * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_D)) cam.Position += cam.Right * speed;
        if (Input::IsKeyDown(SDL_SCANCODE_Q)) cam.Position += glm::vec3(0,0,1) * speed; // Up
        if (Input::IsKeyDown(SDL_SCANCODE_E)) cam.Position -= glm::vec3(0,0,1) * speed; // Down
        
        // Mouse Look (Right Click to Rotate)
        if (Input::IsMouseButtonDown(3)) { 
            cam.Rotate((float)Input::mouseRelX, (float)-Input::mouseRelY);
        }

        // 4. Physics Step
        physicsServer.Update(dt, scene.entities);
    }
        
    void Engine::Render() {
        renderingServer.render(&scene); 
    }

    void Engine::Shutdown() {
        physicsServer.Cleanup(); 
        renderingServer.shutdown();
        displayServer.shutdown();
    }
}