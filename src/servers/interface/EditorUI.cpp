#include "IO/SceneSerializer.hpp"
#include "modules/gltf/AssetLoader.hpp"
#include "EditorUI.hpp"
#include "imgui.h"
#include "servers/rendering/RenderingServer.hpp"
#include "scene/Scene.hpp"
#include "servers/camera/Camera.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "include/portable-file-dialogs.h" 
#include "IO/SceneSerializer.hpp"
#include <iostream>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Crescendo {

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

    // --- INITIALIZE (Restored for your specific ImGui version) ---
    void EditorUI::Initialize(RenderingServer* renderer, SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t queueFamilyIndex, VkRenderPass renderPass, uint32_t imageCount) {
        this->rendererRef = renderer;

        // 1. Create Descriptor Pool
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

        // [FIX] Use the PipelineInfoMain struct required by your header
        init_info.PipelineInfoMain.RenderPass = renderPass; 
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT; 

        ImGui_ImplVulkan_Init(&init_info);
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
    }

    
    void EditorUI::Prepare(Scene* scene, Camera& camera, VkDescriptorSet viewportDescriptor, EngineState& engineState) {
        
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        ImGuizmo::BeginFrame();

        ImGuiIO& io = ImGui::GetIO();

        if (!io.WantCaptureKeyboard || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
             if (io.MouseWheel != 0.0f) {
                 camera.Zoom -= io.MouseWheel; 
                 if (camera.Zoom < 1.0f) camera.Zoom = 1.0f;
                 if (camera.Zoom > 120.0f) camera.Zoom = 120.0f;
                 camera.fov = camera.Zoom; 
             }
             
             if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                 camera.Rotate(io.MouseDelta.x, -io.MouseDelta.y); 
                 
                 float moveSpeed = 5.0f * io.DeltaTime; 
                 if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) moveSpeed *= 3.0f; 

                 if (ImGui::IsKeyDown(ImGuiKey_W)) camera.Position += camera.Front * moveSpeed;
                 if (ImGui::IsKeyDown(ImGuiKey_S)) camera.Position -= camera.Front * moveSpeed;
                 if (ImGui::IsKeyDown(ImGuiKey_D)) camera.Position += camera.Right * moveSpeed;
                 if (ImGui::IsKeyDown(ImGuiKey_A)) camera.Position -= camera.Right * moveSpeed;
                 if (ImGui::IsKeyDown(ImGuiKey_Q)) camera.Position += camera.WorldUp * moveSpeed; 
                 if (ImGui::IsKeyDown(ImGuiKey_E)) camera.Position -= camera.WorldUp * moveSpeed; 
             }
        }

        // --- CTRL+A ADD MENU ---
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_A)) {
            ImGui::OpenPopup("AddEntityPopup");
        }

        if (ImGui::BeginPopup("AddEntityPopup")) {
            ImGui::TextDisabled("Add Entity...");
            ImGui::Separator();
            
            if (ImGui::MenuItem("Empty Prop")) {
                CBaseEntity* ent = scene->CreateEntity("prop_dynamic");
                ent->targetName = "New Prop";
                ent->origin = camera.Position + camera.Front * 5.0f; // Spawn 5 units in front of camera
                selectedObjectIndex = scene->entities.size() - 1;    // Auto-select it
            }

            if (ImGui::MenuItem("Point Light")) {
                CBaseEntity* point = scene->CreateEntity("light_point");
                point->targetName = "Point Light";
                point->origin = camera.Position + camera.Front * 5.0f; // Spawn in front of you
                point->albedoColor = glm::vec3(1.0f, 0.4f, 0.1f);      // Warm fire orange
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
            

            ImGui::EndPopup();
        }

        // 2. DOCKSPACE & MENU
        ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpaceOverViewport(dockSpaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Import Model")) {
                    auto selection = pfd::open_file("Select a file", ".",
                        { "All Models", "*.obj *.gltf *.glb", "GLTF", "*.gltf *.glb", "OBJ", "*.obj" }).result();
                    
                    if (!selection.empty() && rendererRef) {
                        std::string path = selection[0];
                        Crescendo::AssetLoader::loadModel(rendererRef, path, scene);
                    }
                }

                // Save Project
                if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                    // Open a native save dialog
                    auto destination = pfd::save_file("Save Project", ".", { "JSON Map Files", "*json" }).result();
                    if (!destination.empty()) {
                        // Ensure it ends in json
                        if (destination.find("json") == std::string::npos) destination += ".json";

                        SceneSerializer serializer(scene);
                        serializer.Serialize(destination);
                    }
                }

                // Load Project
                if (ImGui::MenuItem("Load Project", "Ctrl+O")) {
                    // Same as above
                    auto selection = pfd::open_file("Load Project", ".", { "JSON Map Files", "*.json" }).result();
                    if (!selection.empty()) {
                        SceneSerializer serializer(scene);
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
            if (fpsTimer >= 0.1f) { // Update every 100ms
                displayFps = io.Framerate;
                fpsTimer = 0.0f;
            }

            // Push the text to the far right of the menu bar
            ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "FPS: %.0f", displayFps);
            // --------------------------------

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

        // 3. VIEWPORT WINDOW
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        ImVec2 viewportPos = ImGui::GetCursorScreenPos();
        this->lastViewportSize = glm::vec2(viewportSize.x, viewportSize.y);

        if (viewportDescriptor != VK_NULL_HANDLE) {
            ImGui::Image((ImTextureID)viewportDescriptor, viewportSize);
        }

       // --- GIZMOS ---
        
        if (viewportSize.x > 0 && viewportSize.y > 0 && scene) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
                
            glm::mat4 view = camera.GetViewMatrix();
            float aspect = viewportSize.x / viewportSize.y;
            
            // Generate the raw OpenGL-style projection matrix
            glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspect, camera.nearClip, camera.farClip);
                
            // DO NOT FLIP THE Y-AXIS! ImGuizmo needs the pure OpenGL matrix to calculate mouse dragging correctly.
            // proj[1][1] *= -1;  <-- Remove this
                
            if (selectedObjectIndex >= 0 && selectedObjectIndex < (int)scene->entities.size()) {
                CBaseEntity* ent = scene->entities[selectedObjectIndex];
                if (ent) {
                    
                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, ent->origin);
                    model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0, 0, 1));
                    model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0, 1, 0));
                    model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1, 0, 0));
                    model = glm::scale(model, ent->scale);

                    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), 
                                         mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(model));

                    if (ImGuizmo::IsUsing()) {
                        float newTranslation[3], newRotation[3], newScale[3];
                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), newTranslation, newRotation, newScale);
                        ent->origin = glm::make_vec3(newTranslation);
                        ent->angles = glm::make_vec3(newRotation);
                        ent->scale  = glm::make_vec3(newScale);
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // 4. HIERARCHY & INSPECTOR
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::Begin("Scene Hierarchy");
        ImGui::PopStyleColor(); // Must pop immediately after Begin so it doesn't affect child windows

        if (scene) {
            for (size_t i = 0; i < scene->entities.size(); i++) {
                CBaseEntity* ent = scene->entities[i];
                if (!ent) continue;
                ImGui::PushID((int)i);
                std::string label = ent->targetName.empty() ? "Entity " + std::to_string(i) : ent->targetName;
                if (ImGui::Selectable(label.c_str(), selectedObjectIndex == (int)i)) {
                    selectedObjectIndex = (int)i;
                }
                ImGui::PopID();
            }
        }
        ImGui::End();

        ImGui::Begin("Inspector");
        
        if (selectedObjectIndex >= 0 && scene && selectedObjectIndex < (int)scene->entities.size()) {
            CBaseEntity* ent = scene->entities[selectedObjectIndex];
            if (ent) {
                ImGui::Text("Transform");
                ImGui::DragFloat3("Position", glm::value_ptr(ent->origin), 0.1f);
                ImGui::DragFloat3("Rotation", glm::value_ptr(ent->angles), 1.0f);
                ImGui::DragFloat3("Scale",    glm::value_ptr(ent->scale), 0.1f);
                
                ImGui::Separator();
                ImGui::Text("Material");
                ImGui::ColorEdit3("Albedo", glm::value_ptr(ent->albedoColor));
                ImGui::SliderFloat("Roughness", &ent->roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("Metallic", &ent->metallic, 0.0f, 1.0f);
                ImGui::SliderFloat("Emission", &ent->emission, 0.0f, 10.0f);

                // [FIX] Always show Transmission so you can TURN ON the glass effect
                ImGui::SliderFloat("Transmission", &ent->transmission, 0.0f, 1.0f);

                if (ent->transmission > 0.0f) {
                    ImGui::Separator();
                    ImGui::Text("Glass / Volume");

                    // This lets you override the glTF's color
                    ImGui::ColorEdit3("Volume Tint", glm::value_ptr(ent->attenuationColor));

                    // This is the "Density" slider. 
                    // Small Value = Dark/Thick (Red). Large Value = Clear/Thin (Yellow).
                    ImGui::DragFloat("Density (Dist)", &ent->attenuationDistance, 0.01f, 0.001f, 10.0f);

                    ImGui::SliderFloat("Refraction (IOR)", &ent->ior, 1.0f, 2.5f); 
                }
            
                ImGui::Separator();
                ImGui::Text("Normal Maps");
                // 0.0 = Flat, 1.0 = Default, >1.0 = Deep/Exaggerated
                ImGui::SliderFloat("Strength", &ent->normalStrength, 0.0f, 5.0f);

                // --- UNIFIED ATMOSPHERE SETTINGS ---
                if (ent->className == "env_sky") { 
                    ImGui::SeparatorText("Atmosphere Settings");
                
                    // 1. THE DROPDOWN MENU
                    const char* skyTypeNames[] = { "Solid Color", "Procedural", "HDR Map" };
                    int currentSkyType = static_cast<int>(scene->environment.skyType);
                    
                    if (ImGui::Combo("Sky Type", &currentSkyType, skyTypeNames, IM_ARRAYSIZE(skyTypeNames))) {
                        scene->environment.skyType = static_cast<SkyType>(currentSkyType);
                    }

                    ImGui::Spacing();

                    // 2. CONTEXT SENSITIVE PANELS
                    if (scene->environment.skyType == SkyType::SolidColor) {
                        ImGui::TextDisabled("Basic unlit background");
                        
                        // [FIX] Edit the entity directly so it saves and syncs!
                        ImGui::ColorEdit3("Background Color", glm::value_ptr(ent->albedoColor));
                    }
                    else if (scene->environment.skyType == SkyType::Procedural) {
                        ImGui::TextDisabled("Sun-driven atmospheric scattering.");
                        
                        // [FIX] Edit the entity directly so it saves and syncs!
                        ImGui::ColorEdit3("Zenith  (Top) Color", glm::value_ptr(ent->albedoColor));
                        ImGui::ColorEdit3("Horizon (Bottom) Color", glm::value_ptr(ent->attenuationColor));
                        
                        ImGui::ColorEdit3("Sun Color", glm::value_ptr(scene->environment.sunColor));
                        ImGui::SliderFloat("Sun Intensity", &scene->environment.sunIntensity, 0.0f, 10.0f);
                    }
                    else if (scene->environment.skyType == SkyType::HDRMap) {
                        ImGui::TextDisabled("Image-based lighting.");

                        if (ImGui::Button("Load New HDR...")) {
                            auto selection = pfd::open_file("Select HDR", ".", {"HDR Files", "*.hdr"}).result();
                            if (!selection.empty()) {
                                rendererRef->loadSkybox(selection[0]);
                            }
                        }
                    }
                    
                    // 3. FOG SETTINGS (Always Visible)
                    if (ImGui::CollapsingHeader("Fog & Global Illumination")) {
                        ImGui::ColorEdit4("Fog Color/Density", glm::value_ptr(scene->environment.fogColor));
                        ImGui::SliderFloat("Fog Falloff", &scene->environment.fogParams.x, 0.0f, 1.0f);
                        ImGui::SliderFloat("Fog Height", &scene->environment.fogParams.w, -50.0f, 50.0f);  
                    }
                }
                // --- END ATMOSPHERE SETTINGS ---

                // --- DIRECTIONAL LIGHT SETTINGS ---
                if (ent->className == "light_directional") {
                    ImGui::SeparatorText("Sun & Shadow Settings");
                    
                    ImGui::ColorEdit3("Light Color", glm::value_ptr(ent->albedoColor));
                    ImGui::SliderFloat("Intensity", &ent->emission, 0.0f, 20.0f);
                    
                    ImGui::Spacing();
                    ImGui::TextDisabled("Use the Rotation Gizmo in the viewport\nto change the sun/shadow direction!");
                    
                    // Sync the engine's global sun to this entity!
                    // This converts the entity's Euler angles (XYZ) into a forward Direction Vector
                    glm::mat4 rotMat = glm::mat4(1.0f);
                    rotMat = glm::rotate(rotMat, glm::radians(ent->angles.z), glm::vec3(0, 0, 1));
                    rotMat = glm::rotate(rotMat, glm::radians(ent->angles.y), glm::vec3(0, 1, 0));
                    rotMat = glm::rotate(rotMat, glm::radians(ent->angles.x), glm::vec3(1, 0, 0));
                    
                    // Forward vector in OpenGL/Vulkan is generally -Z or +X depending on your setup. 
                    // We'll use the matrix's forward vector.
                    glm::vec3 forward = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                    
                    scene->environment.sunDirection = forward;
                    scene->environment.sunColor = ent->albedoColor;
                    scene->environment.sunIntensity = ent->emission;
                }

                // --- POINT LIGHT SETTINGS ---
                if (ent->className == "light_point") {
                    ImGui::SeparatorText("Point Light Settings");
                    ImGui::ColorEdit3("Light Color", glm::value_ptr(ent->albedoColor));
                    ImGui::SliderFloat("Intensity", &ent->emission, 0.0f, 100.0f);
                    ImGui::SliderFloat("Radius", &ent->scale.x, 1.0f, 100.0f); 
                    
                    ImGui::Spacing();
                    ImGui::TextDisabled("Use the Translate Gizmo to move the light!");
                }
            }
        } 

        // Gizmo controls and Post Processing now safely sit OUTSIDE the entity selection!
        ImGui::Separator();
        ImGui::Text("Gizmo Controls");
        if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE)) mCurrentGizmoOperation = ImGuizmo::SCALE;

        ImGui::Separator();
        ImGui::Text("Post Processing");
        ImGui::DragFloat("Bloom Strength", &rendererRef->postProcessSettings.bloomStrength, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat("Bloom Threshold", &rendererRef->postProcessSettings.bloomThreshold, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Exposure", &rendererRef->postProcessSettings.exposure, 0.01f, 0.1f, 5.0f);
        ImGui::DragFloat("Gamma", &rendererRef->postProcessSettings.gamma, 0.01f, 0.1f, 3.0f);
        ImGui::DragFloat("Blur Radius", &rendererRef->postProcessSettings.blurRadius, 0.1f, 0.0f, 10.0f);

        ImGui::End(); // Safely closes the "Inspector" window

        // --- ENGINE SETTINGS WINDOW ---
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

        // --- CONSOLE (Updated to use the class-level flag) ---
        if (showConsole) {
            gameConsole.Draw("Console", &showConsole);
        }

        ImGui::Render(); 
    }
    
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
