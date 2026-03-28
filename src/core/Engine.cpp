#include "Engine.hpp"
#include <iostream>

#include <Jolt/Core/IssueReporting.h>
#include "Jolt/Core/Memory.h"
#include "core/Input.hpp"
#include "scene/BaseEntity.hpp"
#include "servers/networking/NetworkingServer.hpp"
#include "modules/gltf/AssetLoader.hpp"
// --- THE RHI SWITCH ---
#ifdef __EMSCRIPTEN__
    #include "servers/rendering/webgl/WebRenderer.hpp"
#else
    #include "servers/rendering/RenderingServer.hpp"
#endif

#include "IO/SceneManager.hpp"

namespace Crescendo {

    Engine::Engine() : isRunning(false) {}
    Engine::~Engine() {}

    bool Engine::Initialize(const char* title, int width, int height) {

        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        
        if (!displayServer.initialize(title, width, height)) return false;

        // --- PLATFORM RENDERER INJECTION ---
        #ifdef __EMSCRIPTEN__
                renderer = std::make_unique<WebRenderer>();
                // Note: SceneManager & AssetLoader currently require a RenderingServer*. 
                // We will need to pass nullptr for the web player right now until we decouple them!
                if (!renderer->initialize(&displayServer)) return false;
                sceneManager = std::make_unique<SceneManager>(nullptr);
        #else
                renderer = std::make_unique<RenderingServer>();
                if (!renderer->initialize(&displayServer)) return false;
                sceneManager = std::make_unique<SceneManager>(static_cast<RenderingServer*>(renderer.get()));
        #endif
        
        // Create the Vulkan renderer and hook it up!
        renderer = std::make_unique<RenderingServer>();
        if (!renderer->initialize(&displayServer)) return false;

    
        // Note: If SceneManager expects a RenderingServer*, you may need to cast it: 
        // static_cast<RenderingServer*>(renderer.get())
        sceneManager = std::make_unique<SceneManager>(static_cast<RenderingServer*>(renderer.get()));

        // Start Physics 
        physicsServer.Initialize();
        scene.physics = &physicsServer;

        // Start Audio
        if (audioServer.Initialize()) {
            audioServer.LoadAmbientSound("assets/audio/wind.mp3", 0.5f);
        }

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
       
        // Temporarily cast back to RenderingServer to grab the camera
        auto& cam = static_cast<RenderingServer*>(renderer.get())->mainCamera;
        
        // NOTE: Editor Camera movement was DELETED from here!
        // It is now handled exclusively inside EditorUI::Prepare() so it respects the console.

        // =========================================================
        // STATE TRANSITION LOGIC 
        // =========================================================
        
        // 1. Leaving Editor (Starting the game)
        if (currentState == EngineState::Playing && previousState == EngineState::Editor) {
            std::cout << "[Engine] Play Mode: Saving initial state..." << std::endl;
            
            // --- NEW: INITIALIZE SPATIAL AUDIO FOR THIS RUN ---
            audioServer.ClearSpatialEmitters(); // Wipe any leftovers

            glm::vec3 spawnLocation = cam.GetPosition(); 
            
            for (auto* ent : scene.entities) {
                if (ent) {
                    ent->savedOrigin = ent->origin;
                    ent->savedAngles = ent->angles;
                    ent->savedScale = ent->scale; 
                    
                    // IF THE ENTITY IS A SOUND SOURCE, LOAD IT!
                    // This line should now compile perfectly in Engine.cpp
                    if (ent->className == "env_sound") {
                        audioServer.LoadSpatialEmitter(ent->assetPath, ent->origin, ent->emission);
                    }

                    if (ent->targetName == "SpawnPoint") {
                        spawnLocation = ent->origin + glm::vec3(0, 0, 1.0f); 
                        ent->scale = glm::vec3(0.0f); // Turn spawner invisible
                    }
                }
            }
            
            activePlayer = new FPSController();
            activePlayer->Initialize(&physicsServer, spawnLocation);

            // Spawn Player model
            size_t priorCount = scene.entities.size();
            // load the model directly into the scene
            Crescendo::AssetLoader::loadModel(static_cast<RenderingServer*>(renderer.get()), "assets/systemsymbols/defaultplayer.glb", &scene);

            if (scene.entities.size() > priorCount) {
                localPlayerModel = scene.entities[priorCount];
                localPlayerModel->targetName = "LocalPlayer";

                // Auto flag it for enet
                localPlayerModel->syncTransform = true;
                localPlayerModel->networkID = 1;
            }
        }
        // 2. Returning to Editor (From either Playing OR Paused)
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

            if (activePlayer) {
                delete activePlayer;
                activePlayer = nullptr;
            }

            if (localPlayerModel) {
                scene.DeleteEntity(localPlayerModel->index);
                localPlayerModel = nullptr;
            }
        }
        
