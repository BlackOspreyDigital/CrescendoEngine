#include "Engine.hpp"
#include "core/ScriptSystem.hpp"
#include <iostream>

#include <Jolt/Core/IssueReporting.h>
#include "Jolt/Core/Memory.h"
#include "core/Input.hpp"
#include "scene/BaseEntity.hpp"
#include "servers/networking/NetworkingServer.hpp"
#include "IO/SceneManager.hpp"
#include "IO/VirtualFileSystem.hpp"

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



namespace Crescendo {

    Engine::Engine() : isRunning(false) {}
    Engine::~Engine() {
        Shutdown();
    }

    bool Engine::Initialize(const char* title, int width, int height, const std::string& projectPath, bool isTagEditor) {
        this->currentProjectRoot = projectPath;
        this->isTagEditorMode = isTagEditor;
        this->isRunning = true; 

        VirtualFileSystem::Get().SetProjectRoot(projectPath); 

        // [CRITICAL] Jolt Physics allocators MUST be registered before anything else!
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();

        scriptSystem = std::make_unique<ScriptSystem>();
        scriptSystem->Initialize();
        
        if (!displayServer.initialize(title, width, height)) return false;

        #ifdef __EMSCRIPTEN__
            renderer = std::make_unique<WebGPURenderer>(); 
        #else
            renderer = std::make_unique<RenderingServer>();
        #endif

        if (!renderer->initialize(&displayServer)) return false;

        // In Engine::Initialize...
        sceneManager = std::make_unique<SceneManager>(renderer.get());

        // Pass the tag editor configuration polymorphically through the renderer
        renderer->SetTagEditorConfig(this->isTagEditorMode, projectPath);

        // ALWAYS initialize the core servers so memory is safely allocated!
        physicsServer.Initialize();
        scene.physics = &physicsServer;
        audioServer.Initialize();

        // BYPASS THE HEAVY SCENE BOOTSTRAP FOR LEMUR
        if (!isTagEditorMode) {
            audioServer.LoadAmbientSound("assets/audio/wind.mp3", 0.5f);

            CBaseEntity* skyEnt = scene.CreateEntity("env_sky");
            skyEnt->targetName = "Procedural Sky";
            skyEnt->angles = glm::vec3(45.0f, -30.0f, 0.0f);
            skyEnt->albedoColor = glm::vec3(0.5f, 0.7f, 1.0f);      
            skyEnt->attenuationColor = glm::vec3(0.0f, 0.0f, 0.0f); 
        } else {
            std::cout << "[Engine] Tag Editor Mode Active. Scene bypassed." << std::endl;
        }
    
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

    void Engine::ProcessEvents() {
        displayServer.poll_events(isRunning);
    }

    void Engine::Update() {
        float dt = 1.0f / 60.0f; 
        Input::Update();

        // [CRITICAL FIX] Stop the 3D update loop immediately if we are Lemur!
        if (isTagEditorMode) return; 

        // Use the unified Camera getter from IRenderer
        Camera* cam = renderer ? renderer->GetMainCamera() : nullptr;
        if (!cam) return;

        if (currentState == EngineState::Playing && previousState == EngineState::Editor) {
            std::cout << "[Engine] Play Mode: Saving initial state..." << std::endl;
            
            audioServer.ClearSpatialEmitters(); 
            glm::vec3 spawnLocation = cam->GetPosition(); 
            
            for (auto* ent : scene.entities) {
                if (ent) {
                    ent->savedOrigin = ent->origin;
                    ent->savedAngles = ent->savedAngles;
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
            
            // Clean RHI-agnostic AssetLoader invocation
            // Crescendo::AssetLoader::loadModel(renderer.get(), "assets/systemsymbols/defaultplayer.glb", &scene);

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
                glm::vec3 forward = glm::vec3(cam->Front.x, cam->Front.y, 0.0f);
                if (glm::length(forward) > 0.001f) forward = glm::normalize(forward);
                
                glm::vec3 right = glm::vec3(cam->Right.x, cam->Right.y, 0.0f);
                if (glm::length(right) > 0.001f) right = glm::normalize(right);

                glm::vec3 inputDir(0.0f);
                if (Input::IsKeyDown(SDL_SCANCODE_W)) inputDir += forward;
                if (Input::IsKeyDown(SDL_SCANCODE_S)) inputDir -= forward;
                if (Input::IsKeyDown(SDL_SCANCODE_D)) inputDir += right;
                if (Input::IsKeyDown(SDL_SCANCODE_A)) inputDir -= right;

                bool jump = Input::IsKeyDown(SDL_SCANCODE_SPACE);

                activePlayer->Update(dt, &physicsServer, &audioServer, inputDir, jump);
                cam->SetPosition(activePlayer->GetPosition());

                if (localPlayerModel) {
                    localPlayerModel->origin = activePlayer->GetPosition() - glm::vec3(0.0f, 0.0f, 1.0f);
                    localPlayerModel->angles = glm::vec3(90.0f, 0.0f, cam->Yaw);
                }
            }

            cam->Rotate((float)-Input::mouseRelX, (float)-Input::mouseRelY);
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

        audioServer.UpdateListener(glm::vec3(cam->Position), cam->Front, cam->Up);
    }

    void Engine::Render() {
        renderer->render(&scene, sceneManager.get(), currentState);
    }

    void Engine::Shutdown() {
        static bool hasShutdown = false;
        if (hasShutdown) return;
        hasShutdown = true;
        
        std::cout << "[Engine] Commencing Shutdown..." << std::endl;
        Input::UnlockMouse();

        if (activePlayer) { delete activePlayer; activePlayer = nullptr; }
        if (sceneManager) { sceneManager.reset(); }
        scene.Clear(); 

        // ALWAYS CLEANUP PHYSICS TO PREVENT MEMORY LEAKS
        physicsServer.Cleanup(); 

        if (renderer) { 
            renderer->shutdown(); 
            renderer.reset();
        }

        displayServer.shutdown();
    }
}