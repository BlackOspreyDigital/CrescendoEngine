#include "Engine.hpp"
#include "core/ScriptSystem.hpp"
#include <iostream>

#include <Jolt/Core/IssueReporting.h>
#include "Jolt/Core/Memory.h"
#include "core/Input.hpp"
#include "scene/BaseEntity.hpp"
#include "servers/networking/NetworkingServer.hpp"

#ifndef __EMSCRIPTEN__
#include "modules/gltf/AssetLoader.hpp"
#endif

// --- THE RHI SWITCH ---
#ifdef __EMSCRIPTEN__
    #include "servers/rendering/Webgpu/WebGPURenderer.hpp" 
    #include <emscripten.h> // NEW: Required for the Web Main Loop
#else
    #include "servers/rendering/RenderingServer.hpp"
#endif

#include "IO/SceneManager.hpp"
// [INJECTION 1] Swapped BladesUI for CrescendoOS
#include "core/CrescendoOS.hpp"

#include "servers/interface/CanvasRenderer.hpp"

namespace Crescendo {

    Engine::Engine() : isRunning(false) {}
    Engine::~Engine() {
        Shutdown();
    }

    bool Engine::Initialize(const char* title, int width, int height) {

        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();

        scriptSystem = std::make_unique<ScriptSystem>();
        scriptSystem->Initialize();
        
        // --- BOOT CRESCENDO OS SUPERVISOR ---
        this->crescendoOS = std::make_unique<Core::CrescendoOS>();
        this->crescendoOS->Initialize();        
                
        if (!displayServer.initialize(title, width, height)) return false;

        // --- PLATFORM RENDERER INJECTION ---
        #ifdef __EMSCRIPTEN__
                renderer = std::make_unique<WebGPURenderer>(); 
                if (!renderer->initialize(&displayServer)) return false;
                sceneManager = std::make_unique<SceneManager>(nullptr);
        #else
                renderer = std::make_unique<RenderingServer>();
                if (!renderer->initialize(&displayServer)) return false;

                static_cast<RenderingServer*>(renderer.get())->SetOS(this->crescendoOS.get());
                sceneManager = std::make_unique<SceneManager>(static_cast<RenderingServer*>(renderer.get()));
        #endif
        
        // Start Physics 
        physicsServer.Initialize();
        scene.physics = &physicsServer;

        // Start Audio
        if (audioServer.Initialize()) {
            audioServer.LoadAmbientSound("assets/audio/wind.mp3", 0.5f);
        }

        // SCENE BOOTSTRAP
        CBaseEntity* skyEnt = scene.CreateEntity("env_sky");
        skyEnt->targetName = "Procedural Sky";
        skyEnt->angles = glm::vec3(45.0f, -30.0f, 0.0f);
        skyEnt->albedoColor = glm::vec3(0.5f, 0.7f, 1.0f);      
        skyEnt->attenuationColor = glm::vec3(0.0f, 0.0f, 0.0f); 
                
        isRunning = true;
        return true;
    }

    // =========================================================
    // THE MAIN LOOP 
    // =========================================================
    #ifdef __EMSCRIPTEN__
    void WebMainLoopStep(void* arg) {
        Engine* engine = static_cast<Engine*>(arg);
        engine->ProcessEvents();
        engine->Update();
        engine->Render();
    }
    #endif

    void Engine::Run() {
        #ifdef __EMSCRIPTEN__
            emscripten_set_main_loop_arg(WebMainLoopStep, this, 0, true);
        #else
            while (isRunning) {
                ProcessEvents();
                Update();
                Render();
            }
        #endif
    }

    // =========================================================

    void Engine::ProcessEvents() {
        displayServer.poll_events(isRunning);
    }

