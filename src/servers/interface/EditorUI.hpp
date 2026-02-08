#pragma once

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <imgui.h>
#include "deps/imgui/ImGuizmo.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace Crescendo {
    class RenderingServer;
    class Scene;
    class Camera;

    struct Console {
        ImGuiTextBuffer Buf;
        ImVector<int> LineOffsets;
        bool AutoScroll = true;
        bool ScrollToBottom = false;

        void Clear() { Buf.clear(); LineOffsets.clear(); LineOffsets.push_back(0); }
        void AddLog(const char* fmt, ...);
        void Draw(const char* title, bool* p_open = nullptr);
    };

    class EditorUI {
    public:
        EditorUI();
        ~EditorUI();

        // [FIX] Added queueFamilyIndex to arguments
        void Initialize(RenderingServer* renderer, SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t queueFamilyIndex, VkRenderPass renderPass, uint32_t imageCount);
        void Shutdown(VkDevice device);

        void Prepare(Scene* scene, Camera& camera, VkDescriptorSet viewportDescriptor);
        void Render(VkCommandBuffer cmd);
        
        void HandleInput(SDL_Event& event);

        // Accessors
        glm::vec2 GetViewportSize() const { return lastViewportSize; }
        Console& Getconsole() { return gameConsole; }

    private:
        RenderingServer* rendererRef = nullptr;
        VkDescriptorPool imguiPool = VK_NULL_HANDLE;

        // Editor State
        Console gameConsole;
        glm::vec2 lastViewportSize = {1280.0f, 720.0f};

        // Gizmo State
        ImGuizmo::OPERATION mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE mCurrentGizmoMode = ImGuizmo::WORLD;
        int selectedObjectIndex = -1;
        glm::vec3 cursor3DPosition = glm::vec3(0.0f);
    };
}