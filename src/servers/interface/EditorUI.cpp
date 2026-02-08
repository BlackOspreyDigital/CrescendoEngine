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

        // Style
        ImGui::StyleColorsDark();

        // 3. Init ImGui for Vulkan
        ImGui_ImplSDL2_InitForVulkan(window);
        
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = instance;
        init_info.PhysicalDevice = physicalDevice;
        init_info.Device = device;
        init_info.QueueFamily = 0;
        init_info.Queue = graphicsQueue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = imguiPool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = imageCount;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;

        ImGui_ImplVulkan_Init(&init_info);

        // --- MANUAL FONT UPLOAD ---
        
        // 1. Get raw pixels from ImGui
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        // 2. Upload using our helper
        VkImage fontImage;
        VkDeviceMemory fontMemory;
        
        renderer->UploadTexture(pixels, width, height, VK_FORMAT_R8G8B8A8_UNORM, fontImage, fontMemory);
        
        // 3. Create ImageView for the font
        VkImageView fontView = renderer->createImageView(fontImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

        // 4. Create Sampler
        VkSampler fontSampler;
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.minLod = -1000;
        samplerInfo.maxLod = 1000;
        samplerInfo.maxAnisotropy = 1.0f;
        
        if (vkCreateSampler(device, &samplerInfo, nullptr, &fontSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create font sampler!");
        }
        
        // 5. Register with ImGui
        io.Fonts->TexID = (ImTextureID)ImGui_ImplVulkan_AddTexture(fontSampler, fontView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void EditorUI::Shutdown(VkDevice device) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(device, imguiPool, nullptr);
    }

    // ADD Prepare: Handles Logic & State Update (Call BEFORE RenderPass)
    void EditorUI::Prepare(Scene* scene, Camera& camera, VkDescriptorSet viewportDescriptor) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        // --- UI DEFINITION LOGIC ---
        
        // 1. Dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // 2. Viewport Window
        ImGui::Begin("Viewport");
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (lastViewportSize.x != viewportPanelSize.x || lastViewportSize.y != viewportPanelSize.y) {
            lastViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
        }
        
        // Safety Check: Only draw image if descriptor is valid
        if (viewportDescriptor != VK_NULL_HANDLE) {
            ImGui::Image((ImTextureID)viewportDescriptor, viewportPanelSize);
        } else {
            ImGui::Text("Viewport Texture Not Ready");
        }

        // 3. Guizmos
        if (selectedObjectIndex >= 0 && selectedObjectIndex < scene->entities.size()) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 proj = camera.GetProjectionMatrix(viewportPanelSize.x / viewportPanelSize.y);
            proj[1][1] *= -1;

            CBaseEntity* ent = scene->entities[selectedObjectIndex];
            if (ent) {
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0, 0, 1));
                model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0, 1, 0));
                model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1, 0, 0));
                model = glm::scale(model, ent->scale);

                if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(model))) {
                    glm::vec3 skew;
                    glm::vec4 perspective;
                    glm::quat rotation;
                    glm::decompose(model, ent->scale, rotation, ent->origin, skew, perspective);
                    ent->angles = glm::degrees(glm::eulerAngles(rotation));
                }
            }
        }
        ImGui::End();

        // 4. Scene Outliner
        ImGui::Begin("Scene Outliner");
        for (size_t i = 0; i < scene->entities.size(); i++) {
            CBaseEntity* ent = scene->entities[i];
            if(!ent) continue;
            std::string label = ent->targetName.empty() ? "Entity " + std::to_string(i) : ent->targetName;
            if (ImGui::Selectable(label.c_str(), selectedObjectIndex == (int)i)) {
                selectedObjectIndex = i;
            }
        }
        ImGui::End();

        // 5. Inspector
        ImGui::Begin("Inspector");
        if (selectedObjectIndex >= 0 && selectedObjectIndex < (int)scene->entities.size()) {
            CBaseEntity* ent = scene->entities[selectedObjectIndex];
            if(ent) {
                char buffer[256];
                strncpy(buffer, ent->targetName.c_str(), sizeof(buffer));
                if(ImGui::InputText("Name", buffer, sizeof(buffer))) ent->targetName = std::string(buffer);
                ImGui::DragFloat3("Position", &ent->origin.x, 0.1f);
                ImGui::DragFloat3("Rotation", &ent->angles.x, 0.5f);
                ImGui::DragFloat3("Scale", &ent->scale.x, 0.1f);
                if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE)) mCurrentGizmoOperation = ImGuizmo::ROTATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE)) mCurrentGizmoOperation = ImGuizmo::SCALE;
            }
        }
        ImGui::End();

        // 6. Console
        gameConsole.Draw("Console");

        // End Frame Calculation
        ImGui::Render();
    }

    // ADD Render: Only Issues Draw Commands (Call INSIDE RenderPass)
    void EditorUI::Render(VkCommandBuffer cmd) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }

    void EditorUI::HandleInput(SDL_Event& event) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
}