    void Engine::Update() {
        float dt = 1.0f / 60.0f; 
        Input::Update();

        // ------------------------------------------------------------------
        // 1. CRESCENDO OS SUPERVISOR & BLADES UI UPDATE
        // ------------------------------------------------------------------
        if (this->crescendoOS) {
            // --- MASTER ESCAPE HATCH ---
            // Pressing F1 instantly suspends active workspace/game and returns to 360 Dashboard
            if (Input::GetKeyDown(SDL_SCANCODE_F1)) {
                this->crescendoOS->ExitToOS();

                // Sync mouse capture to OS Dashboard visibility
                if (this->crescendoOS->GetCurrentMode() == Core::MasterMode::OS_Dashboard) {
                    Input::UnlockMouse();
                } else {
                    if (currentState == EngineState::Playing) {
                        Input::LockMouse();
                    }
                }
            }

            // --- DASHBOARD NAVIGATION (When in OS Shell mode) ---
            if (this->crescendoOS->GetCurrentMode() == Core::MasterMode::OS_Dashboard) {
                auto& blades = this->crescendoOS->GetBladesUI();
                
                if (Input::GetKeyDown(SDL_SCANCODE_LEFT)) {
                    blades.MoveLeft();
                }
                if (Input::GetKeyDown(SDL_SCANCODE_RIGHT)) {
                    blades.MoveRight();
                }
                if (Input::GetKeyDown(SDL_SCANCODE_RETURN)) {
                    this->crescendoOS->OnTabSelected(blades.GetActiveIndex());
                }
            }

            // Step the OS state machine and spring-damper physics
            this->crescendoOS->Update(dt);
        }

        auto& cam = static_cast<RenderingServer*>(renderer.get())->mainCamera;
        
        if (currentState == EngineState::Playing && previousState == EngineState::Editor) {
            std::cout << "[Engine] Play Mode: Saving initial state..." << std::endl;
            
            audioServer.ClearSpatialEmitters(); 

            glm::vec3 spawnLocation = cam.GetPosition(); 
            
            for (auto* ent : scene.entities) {
                if (ent) {
                    ent->savedOrigin = ent->origin;
                    ent->savedAngles = ent->angles;
                    ent->savedScale = ent->scale; 
                    
                    if (ent->className == "env_sound") {
                        audioServer.LoadSpatialEmitter(ent->assetPath, ent->origin, ent->emission);
                    }

                    if (ent->targetName == "SpawnPoint") {
                        spawnLocation = ent->origin + glm::dvec3(0.0, 0.0, 1.0);
                        ent->scale = glm::vec3(0.0f); 
                    }
                }
            }
            
            activePlayer = new FPSController();
            activePlayer->Initialize(&physicsServer, spawnLocation);

            size_t priorCount = scene.entities.size();
            
            #ifndef __EMSCRIPTEN__
            // Crescendo::AssetLoader::loadModel(static_cast<RenderingServer*>(renderer.get()), "assets/systemsymbols/defaultplayer.glb", &scene);
            #else
                printf("WebAssembly build: Skipping Vulkan AssetLoader.\n");
            #endif

            if (scene.entities.size() > priorCount) {
                localPlayerModel = scene.entities[priorCount];
                localPlayerModel->targetName = "LocalPlayer";
                localPlayerModel->syncTransform = true;
                localPlayerModel->networkID = 1;
            }
        }
        else if (currentState == EngineState::Editor && previousState != EngineState::Editor) {
            std::cout << "[Engine] Editor Mode: Restoring scene..." << std::endl;
            for (auto* ent : scene.entities) {
                if (ent) {
                    ent->origin = ent->savedOrigin;
                    ent->angles = ent->savedAngles;
                    ent->scale = ent->savedScale; 
                    physicsServer.ResetBody(ent->index, ent->origin, ent->angles);
                }
            }

            if (activePlayer) { delete activePlayer; activePlayer = nullptr; }
            if (localPlayerModel) { scene.DeleteEntity(localPlayerModel->index); localPlayerModel = nullptr; }
        }
        
        if (currentState != previousState) {
            if (currentState == EngineState::Playing) {
                Input::LockMouse();
                audioServer.PlayAmbientSound();
                audioServer.PlaySpatialEmitters(); 
            } else {
                Input::UnlockMouse();
                audioServer.StopAmbientSound();    
                audioServer.StopSpatialEmitters(); 
            }
        }
        
        previousState = currentState;

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

                activePlayer->Update(dt, &physicsServer, &audioServer, inputDir, jump);
                cam.SetPosition(activePlayer->GetPosition());

                if (localPlayerModel) {
                    localPlayerModel->origin = activePlayer->GetPosition() - glm::vec3(0.0f, 0.0f, 1.0f);
                    localPlayerModel->angles = glm::vec3(90.0f, 0.0f, cam.Yaw);
                }
            }

            cam.Rotate((float)-Input::mouseRelX, (float)-Input::mouseRelY);
            physicsServer.Update(dt, scene.entities);

            NetworkingServer* activeServer = nullptr;
            for (auto* ent : scene.entities) {
                if (ent && ent->className == "node_network" && ent->netServer && ent->netServer->IsConnected()) {
                    activeServer = ent->netServer;
                    activeServer->Poll(scene.entities); 
                    break;
                }
            }

            if (activeServer) {
                for (auto* ent : scene.entities) {
                    if (ent && ent->syncTransform && activeServer->IsServer()) {
                        activeServer->BroadcastTransform(ent->networkID, ent->origin, glm::radians(ent->angles));
                    }
                }
            }
        }

        audioServer.UpdateListener(glm::vec3(cam.Position), cam.Front, cam.Up);
    }

    void Engine::Render() {
        renderer->render(&scene, sceneManager.get(), currentState);
    }

    void Engine::Shutdown() {
        static bool hasShutdown = false;
        if (hasShutdown) return;
        hasShutdown = true;
        
        std::cout << "[Engine] Commencing Shutdown..." << std::endl;

        // Ensure mouse is freed so SDL doesn't hold locks during destruction
        Input::UnlockMouse();

        if (activePlayer) { delete activePlayer; activePlayer = nullptr; }
        if (sceneManager) { sceneManager.reset(); }
        scene.Clear(); 

        if (crescendoOS) {
            crescendoOS.reset();
        }
        
        physicsServer.Cleanup(); 

        if (renderer) { 
            renderer->shutdown(); 
            renderer.reset();
        }

        displayServer.shutdown();
    }
}