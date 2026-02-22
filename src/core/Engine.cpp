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
        
        if (!displayServer.initialize(title, width, height)) return false;
        if (!renderingServer.initialize(&displayServer)) return false;
        
        // Create visual ground
        renderingServer.createDefaultGround(&scene);
        
        // Start Physics 
        physicsServer.Initialize();
        
        // Create static ground collision
        // Using a dummy entity ID like 9999 for the ground, placing it at Z = -1.0f
        physicsServer.CreateBox(9999, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(500.0f, 500.0f, 1.0f), false);
        
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
        Input::Update();
       
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

        // --- STATE TRANSITION LOGIC ---
        if (currentState == EngineState::Playing && previousState == EngineState::Editor) {
            std::cout << "[Engine] Play Mode: Saving initial state..." << std::endl;
            for (auto* ent : scene.entities) {
                if (ent) {
                    ent->savedOrigin = ent->origin;
                    ent->savedAngles = ent->angles;
                }
            }
        } 
        else if (currentState == EngineState::Editor && previousState == EngineState::Playing) {
            std::cout << "[Engine] Editor Mode: Restoring scene..." << std::endl;
            for (auto* ent : scene.entities) {
                if (ent) {
                    // Restore visuals
                    ent->origin = ent->savedOrigin;
                    ent->angles = ent->savedAngles;
                    // Restore physics
                    physicsServer.ResetBody(ent->index, ent->origin, ent->angles);
                }
            }
        }
        
        // Update the tracker for the next frame
        previousState = currentState;

        // 4. Physics Step
        if (currentState == EngineState::Playing) {
            physicsServer.Update(dt, scene.entities);

            // Drive the car!
            if (activeVehicle) {
                float forward = 0.0f;
                float right = 0.0f;
                float brake = 0.0f;

                // Simple Keyboard Controls (Arrow Keys)
                if (Input::IsKeyDown(SDL_SCANCODE_UP)) forward = 1.0f;     // Gas
                if (Input::IsKeyDown(SDL_SCANCODE_DOWN)) brake = 1.0f;     // Brake/Reverse
                if (Input::IsKeyDown(SDL_SCANCODE_LEFT)) right = -1.0f;    // Steer Left
                if (Input::IsKeyDown(SDL_SCANCODE_RIGHT)) right = 1.0f;    // Steer Right

                activeVehicle->SetInputs(forward, right, brake, 0.0f);
                
                // Sync the visual wheel models to the Jolt physics!
                activeVehicle->UpdateWheelTransforms(
                    vehicleWheels[0], vehicleWheels[1], 
                    vehicleWheels[2], vehicleWheels[3]
                );
            }
        }
    }
        
    void Engine::Render() {
        // Pass the state by reference down to the renderer
        renderingServer.render(&scene, currentState); 
    }

    void Engine::Shutdown() {
        physicsServer.Cleanup(); 
        renderingServer.shutdown();
        displayServer.shutdown();
    }
}