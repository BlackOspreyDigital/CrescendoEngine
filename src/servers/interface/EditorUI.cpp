#include "IO/SceneSerializer.hpp"

#include "modules/gltf/AssetLoader.hpp"
#include "controllers/RTSController.hpp"
#include "core/Engine.hpp"
#include "servers/audio/AudioServer.hpp"
#include "imgui.h"
#include "scene/BaseEntity.hpp"
#include "scene/components/PointLightComponent.hpp"
#include "scene/components/TransformComponent.hpp"    
#include "scene/components/MeshRendererComponent.hpp" 
#include "modules/voxel/VoxelPlanetComponent.hpp"
#include "modules/voxel/VoxelGenerator.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/networking/NetworkingServer.hpp"
#include "scene/Scene.hpp"
#include "src/IO/SceneManager.hpp"
#include "servers/camera/Camera.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "include/portable-file-dialogs.h" 
#include "IO/SceneSerializer.hpp"

#include <fstream>
#include <streambuf>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <vulkan/vulkan_core.h>

namespace Crescendo {

    std::vector<ConsoleMessage> consoleLog;
    std::unordered_map<std::string, float*> floatConVars;

    void AddLog(const std::string& message, ImVec4 color) {
        consoleLog.push_back({ message, color });
    }

    class ConsoleOutStream : public std::streambuf {
    public:
        ConsoleOutStream(std::streambuf* original) : originalStream(original) {}
    protected:
        virtual int_type overflow(int_type c) override {
            if (c != traits_type::eof()) {
                char ch = static_cast<char>(c);
                if (ch == '\n') {
                    AddLog(buffer, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                    buffer.clear();
                } else {
                    buffer += ch;
                }
                if (originalStream) originalStream->sputc(ch);
            }
            return c;
        }
    private:
        std::string buffer;
        std::streambuf* originalStream;

    };

    // Global pointers to hold our streams
    static std::streambuf* g_OriginalCount = nullptr;
    static ConsoleOutStream* g_ConsoleStream = nullptr;

    // --- HELPER FUNCTIONS ---
    static void check_vk_result(VkResult err) {
        if (err == 0) return;
        std::cerr << "[ImGui] Vulkan Error: VkResult = " << err << std::endl;
        if (err < 0) abort();
    }

    void Console::AddLog(const char* fmt, ...) {
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        va_end(args);
        for (int new_size = Buf.size(); old_size < new_size; old_size++)
            if (Buf[old_size] == '\n') LineOffsets.push_back(old_size + 1);

        if (AutoScroll) ScrollToBottom = true;
    }

