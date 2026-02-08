#pragma once

#include "servers/rendering/Vertex.hpp"
#include "Material.hpp"
#include "tiny_obj_loader.h"
#include <map>
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <array>
#include <glm/glm.hpp>
#include <SDL2/SDL.h>
#include <imgui.h>
#include "deps/imgui/ImGuizmo.h"
#include "servers/camera/Camera.hpp"
#include "scene/GameWorld.hpp"
#include "scene/CarController.hpp"
#include "servers/interface/EditorUI.hpp"


namespace tinygltf {
    class Model;
    class Node;
}

namespace Crescendo {
    class DisplayServer;
    class Scene;
    
    struct TextureResource {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkDescriptorSet descriptorSet;
    };

    struct MeshResource {
        std::string name;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };   
    
    struct MeshPushConstants {
        glm::mat4 renderMatrix; 
               
        glm::vec4 camPos;
        glm::vec4 pbrParams;

        glm::vec4 sunDir;
        glm::vec4 sunColor;
        
    };

    class RenderingServer {
    public:
        void render(Scene* scene); // Make sure this takes Scene*
        
        
        RenderingServer();
        bool initialize(DisplayServer* display);
        void shutdown();

        int acquireTexture(const std::string& path);
        void UploadTexture(void* pixels, int width, int height, VkFormat format, VkImage& image, VkDeviceMemory& memory);
        GameWorld* GetWorld() { return &gameWorld; }

        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);

        void loadMaterialsFromOBJ(const std::string& baseDir, const std::vector<tinyobj::material_t>& materials);
        const Material& getMaterial(uint32_t id) const { return materialBank[id]; }

        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

        // EDITOR AND GAME LOGIC
        bool isPlayMode = false;
        CarController* activeCar = nullptr;

        void loadGLTF(const std::string& filePath, Scene* scene);  
        Camera mainCamera;
        GameWorld gameWorld;
        std::vector<MeshResource> meshes;

    private:
        
