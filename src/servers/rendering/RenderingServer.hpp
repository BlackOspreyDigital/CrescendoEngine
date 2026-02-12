#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE 

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

// [FIX] Direct include (Makefile handles path)
#include "ImGuizmo.h" 

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
    };

    struct MeshResource {
        std::string name;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
        int textureID = -1;
    };   

    struct ResourceCache {
        std::unordered_map<std::string, int32_t> textures;
        std::unordered_map<std::string, int32_t> meshes;
    };

    // Queue Indicies
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
       VkSurfaceCapabilitiesKHR capabilities;
       std::vector<VkSurfaceFormatKHR> formats;
       std::vector<VkPresentModeKHR> presentModes; // Fix: changed < to >
    };
    
    // [FIX] MASTER ALIGNMENT (192 Bytes)
    struct MeshPushConstants {
       glm::mat4 renderMatrix; // MVP (Offset 0)
       glm::mat4 modelMatrix;  // [NEW] World Space (Offset 64)
       glm::vec4 camPos;       // (Offset 128)
       glm::vec4 pbrParams;    // (Offset 144)
       glm::vec4 sunDir;       // (Offset 160)
       glm::vec4 sunColor;     // (Offset 176)
       glm::vec4 albedoTint;   // (Offset 192) - Padding/Extra
    };

    struct PostProcessPushConstants {
        float bloomIntensity;
        float exposure;
        float gamma;
        float padding; // Align to 16 bytes
    };

    class RenderingServer {
    public:
        void render(Scene* scene); 
        RenderingServer();
        bool initialize(DisplayServer* display);
        void shutdown();

        int acquireMesh(const std::string& path, const std::string& name,
                        const std::vector<Vertex>& vertices,
                        const std::vector<uint32_t>& indices);
        int acquireTexture(const std::string& path);
        void UploadTexture(void* pixels, int width, int height, VkFormat format, VkImage& image, VkDeviceMemory& memory);
        GameWorld* GetWorld() { return &gameWorld; }

        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);

        void loadMaterialsFromOBJ(const std::string& baseDir, const std::vector<tinyobj::material_t>& materials);
        const Material& getMaterial(uint32_t id) const { return materialBank[id]; }

        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

        bool isPlayMode = false;
        CarController* activeCar = nullptr;

        void loadModel(const std::string& path);
        void loadGLTF(const std::string& filePath, Scene* scene);  

        struct {
            float bloomIntensity = 1.0f;
            float exposure = 1.0f;
            float gamma = 1.0f;
        } postProcessSettings;
        
        Camera mainCamera;
        GameWorld gameWorld;
        std::vector<MeshResource> meshes;
        int waterTextureID = 0;

    private:
        ResourceCache cache;
        
        private:
            const std::vector<const char*> validationLayers = {
                "VK_LAYER_KHRONOS_validation"
            };

        #ifdef NDEBUG
            const bool enableValidationLayers = false;
        #else
            const bool enableValidationLayers = true;
        #endif
        
        EditorUI editorUI;         
        DisplayServer* display_ref;

        SDL_Window* window = nullptr;

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

        VkImage skyImage = VK_NULL_HANDLE;
        VkDeviceMemory skyImageMemory =VK_NULL_HANDLE;
        VkImageView skyImageView = VK_NULL_HANDLE;
        VkSampler skySampler = VK_NULL_HANDLE;

        VkImage textureImage = VK_NULL_HANDLE;
        VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
        VkImageView textureImageView = VK_NULL_HANDLE;
        VkSampler textureSampler = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet;

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
        VkDescriptorSet viewportDescriptorSet = VK_NULL_HANDLE;
    
        VkImage viewportDepthImage = VK_NULL_HANDLE;
        VkDeviceMemory viewportDepthImageMemory = VK_NULL_HANDLE;
        VkImageView viewportDepthImageView = VK_NULL_HANDLE;

        glm::vec3 modelPos = glm::vec3(0.0f);
        glm::vec3 modelRot = glm::vec3(90.0f, 0.0f, 0.0f);
        glm::vec3 modelScale = glm::vec3(1.0f);

        VkImage bloomBrightImage = VK_NULL_HANDLE;
        VkDeviceMemory bloomBrightImageMemory = VK_NULL_HANDLE;
        VkImageView bloomBrightImageView = VK_NULL_HANDLE;
        VkDeviceMemory bloomBrightMemory = VK_NULL_HANDLE;

        VkImage bloomBlurImage = VK_NULL_HANDLE;
        VkDeviceMemory bloomBlurImageMemory = VK_NULL_HANDLE;
        VkImageView bloomBlurImageView = VK_NULL_HANDLE;

        VkFramebuffer bloomFramebuffer = VK_NULL_HANDLE;
        VkRenderPass bloomRenderPass = VK_NULL_HANDLE;
        VkPipeline bloomPipeline = VK_NULL_HANDLE;

        VkDescriptorSet compositeDescriptorSet = VK_NULL_HANDLE;
        VkRenderPass postProcessRenderPass = VK_NULL_HANDLE; 
        VkFramebuffer postProcessFramebuffer = VK_NULL_HANDLE;

        VkImage finalImage = VK_NULL_HANDLE;
        VkDeviceMemory finalImageMemory = VK_NULL_HANDLE;
        VkImageView finalImageView = VK_NULL_HANDLE;
        VkFramebuffer finalFramebuffer = VK_NULL_HANDLE;
        
        VkRenderPass compositeRenderPass = VK_NULL_HANDLE; // RenderPass for the Post-Process Stage

        VkDescriptorSetLayout postProcessLayout = VK_NULL_HANDLE;
        VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE; 
        VkPipeline compositePipeline = VK_NULL_HANDLE;
        
        VkPipeline waterPipeline = VK_NULL_HANDLE; 
        bool createWaterPipeline();
        void createWaterMesh();

        bool showNodeEditor = false;
        ImVec2 nodeGridOffset = {0.0f, 0.0f}; 
        float nodeZoom = 1.0f;                
        
        glm::vec2 lastViewportSize = {1280.0f, 720.0f}; 

        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
            
       void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
                    
        void copyBufferToImage(VkBuffer buffer, VkImage image, 
                               uint32_t width, uint32_t height);

        bool isDeviceSuitable(VkPhysicalDevice device);
                    
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

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
        void recreateSwapChain(SDL_Window* window);
        void cleanupSwapChain();
        bool createBloomResources();
        bool createBloomPipeline();
        bool createCompositePipeline();
        void updateCompositeDescriptors();
        bool createHDRImage(const std::string& path, VkImage& image, VkDeviceMemory& memory);
        void createDefaultTexture();
        void processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene);
        

        glm::vec3 sunDirection = glm::normalize(glm::vec3(1.0f, -3.0, -1.0));
        glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
        float sunIntensity = 1.2f;

        struct Console{
            ImGuiTextBuffer     Buf;
            ImVector<int>       LineOffsets;
            bool                AutoScroll;
            bool                ScrollToBottom;
            Console() { AutoScroll = true; ScrollToBottom = false; Clear(); }
            void Clear() { Buf.clear(); LineOffsets.clear(); LineOffsets.push_back(0); }
            void AddLog(const char* fmt, ...) IM_FMTARGS(2) {
                int old_size = Buf.size();
                va_list args;
                va_start(args, fmt);
                Buf.appendfv(fmt, args);
                va_end(args);
                for (int new_size = Buf.size(); old_size < new_size; old_size++)
                    if (Buf[old_size] == '\n') LineOffsets.push_back(old_size + 1);
            }
            void Draw(const char* title, bool* p_open = NULL) { /* ... keep implementation ... */ }
        };
        Console gameConsole;
        
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkShaderModule createShaderModule(const std::vector<char>& code);
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    };
}