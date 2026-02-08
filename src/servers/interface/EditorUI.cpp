#include "EditorUI.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "scene/Scene.hpp"
#include "servers/camera/Camera.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "include/portable-file-dialogs.h" // Needed for file picker
#include <iostream>
#include <cmath> // For sin/cos

// GLM includes for Gizmo
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Crescendo {

    // Helper functions for Raycasting (needed for 3D cursor)
    struct Ray { glm::vec3 origin; glm::vec3 direction; };

    Ray ScreenToWorldRay(glm::vec2 mousePos, glm::vec2 viewportSize, glm::mat4 view, glm::mat4 projection) {
        float x = (2.0f * mousePos.x) / viewportSize.x - 1.0f;
        float y = 1.0f - (2.0f * mousePos.y) / viewportSize.y;
        glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);
        glm::vec4 rayEye = glm::inverse(projection) * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
        glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
        rayWorld = glm::normalize(rayWorld);
        return { glm::vec3(glm::inverse(view)[3]), rayWorld };
    }

    bool RayPlaneIntersection(Ray ray, glm::vec3 planeNormal, float planeHeight, glm::vec3& outIntersection) {
        float denom = glm::dot(planeNormal, ray.direction);
        if (std::abs(denom) > 1e-6) {
            glm::vec3 planeCenter = glm::vec3(0, 0, planeHeight);
            float t = glm::dot(planeCenter - ray.origin, planeNormal) / denom;
            if (t >= 0) {
                outIntersection = ray.origin + (ray.direction * t);
                return true;
            }
        }
        return false;
    }

    static void check_vk_result(VkResult err) {
        if (err == 0) return;
        std::cerr << "[ImGui Vulkan Error] VkResult = " << err << std::endl;
        if (err < 0) abort();
    }

    void Console::AddLog(const char* fmt, ...) {}
    void Console::Draw(const char* title, bool* p_open) {}

    EditorUI::EditorUI() {}
    EditorUI::~EditorUI() {}

    void EditorUI::Initialize(RenderingServer* renderer, SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t queueFamilyIndex, VkRenderPass renderPass, uint32_t imageCount) {
        this->rendererRef = renderer;

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
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 

        ImGui::StyleColorsDark();

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

    void EditorUI::Prepare(Scene* scene, Camera& camera, VkDescriptorSet viewportDescriptor) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGuiIO& io = ImGui::GetIO();

        // ---------------------------------------------------------
        // 1. INPUT HANDLING (Camera)
        // ---------------------------------------------------------
        // Only move camera if right mouse button is held (standard editor control)
        if (!io.WantCaptureKeyboard || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
             if (io.MouseWheel != 0.0f) camera.Zoom(io.MouseWheel * 0.5f);
             
             if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                 camera.Rotate(io.MouseDelta.x * 0.5f, io.MouseDelta.y * 0.5f);
                 
                 float moveSpeed = 5.0f * io.DeltaTime;
                 if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) moveSpeed *= 2.0f;

                 float yawRad = glm::radians(camera.yaw);
                 glm::vec3 forwardDir = -glm::vec3(cos(yawRad), sin(yawRad), 0.0f);
                 glm::vec3 rightDir   = -glm::vec3(sin(yawRad), -cos(yawRad), 0.0f);

                 if (ImGui::IsKeyDown(ImGuiKey_W)) camera.target += forwardDir * moveSpeed;
                 if (ImGui::IsKeyDown(ImGuiKey_S)) camera.target -= forwardDir * moveSpeed;
                 if (ImGui::IsKeyDown(ImGuiKey_D)) camera.target += rightDir * moveSpeed;
                 if (ImGui::IsKeyDown(ImGuiKey_A)) camera.target -= rightDir * moveSpeed;
             }
        }

        // ---------------------------------------------------------
        // 2. DOCKSPACE & MENU
        // ---------------------------------------------------------
        ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpaceOverViewport(dockSpaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Import Model")) {
                     auto selection = pfd::open_file("Select a file", ".",
                        { "All Models", "*.obj *.gltf *.glb", "GLTF", "*.gltf *.glb", "OBJ", "*.obj" }).result();
                     if (!selection.empty() && rendererRef) {
                         std::string path = selection[0];
                         if (path.find(".gltf") != std::string::npos || path.find(".glb") != std::string::npos) {
                             rendererRef->loadGLTF(path, scene);
                         } else {
                             rendererRef->loadModel(path);
                         }
                     }
                }
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    SDL_Event quit_event; quit_event.type = SDL_QUIT; SDL_PushEvent(&quit_event);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ---------------------------------------------------------
        // 3. VIEWPORT WINDOW
        // ---------------------------------------------------------
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        ImVec2 viewportPos = ImGui::GetCursorScreenPos();
        this->lastViewportSize = glm::vec2(viewportSize.x, viewportSize.y);

        if (viewportDescriptor != VK_NULL_HANDLE) {
            ImGui::Image((ImTextureID)viewportDescriptor, viewportSize);
        } else {
            ImGui::Text("Loading Viewport...");
        }

        // --- GIZMOS ---
        if (viewportSize.x > 0 && viewportSize.y > 0 && scene) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

            glm::mat4 view = camera.GetViewMatrix();
            float aspect = viewportSize.x / viewportSize.y;
            glm::mat4 proj = camera.GetProjectionMatrix(aspect); 
            // ImGuizmo expects OpenGL style projection (Y-Up), but Vulkan is Y-Down.
            // However, your Camera class might already handle this flip. 
            // If the Gizmo is upside down, flip proj[1][1] *= -1 here.

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

        // ---------------------------------------------------------
        // 4. HIERARCHY
        // ---------------------------------------------------------
        ImGui::Begin("Scene Hierarchy");
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

        // ---------------------------------------------------------
        // 5. INSPECTOR
        // ---------------------------------------------------------
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
            }
        } else {
            ImGui::Text("Select an object to inspect.");
        }
        
        ImGui::Separator();
        ImGui::Text("Gizmo Controls");
        if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE)) mCurrentGizmoOperation = ImGuizmo::SCALE;

        ImGui::End();

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