#include "EditorUI.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "scene/Scene.hpp"
#include "servers/camera/Camera.hpp"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include <iostream>

namespace Crescendo {

    // Helper to catch ImGui Vulkan errors
    static void check_vk_result(VkResult err) {
        if (err == 0) return;
        std::cerr << "[ImGui Vulkan Error] VkResult = " << err << std::endl;
        if (err < 0) abort();
    }

    // Stub out Console for now
    void Console::AddLog(const char* fmt, ...) {}
    void Console::Draw(const char* title, bool* p_open) {}

    EditorUI::EditorUI() {}
    EditorUI::~EditorUI() {}

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
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool);

        // 2. Initialize ImGui Context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 

        ImGui::StyleColorsDark();

        // 3. Init ImGui for Vulkan
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

        // [CRITICAL] Newer ImGui structure
        init_info.PipelineInfoMain.RenderPass = renderPass; 
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT; 

        ImGui_ImplVulkan_Init(&init_info);
    }

    void EditorUI::Shutdown(VkDevice device) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }

        if (imguiPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, imguiPool, nullptr);
            imguiPool = VK_NULL_HANDLE;
        }
    }

    void EditorUI::Prepare(Scene* scene, Camera& camera, VkDescriptorSet viewportDescriptor) {
        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        // ImGuizmo::BeginFrame(); // DISABLED

        // --- MINIMAL UI ---
        ImGui::Begin("Engine Status");
        ImGui::Text("Hello! The engine is running.");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        
        if (viewportDescriptor == VK_NULL_HANDLE) {
            ImGui::TextColored(ImVec4(1,0,0,1), "Viewport Texture: NULL (Loading...)");
        } else {
            ImGui::TextColored(ImVec4(0,1,0,1), "Viewport Texture: READY");
             // Un-comment the line below ONLY if you want to test the texture crash specifically
            // ImGui::Image((ImTextureID)viewportDescriptor, ImVec2(512, 288)); 
        }

        ImGui::End();
        // ------------------

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