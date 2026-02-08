#include "EditorUI.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "scene/Scene.hpp"
#include "servers/camera/Camera.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "include/portable-file-dialogs.h"
#include "src/IO/Serializer.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp> 
#include <iostream>

namespace Crescendo {

    // --- CONSOLE IMPLEMENTATION ---
    void Console::AddLog(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        va_end(args);
        if (AutoScroll) ScrollToBottom = true;
    }

    void Console::Draw(const char* title, bool* p_open) {
        if (!ImGui::Begin(title, p_open)) { ImGui::End(); return; }
        if (ImGui::Button("Clear")) Clear();
        ImGui::Separator();
        ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(Buf.begin());
        if (ScrollToBottom) ImGui::SetScrollHereY(1.0f);
        ScrollToBottom = false;
        ImGui::EndChild();
        ImGui::End();
    }

    // --- EDITOR UI IMPLEMENTATION ---
    EditorUI::EditorUI() {}
    EditorUI::~EditorUI() {}

    void EditorUI::Initialize(RenderingServer* renderer, SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, VkRenderPass renderPass, uint32_t imageCount) {
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
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool);

        // 2. Initialize ImGui Context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        SetupStyle();

        // 3. Init ImGui for Vulkan
        ImGui_ImplSDL2_InitForVulkan(window);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = instance;
        init_info.PhysicalDevice = physicalDevice;
        init_info.Device = device;
        init_info.Queue = graphicsQueue;
        init_info.DescriptorPool = imguiPool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = imageCount;
        init_info.PipelineInfoMain.RenderPass = renderPass; 
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        
        ImGui_ImplVulkan_Init(&init_info);

        // --- MANUAL FONT UPLOAD ---
        
        // 1. Get raw pixels from ImGui
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        // 2. Upload using our helper
        renderer->UploadTexture(pixels, width, height, VK_FORMAT_R8G8B8A8_UNORM);
        
        // Create a local sampler for the font
        VkSampler fontSampler;
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        vkCreateSampler(device, &samplerInfo, nullptr, &fontSampler);
        
        // Removed LogoImageView as its being purged from entire system. legacy feat
        ImGui::GetIO().Fonts->TexID = (ImTextureID)ImGui_ImplVulkan_AddTexture(fontSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        if (vkCreateSampler(device, &samplerInfo, nullptr, &fontSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create font sampler!");
        }

        // 5. Register with ImGui (This makes the text visible)
        io.Fonts->TexID = (ImTextureID)ImGui_ImplVulkan_AddTexture(fontSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void EditorUI::SetupStyle() {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    }

    void EditorUI::Shutdown(VkDevice device) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        if (imguiPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, imguiPool, nullptr);
        }
        
    }

    void EditorUI::HandleInput(SDL_Event& event) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    void EditorUI::Draw(VkCommandBuffer cmd, Scene* scene, Camera& camera, VkDescriptorSet viewportDescriptor) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) { /* Handle Exit */ }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Viewport Window
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");
            
            ImVec2 viewportSize = ImGui::GetContentRegionAvail();
            if (viewportSize.x != lastViewportSize.x || viewportSize.y != lastViewportSize.y) {
                lastViewportSize = { viewportSize.x, viewportSize.y };
                // Here you could trigger a resize event for the Renderer if needed
            }

            // Draw the texture we rendered in Pass 1
            if (viewportDescriptor) {
                ImGui::Image((ImTextureID)viewportDescriptor, viewportSize);
            }

            // Gizmos
            if (selectedObjectIndex >= 0 && selectedObjectIndex < scene->entities.size()) {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, viewportSize.x, viewportSize.y);

                glm::mat4 view = camera.GetViewMatrix();
                glm::mat4 proj = camera.GetProjectionMatrix(viewportSize.x / viewportSize.y);
                proj[1][1] *= -1;

                CBaseEntity* ent = scene->entities[selectedObjectIndex];
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0,0,1));
                model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0,1,0));
                model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1,0,0));
                model = glm::scale(model, ent->scale);

                ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), 
                    mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(model));

                if (ImGuizmo::IsUsing()) {
                    float matrixTranslation[3], matrixRotation[3], matrixScale[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), matrixTranslation, matrixRotation, matrixScale);
                    ent->origin = glm::make_vec3(matrixTranslation);
                    ent->angles = glm::make_vec3(matrixRotation);
                    ent->scale = glm::make_vec3(matrixScale);
                }
            }
        ImGui::End();
        ImGui::PopStyleVar();

        // Hierarchy Window
        ImGui::Begin("Hierarchy");
        for (int i = 0; i < scene->entities.size(); i++) {
            std::string label = scene->entities[i]->targetName;
            if (label.empty()) label = "Entity " + std::to_string(i);
            
            if (ImGui::Selectable(label.c_str(), selectedObjectIndex == i)) {
                selectedObjectIndex = i;
            }
        }
        ImGui::End();

        // Inspector Window
        ImGui::Begin("Inspector");
        if (selectedObjectIndex >= 0 && selectedObjectIndex < scene->entities.size()) {
            CBaseEntity* ent = scene->entities[selectedObjectIndex];
            ImGui::Text("Name: %s", ent->targetName.c_str());
            ImGui::DragFloat3("Position", &ent->origin.x, 0.1f);
            ImGui::DragFloat3("Rotation", &ent->angles.x, 0.5f);
            ImGui::DragFloat3("Scale", &ent->scale.x, 0.1f);
            
            // Gizmo Controls
            if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE)) mCurrentGizmoOperation = ImGuizmo::SCALE;
        }
        ImGui::End();

        // Console Window
        gameConsole.Draw("Console");

        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
}