    void Console::Draw(const char* title, bool* p_open) {
        ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title, p_open)) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginPopup("Options")) {
            ImGui::Checkbox("Auto-scroll", &AutoScroll);
            if (ImGui::Button("Clear")) Clear();
            ImGui::EndPopup();
        }

        if (ImGui::Button("Options")) ImGui::OpenPopup("Options");
        ImGui::SameLine();
        bool clear = ImGui::Button("Clear");
        if (clear) Clear();
        ImGui::SameLine();
        bool copy = ImGui::Button("Copy");

        ImGui::Separator();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        if (clear) Clear();
        if (copy) ImGui::LogToClipboard();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
        const char* buf = Buf.begin();
        const char* buf_end = Buf.end();

        if (Filter.IsActive()) {
            for (int line_no = 0; line_no < LineOffsets.Size; line_no++) {
                const char* line_start = buf + LineOffsets[line_no];
                const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                if (Filter.PassFilter(line_start, line_end))
                    ImGui::TextUnformatted(line_start, line_end);
            }
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(LineOffsets.Size);
            while (clipper.Step()) {
                for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++) {
                    const char* line_start = buf + LineOffsets[line_no];
                    const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                    ImGui::TextUnformatted(line_start, line_end);
                }
            }
            clipper.End();
        }
        ImGui::PopStyleVar();

        if (ScrollToBottom && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ScrollToBottom = false;
        ImGui::EndChild();
        ImGui::End();
    }

    // --- CONSTRUCTOR / DESTRUCTOR ---
    EditorUI::EditorUI() : rendererRef(nullptr), selectedObjectIndex(-1) {
        mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        mCurrentGizmoMode = ImGuizmo::WORLD;
        activeSynthParams = SoundSynth::GetPresetCannon();
        std::snprintf(synthTargetFilepath, sizeof(synthTargetFilepath), "%s", activeSynthParams.targetFilepath.c_str());
        sequencerNotes = {
            {60, 0, 1}, {60, 1, 1}, {67, 2, 1}, {67, 3, 1},
            {69, 4, 1}, {69, 5, 1}, {67, 6, 1},
            {65, 8, 1}, {65, 9, 1}, {64, 10, 1}, {64, 11, 1},
            {62, 12, 1}, {62, 13, 1}, {60, 14, 1}
        };
        synthWaveformDirty = true;
    }

    EditorUI::~EditorUI() {}

    // Move this to its own respected config file to unclutter editor ui.
    void EditorUI::SetCrescendoEditorStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        ImVec4 ashGreyDark   = ImVec4(0.10f, 0.10f, 0.11f, 1.00f); 
        ImVec4 ashGreyMedium = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
        ImVec4 ashGreyLight  = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
        ImVec4 goldOrange    = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
        ImVec4 goldHover     = ImVec4(1.00f, 0.80f, 0.30f, 1.00f);

        colors[ImGuiCol_WindowBg]             = ashGreyDark;   
        colors[ImGuiCol_ChildBg]              = ashGreyDark;
        colors[ImGuiCol_PopupBg]              = ashGreyDark;
        colors[ImGuiCol_MenuBarBg]            = ashGreyMedium;
        colors[ImGuiCol_TitleBg]              = ashGreyDark;
        colors[ImGuiCol_TitleBgActive]        = ashGreyMedium;
        colors[ImGuiCol_TitleBgCollapsed]     = ashGreyDark;
        colors[ImGuiCol_Header]               = ashGreyMedium;
        colors[ImGuiCol_HeaderHovered]        = goldOrange;
        colors[ImGuiCol_HeaderActive]         = goldOrange;
        colors[ImGuiCol_Text]                 = goldOrange;    
        colors[ImGuiCol_TextSelectedBg]       = ImVec4(1.00f, 0.65f, 0.00f, 0.35f);
        colors[ImGuiCol_FrameBg]              = ashGreyMedium;
        colors[ImGuiCol_FrameBgHovered]       = ashGreyLight;
        colors[ImGuiCol_FrameBgActive]        = ashGreyLight;
        colors[ImGuiCol_Button]               = ashGreyMedium;
        colors[ImGuiCol_ButtonHovered]        = goldHover;
        colors[ImGuiCol_ButtonActive]         = goldOrange;
        colors[ImGuiCol_SliderGrab]           = goldOrange;
        colors[ImGuiCol_SliderGrabActive]     = goldHover;
        colors[ImGuiCol_CheckMark]            = goldOrange;
        colors[ImGuiCol_Tab]                  = ashGreyDark;
        colors[ImGuiCol_TabHovered]           = goldHover;
        colors[ImGuiCol_TabActive]            = ashGreyMedium;
        colors[ImGuiCol_TabUnfocused]         = ashGreyDark;
        colors[ImGuiCol_TabUnfocusedActive]   = ashGreyMedium;
        colors[ImGuiCol_DockingPreview]       = ImVec4(1.00f, 0.65f, 0.00f, 0.70f);
        colors[ImGuiCol_Border]               = ashGreyMedium;
        colors[ImGuiCol_Separator]            = ashGreyMedium;

        style.WindowRounding = 5.0f;
        style.FrameRounding  = 3.0f;
        style.PopupRounding  = 5.0f;
    }

    // --- GUI SHUTDOWN ---
    void EditorUI::Initialize(RenderingServer* renderer, SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t queueFamilyIndex, VkRenderPass renderPass, uint32_t imageCount) {
        this->rendererRef = renderer;
        
        g_OriginalCount = std::cout.rdbuf();
        g_ConsoleStream = new ConsoleOutStream(g_OriginalCount);
        std::cout.rdbuf(g_ConsoleStream);

        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * 11;
        pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui Descriptor Pool");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 
        ImGui::StyleColorsDark();

        SetCrescendoEditorStyle();

        ImGui_ImplSDL2_InitForVulkan(window);
        
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = instance;
        init_info.PhysicalDevice = physicalDevice;
        init_info.Device = device;
        init_info.QueueFamily = queueFamilyIndex;
        init_info.Queue = graphicsQueue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = imguiPool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = imageCount;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = check_vk_result;

        init_info.PipelineInfoMain.RenderPass = renderPass; 
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT; 

        ImGui_ImplVulkan_Init(&init_info);

        // --- LOAD ASSET BROWSER ICONS ---
        // Make sure these files are placed inside your assets/icons/ folder!
    
    }

    void EditorUI::Shutdown(VkDevice device) {
        vkDeviceWaitIdle(device);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown(); 
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        if (imguiPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, imguiPool, nullptr);
            imguiPool = VK_NULL_HANDLE;
        }

        // RESTORE STD::COUT
        if (g_OriginalCount) {
            std::cout.rdbuf(g_OriginalCount);
            delete g_ConsoleStream;
            g_ConsoleStream = nullptr;
            g_OriginalCount = nullptr;
        }
    }
    
    void EditorUI::Prepare(Scene* scene, SceneManager* sceneManager, Camera& camera, VkDescriptorSet viewportDescriptor, EngineState& engineState) {

        // 1. Get the active scene from the manager
        if (!sceneManager) return;
        if (!scene) return;
        
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        ImGuizmo::BeginFrame();

        ImGuiIO& io = ImGui::GetIO();


        // --- LOCK IMGUI MOUSE WHEN PLAYING ---
        if (engineState == EngineState::Playing && !Crescendo::rtsModeGlobal) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }

        
        // --- HOTKEYS & STATE MANAGEMENT ---
        

        // Toggle Console with '~'
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
            showConsole = !showConsole;
            // Auto-pause if we open the console while playing
            if (showConsole && engineState == EngineState::Playing) {
                engineState = EngineState::Paused;
            }
        }

        // Toggle Pause Menu with 'ESC'
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (engineState == EngineState::Playing) {
                engineState = EngineState::Paused;
                showPauseMenu = true;
            } else if (engineState == EngineState::Paused) {
                engineState = EngineState::Playing;
                showPauseMenu = false;
                showConsole = false; // Close console when resuming
            }
        } 

        // --- EDITOR CAMERA INPUT ---
        // ONLY allow camera movement if we are actually in Editor Mode!
        if (engineState == EngineState::Editor) {
            if (!io.WantCaptureKeyboard || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    if (io.MouseWheel != 0.0f) {
                        camera.MovementSpeed *= std::pow(1.25f, io.MouseWheel);
                        camera.MovementSpeed = glm::clamp(camera.MovementSpeed, 1.0f, 10000000.0f);
                    }

                    camera.Rotate(-io.MouseDelta.x, -io.MouseDelta.y); 
                    
                    // CLAMP THE DELTA TIME: Never allow a frame step larger than 50ms
                    float safeDelta = io.DeltaTime;
                    if (safeDelta > 0.05f) safeDelta = 0.05f;
                    float moveSpeed = camera.MovementSpeed * safeDelta; 
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) moveSpeed *= 3.0f; 
                    if (ImGui::IsKeyDown(ImGuiKey_W)) camera.Position += camera.Front * moveSpeed;
                    if (ImGui::IsKeyDown(ImGuiKey_S)) camera.Position -= camera.Front * moveSpeed;
                    if (ImGui::IsKeyDown(ImGuiKey_D)) camera.Position += camera.Right * moveSpeed;
                    if (ImGui::IsKeyDown(ImGuiKey_A)) camera.Position -= camera.Right * moveSpeed;
                    if (ImGui::IsKeyDown(ImGuiKey_Q)) camera.Position += camera.WorldUp * moveSpeed; 
                    if (ImGui::IsKeyDown(ImGuiKey_E)) camera.Position -= camera.WorldUp * moveSpeed; 
                }
            }
        }

        // CTRL + A Add Menu
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_A)) {
            ImGui::OpenPopup("AddEntityPopup");
        }

        if (ImGui::BeginPopup("AddEntityPopup")) {
            ImGui::TextDisabled("Add Entity...");
            ImGui::Separator();
            
            if (ImGui::MenuItem("Empty Prop")) {
                CBaseEntity* ent = scene->CreateEntity("prop_dynamic");
                ent->targetName = "New Prop";
                ent->origin = camera.Position + glm::dvec3(camera.Front * 5.0f);
                
                // Auto-attach core components
                ent->AddComponent<TransformComponent>();
                ent->AddComponent<MeshRendererComponent>();
                selectedObjectIndex = scene->entities.size() - 1; 
            }

            if (ImGui::MenuItem("Voxel Planet")) {
                CBaseEntity* planet = scene->CreateEntity("prop_dynamic");
                planet->targetName = "Voxel Planet";

                planet->AddComponent<TransformComponent>();
                planet->AddComponent<MeshRendererComponent>();
                planet->AddComponent<Crescendo::Voxel::VoxelPlanetComponent>();

                auto planetComp = planet->GetComponent<Crescendo::Voxel::VoxelPlanetComponent>();
                planetComp->settings.radius = 25000.0f;
                planetComp->settings.amplitude = 1500.0f;
                planetComp->settings.frequency = 0.00005f;

                Crescendo::Voxel::ChunkMesh planetMesh = Crescendo::Voxel::VoxelGenerator::GeneratePlanetMesh(planetComp->settings, 64);
                int modelIdx = sceneManager->GetRenderer()->acquireMesh("PROCEDURAL", "VoxelPlanet", planetMesh.vertices, planetMesh.indices);
                planet->modelIndex = modelIdx;

                float safeDistance = planetComp->settings.radius + 5000.0f;
                planet->origin = camera.Position + glm::dvec3(camera.Front * safeDistance);
                planet->albedoColor = glm::vec3(0.3f, 0.85f, 0.4f);
                planet->emission = 0.2f;
                selectedObjectIndex = scene->entities.size() - 1;
            }

            if (ImGui::MenuItem("Point Light")) {
                CBaseEntity* point = scene->CreateEntity("light_point");
                point->targetName = "Point Light";
                point->albedoColor = glm::vec3(1.0f, 0.4f, 0.1f);               // Warm fire orange
                point->emission = 25.0f;  // Intensity
                point->scale.x = 15.0f;   // RADIUS: How far the light reaches!
                selectedObjectIndex = scene->entities.size() - 1;
            }
            
            if (ImGui::MenuItem("Directional Light (Sun)")) {
                CBaseEntity* sun = scene->CreateEntity("light_directional");
                sun->targetName = "Sun Light";
                sun->angles = glm::vec3(45.0f, -30.0f, 0.0f);
                sun->albedoColor = glm::vec3(1.0f, 0.95f, 0.9f); // Warm sunlight
                sun->emission = 5.0f; // Intensity
                selectedObjectIndex = scene->entities.size() - 1;
            }
            
            if (ImGui::MenuItem("Atmosphere (Skybox)")) {
                CBaseEntity* sky = scene->CreateEntity("env_sky");
                sky->targetName = "Sky Environment";
                selectedObjectIndex = scene->entities.size() - 1;
            }

            if (ImGui::MenuItem("Player Spawn Point")) {
                size_t priorCount = scene->entities.size();

                Crescendo::AssetLoader::loadModel(rendererRef, "assets/systemsymbols/playerspawner.glb", scene);

                if (scene->entities.size() > priorCount) {
                    CBaseEntity* spawner = scene->entities[priorCount];

                    spawner->targetName = "SpawnPoint";
                    spawner->origin = camera.Position + glm::dvec3(camera.Front * 5.0f);

                    selectedObjectIndex = priorCount; // Auto-select in inspector
                }
            }

            if (ImGui::MenuItem("Network Manager")) {
                CBaseEntity* netNode = scene->CreateEntity("node_network");
                netNode->targetName = "Network Manager";

                // Set default network values
                netNode->isHost = true;
                netNode->networkPort = 7777;
                netNode->networkIP = "127.0.0.1";

                selectedObjectIndex = scene->entities.size() -1; // Auto-Select
            }
            
            ImGui::EndPopup();
        }

        // 2. DOCKSPACE & MENU
        ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpaceOverViewport(dockSpaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Import Model")) {
                    auto selection = pfd::open_file("Select a file", ".", std::vector<std::string>{ "All Models", "*.obj *.gltf *.glb", "GLTF", "*.gltf *.glb", "OBJ", "*.obj" }).result();
                    
                    if (!selection.empty() && rendererRef) {
                        std::string path = selection[0];
                        Crescendo::AssetLoader::loadModel(rendererRef, path, scene);
                    }
                }

                // Save Project
                if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                    // Use pfd::save_file instead of open_file, and declare the 'destination' string!
                    std::string destination = pfd::save_file("Choose file", ".", std::vector<std::string>{ "JSON Files", "*.json" }).result();
                    
                    if (!destination.empty()) {
                        // Ensure it ends in json
                        if (destination.find(".json") == std::string::npos) destination += ".json";

                        SceneSerializer serializer(scene, rendererRef);
                        serializer.Serialize(destination);
                    }
                }

                // Load Project
                if (ImGui::MenuItem("Load Project", "Ctrl+O")) {
                    auto selection = pfd::open_file("Load Project", ".", std::vector<std::string>{ "JSON Map Files", "*.json" }).result();
                    if (!selection.empty()) {
                        SceneSerializer serializer(scene, rendererRef);
                        serializer.Deserialize(selection[0]);
                    }
                }

                ImGui::Separator();

                // Clear Scene
                if (ImGui::MenuItem("Clear Scene")) {
                    if (scene) scene->Clear();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    SDL_Event quit_event; quit_event.type = SDL_QUIT; SDL_PushEvent(&quit_event);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Deselect All", "Esc")) { selectedObjectIndex = -1; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Engine Settings", NULL, &showSettingsWindow);
                ImGui::MenuItem("Console", NULL, &showConsole);
                ImGui::MenuItem("Audio Synthesizer", NULL, &showAudioSynthWindow);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem("About", NULL, &showAboutWindow);
                ImGui::EndMenu();
            }

            static float fpsTimer = 0.0f;
            static float displayFps = 0.0f;
            
            fpsTimer += io.DeltaTime;
            if (fpsTimer >= 0.1f) { 
                displayFps = io.Framerate;
                fpsTimer = 0.0f;
            }

            // --- RIGHT-ALIGNED UI BLOCK ---
            // Reserve 160 pixels from the right edge so BOTH items fit without wrapping!
            ImGui::SameLine(ImGui::GetWindowWidth() - 160.0f);
            
            // 1. Draw FPS Counter First (Left side of the block)
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "FPS: %.0f", displayFps);
            
            // Separator
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            
            // --- ACTUAL NETWORK STATUS CHECK ---
            // Sweep the scene to see if our node_network is actively hosting/connected
            bool isConnected = false; 
            if (scene) {
                for (auto* ent : scene->entities) {
                    if (ent && ent->className == "node_network" && ent->netServer) {
                        isConnected = ent->netServer->IsConnected();
                        break;
                    }
                }
            }

            // 2. Draw Status Circle (Right side of the block)
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            float circleRadius = 6.0f;

            // Nudge the cursor and center vertically
            cursorPos.x += circleRadius + 2.0f;
            cursorPos.y += ImGui::GetTextLineHeight() * 0.5f;

            ImU32 statusColor = isConnected ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);
            ImGui::GetWindowDrawList()->AddCircleFilled(cursorPos, circleRadius, statusColor);

            // Move ImGui's internal cursor past the circle we just custom-painted
            ImGui::Dummy(ImVec2(circleRadius * 2.0f + 4.0f, 0.0f));
            ImGui::SameLine();
            ImGui::Text(isConnected ? "Online " : "Offline");

            ImGui::EndMainMenuBar();
        }

        // STATE TOOL BAR
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + 25.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.16f, 0.9f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        
        ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        
        if (ImGui::Button(engineState == EngineState::Playing ? "Playing" : "Play")) {
            engineState = EngineState::Playing;
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause")) {
            engineState = EngineState::Paused;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            engineState = EngineState::Editor;
            // Next step: Reset car transforms here
        }

        // (PlayStation Blue button removed)
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        // --- END TOOLBAR CODE ---

        // --- 3. VIEWPORT WINDOW ---
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        ImVec2 viewportPos = ImGui::GetCursorScreenPos();
        this->lastViewportSize = glm::vec2(viewportSize.x, viewportSize.y);

        if (viewportDescriptor != VK_NULL_HANDLE) {
            ImGui::Image((ImTextureID)viewportDescriptor, viewportSize);
        }

        // Scene manager access
        if (ImGui::BeginTabBar("SceneTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {

            // Loop through all currently open scenes
            auto openScenes = sceneManager->GetOpenScenes();
            for (auto& openScene : openScenes) {
            
                bool isOpen = true; 
                ImGuiTabItemFlags flags = (sceneManager->GetActiveScene() == openScene) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            
                if (ImGui::BeginTabItem(openScene->name.c_str(), &isOpen, flags)) {
                    if (sceneManager->GetActiveScene() != openScene) {
                        sceneManager->SetActiveScene(openScene);
                        selectedObjectIndex = -1;
                    }
                    ImGui::EndTabItem();
                }
            
                if (!isOpen) {
                    sceneManager->CloseScene(openScene);
                }
            }
        
            // "+" button at the end to quickly make a new scene
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
                auto newTab = sceneManager->CreateScene("New Prefab");
                sceneManager->SetActiveScene(newTab);
            }
        
            ImGui::EndTabBar();
        }

        // --- NEW SCENE MODAL ---
        /* 

        // Need to refactor this, putting a pin in it for now its not important
        // We will need to modularize this into its own component or module system 
        // Refactor the dynamic rendering before squashing this...
        
        if (showNewSceneModal) {
            ImGui::OpenPopup("Create New Scene");
        }

        if (ImGui::BeginPopupModal("Create New Scene", &showNewSceneModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char sceneNameBuf[128] = "Untitled Scene";
            ImGui::InputText("Scene Name", sceneNameBuf, IM_ARRAYSIZE(sceneNameBuf));

            ImGui::Spacing();

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                auto newScene = sceneManager->CreateScene(sceneNameBuf);

                // Bootstrap with default lighting so we don't spawn into a black void
                CBaseEntity* sky = newScene->CreateEntity("env_sky");
                sky->targetName = "Procedural Sky";
                sky->albedoColor = glm::vec3(0.5f, 0.7f, 1.0f);

                CBaseEntity* sun = newScene->CreateEntity("light_directional");
                sun->targetName = "Sun Light";
                sun->angles = glm::vec3(45.0f, -30.0f, 0.0f);
                sun->albedoColor = glm::vec3(1.0f, 0.95f, 0.9f);
                sun->emission = 5.0f;

                sceneManager->SetActiveScene(newScene);
                selectedObjectIndex = -1; // Deselect the old scene's objects

                showNewSceneModal = false;
                ImGui::CloseCurrentPopup();
                
                // Reset buffer for the next time we open it
                strcpy(sceneNameBuf, "Untitled Scene"); 
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                showNewSceneModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        */

        // --- GIZMOS ---
        // Hide the gizmo entirely if we are playtesting
        if (engineState == EngineState::Editor && viewportSize.x > 0 && viewportSize.y > 0 && scene) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
                
            // 1. Grab your camera's exact RTE View Matrix
            glm::mat4 view = camera.GetViewMatrix();
            float aspect = viewportSize.x / viewportSize.y;
            
            // 2. Feed ImGuizmo a localized projection matrix to prevent 32-bit jitter
            glm::mat4 gizmoProj = glm::perspective(glm::radians(camera.fov), aspect, 1000.0f, 0.1f);
            
            if (selectedObjectIndex >= 0 && selectedObjectIndex < (int)scene->entities.size()) {
                CBaseEntity* ent = scene->entities[selectedObjectIndex];
                
                // 3. Hide the physical gizmo for global entities that don't need a transform
                bool isGlobalEntity = (ent->className == "env_sky" || ent->className == "node_network");

                if (ent && !isGlobalEntity) {
                    
                    // 4. Calculate 64-bit camera-relative position before downcasting
                    glm::vec3 relativePos = glm::vec3(ent->origin - camera.Position);
                    
                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, relativePos);
                    model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0, 0, 1));
                    model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0, 1, 0));
                    model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1, 0, 0));
                    model = glm::scale(model, ent->scale);

                    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProj), 
                                         mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(model));

                    if (ImGuizmo::IsUsing()) {
                        float newTranslation[3], newRotation[3], newScale[3];
                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), newTranslation, newRotation, newScale);

                        // 5. Promote the 32-bit camera-relative translation back up to 64-bit absolute world space
                        ent->origin = camera.Position + glm::dvec3(newTranslation[0], newTranslation[1], newTranslation[2]);
                        ent->angles = glm::make_vec3(newRotation);
                        ent->scale  = glm::make_vec3(newScale);
                    }
                } 
            } 
        }

        // --- ADD THIS: DRAG TARGET ---
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                const char* path = (const char*)payload->Data;
                std::string ext = std::filesystem::path(path).extension().string();
            
                if (ext == ".glb" || ext == ".gltf" || ext == ".obj") {
                    size_t priorCount = scene->entities.size();
                    Crescendo::AssetLoader::loadModel(rendererRef, path, scene);

                    if (scene->entities.size() > priorCount) {
                        selectedObjectIndex = scene->entities.size() - 1;
                        // Place it in front of the camera
                        scene->entities[selectedObjectIndex]->origin = camera.Position + glm::dvec3(camera.Front * 5.0f);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if ((engineState == EngineState::Playing || engineState == EngineState::Paused) && Crescendo::activeRTSControllerGlobal) {
            Crescendo::activeRTSControllerGlobal->DrawViewportOverlays(viewportPos, viewportSize, ImGui::GetWindowDrawList());
        }

        ImGui::End(); // Closes "Viewport"
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if ((engineState == EngineState::Playing || engineState == EngineState::Paused) && Crescendo::activeRTSControllerGlobal) {
            Crescendo::activeRTSControllerGlobal->DrawHUD(viewportPos, viewportSize);
        }

        // --- 4. HIERARCHY & INSPECTOR ---
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::Begin("Scene Hierarchy");
        ImGui::PopStyleColor();

        if (scene) {
            struct EntityGroup {
                std::string name;
                std::vector<size_t> indices;
            };

            std::vector<EntityGroup> groups;
            std::unordered_map<std::string, size_t> groupMap;

            for (size_t i = 0; i < scene->entities.size(); i++) {
                CBaseEntity* ent = scene->entities[i];
                if (!ent) continue;
                std::string key = ent->targetName.empty() ? ("Entity (" + ent->className + ")") : ent->targetName;
                if (groupMap.find(key) == groupMap.end()) {
                    groupMap[key] = groups.size();
                    groups.push_back({ key, { i } });
                } else {
                    groups[groupMap[key]].indices.push_back(i);
                }
            }

            auto DrawEntityItem = [&](size_t i, const std::string& displayLabel) {
                CBaseEntity* ent = scene->entities[i];
                if (!ent) return;
                ImGui::PushID((int)i);
                if (ImGui::Selectable(displayLabel.c_str(), selectedObjectIndex == (int)i)) {
                    selectedObjectIndex = (int)i;
                }

                // --- RIGHT-CLICK CONTEXT MENU ---
                if (ImGui::BeginPopupContextItem()) {
                    selectedObjectIndex = (int)i;
                    ImGui::TextDisabled("%s", displayLabel.c_str());
                    ImGui::Separator();

                    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                        CBaseEntity* orig = scene->entities[i];
                        CBaseEntity* clone = scene->CreateEntity(orig->className);
                        clone->targetName = orig->targetName + " (Copy)";
                        clone->modelIndex = orig->modelIndex;
                        clone->textureID  = orig->textureID;
                        clone->assetPath  = orig->assetPath;
                        clone->origin = orig->origin;
                        clone->angles = orig->angles;
                        clone->scale  = orig->scale;
                        clone->albedoColor = orig->albedoColor;
                        clone->emission    = orig->emission;
                        clone->roughness   = orig->roughness;
                        clone->metallic    = orig->metallic;
                        clone->alpha       = orig->alpha;
                        clone->transmission = orig->transmission;
                        clone->ior         = orig->ior;
                        clone->attenuationColor = orig->attenuationColor;
                        clone->attenuationDistance = orig->attenuationDistance;
                        clone->normalStrength = orig->normalStrength;
                        if (orig->HasComponent<TransformComponent>()) clone->AddComponent<TransformComponent>();
                        if (orig->HasComponent<MeshRendererComponent>()) clone->AddComponent<MeshRendererComponent>();
                        if (orig->HasComponent<PointLightComponent>()) clone->AddComponent<PointLightComponent>();
                        selectedObjectIndex = clone->index; 
                    }
                    
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    if (ImGui::MenuItem("Delete", "Del")) {
                        scene->DeleteEntity(i);
                        selectedObjectIndex = -1; 
                    }
                    ImGui::PopStyleColor();

                    ImGui::EndPopup();
                }

                ImGui::PopID();
            };

            for (const auto& grp : groups) {
                if (grp.indices.size() == 1) {
                    DrawEntityItem(grp.indices[0], grp.name);
                } else {
                    std::string header = grp.name + " (" + std::to_string(grp.indices.size()) + ")";
                    if (ImGui::TreeNode(header.c_str())) {
                        for (size_t idx = 0; idx < grp.indices.size(); ++idx) {
                            size_t entIdx = grp.indices[idx];
                            std::string subLabel = grp.name + " [" + std::to_string(idx) + "]";
                            DrawEntityItem(entIdx, subLabel);
                        }
                        ImGui::TreePop();
                    }
                }
            }
        }
        ImGui::End();

        // --- THE NEW COMPONENT-STYLE INSPECTOR ---
        ImGui::Begin("Inspector");
        
        if (selectedObjectIndex >= 0 && scene && selectedObjectIndex < (int)scene->entities.size()) {
            CBaseEntity* ent = scene->entities[selectedObjectIndex];
            if (ent) {
                // --- 1. ENTITY HEADER ---
                bool active = true; 
                ImGui::Checkbox("##Active", &active);
                ImGui::SameLine();
                
                char nameBuf[256];
                strncpy(nameBuf, ent->targetName.c_str(), sizeof(nameBuf));
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                
                ImGui::PushItemWidth(-1);
                if (ImGui::InputText("##Name", nameBuf, sizeof(nameBuf))) {
                    ent->targetName = nameBuf;
                }
                ImGui::PopItemWidth();

                ImGui::Separator();
                ImGui::Spacing();

                // =========================================================
                // 1. THE NETWORK MANAGER UI
                // =========================================================

                if (ent->className == "node_network") {
                    ImGui::Text("Network Manager Settings");

                    ImGui::Checkbox("Host Server", &ent->isHost);

                    // IP Address requires a char buffer for ImGui
                    char ipBuffer[64];
                    strncpy(ipBuffer, ent->networkIP.c_str(), sizeof(ipBuffer));
                    ipBuffer[sizeof(ipBuffer) -1] = '\0';
                    if (ImGui::InputText("IP Address", ipBuffer, sizeof(ipBuffer))) {
                        ent->networkIP = ipBuffer;
                    }

                    ImGui::InputInt("Port", &ent->networkPort);

                    ImGui::Spacing();

                    // Connection Controls
                    if (ent->netServer == nullptr || !ent->netServer->IsConnected()) {
                        if (ImGui::Button(ent->isHost? "Start Host": "Connected to server", ImVec2(-1, 0))) {
                            if (ent->netServer == nullptr) {
                                ent->netServer = new NetworkingServer();
                            }
                            ent->netServer->Initialize(ent->isHost, ent->networkPort, ent->networkIP);
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Online");
                        if (ImGui::Button("Disconnect", ImVec2(-1, 0))) {
                            ent->netServer->Shutdown();
                            delete ent->netServer;
                            ent->netServer = nullptr;
                        }
                    }
                }

                // =========================================================
                // 2. THE MULTIPLAYER SYNC UI
                // =========================================================
                else {
                    ImGui::Text("Multiplayer Synchronization");
                    ImGui::Checkbox("Sync Transform over Network", &ent->syncTransform);

                    if (ent->syncTransform) {
                        // Cast to int for ImGui, but ensure it stays positive for the uint32_t
                        int tempNetID = static_cast<int>(ent->networkID);
                        
                        if (ImGui::InputInt("Network ID", &tempNetID)) {
                            // YOU ARE MISSING THIS LINE:
                            ent->networkID = static_cast<uint32_t>(std::max(0, tempNetID));
                        }
                        
                        ImGui::TextDisabled("Note: Network ID must match on both clients.");
                    }
                }
                // --- TRANSFORM (Fallback if no TransformComponent attached) ---
                if (!ent->HasComponent<TransformComponent>()) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
                    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
                        glm::vec3 tempOrigin = glm::vec3(ent->origin);
                        if (ImGui::DragFloat3("Position", glm::value_ptr(tempOrigin), 0.1f)) {
                            ent->origin = glm::dvec3(tempOrigin);
                        }
                        ImGui::DragFloat3("Rotation", glm::value_ptr(ent->angles), 1.0f);
                        ImGui::DragFloat3("Scale",    glm::value_ptr(ent->scale), 0.1f);
                        ImGui::Spacing();
                    }
                    ImGui::PopStyleColor();
                }

                // --- MATERIAL & BSDF PROPERTIES (Fallback if no MeshRendererComponent attached) ---
                if (!ent->HasComponent<MeshRendererComponent>()) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
                    if (ImGui::CollapsingHeader("Material (BSDF & PBR)", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
                        ImGui::ColorEdit3("Albedo", glm::value_ptr(ent->albedoColor));
                        ImGui::SliderFloat("Roughness", &ent->roughness, 0.0f, 1.0f);
                        ImGui::SliderFloat("Metallic", &ent->metallic, 0.0f, 1.0f);
                        ImGui::SliderFloat("Emission", &ent->emission, 0.0f, 20.0f);
                        ImGui::SliderFloat("Clearcoat", &ent->clearcoat, 0.0f, 1.0f);
                        ImGui::SliderFloat("Coat Roughness", &ent->clearcoatRoughness, 0.0f, 1.0f);
                        ImGui::SliderFloat("Sheen", &ent->sheen, 0.0f, 1.0f);
                        ImGui::SliderFloat("Specular Weight", &ent->specularWeight, 0.0f, 1.0f);
                        ImGui::SliderFloat("Subsurface", &ent->subsurface, 0.0f, 1.0f);
                        ImGui::SliderFloat("Specular", &ent->specular, 0.0f, 1.0f);
                        ImGui::SliderFloat("Specular Tint", &ent->specularTint, 0.0f, 1.0f);
                        ImGui::SliderFloat("Anisotropic", &ent->anisotropic, 0.0f, 1.0f);

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Transparency, Refraction & Volume");
                        ImGui::SliderFloat("Alpha (Opacity)", &ent->alpha, 0.0f, 1.0f);
                        ImGui::SliderFloat("Transmission (Glass/Energy)", &ent->transmission, 0.0f, 1.0f);

                        if (ent->transmission > 0.0f) {
                            ImGui::Indent();
                            ImGui::ColorEdit3("Volume Tint", glm::value_ptr(ent->attenuationColor));
                            ImGui::DragFloat("Density (Dist)", &ent->attenuationDistance, 0.1f, 0.001f, 100.0f);
                            ImGui::DragFloat("Thickness", &ent->thickness, 0.01f, 0.0f, 10.0f);
                            ImGui::SliderFloat("Refraction (IOR)", &ent->ior, 1.0f, 2.5f);
                            ImGui::Unindent();
                        }

                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::SliderFloat("Normal Strength", &ent->normalStrength, 0.0f, 5.0f);
                        ImGui::Spacing();
                    }
                    ImGui::PopStyleColor();
                }


                // --- 3. DYNAMIC COMPONENTS ---
                for (auto& comp : ent->components) {
                    if (comp->GetName() == "Procedural Planet") continue;
                    
                    ImGui::PushID(comp.get());
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
                    
                    if (ImGui::CollapsingHeader(comp->GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
                        comp->DrawInspectorUI();
                        ImGui::Spacing();
                    }
                    
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                
                
                // Atmosphere & Skybox
                if (ent->className == "env_sky") {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
                    if (ImGui::CollapsingHeader("Atmosphere (Skybox)", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
                        // ... (SkyType Combo Box) ...

                        if (scene->environment.skyType == SkyType::SolidColor) {
                            ImGui::ColorEdit3("Background Color", glm::value_ptr(ent->albedoColor));
                        }
                        else if (scene->environment.skyType == SkyType::Procedural) {
                            ImGui::ColorEdit3("Zenith Color", glm::value_ptr(ent->albedoColor));
                            ImGui::ColorEdit3("Horizon Color", glm::value_ptr(ent->attenuationColor));
                            ImGui::SliderFloat("Sun Intensity", &ent->emission, 0.0f, 10.0f);
                        }
                        else if (scene->environment.skyType == SkyType::HDRMap) {
                            if (ImGui::Button("Load New HDR...")) {
                                auto selection = pfd::open_file("Select HDR", ".", std::vector<std::string>{"HDR Files", "*.hdr"}).result();
                                if (!selection.empty()) {
                                    rendererRef->loadSkybox(selection[0], scene);
                                }
                            }
                        }
                        
                        ImGui::Separator();
                        
                        ImGui::SliderFloat("Ambient HDR Bounce", &scene->environment.ambientIntensity, 0.0f, 2.0f, "%.2f");

                        ImGui::Checkbox("Enable Fog", &scene->environment.enableFog);
                        if (scene->environment.enableFog) {
                            ImGui::ColorEdit4("Color/Density", glm::value_ptr(scene->environment.fogColor));
                            ImGui::SliderFloat("Max Opacity", &scene->environment.fogParams.y, 0.0f, 1.0f);
                            ImGui::SliderFloat("Start Dist", &scene->environment.fogParams.z, 0.0f, 100.0f); 
                            ImGui::SliderFloat("Falloff", &scene->environment.fogParams.x, 0.0f, 1.0f);
                            ImGui::SliderFloat("Height", &scene->environment.fogParams.w, -50.0f, 50.0f);  
                        }
                        ImGui::Spacing();
                    }
                    ImGui::PopStyleColor();
                }
                // --- Voxel Planet Settings ---
                if (ent->HasComponent<Crescendo::Voxel::VoxelPlanetComponent>()) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
                    if (ImGui::CollapsingHeader("Voxel Planet & Atmosphere", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
                        auto planet = ent->GetComponent<Crescendo::Voxel::VoxelPlanetComponent>();
                        ImGui::DragFloat("Radius", &planet->settings.radius, 100.0f, 100.0f, 1000000.0f);
                        ImGui::DragFloat("Amplitude", &planet->settings.amplitude, 10.0f, 0.0f, 10000.0f);
                        ImGui::SliderInt("Octaves", &planet->settings.octaves, 1, 8);

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "Atmosphere Shading");
                        ImGui::DragFloat("Atmo Ceiling", &planet->atmosphereCeiling, 0.001f, 1.001f, 2.0f, "%.3f");
                        ImGui::DragFloat("Atmo Floor", &planet->atmosphereFloor, 10.0f, -5000.0f, 5000.0f);
                        ImGui::SliderFloat("Atmo Intensity", &planet->atmosphereIntensity, 1.0f, 50.0f, "%.1f");
                        ImGui::ColorEdit3("Rayleigh (RGB)", glm::value_ptr(planet->rayleigh), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                        ImGui::DragFloat("Mie (Haze)", &planet->mie, 0.00001f, 0.0f, 0.02f, "%.6f");
                    }
                    ImGui::PopStyleColor();
                }

                // --- 4. LEGACY COMPONENTS (To be refactored) ---
                // --- 4. LEGACY COMPONENTS (To be refactored) ---
                if (ent->className == "env_sound") {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
                    if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
                        char audioBuf[256];
                        strncpy(audioBuf, ent->assetPath.c_str(), sizeof(audioBuf));
                        audioBuf[sizeof(audioBuf) - 1] = '\0';
                        
                        ImGui::TextDisabled("Audio File Path");
                        if (ImGui::InputText("##AudioPath", audioBuf, sizeof(audioBuf))) {
                            ent->assetPath = audioBuf;
                        }
                        ImGui::SliderFloat("Volume", &ent->emission, 0.0f, 5.0f);
                        ImGui::Spacing();
                    }
                    ImGui::PopStyleColor();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // --- 5. ADD COMPONENT MENU ---
                if (ImGui::Button("Add Component", ImVec2(-1, 30))) {
                    ImGui::OpenPopup("AddComponentPopup");
                }

                if (ImGui::BeginPopup("AddComponentPopup")) {
                    ImGui::TextDisabled("Available Components");
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Point Light", nullptr, false, !ent->HasComponent<PointLightComponent>())) {
                        ent->AddComponent<PointLightComponent>();
                    }

                    if (ImGui::MenuItem("Mesh Renderer", nullptr, false, !ent->HasComponent<MeshRendererComponent>())) {
                        ent->AddComponent<MeshRendererComponent>();
                    }
                    
                    if (ImGui::MenuItem("Audio Source", nullptr, false, ent->className != "env_sound")) {
                        ent->className = "env_sound";
                        ent->emission = 1.0f;
                        ent->assetPath = "assets/audio/default.wav";
                    }

                    ImGui::EndPopup();
                }
            } // Closes 'if (ent)'
        } 

        // --- GIZMOS & POST PROCESSING ---
        if (ImGui::CollapsingHeader("Editor Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Selection Outline", &showSelectionOutline);
            ImGui::Separator();
            ImGui::Text("Gizmo Mode");
            if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE)) mCurrentGizmoOperation = ImGuizmo::SCALE;
        }

        if (ImGui::CollapsingHeader("Post Processing")) {
            ImGui::DragFloat("Bloom Strength", &rendererRef->postProcessSettings.bloomStrength, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Bloom Threshold", &rendererRef->postProcessSettings.bloomThreshold, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Exposure", &rendererRef->postProcessSettings.exposure, 0.01f, 0.1f, 5.0f);
            ImGui::DragFloat("Gamma", &rendererRef->postProcessSettings.gamma, 0.01f, 0.1f, 3.0f);
            ImGui::DragFloat("Blur Radius", &rendererRef->postProcessSettings.blurRadius, 0.1f, 0.0f, 10.0f);
        }

        ImGui::End(); // Close Inspector

        //-----------------------------
        //     ASSET BROWSER DRAWER 
        //-----------------------------

        ImGui::Begin("Project");

        // Top bar for navigation
        if (currentAssetDirectory != std::filesystem::path("assets")) {
            if (ImGui::Button("Back")) {
                currentAssetDirectory = currentAssetDirectory.parent_path();
            }
            ImGui::SameLine();
        }
        ImGui::Text("%s", currentAssetDirectory.string().c_str());
        ImGui::Separator();

        // Calculate a responsive grid
        float padding = 16.0f;
        float thumbnailSize = 64.0f;
        float cellSize = thumbnailSize + padding;

        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        if (ImGui::BeginTable("AssetBrowserGrid", columnCount)) {
            for (auto& directoryEntry : std::filesystem::directory_iterator(currentAssetDirectory)) {
                ImGui::TableNextColumn();
                
                const auto& path = directoryEntry.path();
                std::string filenameString = path.filename().string();
                
                ImGui::PushID(filenameString.c_str());

                VkDescriptorSet displayIcon = VK_NULL_HANDLE;
                bool isModel = false;

                // --- 1. DETERMINE THE CORRECT ICON ---
                if (directoryEntry.is_directory()) {
                    displayIcon = icons.folderIcon;
                } else {
                    std::string ext = path.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (ext == ".glb" || ext == ".gltf" || ext == ".obj") {
                        displayIcon = icons.modelIcon; // Or fallback to a generic mesh icon if you make one
                        isModel = true;
                    } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
                        // Use spatial for audio!
                        displayIcon = icons.spatialAudioIcon ? icons.spatialAudioIcon : icons.audioIcon; 
                    } else if (ext == ".frag" || ext == ".vert" || ext == ".spv" || ext == ".glsl") {
                        displayIcon = icons.shaderIcon;
                    } else if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".lua" || ext == ".py") {
                        displayIcon = icons.scriptIcon;
                    } else if (ext == ".json" || ext == ".scene") { 
                        displayIcon = icons.projectFolderIcon;
                    } else {
                        displayIcon = icons.fileIcon; // Generic file fallback
                    }
                }

                // --- 2. DRAW THE THUMBNAIL BUTTON ---
                if (directoryEntry.is_directory()) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent BG
                    
                    if (icons.folderIcon) {
                        if (ImGui::ImageButton("##folder", (ImTextureID)icons.folderIcon, ImVec2(thumbnailSize, thumbnailSize))) {
                            currentAssetDirectory /= path.filename();
                        }
                    } else {
                        if (ImGui::Button("DIR", ImVec2(thumbnailSize, thumbnailSize))) currentAssetDirectory /= path.filename();
                    }
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent BG
                    
                    if (displayIcon) {
                        
                        if (ImGui::ImageButton("##file", (ImTextureID)displayIcon, ImVec2(thumbnailSize, thumbnailSize))) {
                            if (isModel) { 
                                Crescendo::AssetLoader::loadModel(rendererRef, path.string(), scene);
                            }
                        }
                    } else {
                        if (ImGui::Button("FILE", ImVec2(thumbnailSize, thumbnailSize))) {
                            if (isModel) {
                                Crescendo::AssetLoader::loadModel(rendererRef, path.string(), scene);
                            }
                        }
                    }
                    ImGui::PopStyleColor();

                    // --- 3. DRAG AND DROP SOURCE (Files Only) ---
                    if (!directoryEntry.is_directory()) {
                        if (ImGui::BeginDragDropSource()) {
                            std::string pathStr = path.string();
                            ImGui::SetDragDropPayload("ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
                            ImGui::Text("Import %s", filenameString.c_str());
                            ImGui::EndDragDropSource();
                        }
                    }
                } 
                
                ImGui::TextWrapped("%s", filenameString.c_str());
                ImGui::PopID();
            } 
            ImGui::EndTable();
        }

        ImGui::End(); // Close Project Browser

        // --- ENGINE GRAPHICS SETTINGS ---
        if (showSettingsWindow) {
            ImGui::Begin("Engine Settings", &showSettingsWindow);
            
            if (ImGui::CollapsingHeader("Graphics & Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
                
                ImGui::Checkbox("Enable SSAO", &rendererRef->renderSettings.enableSSAO);
                if (rendererRef->renderSettings.enableSSAO) {
                    ImGui::Indent();
                    ImGui::Checkbox("Half-Resolution SSAO", &rendererRef->renderSettings.halfResSSAO);
                    ImGui::Unindent();
                }

                ImGui::Separator();

                ImGui::Checkbox("Enable SSR", &rendererRef->renderSettings.enableSSR);
                if (rendererRef->renderSettings.enableSSR) {
                    ImGui::Indent();
                    ImGui::Checkbox("Half-Resolution SSR", &rendererRef->renderSettings.halfResSSR);
                    ImGui::Unindent();
                }
            }

            // --- ENGINE GRAPHICS SETTINGS ---
            if (ImGui::CollapsingHeader("Engine Graphics")) {
                ImGui::Spacing();

                const char* msaaItems[] = { "Off (1x)", "2x", "4x", "8x" };

                // Use rendererRef instead of renderer
                int currentMSAA = 0;
                if (rendererRef->msaaSamples == VK_SAMPLE_COUNT_2_BIT) currentMSAA = 1;
                if (rendererRef->msaaSamples == VK_SAMPLE_COUNT_4_BIT) currentMSAA = 2;
                if (rendererRef->msaaSamples == VK_SAMPLE_COUNT_8_BIT) currentMSAA = 3;

                if (ImGui::Combo("Anti-Aliasing", &currentMSAA, msaaItems, IM_ARRAYSIZE(msaaItems))) {
                    VkSampleCountFlagBits newMSAA = VK_SAMPLE_COUNT_1_BIT;

                    if (currentMSAA == 0) newMSAA = VK_SAMPLE_COUNT_1_BIT;
                    if (currentMSAA == 1) newMSAA = VK_SAMPLE_COUNT_2_BIT;
                    if (currentMSAA == 2) newMSAA = VK_SAMPLE_COUNT_4_BIT;
                    if (currentMSAA == 3) newMSAA = VK_SAMPLE_COUNT_8_BIT;

                    // Trigger the rebuild using rendererRef!
                    rendererRef->pendingMsaaSamples = newMSAA; 
                    rendererRef->msaaNeedsRebuild = true;
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Changing MSAA recompiles pipelines.");
            }
            ImGui::End();
        }

        // --- ABOUT WINDOW ---
        if (showAboutWindow) {
            ImGui::Begin("About", &showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "Osprey Engine (Bravo Build)");
            ImGui::Separator();
            ImGui::Text("Developed by Yan Nett");
            ImGui::Text("Powered by Vulkan & SDL2");
            ImGui::Spacing();
            ImGui::TextWrapped("A lightweight, high-performance 3D engine built for scalability.");
            ImGui::End();
        }

        // --- DEVELOPER CONSOLE WINDOW (rebuild2)
        if (showConsole) {
            ImGui::SetNextWindowSize(ImVec2(520, 300), ImGuiCond_FirstUseEver);
            
            // Set opacity to 80% (0.0f is invisible, 1.0f is solid)
            ImGui::SetNextWindowBgAlpha(0.8f); 

            ImGui::Begin("Developer Console", &showConsole);

            ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);

            for (const auto& msg : consoleLog) {
                // Check if the message starts with a bracket
                size_t startBracket = msg.text.find('[');
                size_t endBracket = msg.text.find(']');

                if (startBracket == 0 && endBracket != std::string::npos) {
                    // Split the string into "[Tag]" and " The rest of the message"
                    std::string tag = msg.text.substr(0, endBracket + 1);
                    std::string rest = msg.text.substr(endBracket + 1);

                    // Draw the bracketed tag in crimson red
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", tag.c_str());
                    
                    // Tell ImGui to stay on the same line (0 pixel offset)
                    ImGui::SameLine(0.0f, 0.0f);
                    
                    // Draw the rest of the message in the originally assigned color
                    ImGui::TextColored(msg.color, "%s", rest.c_str());
                } else {
                    // If there are no brackets, just draw the whole line normally
                    ImGui::TextColored(msg.color, "%s", msg.text.c_str());
                }
            }

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            ImGui::Separator();

            static char inputBuf[256] = "";
            bool reclaim_focus = false;

            if (ImGui::InputText("##Input", inputBuf, IM_ARRAYSIZE(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                std::string command = inputBuf;
                if (!command.empty()) {
                    // Echo the command in white
                    AddLog("] " + command, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

                    // --- COMMAND PARSING LOGIC ---
                    size_t spacePos = command.find(" ");
                    
                    if (spacePos != std::string::npos) {
                        // We have a space, meaning they are trying to SET a value
                        std::string cmdName = command.substr(0, spacePos);
                        std::string cmdArg = command.substr(spacePos + 1);

                        if (floatConVars.find(cmdName) != floatConVars.end()) {
                            try {
                                float newVal = std::stof(cmdArg);
                                *floatConVars[cmdName] = newVal; // Update the actual game variable
                                AddLog(cmdName + " set to " + std::to_string(newVal), ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                            } catch (...) {
                                AddLog("Usage: " + cmdName + " <float>", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                            }
                        } else {
                            AddLog("Unknown command: " + cmdName, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                        }
                    } else {
                        // No space, meaning they are checking a value or running a single command
                        if (command == "clear") {
                            consoleLog.clear();
                        } else if (floatConVars.find(command) != floatConVars.end()) {
                            // Print current value
                            AddLog(command + " = " + std::to_string(*floatConVars[command]), ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                        } else {
                            AddLog("Unknown command: " + command, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                        }
                    }

                    inputBuf[0] = '\0'; // Clear Input
                    reclaim_focus = true;
                }
            }
            
            ImGui::SetItemDefaultFocus();
            if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1);

            ImGui::End(); // Close Developer Console
        } // Closes if (showConsole)
        if (showAudioSynthWindow) {
            DrawAudioSynthesizerWindow();
        }

        ImGui::Render(); 
    } // Closes EditorUI::Prepare()
    
    void EditorUI::Render(VkCommandBuffer cmd) {
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (draw_data) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
        }
    }

    void EditorUI::HandleInput(SDL_Event& event) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    void EditorUI::DrawAudioSynthesizerWindow() {
        ImGuiViewport* audioViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(audioViewport->Pos.x + audioViewport->Size.x * 0.5f,
                   audioViewport->Pos.y + audioViewport->Size.y * 0.5f),
            ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(880, 680), ImGuiCond_FirstUseEver);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.015f, 0.015f, 0.02f, 0.97f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.025f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.035f, 0.035f, 0.045f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.01f, 0.01f, 0.015f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.06f, 0.06f, 0.07f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.28f, 0.28f, 0.32f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.095f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.20f, 0.20f, 0.23f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.095f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.29f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.08f, 0.08f, 0.095f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.29f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.72f, 0.72f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, ImVec4(0.95f, 0.95f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.82f, 0.82f, 0.88f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.68f, 0.68f, 0.74f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.95f, 0.95f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.65f, 0.65f, 0.72f, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.91f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.52f, 0.52f, 0.56f, 1.0f));

        if (!ImGui::Begin("Audio Synthesizer & Sound Lab", &showAudioSynthWindow)) {
            ImGui::End();
            ImGui::PopStyleColor(26);
            ImGui::PopStyleVar(4);
            return;
        }

        const ImVec4 audioAccent(0.82f, 0.82f, 0.88f, 1.0f);
        ImDrawList* audioDrawList = ImGui::GetWindowDrawList();
        ImVec2 audioPanelMin = ImGui::GetCursorScreenPos();
        ImVec2 audioPanelMax(audioPanelMin.x + ImGui::GetContentRegionAvail().x,
                             audioPanelMin.y + ImGui::GetContentRegionAvail().y);
        audioDrawList->AddRectFilledMultiColor(
            audioPanelMin,
            audioPanelMax,
            IM_COL32(28, 28, 32, 245),
            IM_COL32(28, 28, 32, 245),
            IM_COL32(5, 5, 7, 250),
            IM_COL32(5, 5, 7, 250));
        audioDrawList->AddRect(
            audioPanelMin,
            audioPanelMax,
            IM_COL32(110, 110, 120, 150),
            8.0f,
            0,
            1.0f);
        audioDrawList->AddRectFilledMultiColor(
            audioPanelMin,
            ImVec2(audioPanelMax.x, audioPanelMin.y + 46.0f),
            IM_COL32(255, 255, 255, 34),
            IM_COL32(255, 255, 255, 34),
            IM_COL32(255, 255, 255, 0),
            IM_COL32(255, 255, 255, 0));
        ImGui::BeginChild("SynthControls", ImVec2(0.0f, -238.0f), false);

        auto MidiToFrequency = [](int midiNote) {
            return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
        };

        auto PlaySequencerLoop = [&]() {
            constexpr size_t sequencerStepCount = 16;
            if (!AudioServer::Get()) return;

            constexpr uint32_t sampleRate = 44100;
            float stepDuration = 60.0f / static_cast<float>(std::max(1, sequencerBpm)) / 4.0f;
            size_t stepSamples = std::max<size_t>(1, static_cast<size_t>(stepDuration * sampleRate));
            std::vector<int16_t> sequence(stepSamples * sequencerStepCount, 0);

            for (const SequencerNote& note : sequencerNotes) {
                if (note.midiNote < 0 || note.startStep < 0 || note.startStep >= static_cast<int>(sequencerStepCount)) continue;

                SynthSoundParams noteParams = activeSynthParams;
                noteParams.baseFreq = MidiToFrequency(note.midiNote);
                noteParams.targetFreq = noteParams.baseFreq;
                noteParams.pitchSlideDuration = 0.0f;
                noteParams.pitchJumpEnabled = false;
                noteParams.vibratoRate = 0.0f;
                noteParams.vibratoDepth = 0.0f;
                noteParams.vibratoDelay = 0.0f;

                float noteDuration = stepDuration * static_cast<float>(std::max(1, note.lengthSteps));
                float minimumSustain = noteDuration - noteParams.attackTime - noteParams.decayTime - noteParams.releaseTime;
                noteParams.sustainTime = std::max(noteParams.sustainTime, std::max(0.0f, minimumSustain));

                std::vector<int16_t> notePcm = SoundSynth::GeneratePCM(noteParams, sampleRate);
                size_t destinationOffset = static_cast<size_t>(note.startStep) * stepSamples;
                size_t copyCount = std::min(notePcm.size(), sequence.size() - destinationOffset);
                for (size_t i = 0; i < copyCount; ++i) {
                    int mixed = static_cast<int>(sequence[destinationOffset + i]) + static_cast<int>(notePcm[i]);
                    sequence[destinationOffset + i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
                }
            }

            AudioServer::Get()->PlaySequencerPcm(sequence, sampleRate, activeSynthParams.masterVolume);
        };

        if (sequencerPlaying) {
            constexpr float stepsPerBeat = 4.0f;
            float stepDuration = 60.0f / static_cast<float>(std::max(1, sequencerBpm)) / stepsPerBeat;
            float loopDuration = stepDuration * 16.0f;
            sequencerStepTimer += ImGui::GetIO().DeltaTime;
            while (sequencerStepTimer >= loopDuration) {
                sequencerStepTimer -= loopDuration;
                PlaySequencerLoop();
            }
            sequencerCurrentStep = std::min(
                static_cast<int>(sequencerStepTimer / stepDuration),
                15);
        }

        // Cache / update downsampled waveform for real-time visualization
        if (synthWaveformDirty || cachedWaveformPlot.empty()) {
            auto floatBuf = SoundSynth::GenerateFloat(activeSynthParams, 44100);
            cachedWaveformPlot.resize(512);
            if (floatBuf.empty()) {
                std::fill(cachedWaveformPlot.begin(), cachedWaveformPlot.end(), 0.0f);
            } else {
                float step = (float)floatBuf.size() / 512.0f;
                for (size_t i = 0; i < 512; i++) {
                    size_t idx = (size_t)(i * step);
                    if (idx >= floatBuf.size()) idx = floatBuf.size() - 1;
                    cachedWaveformPlot[i] = floatBuf[idx];
                }
            }
            synthWaveformDirty = false;
        }

        auto DirtySliderFloat = [&](const char* label, float* v, float v_min, float v_max, const char* format = "%.3f") {
            if (ImGui::SliderFloat(label, v, v_min, v_max, format)) {
                synthWaveformDirty = true;
            }
        };

        auto DirtySliderInt = [&](const char* label, int* v, int v_min, int v_max) {
            if (ImGui::SliderInt(label, v, v_min, v_max)) {
                synthWaveformDirty = true;
            }
        };

        auto DirtyCheckbox = [&](const char* label, bool* v) {
            if (ImGui::Checkbox(label, v)) {
                synthWaveformDirty = true;
            }
        };

        // --- TOP TOOLBAR ---
        std::vector<std::string> presetNames = SoundSynth::GetPresetNames();
        presetNames.push_back("Custom / User");

        if (ImGui::BeginCombo("Sound Preset", presetNames[selectedSynthPresetIndex < (int)presetNames.size() ? selectedSynthPresetIndex : 0].c_str())) {
            for (int i = 0; i < (int)presetNames.size(); i++) {
                bool isSelected = (selectedSynthPresetIndex == i);
                if (ImGui::Selectable(presetNames[i].c_str(), isSelected)) {
                    selectedSynthPresetIndex = i;
                    if (i < (int)presetNames.size() - 1) {
                        activeSynthParams = SoundSynth::GetPresetByName(presetNames[i]);
                        std::snprintf(synthTargetFilepath, sizeof(synthTargetFilepath), "%s", activeSynthParams.targetFilepath.c_str());
                        synthStatusMessage = "Loaded preset: " + activeSynthParams.name;
                    } else {
                        activeSynthParams.name = "Custom Sound";
                        synthStatusMessage = "Custom preset mode";
                    }
                    synthWaveformDirty = true;
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        // Action Buttons Row
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.78f, 0.35f, 1.0f));
        if (ImGui::Button(" > Play / Audition ", ImVec2(160, 32))) {
            if (AudioServer::Get()) {
                AudioServer::Get()->PlaySynthSound(activeSynthParams);
            }
            synthStatusMessage = "Auditioning: " + activeSynthParams.name;
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button("Save to File", ImVec2(120, 32))) {
            activeSynthParams.targetFilepath = synthTargetFilepath;
            auto pcm = SoundSynth::GeneratePCM(activeSynthParams);
            if (SoundSynth::WriteWavFile(activeSynthParams.targetFilepath, pcm)) {
                synthStatusMessage = "Saved sound to: " + activeSynthParams.targetFilepath;
            } else {
                synthStatusMessage = "Failed to write: " + activeSynthParams.targetFilepath;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Regenerate All Sounds", ImVec2(180, 32))) {
            AudioServer::GenerateProceduralSounds(true);
            synthStatusMessage = "Regenerated all 11 procedural engine sounds in assets/audio/!";
        }

        ImGui::SameLine();
        if (ImGui::Button("Mutate", ImVec2(90, 32))) {
            SoundSynth::MutateParams(activeSynthParams, 0.25f);
            synthWaveformDirty = true;
            synthStatusMessage = "Mutated sound parameters";
        }

        ImGui::SameLine();
        if (ImGui::Button("Randomize", ImVec2(100, 32))) {
            activeSynthParams = SoundSynth::GenerateRandom(activeSynthParams.waveform);
            std::snprintf(synthTargetFilepath, sizeof(synthTargetFilepath), "%s", activeSynthParams.targetFilepath.c_str());
            synthWaveformDirty = true;
            synthStatusMessage = "Generated random procedural sound";
        }

        ImGui::Spacing();

        // --- WAVEFORM & TELEMETRY ---
        ImGui::PlotLines("##WaveformPlot", cachedWaveformPlot.data(), (int)cachedWaveformPlot.size(), 0, nullptr, -1.05f, 1.05f, ImVec2(-1, 90));

        float peak = 0.0f;
        for (float s : cachedWaveformPlot) peak = std::max(peak, std::abs(s));
        float peakDb = (peak > 0.00001f) ? 20.0f * std::log10(peak) : -96.0f;

        ImGui::Text("Duration: %.3f s (%d samples) | Peak: %.1f dBFS | File: %s",
            activeSynthParams.GetTotalDuration(),
            (int)(activeSynthParams.GetTotalDuration() * 44100.0f),
            peakDb,
            synthTargetFilepath);

        if (!synthStatusMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "  [%s]", synthStatusMessage.c_str());
        }

        ImGui::Separator();
        ImGui::Spacing();

        // --- TAB BAR ---
        if (ImGui::BeginTabBar("SynthTabs")) {

            // TAB 1: OSCILLATOR & NOISE
            if (ImGui::BeginTabItem("Oscillator & Noise")) {
                ImGui::Spacing();

                const char* waveItems[] = { "Sine", "Triangle", "Sawtooth", "Square", "White Noise", "Pink Noise", "Brown Noise", "FM (Chirp)" };
                int currentWave = (int)activeSynthParams.waveform;
                if (ImGui::Combo("Primary Waveform", &currentWave, waveItems, IM_ARRAYSIZE(waveItems))) {
                    activeSynthParams.waveform = (WaveformType)currentWave;
                    synthWaveformDirty = true;
                }

                DirtySliderFloat("Base Frequency (Hz)", &activeSynthParams.baseFreq, 20.0f, 8000.0f, "%.1f Hz");
                if (activeSynthParams.waveform == WaveformType::Square) {
                    DirtySliderFloat("Pulse Width (PWM)", &activeSynthParams.pulseWidth, 0.05f, 0.95f);
                }
                DirtySliderFloat("Oscillator Mix", &activeSynthParams.oscMix, 0.0f, 1.0f);

                ImGui::Separator();
                ImGui::TextColored(audioAccent, "Sub-Oscillator Layer");
                DirtyCheckbox("Enable Sub-Oscillator", &activeSynthParams.subOscEnabled);
                if (activeSynthParams.subOscEnabled) {
                    const char* subWaves[] = { "Sine", "Triangle", "Sawtooth", "Square" };
                    int currentSub = (int)activeSynthParams.subWaveform;
                    if (currentSub > 3) currentSub = 0;
                    if (ImGui::Combo("Sub Waveform", &currentSub, subWaves, IM_ARRAYSIZE(subWaves))) {
                        activeSynthParams.subWaveform = (WaveformType)currentSub;
                        synthWaveformDirty = true;
                    }
                    const char* octaves[] = { "-2 Octaves", "-1 Octave", "Same (0)", "+1 Octave" };
                    int octIdx = activeSynthParams.subOctave + 2;
                    if (octIdx < 0 || octIdx > 3) octIdx = 1;
                    if (ImGui::Combo("Sub Octave", &octIdx, octaves, IM_ARRAYSIZE(octaves))) {
                        activeSynthParams.subOctave = octIdx - 2;
                        synthWaveformDirty = true;
                    }
                    DirtySliderFloat("Sub Mix", &activeSynthParams.subMix, 0.0f, 1.0f);
                }

                ImGui::Separator();
                ImGui::TextColored(audioAccent, "Noise Layer");
                DirtyCheckbox("Enable Noise Layer", &activeSynthParams.noiseEnabled);
                if (activeSynthParams.noiseEnabled) {
                    const char* noiseTypes[] = { "White (Flat)", "Pink (1/f)", "Brown (1/f^2)" };
                    int currentNoise = (int)activeSynthParams.noiseType;
                    if (ImGui::Combo("Noise Type", &currentNoise, noiseTypes, IM_ARRAYSIZE(noiseTypes))) {
                        activeSynthParams.noiseType = (NoiseType)currentNoise;
                        synthWaveformDirty = true;
                    }
                    DirtySliderFloat("Noise Mix", &activeSynthParams.noiseMix, 0.0f, 1.0f);
                    DirtySliderFloat("Noise Decay Rate", &activeSynthParams.noiseDecay, 0.1f, 5.0f);
                }

                ImGui::Separator();
                ImGui::TextColored(audioAccent, "Frequency Modulation (FM)");
                DirtyCheckbox("Enable FM", &activeSynthParams.fmEnabled);
                if (activeSynthParams.fmEnabled) {
                    DirtySliderFloat("Modulator Frequency Ratio", &activeSynthParams.fmRatio, 0.1f, 10.0f, "%.2fx");
                    DirtySliderFloat("Modulation Index / Depth", &activeSynthParams.fmDepth, 0.0f, 10.0f);
                }

                ImGui::EndTabItem();
            }

            // TAB 2: PITCH & GLIDE
            if (ImGui::BeginTabItem("Pitch & Glide")) {
                ImGui::Spacing();

                DirtySliderFloat("Target / End Frequency", &activeSynthParams.targetFreq, 20.0f, 8000.0f, "%.1f Hz");
                DirtySliderFloat("Pitch Slide Duration (0 = Full)", &activeSynthParams.pitchSlideDuration, 0.0f, 2.0f, "%.3f s");

                const char* curves[] = { "Linear", "Exponential", "S-Curve" };
                int currentCurve = (int)activeSynthParams.pitchSlideCurve;
                if (ImGui::Combo("Slide Curve", &currentCurve, curves, IM_ARRAYSIZE(curves))) {
                    activeSynthParams.pitchSlideCurve = (PitchSlideType)currentCurve;
                    synthWaveformDirty = true;
                }

                ImGui::Separator();
                ImGui::TextColored(audioAccent, "Pitch Jump (2-Stage Attack)");
                DirtyCheckbox("Enable Pitch Jump", &activeSynthParams.pitchJumpEnabled);
                if (activeSynthParams.pitchJumpEnabled) {
                    DirtySliderFloat("Jump Trigger Time", &activeSynthParams.pitchJumpTime, 0.001f, 0.5f, "%.3f s");
                    DirtySliderFloat("Jump Amount (Hz)", &activeSynthParams.pitchJumpAmount, -2000.0f, 2000.0f, "%.1f Hz");
                }

                ImGui::Separator();
                ImGui::TextColored(audioAccent, "Vibrato (Pitch LFO)");
                DirtySliderFloat("Vibrato Rate (Speed)", &activeSynthParams.vibratoRate, 0.0f, 40.0f, "%.1f Hz");
                DirtySliderFloat("Vibrato Depth", &activeSynthParams.vibratoDepth, 0.0f, 100.0f, "%.1f Hz");
                DirtySliderFloat("Vibrato Delay", &activeSynthParams.vibratoDelay, 0.0f, 1.0f, "%.3f s");

                ImGui::EndTabItem();
            }

            // TAB 3: AMPLITUDE ENVELOPE (ADSR)
            if (ImGui::BeginTabItem("Envelope (ADSR)")) {
                ImGui::Spacing();

                DirtySliderFloat("Attack Time", &activeSynthParams.attackTime, 0.001f, 1.0f, "%.3f s");
                DirtySliderFloat("Decay Time", &activeSynthParams.decayTime, 0.001f, 2.0f, "%.3f s");
                DirtySliderFloat("Sustain Level", &activeSynthParams.sustainLevel, 0.0f, 1.0f);
                DirtySliderFloat("Sustain Time", &activeSynthParams.sustainTime, 0.0f, 3.0f, "%.3f s");
                DirtySliderFloat("Release Time", &activeSynthParams.releaseTime, 0.001f, 2.0f, "%.3f s");
                DirtySliderFloat("Transient Attack Punch Boost", &activeSynthParams.punch, 0.0f, 1.0f);

                ImGui::Separator();
                DirtySliderFloat("Master Volume Gain", &activeSynthParams.masterVolume, 0.0f, 2.0f);
                DirtyCheckbox("Enable Soft Limiter / Clipper", &activeSynthParams.limiterEnabled);

                ImGui::EndTabItem();
            }

            // TAB 4: EFFECTS RACK
            if (ImGui::BeginTabItem("Effects Rack")) {
                ImGui::Spacing();
                ImGui::TextColored(audioAccent, "DSP Signal Chain: [Oscillators] -> [Filter] -> [Distortion] -> [Bitcrusher] -> [Delay] -> [Reverb] -> [Limiter]");
                ImGui::Separator();

                // 1. BIQUAD FILTER
                if (ImGui::CollapsingHeader("Biquad Filter", ImGuiTreeNodeFlags_DefaultOpen)) {
                    DirtyCheckbox("Enable Filter", &activeSynthParams.filterEnabled);
                    if (activeSynthParams.filterEnabled) {
                        const char* filterTypes[] = { "None", "LowPass", "HighPass", "BandPass", "Notch" };
                        int currentFilter = (int)activeSynthParams.filterType;
                        if (ImGui::Combo("Filter Type", &currentFilter, filterTypes, IM_ARRAYSIZE(filterTypes))) {
                            activeSynthParams.filterType = (FilterType)currentFilter;
                            synthWaveformDirty = true;
                        }
                        DirtySliderFloat("Cutoff Frequency (Hz)", &activeSynthParams.filterCutoff, 20.0f, 18000.0f, "%.1f Hz");
                        DirtySliderFloat("Resonance (Q)", &activeSynthParams.filterResonance, 0.1f, 10.0f);
                        DirtySliderFloat("Cutoff Sweep (+/- Hz)", &activeSynthParams.filterCutoffSweep, -5000.0f, 5000.0f, "%.1f Hz");
                    }
                }

                // 2. DISTORTION / SATURATION
                if (ImGui::CollapsingHeader("Distortion & Saturation", ImGuiTreeNodeFlags_DefaultOpen)) {
                    DirtyCheckbox("Enable Distortion", &activeSynthParams.distortionEnabled);
                    if (activeSynthParams.distortionEnabled) {
                        const char* distTypes[] = { "Soft Tanh (Tape Saturation)", "Hard Clip (Fuzz)", "Foldback (Wavefolder)" };
                        int currentDist = (int)activeSynthParams.distortionType;
                        if (ImGui::Combo("Distortion Type", &currentDist, distTypes, IM_ARRAYSIZE(distTypes))) {
                            activeSynthParams.distortionType = (DistortionType)currentDist;
                            synthWaveformDirty = true;
                        }
                        DirtySliderFloat("Drive", &activeSynthParams.distortionDrive, 1.0f, 20.0f, "%.1fx");
                        DirtySliderFloat("Distortion Wet Mix", &activeSynthParams.distortionMix, 0.0f, 1.0f);
                    }
                }

                // 3. BITCRUSHER
                if (ImGui::CollapsingHeader("Bitcrusher (Lo-Fi)")) {
                    DirtyCheckbox("Enable Bitcrusher", &activeSynthParams.bitcrushEnabled);
                    if (activeSynthParams.bitcrushEnabled) {
                        DirtySliderInt("Bit Depth", &activeSynthParams.bitcrushBits, 2, 16);
                        DirtySliderInt("Downsample Factor", &activeSynthParams.bitcrushDownsample, 1, 32);
                        DirtySliderFloat("Bitcrusher Wet Mix", &activeSynthParams.bitcrushMix, 0.0f, 1.0f);
                    }
                }

                // 4. DELAY / ECHO
                if (ImGui::CollapsingHeader("Delay & Echo")) {
                    DirtyCheckbox("Enable Delay", &activeSynthParams.delayEnabled);
                    if (activeSynthParams.delayEnabled) {
                        DirtySliderFloat("Delay Time (ms)", &activeSynthParams.delayTimeMs, 10.0f, 1000.0f, "%.1f ms");
                        DirtySliderFloat("Feedback", &activeSynthParams.delayFeedback, 0.0f, 0.95f);
                        DirtySliderFloat("High Frequency Damping", &activeSynthParams.delayDamping, 0.0f, 0.95f);
                        DirtySliderFloat("Delay Wet Mix", &activeSynthParams.delayMix, 0.0f, 1.0f);
                    }
                }

                // 5. REVERB / SPACE
                if (ImGui::CollapsingHeader("Reverb & Space")) {
                    DirtyCheckbox("Enable Reverb", &activeSynthParams.reverbEnabled);
                    if (activeSynthParams.reverbEnabled) {
                        DirtySliderFloat("Room Size", &activeSynthParams.reverbRoomSize, 0.0f, 1.0f);
                        DirtySliderFloat("Damping", &activeSynthParams.reverbDamping, 0.0f, 1.0f);
                        DirtySliderFloat("Reverb Wet Mix", &activeSynthParams.reverbMix, 0.0f, 1.0f);
                    }
                }

                // 6. FUTURE DSP EXPANSION ARCHITECTURE
                if (ImGui::CollapsingHeader("+ Add Effect (DSP Chain Extensibility)")) {
                    ImGui::TextWrapped("The synth uses the modular ISynthEffect interface. Any custom effect implementing Process(std::vector<float>& buffer, uint32_t sampleRate) can be linked into the engine pipeline.");
                    ImGui::TextDisabled("Future effect targets: Phaser, Pitch Shifter, Multi-band EQ, Stereo Chorus, Compressor.");
                }

                ImGui::EndTabItem();
            }

            // TAB 5: PRESET FILE I/O
            if (ImGui::BeginTabItem("Preset File Manager")) {
                ImGui::Spacing();

                char nameBuf[128];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", activeSynthParams.name.c_str());
                if (ImGui::InputText("Preset Name", nameBuf, sizeof(nameBuf))) {
                    activeSynthParams.name = nameBuf;
                }

                ImGui::InputText("Export Filepath", synthTargetFilepath, sizeof(synthTargetFilepath));
                activeSynthParams.targetFilepath = synthTargetFilepath;

                ImGui::Spacing();
                static char jsonFilepath[256] = "tags/audio/my_preset.json";
                ImGui::InputText("Preset JSON Path", jsonFilepath, sizeof(jsonFilepath));

                if (ImGui::Button("Save JSON Preset")) {
                    if (SoundSynth::SavePresetFile(jsonFilepath, activeSynthParams)) {
                        synthStatusMessage = "Preset saved to " + std::string(jsonFilepath);
                    } else {
                        synthStatusMessage = "Failed to save JSON preset";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load JSON Preset")) {
                    if (SoundSynth::LoadPresetFile(jsonFilepath, activeSynthParams)) {
                        std::snprintf(synthTargetFilepath, sizeof(synthTargetFilepath), "%s", activeSynthParams.targetFilepath.c_str());
                        synthWaveformDirty = true;
                        synthStatusMessage = "Preset loaded from " + std::string(jsonFilepath);
                    } else {
                        synthStatusMessage = "Failed to load JSON preset";
                    }
                }

                ImGui::Separator();
                ImGui::Text("JSON Output Preview:");
                std::string jsonStr = SoundSynth::ToJson(activeSynthParams);
                ImGui::InputTextMultiline("##JsonPreview", const_cast<char*>(jsonStr.c_str()), jsonStr.size() + 1, ImVec2(-1, 200), ImGuiInputTextFlags_ReadOnly);

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(audioAccent, "NOTE SEQUENCER");
        ImGui::SameLine();
        ImGui::TextDisabled("16-step MIDI piano roll");

        if (ImGui::Button(sequencerPlaying ? "Stop Sequence" : "Play Sequence", ImVec2(132.0f, 26.0f))) {
            if (sequencerPlaying) {
                sequencerPlaying = false;
                if (AudioServer::Get()) AudioServer::Get()->StopSequencer();
            } else {
                sequencerPlaying = true;
                sequencerCurrentStep = 0;
                sequencerStepTimer = 0.0f;
                PlaySequencerLoop();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Notes", ImVec2(96.0f, 26.0f))) {
            sequencerNotes.clear();
            sequencerPlaying = false;
            if (AudioServer::Get()) AudioServer::Get()->StopSequencer();
            sequencerCurrentStep = 0;
            sequencerStepTimer = 0.0f;
        }
        ImGui::SameLine();
        auto TransposeSequencer = [&](int semitones) {
            bool canTranspose = true;
            for (SequencerNote& note : sequencerNotes) {
                if (note.midiNote >= 0 && (note.midiNote + semitones < 0 || note.midiNote + semitones > 127)) {
                    canTranspose = false;
                    break;
                }
            }
            if (!canTranspose) return;

            for (SequencerNote& note : sequencerNotes) {
                if (note.midiNote >= 0) note.midiNote += semitones;
            }
            sequencerTransposeOctaves += semitones / 12;
            sequencerViewOctave = std::clamp(sequencerViewOctave + semitones / 12, -5, 3);
        };
        if (ImGui::Button("Octave -", ImVec2(82.0f, 26.0f))) {
            TransposeSequencer(-12);
        }
        ImGui::SameLine();
        if (ImGui::Button("Octave +", ImVec2(82.0f, 26.0f))) {
            TransposeSequencer(12);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("View: C%d-B%d", 4 + sequencerViewOctave, 5 + sequencerViewOctave);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderInt("BPM", &sequencerBpm, 40, 240);
        ImGui::SameLine();
        ImGui::TextDisabled("Left-click: place/remove  Right-click: clear  Wheel: change octave view");

        ImGui::BeginChild("MidiPianoRoll", ImVec2(0.0f, 178.0f), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const int pianoLowestMidi = 60 + sequencerViewOctave * 12;
        const int pianoHighestMidi = pianoLowestMidi + 23;
        const int pianoRowCount = pianoHighestMidi - pianoLowestMidi + 1;
        constexpr float pianoRowHeight = 7.0f;
        constexpr float pianoLabelWidth = 44.0f;

        ImVec2 pianoMin = ImGui::GetCursorScreenPos();
        float pianoWidth = ImGui::GetContentRegionAvail().x;
        float gridWidth = std::max(1.0f, pianoWidth - pianoLabelWidth);
        float pianoHeight = pianoRowHeight * static_cast<float>(pianoRowCount);
        ImVec2 gridMin(pianoMin.x + pianoLabelWidth, pianoMin.y);
        ImVec2 gridMax(pianoMin.x + pianoWidth, pianoMin.y + pianoHeight);
        ImDrawList* pianoDrawList = ImGui::GetWindowDrawList();

        pianoDrawList->AddRectFilled(pianoMin, gridMax, IM_COL32(8, 8, 10, 255), 4.0f);

        auto IsBlackKey = [](int midiNote) {
            int pitchClass = midiNote % 12;
            return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 ||
                   pitchClass == 8 || pitchClass == 10;
        };


        for (int row = 0; row < pianoRowCount; ++row) {
            int midiNote = pianoHighestMidi - row;
            float y0 = pianoMin.y + row * pianoRowHeight;
            float y1 = y0 + pianoRowHeight;
            bool blackKey = IsBlackKey(midiNote);

            pianoDrawList->AddRectFilled(
                ImVec2(pianoMin.x, y0),
                ImVec2(gridMin.x, y1),
                blackKey ? IM_COL32(18, 18, 21, 255) : IM_COL32(45, 45, 50, 255));
            pianoDrawList->AddLine(
                ImVec2(pianoMin.x, y1),
                ImVec2(gridMax.x, y1),
                IM_COL32(38, 38, 43, 255));

            if (midiNote % 12 == 0) {
                int octave = midiNote / 12 - 1;
                char label[8];
                std::snprintf(label, sizeof(label), "C%d", octave);
                pianoDrawList->AddText(
                    ImVec2(pianoMin.x + 7.0f, y0 - 1.0f),
                    IM_COL32(210, 210, 218, 255),
                    label);
            }
        }

        constexpr int sequencerStepCount = 16;
        float stepWidth = gridWidth / static_cast<float>(sequencerStepCount);
        for (int step = 0; step < sequencerStepCount; ++step) {
            float x0 = gridMin.x + step * stepWidth;
            if (sequencerPlaying && step == sequencerCurrentStep) {
                pianoDrawList->AddRectFilled(
                    ImVec2(x0, gridMin.y),
                    ImVec2(x0 + stepWidth, gridMax.y),
                    IM_COL32(120, 120, 130, 42));
            }
            pianoDrawList->AddLine(
                ImVec2(x0, gridMin.y),
                ImVec2(x0, gridMax.y),
                step % 4 == 0 ? IM_COL32(100, 100, 110, 150) : IM_COL32(35, 35, 40, 220));
        }

        for (size_t noteIndex = 0; noteIndex < sequencerNotes.size(); ++noteIndex) {
            const SequencerNote& note = sequencerNotes[noteIndex];
            if (note.startStep < 0 || note.startStep >= sequencerStepCount ||
                note.midiNote < pianoLowestMidi || note.midiNote > pianoHighestMidi) {
                continue;
            }

            int endStep = std::clamp(note.startStep + std::max(1, note.lengthSteps), note.startStep + 1, sequencerStepCount);
            int row = pianoHighestMidi - note.midiNote;
            float x0 = gridMin.x + note.startStep * stepWidth + 2.0f;
            float x1 = gridMin.x + endStep * stepWidth - 2.0f;
            float y0 = pianoMin.y + row * pianoRowHeight + 1.0f;
            float y1 = y0 + pianoRowHeight - 2.0f;
            bool active = sequencerPlaying &&
                          sequencerCurrentStep >= note.startStep &&
                          sequencerCurrentStep < endStep;

            pianoDrawList->AddRectFilled(
                ImVec2(x0, y0),
                ImVec2(x1, y1),
                active ? IM_COL32(235, 235, 245, 255) : IM_COL32(165, 165, 178, 245),
                2.0f);
            pianoDrawList->AddLine(
                ImVec2(x1 - 3.0f, y0 + 1.0f),
                ImVec2(x1 - 3.0f, y1 - 1.0f),
                IM_COL32(65, 65, 72, 230),
                2.0f);
        }
        pianoDrawList->AddLine(gridMax, ImVec2(gridMin.x, gridMax.y), IM_COL32(100, 100, 110, 180));

        ImGui::InvisibleButton("##MidiPianoRollCanvas", ImVec2(pianoWidth, pianoHeight));
        if (ImGui::IsItemHovered()) {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            float wheelDelta = ImGui::GetIO().MouseWheel;
            if (std::abs(wheelDelta) > 0.0f) {
                sequencerViewOctave = std::clamp(
                    sequencerViewOctave + (wheelDelta > 0.0f ? 1 : -1),
                    -5,
                    3);
            }
            int hoveredRow = (mousePos.y >= gridMin.y && mousePos.y < gridMax.y)
                ? std::clamp(static_cast<int>((mousePos.y - gridMin.y) / pianoRowHeight), 0, pianoRowCount - 1)
                : -1;
            int hoveredMidi = hoveredRow >= 0 ? pianoHighestMidi - hoveredRow : -1;
            int hoveredStep = (mousePos.x >= gridMin.x && mousePos.x < gridMax.x)
                ? std::clamp(static_cast<int>((mousePos.x - gridMin.x) / stepWidth), 0, sequencerStepCount - 1)
                : -1;

            int edgeNoteIndex = -1;
            if (hoveredMidi >= 0 && hoveredStep >= 0) {
                for (size_t i = 0; i < sequencerNotes.size(); ++i) {
                    const SequencerNote& note = sequencerNotes[i];
                    int endStep = std::clamp(note.startStep + std::max(1, note.lengthSteps), note.startStep + 1, sequencerStepCount);
                    float noteEndX = gridMin.x + endStep * stepWidth;
                    if (note.midiNote == hoveredMidi &&
                        mousePos.x >= noteEndX - 7.0f &&
                        mousePos.x <= noteEndX + 2.0f &&
                        hoveredStep >= note.startStep &&
                        hoveredStep < endStep) {
                        edgeNoteIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (edgeNoteIndex >= 0) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (edgeNoteIndex >= 0) {
                    sequencerResizingNote = edgeNoteIndex;
                } else if (hoveredMidi >= 0 && hoveredStep >= 0) {
                    int existingNote = -1;
                    for (size_t i = 0; i < sequencerNotes.size(); ++i) {
                        const SequencerNote& note = sequencerNotes[i];
                        if (note.midiNote == hoveredMidi &&
                            hoveredStep >= note.startStep &&
                            hoveredStep < note.startStep + std::max(1, note.lengthSteps)) {
                            existingNote = static_cast<int>(i);
                            break;
                        }
                    }

                    if (existingNote >= 0) {
                        sequencerNotes.erase(sequencerNotes.begin() + existingNote);
                    } else {
                        sequencerNotes.push_back({ hoveredMidi, hoveredStep, 1 });
                    }
                }
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoveredMidi >= 0 && hoveredStep >= 0) {
                for (auto it = sequencerNotes.begin(); it != sequencerNotes.end(); ++it) {
                    if (it->midiNote == hoveredMidi &&
                        hoveredStep >= it->startStep &&
                        hoveredStep < it->startStep + std::max(1, it->lengthSteps)) {
                        sequencerNotes.erase(it);
                        break;
                    }
                }
            }
        }

        if (sequencerResizingNote >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                sequencerResizingNote < static_cast<int>(sequencerNotes.size())) {
                const SequencerNote& note = sequencerNotes[sequencerResizingNote];
                float mouseX = ImGui::GetIO().MousePos.x;
                int endStep = std::clamp(
                    static_cast<int>(std::ceil((mouseX - gridMin.x) / stepWidth)),
                    note.startStep + 1,
                    sequencerStepCount);
                sequencerNotes[sequencerResizingNote].lengthSteps = endStep - note.startStep;
            } else {
                sequencerResizingNote = -1;
            }
        }

        ImGui::EndChild();

        ImGui::End();
        ImGui::PopStyleColor(26);
        ImGui::PopStyleVar(4);

    }
}
