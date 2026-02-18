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
        // Manually Hook Jolt here
        JPH::AssertFailed = CustomAssertFailed;

        // 1. Initialize Display (SDL_Init happens here)
        if (!displayServer.initialize(title, width, height)) return false;

        

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
       // 1. Poll Inputs (Keyboard/Mouse)
       Input::Update();
       
       // 2. Game Logic / Car Control
       if (renderingServer.activeCar) {
           float forward = 0.0f;
           float right = 0.0f;
           float brake = 0.0f;
           float handbrake = 0.0f;

           // Simple Arcade Controls
           if (Input::IsKeyDown(SDL_SCANCODE_W)) forward = 1.0f;
           if (Input::IsKeyDown(SDL_SCANCODE_S)) forward = -1.0f; // Jolt handles reverse automatically if forward is negative
           if (Input::IsKeyDown(SDL_SCANCODE_A)) right = -1.0f;   // Left
           if (Input::IsKeyDown(SDL_SCANCODE_D)) right = 1.0f;    // Right
           if (Input::IsKeyDown(SDL_SCANCODE_SPACE)) handbrake = 1.0f;

           renderingServer.activeCar->SetDriverInput(forward, right, brake, handbrake);

           } 
           else {
           // [FIX] ADD THIS: Free Flying Camera Mode
           float dt = 1.0f / 60.0f; // Or calculate real delta time
           
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
           if (Input::IsMouseButtonDown(3)) { // 3 is usually Right Mouse Button in SDL
               // Use Input::mouseRelX / mouseRelY which you are tracking in Input.cpp
               cam.Rotate((float)Input::mouseRelX, (float)-Input::mouseRelY);
           }
       }

       // 3. Physics Step
       float dt = 1.0f / 60.0f; 
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
        
        
    }
}