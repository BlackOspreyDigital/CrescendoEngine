#include "Engine.hpp"
#include <iostream>
#include <cstdarg> 
#include <Jolt/Core/IssueReporting.h>
#include <SDL2/SDL_image.h> // [FIX] Required for IMG_Init
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

           // --- CHASE CAMERA LOGIC ---
           // Get the Jolt Body Position
           JPH::BodyID carBodyID = renderingServer.activeCar->vehicle->GetVehicleBody()->GetID();
           JPH::Vec3 carPosJ = physicsServer.bodyInterface->GetPosition(carBodyID);
           JPH::Vec3 carVelJ = physicsServer.bodyInterface->GetLinearVelocity(carBodyID);
           
           glm::vec3 carPos = glm::vec3(carPosJ.GetX(), carPosJ.GetY(), carPosJ.GetZ());
           glm::vec3 carVel = glm::vec3(carVelJ.GetX(), carVelJ.GetY(), carVelJ.GetZ());

           // Calculate "Forward" based on velocity (prevents camera snapping when spinning)
           // Fallback to car rotation if not moving
           glm::vec3 camForward;
           if (glm::length(carVel) > 2.0f) {
               camForward = glm::normalize(carVel);
           } else {
               // If stopped, use the actual car mesh rotation
               // (You might need to store the car's forward vector in CarController to make this cleaner)
               camForward = glm::vec3(1, 0, 0); // Temporary default
           }
           // Flatten forward vector to keep camera horizontal
           camForward.z = 0; 
           camForward = glm::normalize(camForward);

           // Desired Camera Position: 6 units behind, 3 units up
           glm::vec3 targetPos = carPos - (camForward * 6.0f) + glm::vec3(0, 3.0f, 0);

           // Smoothly interpolate current camera position to target
           auto& cam = renderingServer.mainCamera;
           glm::vec3 currentPos = cam.GetPosition();
           
           // 5.0f * dt creates a smooth "spring" effect
           float dt = 1.0f / 60.0f; 
           cam.SetPosition(glm::mix(currentPos, targetPos, 5.0f * dt));

           // Always look at the car
           // We look slightly above the car (carPos + 1.0f) so the car isn't dead center
           cam.LookAt(carPos + glm::vec3(0, 1.0f, 0));
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
        
        IMG_Quit(); 
    }
}