        // FIX: Update this signature to match your new logic
        // changed 'int parentIndex' to 'CBaseEntity* parent'
        // changed 'std::string path' to 'std::string baseDir'
        void processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene);

        EditorUI editorUI;
         
        DisplayServer* display_ref;
        void setupUIDescriptors();
        
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
        uint32_t currentFrame = 0;
        static constexpr int MAX_TEXTURES = 100;

        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        
        int selectedObjectIndex = -1;

        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;
        std::vector<VkFramebuffer> swapChainFramebuffers;

        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;

        VkImage textureImage = VK_NULL_HANDLE;
        VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
        VkImageView textureImageView = VK_NULL_HANDLE;
        VkSampler textureSampler = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        std::vector<Material> materialBank;
        std::map<std::string, uint32_t> materialMap; 
        std::vector<TextureResource> textureBank;
        std::unordered_map<std::string, int> textureMap;
        std::unordered_map<std::string, uint32_t> meshMap;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipeline skyPipeline = VK_NULL_HANDLE;
        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline transparentPipeline = VK_NULL_HANDLE;
        bool createTransparentPipeline();
        
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;        
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;

        VkImage viewportImage = VK_NULL_HANDLE;
        VkDeviceMemory viewportImageMemory = VK_NULL_HANDLE;
        VkImageView viewportImageView = VK_NULL_HANDLE;
        VkSampler viewportSampler = VK_NULL_HANDLE;
        VkFramebuffer viewportFramebuffer = VK_NULL_HANDLE;
        VkRenderPass viewportRenderPass = VK_NULL_HANDLE;
        VkDescriptorSet viewportDescriptorSet = VK_NULL_HANDLE; // Passed to EditorUI
        

        glm::vec3 modelPos = glm::vec3(0.0f);
        glm::vec3 modelRot = glm::vec3(90.0f, 0.0f, 0.0f);
        glm::vec3 modelScale = glm::vec3(1.0f);

        

        VkPipeline gridPipeline = VK_NULL_HANDLE;
        bool createGridPipeline();

        VkPipeline waterPipeline = VK_NULL_HANDLE; 
        bool createWaterPipeline();
        int waterTextureID = -1;
        void createWaterMesh();

        bool showNodeEditor = false;
        ImVec2 nodeGridOffset = {0.0f, 0.0f}; 
        float nodeZoom = 1.0f;                
        
        glm::vec2 lastViewportSize = {1280.0f, 720.0f}; 
        bool createInstance();
        bool setupDebugMessenger();
        bool createSurface();
        bool pickPhysicalDevice();
        bool createLogicalDevice();
        bool createSwapChain();
        bool createImageViews();
        bool createDepthResources();
        bool createRenderPass();
        bool createDescriptorSetLayout();
        bool createGraphicsPipeline();
        bool createFramebuffers();
        bool createTextureImage();
        bool createTextureImageView();
        bool createTextureImage(const std::string& path, VkImage& image, VkDeviceMemory& memory);
        VkImageView createTextureImageView(VkImage& image); 
        bool createTextureSampler();
        bool createDescriptorPool();
        bool createDescriptorSets();
        bool createVertexBuffer(const std::vector<Vertex>& vertices, VkBuffer& buffer, VkDeviceMemory& memory);
        bool createIndexBuffer(const std::vector<uint32_t>& indices, VkBuffer& buffer, VkDeviceMemory& memory);
        bool createCommandPool();
        bool createCommandBuffers();
        bool createSyncObjects();
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        bool createImGuiDescriptorPool();
        bool initImGui(SDL_Window* window);
        bool createViewportResources();
        
        void updateUniformBuffer(uint32_t currentImage, Scene* scene);

        glm::vec3 sunDirection = glm::normalize(glm::vec3(1.0f, -3.0, -1.0));
        glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
        float sunIntensity = 1.2f;

        void recreateSwapChain(SDL_Window* window);
        void cleanupSwapChain();
        void loadModel(const std::string& path);
        void processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir);
        void createProceduralGrid();

        
        // ... (Console struct and other helpers remain same) ...
        struct Console{
            ImGuiTextBuffer     Buf;
            ImVector<int>       LineOffsets;
            bool                AutoScroll;
            bool                ScrollToBottom;

            Console() {
                AutoScroll = true;
                ScrollToBottom = false;
                Clear();
            }

            void Clear() {
                Buf.clear();
                LineOffsets.clear();
                LineOffsets.push_back(0);
            }

            void AddLog(const char* fmt, ...) IM_FMTARGS(2) {
                int old_size = Buf.size();
                va_list args;
                va_start(args, fmt);
                Buf.appendfv(fmt, args);
                va_end(args);
                for (int new_size = Buf.size(); old_size < new_size; old_size++)
                    if (Buf[old_size] == '\n')
                        LineOffsets.push_back(old_size + 1);
                if (AutoScroll)
                    ScrollToBottom = true;
            }

            void Draw(const char* title, bool* p_open = NULL) {
                if (!ImGui::Begin(title, p_open)) {
                    ImGui::End();
                    return;
                }

                if (ImGui::BeginPopup("Options")) {
                    ImGui::Checkbox("Auto-scroll", &AutoScroll);
                    if (ImGui::Button("Clear")) Clear ();
                    ImGui::EndPopup();
                }
                if (ImGui::Button("Options")) ImGui::OpenPopup("Options");
                ImGui::SameLine();
                if (ImGui::Button("Clear")) Clear();
                ImGui::Separator();

                const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
                ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);

                if (ImGui::BeginPopupContextWindow()) {
                    if (ImGui::Selectable("Clear")) Clear();
                    ImGui::EndPopup();
                }

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); 
                const char* buf = Buf.begin();
                const char* buf_end = Buf.end();

                ImGuiListClipper clipper;
                clipper.Begin(LineOffsets.Size);
                while (clipper.Step()) {
                    for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++) {
                        const char* line_start = buf + LineOffsets[line_no];
                        const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] -1) : buf_end;
                        ImGui::TextUnformatted(line_start, line_end);
                    }
                }
                clipper.End();
                ImGui::PopStyleVar();

                if (ScrollToBottom && (AutoScroll || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
                    ImGui::SetScrollHereY(1.0f);
                ScrollToBottom = false;

                ImGui::EndChild();
                ImGui::Separator();

                static char inputBuf[256] = "";
                ImGui::PushItemWidth(-1);
                if (ImGui::InputText("##Input", inputBuf, IM_ARRAYSIZE(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    AddLog("# command: %s\n", inputBuf);
                    strcpy(inputBuf, "");
                    ImGui::SetKeyboardFocusHere(-1);
                }
                ImGui::PopItemWidth();

                ImGui::End();
            }
        };

        Console gameConsole;

        std::string decodeUri(const std::string& uri) {
            std::string result;
            for (size_t i = 0; i < uri.length(); i++) {
                if (uri[i] == '%' && i + 2 < uri.length()) {
                    std::string hex = uri.substr(i + 1, 2);
                    char c = static_cast<char>(strtol(hex.c_str(), nullptr, 16));
                    result += c;
                    i += 2;
                } else {
                    result += uri[i];
                }
            }
            return result;
        }

        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkShaderModule createShaderModule(const std::vector<char>& code);
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
        
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        
        
        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        #ifdef NDEBUG
        const bool enableValidationLayers = false;
        #else
        const bool enableValidationLayers = true;
        #endif

        struct QueueFamilyIndices {
            std::optional<uint32_t> graphicsFamily;
            std::optional<uint32_t> presentFamily;
            bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
        };
        bool isDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

        struct SwapChainSupportDetails {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    };
}