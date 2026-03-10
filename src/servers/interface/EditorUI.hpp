#pragma once

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <imgui.h>
#include "deps/imgui/ImGuizmo.h"
#include <glm/glm.hpp>
#include "core/EngineState.hpp"
#include "servers/camera/Camera.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <vulkan/vulkan_core.h>


namespace Crescendo {
    class RenderingServer;
    class Scene;
    class SceneManager;
    class Camera;

    void UpdateEditorCamera(Crescendo::Camera& editorCam, float deltaTime);

    // --- DEV CONSOLE ---
    struct ConsoleMessage {
        std::string text;
        ImVec4 color;
    };
   
    extern std::vector<ConsoleMessage> consoleLog;
    extern std::unordered_map<std::string, float*> floatConVars;

    extern std::vector<ConsoleMessage> consoleLog;
    void AddLog(const std::string& message, ImVec4 color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

    struct Console {
        ImGuiTextBuffer     Buf;
        ImGuiTextFilter     Filter; 

        ImVector<int>       LineOffsets;
        bool                AutoScroll = true;
        bool                ScrollToBottom = false;

        void Clear() { Buf.clear(); LineOffsets.clear(); LineOffsets.push_back(0); }
        void AddLog(const char* fmt, ...);
        void Draw(const char* title, bool* p_open = nullptr);
    };

    

    class EditorUI {
    public:
        EditorUI();
        ~EditorUI();

        void Initialize(RenderingServer* renderer, SDL_Window* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t queueFamilyIndex, VkRenderPass renderPass, uint32_t imageCount);
        void Shutdown(VkDevice device);

        void Prepare(Scene* scene, SceneManager* sceneManager, Camera& camera, VkDescriptorSet viewportDescriptor, EngineState& engineState);
        void Render(VkCommandBuffer cmd);
        
        void HandleInput(SDL_Event& event);

        // Accessors
        glm::vec2 GetViewportSize() const { return lastViewportSize; }
        Console& Getconsole() { return gameConsole; }

        int GetSelectedObjectIndex() const { return selectedObjectIndex; }
        bool GetShowSelectionOutline() const { return showSelectionOutline; }

        // Managers
        SceneManager* sceneManager = nullptr;
       

    private:
        RenderingServer* rendererRef;
        
        // Descriptor Pool for ImGui
        VkDescriptorPool imguiPool = VK_NULL_HANDLE;

        struct EditorIcons {
            VkDescriptorSet folderIcon = VK_NULL_HANDLE;
            VkDescriptorSet projectFolderIcon = VK_NULL_HANDLE;
            VkDescriptorSet scriptIcon = VK_NULL_HANDLE;
            VkDescriptorSet shaderIcon = VK_NULL_HANDLE;
            VkDescriptorSet audioIcon = VK_NULL_HANDLE;
            VkDescriptorSet spatialAudioIcon = VK_NULL_HANDLE;
            VkDescriptorSet fileIcon = VK_NULL_HANDLE;
            VkDescriptorSet modelIcon = VK_NULL_HANDLE;
        };

        EditorIcons icons;

        // Toggables
        bool showSettingsWindow = false;
        bool showAboutWindow = false;
        bool showConsole = true;
        bool showSelection = true;
        bool showSelectionOutline = true;
        // Asset Browser State
        std::filesystem::path currentAssetDirectory = "assets";

        // Editor State
        Console gameConsole;
        glm::vec2 lastViewportSize = {1280.0f, 720.0f};

        // Gizmo State
        ImGuizmo::OPERATION mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE mCurrentGizmoMode = ImGuizmo::WORLD;
        
        // Selection & Cursor
        int selectedObjectIndex = -1; 
        glm::vec3 cursor3DPosition = glm::vec3(0.0f);

        // Themes
        void SetCrescendoEditorStyle();
    };
}