        // 3. Handle Mouse Locking & Audio
        if (currentState != previousState) {
            if (currentState == EngineState::Playing) {
                SDL_SetRelativeMouseMode(SDL_TRUE); 
                audioServer.PlayAmbientSound();
                audioServer.PlaySpatialEmitters(); 
            } else {
                SDL_SetRelativeMouseMode(SDL_FALSE); 
                audioServer.StopAmbientSound();    
                audioServer.StopSpatialEmitters(); 
            }
        }
        
        previousState = currentState;

        // =========================================================
        // PLAY MODE: FPS Controller Physics & Network
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

                // ADD &audioServer RIGHT HERE!
                activePlayer->Update(dt, &physicsServer, &audioServer, inputDir, jump);
                cam.SetPosition(activePlayer->GetPosition());

                if (localPlayerModel) {
                    // Offset by -1.0f on Z so the model is at your feet
                    localPlayerModel->origin = activePlayer->GetPosition() - glm::vec3(0.0f, 0.0f, 1.0f);

                    // Rotate the model to face the direction you are looking
                    localPlayerModel->angles = glm::vec3(90.0f, 0.0f, cam.Yaw);
                }
            }

            // Mouse Look ONLY happens here if we are actively playing
            // Add the minus sign to -Input::mouseRelX to fix the inverted left/right panning!
            cam.Rotate((float)-Input::mouseRelX, (float)-Input::mouseRelY);

            physicsServer.Update(dt, scene.entities);

            // =========================================================
            // MULTIPLAYER SYNC LOOP
            // =========================================================

            NetworkingServer* activeServer = nullptr;

            // 1. Find the Network Manager and process incoming movements
            for (auto* ent : scene.entities) {
                if (ent && ent->className == "node_network" && ent->netServer && ent->netServer->IsConnected()) {
                    activeServer = ent->netServer;
                    activeServer->Poll(scene.entities); // Apply incoming data to the scene
                    break;
                }
            }

            // 2. Broadcast local movements out to the server/clients
            if (activeServer) {
                for (auto* ent : scene.entities) {
                    // Broadcast networked entities (like localPlayerModel)
                    if (ent && ent->syncTransform && activeServer->IsServer()) {
                        activeServer->BroadcastTransform(
                            ent->networkID,
                            ent->origin,
                            glm::radians(ent->angles)
                        );
                    }
                }
            }
        }

        // ---3D Audio Listener (spatial)
        // Bind the audioservers ears to our cameras exact position and rotation.
        audioServer.UpdateListener(cam.Position, cam.Front, cam.Up);
    }

    void Engine::Render() {
        renderer->render(&scene, sceneManager.get(), currentState);
    }

    void Engine::Shutdown() {
        std::cout << "[Engine] Commencing Shutdown..." << std::endl;

        if (activePlayer) {
            delete activePlayer;
            activePlayer = nullptr;
        }

        // 1. CLEAR THE SCENE DATA FIRST
        // This ensures all MeshResources and Buffers owned by entities 
        // are dropped while the Vulkan Allocator is still 100% alive.
        sceneManager.reset(); 
        scene.entities.clear(); 

        // 2. Now it is safe to clean up systems
        physicsServer.Cleanup(); 
        renderer->shutdown();
        displayServer.shutdown();
    }
}