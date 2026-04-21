#include "IO/SceneSerializer.hpp"

#include "modules/gltf/AssetLoader.hpp"
#include "EditorUI.hpp"
#include "imgui.h"
#include "scene/BaseEntity.hpp"
#include "scene/components/PointLightComponent.hpp"
#include "scene/components/TransformComponent.hpp"    
#include "scene/components/MeshRendererComponent.hpp" 
#include "scene/components/ProceduralPlanetComponent.hpp"
#include "modules/terrain/TerrainManager.hpp"
#include "modules/terrain/OctreeNode.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/networking/NetworkingServer.hpp"
#include "scene/Scene.hpp"
#include "src/IO/SceneManager.hpp"
#include "servers/camera/Camera.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "include/portable-file-dialogs.h" 
#include "IO/SceneSerializer.hpp"


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
        icons.folderIcon        = rendererRef->getImGuiTextureID("assets/icons/folder.png");
        icons.projectFolderIcon = rendererRef->getImGuiTextureID("assets/icons/crescendoprojectfolder.png");
        icons.scriptIcon        = rendererRef->getImGuiTextureID("assets/icons/script.png");
        icons.shaderIcon        = rendererRef->getImGuiTextureID("assets/icons/shader.png");
        icons.audioIcon         = rendererRef->getImGuiTextureID("assets/icons/audio.png");
        icons.spatialAudioIcon  = rendererRef->getImGuiTextureID("assets/icons/spatialaudio.png");
        
        // Optional generic fallbacks
        icons.fileIcon          = rendererRef->getImGuiTextureID("assets/icons/file.png");
        icons.modelIcon         = rendererRef->getImGuiTextureID("assets/icons/model.png");
    
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
        // If we are actively playing, strip ImGui's ability to see the mouse 
        // so you don't accidentally hover/click invisible UI elements!
        if (engineState == EngineState::Playing) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
        
        // --- HOTKEYS & STATE MANAGEMENT ---
        // static bool showPauseMenu = false;

        // Toggle Console with '~'
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
            showConsole = !showConsole;
            // Auto-pause if we open the console while playing
            if (showConsole && engineState == EngineState::Playing) {
                engineState = EngineState::Paused;
            }
        }

        /* Toggle Pause Menu with 'ESC'
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (engineState == EngineState::Playing) {
                engineState = EngineState::Paused;
                showPauseMenu = true;
            } else if (engineState == EngineState::Paused) {
                engineState = EngineState::Playing;
                showPauseMenu = false;
                showConsole = false; // Close console when resuming
            }
        } */

        // --- EDITOR CAMERA INPUT ---
        // ONLY allow camera movement if we are actually in Editor Mode!
        if (engineState == EngineState::Editor) {
            if (!io.WantCaptureKeyboard || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (io.MouseWheel != 0.0f) {
                    camera.Zoom -= io.MouseWheel; 
                    if (camera.Zoom < 1.0f) camera.Zoom = 1.0f;
                    if (camera.Zoom > 120.0f) camera.Zoom = 120.0f;
                    camera.fov = camera.Zoom; 
                }
                
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    camera.Rotate(-io.MouseDelta.x, -io.MouseDelta.y); 
                    
                    // CLAMP THE DELTA TIME: Never allow a frame step larger than 50ms
                    float safeDelta = io.DeltaTime;
                    if (safeDelta > 0.05f) safeDelta = 0.05f;
                    float moveSpeed = 15.0f * safeDelta; 
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) moveSpeed *= 33.0f; 
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

            if (ImGui::MenuItem("Procedural Planet")) {
                CBaseEntity* planet = scene->CreateEntity("prop_dynamic");
                planet->targetName = "Voxel Planet";
                planet->origin = camera.Position + glm::dvec3(camera.Front * 5000.0f);

                planet->AddComponent<TransformComponent>();
                planet->AddComponent<MeshRendererComponent>();
                planet->AddComponent<ProceduralPlanetComponent>();

                auto planetComp = planet->GetComponent<ProceduralPlanetComponent>();
                
                // PPG SCALE ---
                planetComp->settings.radius = 3000.0f;     // A massive 6km wide planet!
                planetComp->settings.amplitude = 150.0f;   // Mountains that reach into the clouds
                planetComp->settings.frequency = 0.002f;   // Stretch the noise so it forms continents
                planetComp->lodSplitThreshold = 1.1f;      // Tweak this to control how soon chunks split
                
                // Start the root node at a massive size and LOD 6
                float planetSize = planetComp->settings.radius * 2.2f;
                planetComp->rootNode = std::make_unique<Crescendo::Terrain::OctreeNode>(
                    glm::vec3(0.0f), planetSize, 6 
                );

                // Initialize the Manager!
                planetComp->chunkManager = std::make_unique<Crescendo::Terrain::TerrainManager>();

                // GENERATE THE ATMOSPHERE MESH ---
                std::vector<Vertex> atmoVerts;
                std::vector<uint32_t> atmoIndices;
                
                // Change atmosphereScale to atmosphereCeiling
                float atmoRadius = planetComp->settings.radius * planetComp->atmosphereCeiling;
                
                // 64x64 segments makes it incredibly smooth from space!
                Crescendo::Terrain::VoxelGenerator::GenerateWaterSphere(atmoRadius, 64, 64, atmoVerts, atmoIndices);

                // --- Send to vram ---
                planetComp->atmosphereMeshID = sceneManager->GetRenderer()->acquireMesh("PROCEDURAL", "Atmosphere", atmoVerts, atmoIndices);
                
                // ==========================================
                // 2. GENERATE THE OCEAN MESH
                // ==========================================
                std::vector<Vertex> waterVerts;
                std::vector<uint32_t> waterIndices;
                
                // THE FIX: Generate a radius of 1.0 so the Entity scale applies correctly!
                Crescendo::Terrain::VoxelGenerator::GenerateWaterSphere(1.0f, 64, 64, waterVerts, waterIndices);

                // Upload to GPU and get the ID
                int waterMeshID = sceneManager->GetRenderer()->acquireMesh("PROCEDURAL", "Ocean", waterVerts, waterIndices);

                // ==========================================
                // 3. CREATE THE OCEAN ENTITY
                // ==========================================
                CBaseEntity* ocean = scene->CreateEntity("prop_water"); 
                ocean->targetName = "Procedural Ocean";
                
                // Make sure it spawns at the planet origin!
                // (If your planet entity is named something other than 'newEnt', change it here!)
                ocean->origin = planet->origin;
                
                ocean->modelIndex = waterMeshID;
                planet->modelIndex = -1; 
                planet->albedoColor = glm::vec3(0.2f, 0.6f, 0.3f); 
                planet->roughness = 0.9f;
                planet->metallic = 0.0f;

                ocean->scale = glm::vec3(planetComp->settings.radius + 15.0f);     // Water level
                ocean->albedoColor = glm::vec3(0.0f, 0.2f, 0.6f); 
                ocean->roughness = 0.1f; 
                ocean->transmission = 1.0f; 
                
                planet->children.push_back(ocean);
                selectedObjectIndex = scene->entities.size() - 2; 
            }
            
            if (ImGui::MenuItem("Point Light")) {
                CBaseEntity* point = scene->CreateEntity("light_point");
                point->targetName = "Point Light";
                point->origin = camera.Position + glm::dvec3(camera.Front * 5.0f);
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
        
        if (viewportSize.x > 0 && viewportSize.y > 0 && scene) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
                
            glm::mat4 view = camera.GetViewMatrix();
            float aspect = viewportSize.x / viewportSize.y;
            
            // Generate the raw OpenGL-style projection matrix
            glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspect, camera.nearClip, camera.farClip);
                
            if (selectedObjectIndex >= 0 && selectedObjectIndex < (int)scene->entities.size()) {
                CBaseEntity* ent = scene->entities[selectedObjectIndex];
                if (ent) {
                    
                    glm::mat4 model = glm::mat4(1.0f);
                    // Cast down to vec3 for the Gizmo matrix
                    model = glm::translate(model, glm::vec3(ent->origin));
                    model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0, 0, 1));
                    model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0, 1, 0));
                    model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1, 0, 0));
                    model = glm::scale(model, ent->scale);
                                    
                    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), 
                                         mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(model));
                                    
                    if (ImGuizmo::IsUsing()) {
                        float newTranslation[3], newRotation[3], newScale[3];
                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), newTranslation, newRotation, newScale);
                        
                        // Cast the float array back into a dvec3
                        ent->origin = glm::dvec3(newTranslation[0], newTranslation[1], newTranslation[2]);
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

        ImGui::End(); // Closes "Viewport"
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // --- 4. HIERARCHY & INSPECTOR ---

        
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::Begin("Scene Hierarchy");
        ImGui::PopStyleColor();

        if (scene) {
            for (size_t i = 0; i < scene->entities.size(); i++) {
                CBaseEntity* ent = scene->entities[i];
                if (!ent) continue;
                ImGui::PushID((int)i);
                std::string label = ent->targetName.empty() ? "Entity " + std::to_string(i) : ent->targetName;
                if (ImGui::Selectable(label.c_str(), selectedObjectIndex == (int)i)) {
                    selectedObjectIndex = (int)i;
                }

                // --- RIGHT-CLICK CONTEXT MENU ---
                if (ImGui::BeginPopupContextItem()) {
                    selectedObjectIndex = (int)i; // Auto-select the item you right-clicked
                    
                    ImGui::TextDisabled("%s", label.c_str());
                    ImGui::Separator();

                    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                        CBaseEntity* orig = scene->entities[i];
                        CBaseEntity* clone = scene->CreateEntity(orig->className);
                        
                        clone->targetName = orig->targetName + " (Copy)";
                        clone->modelIndex = orig->modelIndex;
                        clone->textureID  = orig->textureID;
                        clone->assetPath  = orig->assetPath;
                        
                        // Copy Transform
                        clone->origin = orig->origin;
                        clone->angles = orig->angles;
                        clone->scale  = orig->scale;
                        
                        // Copy PBR Material Data
                        clone->albedoColor = orig->albedoColor;
                        clone->emission    = orig->emission;
                        clone->roughness   = orig->roughness;
                        clone->metallic    = orig->metallic;
                        clone->transmission = orig->transmission;
                        clone->ior         = orig->ior;
                        clone->attenuationColor = orig->attenuationColor;
                        clone->attenuationDistance = orig->attenuationDistance;
                        clone->normalStrength = orig->normalStrength;

                        // Re-attach Bridge Components
                        if (orig->HasComponent<TransformComponent>()) clone->AddComponent<TransformComponent>();
                        if (orig->HasComponent<MeshRendererComponent>()) clone->AddComponent<MeshRendererComponent>();
                        if (orig->HasComponent<PointLightComponent>()) clone->AddComponent<PointLightComponent>();
                        
                        // Select the newly duplicated item
                        selectedObjectIndex = clone->index; 
                    }
                    
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // Red text
                    if (ImGui::MenuItem("Delete", "Del")) {
                        
                        scene->DeleteEntity(i); // Call our new safe delete!
                        
                        // Reset selection so the Inspector doesn't try to draw a deleted object
                        selectedObjectIndex = -1; 
                    }
                    ImGui::PopStyleColor();

                    ImGui::EndPopup();
                }

                ImGui::PopID();
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
                        const char* skyTypeNames[] = { "Solid Color", "Procedural", "HDR Map" };
                        int currentSkyType = static_cast<int>(scene->environment.skyType);
                        
                        if (ImGui::Combo("Sky Type", &currentSkyType, skyTypeNames, IM_ARRAYSIZE(skyTypeNames))) {
                            scene->environment.skyType = static_cast<SkyType>(currentSkyType);
                        }

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

                // --- PROCEDURAL PLANET SETTINGS ---
                if (ent->HasComponent<ProceduralPlanetComponent>()) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.22f, 1.0f));
                    if (ImGui::CollapsingHeader("Procedural Planet", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {

                        // 1. Removed the '&' so it is correctly captured as a pointer
                        auto planet = ent->GetComponent<ProceduralPlanetComponent>();
                        bool needsRebuild = false;
                    
                        // 2. Swapped all the dots '.' for arrows '->'
                        ImGui::SliderFloat("Radius", &planet->settings.radius, 1.0f, 50.0f);
                        if (ImGui::IsItemDeactivatedAfterEdit()) needsRebuild = true;
                    
                        ImGui::SliderInt("Octaves", &planet->settings.octaves, 1, 8);
                        if (ImGui::IsItemDeactivatedAfterEdit()) needsRebuild = true;
                    
                        ImGui::SliderFloat("Amplitude", &planet->settings.amplitude, 0.0f, 20.0f);
                        if (ImGui::IsItemDeactivatedAfterEdit()) needsRebuild = true;
                    
                        ImGui::SliderFloat("Frequency", &planet->settings.frequency, 0.01f, 1.0f);
                        if (ImGui::IsItemDeactivatedAfterEdit()) needsRebuild = true;
                    
                        ImGui::SliderInt("Resolution", &planet->resolution, 8, 64);
                        if (ImGui::IsItemDeactivatedAfterEdit()) needsRebuild = true;
                    
                        // 3. Fixed the ImVec2 typo!
                        if (ImGui::Button("Regenerate Mesh", ImVec2(-1, 0))) {
                            needsRebuild = true;
                        }
                    
                        if (needsRebuild) {
                        
                        // Diameter = Radius * 2. Add the mountain amplitude, plus a 20% safety margin.
                        planet->chunkSize = (planet->settings.radius + planet->settings.amplitude) * 2.2f;
                            
                        // Center the chunk perfectly using the new dynamic size
                        glm::vec3 genOrigin = glm::vec3(-planet->chunkSize / 2.0f);
                            
                        auto chunk = Crescendo::Terrain::VoxelGenerator::GenerateChunk(
                            genOrigin, planet->resolution, planet->chunkSize, planet->settings
                        );
                    
                        // Send new mesh to Vulkan (with our safety net!)
                        if (!chunk.vertices.empty() && !chunk.indices.empty()) {
                            ent->modelIndex = rendererRef->acquireMesh("PROCEDURAL", "PlanetChunk", chunk.vertices, chunk.indices);
                        } else {
                            ent->modelIndex = -1; 
                        }
                    }
                        ImGui::Spacing();
                    }
                    ImGui::PopStyleColor();
                }

                // ATMO TWEAKS
                if (selectedObjectIndex >= 0 && selectedObjectIndex < scene->entities.size()) {
                    CBaseEntity* entity = scene->entities[selectedObjectIndex];

                    // ... (Transform UI, MeshRenderer UI) ...
                
                    if (entity->HasComponent<ProceduralPlanetComponent>()) {
                        auto* planetComponent = entity->GetComponent<ProceduralPlanetComponent>();

                        // YOUR EXISTING PLANET SLIDERS ARE HERE:
                        ImGui::SliderFloat("AtmoRadius", &planetComponent->settings.radius, 1.0f, 100.0f);
                        // ... (Octaves, Amplitude, etc) ...
                    
                        // ---> PASTE THE ATMOSPHERE UI BLOCK RIGHT HERE! <---
                        if (ImGui::CollapsingHeader("Atmosphere Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                            // Add the pointer to the variables! (change 'planet' to 'planetComp' if that's what is used in this block)
                            ImGui::DragFloat("Atmo Ceiling (Scale)", &planetComponent->atmosphereCeiling, 0.01f, 1.0f, 3.0f);
                            ImGui::DragFloat("Atmo Floor (Offset)", &planetComponent->atmosphereFloor, 1.0f, -500.0f, 500.0f);
                            ImGui::SliderFloat("Atmo Intensity", &planetComponent->atmosphereIntensity, 1.0f, 50.0f, "%.1f");

                            ImGui::ColorEdit3("Rayleigh", glm::value_ptr(planetComponent->rayleigh), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                            ImGui::SliderFloat("Mie (Haze)", &planetComponent->mie, 0.0001f, 0.02f, "%.4f");
                        }
                    }
                }

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
                        // FIX: Added "##folder" as the first argument!
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
}
