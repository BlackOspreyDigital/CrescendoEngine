#include "Engine.hpp"
#include <iostream>

#include <Jolt/Core/IssueReporting.h>

#include "Jolt/Core/Memory.h"
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

        JPH::RegisterDefaultAllocator();
        
        JPH::Factory::sInstance = new JPH::Factory();
        
        if (!displayServer.initialize(title, width, height)) return false;
        if (!renderingServer.initialize(&displayServer)) return false;
        
        
        
        // Start Physics 
        physicsServer.Initialize();
        scene.physics = &physicsServer;
        
        
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
        
        // --- EDITOR MODE: Free-Fly Camera ---
        if (currentState == EngineState::Editor) {
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
        
        previousState = currentState;

        // --- PLAY MODE: FPS Controller & Physics ---
        if (currentState == EngineState::Playing) {
            
            if (activePlayer) {
                // 1. Flatten the camera vectors to the XY ground plane
                glm::vec3 forward = glm::normalize(glm::vec3(cam.Front.x, cam.Front.y, 0.0f));
                glm::vec3 right = glm::normalize(glm::vec3(cam.Right.x, cam.Right.y, 0.0f));

                // 2. Build the movement input vector
                glm::vec3 inputDir(0.0f);
                if (Input::IsKeyDown(SDL_SCANCODE_W)) inputDir += forward;
                if (Input::IsKeyDown(SDL_SCANCODE_S)) inputDir -= forward;
                if (Input::IsKeyDown(SDL_SCANCODE_D)) inputDir += right;
                if (Input::IsKeyDown(SDL_SCANCODE_A)) inputDir -= right;

                bool jump = Input::IsKeyDown(SDL_SCANCODE_SPACE);

                // 3. Run the Source Engine math!
                activePlayer->Update(dt, &physicsServer, inputDir, jump);

                // 4. Lock the camera to the player's head
                cam.SetPosition(activePlayer->GetPosition());
            }

            // Mouse Look in Play Mode (Still holding Right Click for now)
            if (Input::IsMouseButtonDown(3)) { 
                cam.Rotate((float)Input::mouseRelX, (float)-Input::mouseRelY);
            }

            // Step the world physics
            physicsServer.Update(dt, scene.entities);
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