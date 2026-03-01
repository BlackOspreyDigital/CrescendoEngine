#include "Engine.hpp"
#include <iostream>

#include <Jolt/Core/IssueReporting.h>

#include "Jolt/Core/Memory.h"
#include "core/Input.hpp"
#include "scene/BaseEntity.hpp"

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

        // =========================================================
        // SCENE BOOTSTRAP
        // =========================================================

        // Spawn Sky Entity
        CBaseEntity* skyEnt = scene.CreateEntity("env_sky");
        skyEnt->targetName = "Procedural Sky";
        skyEnt->angles = glm::vec3(45.0f, -30.0f, 0.0f);
        skyEnt->albedoColor = glm::vec3(0.5f, 0.7f, 1.0f);      // Zenith
        skyEnt->attenuationColor = glm::vec3(0.0f, 0.0f, 0.0f); // Horizon
                
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
        
        // NOTE: Editor Camera movement was DELETED from here!
        // It is now handled exclusively inside EditorUI::Prepare() so it respects the console.

        // =========================================================
        // STATE TRANSITION LOGIC 
        // =========================================================
        
        // 1. Leaving Editor (Starting the game)
        if (currentState == EngineState::Playing && previousState == EngineState::Editor) {
            std::cout << "[Engine] Play Mode: Saving initial state..." << std::endl;
            
            glm::vec3 spawnLocation = cam.GetPosition(); 
            
            for (auto* ent : scene.entities) {
                if (ent) {
                    ent->savedOrigin = ent->origin;
                    ent->savedAngles = ent->angles;
                    ent->savedScale = ent->scale; 
                    
                    if (ent->targetName == "SpawnPoint") {
                        spawnLocation = ent->origin + glm::vec3(0, 0, 1.0f); 
                        ent->scale = glm::vec3(0.0f); // Turn spawner invisible
                    }
                }
            }

            activePlayer = new FPSController();
            activePlayer->Initialize(&physicsServer, spawnLocation);
        } 
        // 2. Returning to Editor (From either Playing OR Paused)
        else if (currentState == EngineState::Editor && previousState != EngineState::Editor) {
            std::cout << "[Engine] Editor Mode: Restoring scene..." << std::endl;
            for (auto* ent : scene.entities) {
                if (ent) {
                    ent->origin = ent->savedOrigin;
                    ent->angles = ent->savedAngles;
                    ent->scale = ent->savedScale; // Restores visibility
                    
                    physicsServer.ResetBody(ent->index, ent->origin, ent->angles);
                }
            }

            if (activePlayer) {
                delete activePlayer;
                activePlayer = nullptr;
            }
        }

        // 3. Handle Mouse Locking
        if (currentState != previousState) {
            if (currentState == EngineState::Playing) {
                SDL_SetRelativeMouseMode(SDL_TRUE); // Locks and hides OS mouse
            } else {
                SDL_SetRelativeMouseMode(SDL_FALSE); // Frees mouse for Menus/Editor
            }
        }
        
        previousState = currentState;

        // =========================================================
        // PLAY MODE: FPS Controller & Physics
        // =========================================================
        if (currentState == EngineState::Playing) {
            
            if (activePlayer) {
                glm::vec3 forward = glm::vec3(cam.Front.x, cam.Front.y, 0.0f);
                if (glm::length(forward) > 0.001f) forward = glm::normalize(forward);
                
                glm::vec3 right = glm::vec3(cam.Right.x, cam.Right.y, 0.0f);
                if (glm::length(right) > 0.001f) right = glm::normalize(right);

                glm::vec3 inputDir(0.0f);
                if (Input::IsKeyDown(SDL_SCANCODE_W)) inputDir += forward;
                if (Input::IsKeyDown(SDL_SCANCODE_S)) inputDir -= forward;
                if (Input::IsKeyDown(SDL_SCANCODE_D)) inputDir += right;
                if (Input::IsKeyDown(SDL_SCANCODE_A)) inputDir -= right;

                bool jump = Input::IsKeyDown(SDL_SCANCODE_SPACE);

                activePlayer->Update(dt, &physicsServer, inputDir, jump);
                cam.SetPosition(activePlayer->GetPosition());
            }

            // Mouse Look ONLY happens here if we are actively playing
            cam.Rotate((float)Input::mouseRelX, (float)-Input::mouseRelY);

            physicsServer.Update(dt, scene.entities);
        }
    }

    void Engine::Render() {
        // Pass the state by reference down to the renderer
        renderingServer.render(&scene, currentState); 
    }

    void Engine::Shutdown() {
        // Clean up player if we closed the engine while in Play mode
        if (activePlayer) {
            delete activePlayer;
            activePlayer = nullptr;
        }

        physicsServer.Cleanup(); 
        renderingServer.shutdown();
        displayServer.shutdown();
    }
}