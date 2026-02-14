#pragma once

#include "Jolt/Core/Core.h"
#include <cstdint>
#include <vulkan/vulkan_core.h>
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

// [FIX] INCLUDE ORDER MATTERS: ImGui FIRST, then ImGuizmo
#include <imgui.h>
#include "ImGuizmo.h" 

#include "servers/camera/Camera.hpp"
#include "scene/GameWorld.hpp"
#include "scene/CarController.hpp"
#include "servers/interface/EditorUI.hpp"

namespace tinygltf { class Model; class Node; }

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

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };

    struct SwapChainSupportDetails {
       VkSurfaceCapabilitiesKHR capabilities;
       std::vector<VkSurfaceFormatKHR> formats;
       std::vector<VkPresentModeKHR> presentModes; 
    };
    
    // [SSBO] The Big Data Struct
    struct EntityData {
        glm::mat4 modelMatrix;      // 64 bytes
        glm::vec4 sphereBounds;     // 16 bytes 
        glm::vec4 albedoTint;       // 16 bytes 
        glm::vec4 pbrParams;        // 16 bytes 
        glm::vec4 volumeParams;     // 16 bytes 
        glm::vec4 volumeColor;      // 16 bytes 
    };

    // [GFD] accessible by all shaders auto
    struct GlobalUniforms {
        glm::mat4 viewProj;         // 64 bytes
        glm::mat4 view;             // 64 bytes
        glm::mat4 proj;             // 64 bytes
        glm::vec4 cameraPos;        // 16 bytes
        glm::vec4 sunDirection;     // 16 bytes
        glm::vec4 sunColor;         // 16 bytes
        glm::vec4 params;           // 16 bytes <--- THIS WAS MISSING
    };

    // [PUSH CONSTANT] For Main Objects (Tiny!)
    struct PushConsts {
        uint32_t entityIndex;   // 4 bytes
    };

    // [ADD THIS] For Skybox (Needs Matrix)
    struct SkyboxPushConsts {
        glm::mat4 invViewProj;  // 64 bytes
    };
    
    struct PostProcessPushConstants {
        float bloomIntensity;
        float exposure;
        float gamma;
        float padding; 
    };

    class RenderingServer {
    public:
        RenderingServer();
        bool initialize(DisplayServer* display);
        void shutdown();
        void render(Scene* scene); 

        // Asset Management
        int acquireMesh(const std::string& path, const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        int acquireTexture(const std::string& path);
        void UploadTexture(void* pixels, int width, int height, VkFormat format, VkImage& image, VkDeviceMemory& memory);
        
        GameWorld* GetWorld() { return &gameWorld; }

        // Pipeline Helpers
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

        // Loaders
        void loadModel(const std::string& path);
        void loadGLTF(const std::string& filePath, Scene* scene);
        // [FIX] Ensure declaration matches definition
        void processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene, glm::mat4 parentMatrix = glm::mat4(1.0f));
        void loadMaterialsFromOBJ(const std::string& baseDir, const std::vector<tinyobj::material_t>& materials);

        // Public State
        bool isPlayMode = false;
        CarController* activeCar = nullptr;
        
        struct {
            float bloomIntensity = 1.0f;
            float exposure = 1.0f;
            float gamma = 1.0f;
        } postProcessSettings;
        
        Camera mainCamera;
        GameWorld gameWorld;
        std::vector<MeshResource> meshes;
        int waterTextureID = 0;

        // [FIX] Use the Console defined in EditorUI.hpp
        Console gameConsole;

    private:
        // Core Vulkan
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        
        // --- [SSBO] RESOURCES ---
        static constexpr size_t MAX_ENTITIES = 10000;
        VkBuffer entityStorageBuffer = VK_NULL_HANDLE;
        VkDeviceMemory entityStorageBufferMemory = VK_NULL_HANDLE;
        void* entityStorageBufferMapped = nullptr;
        void createStorageBuffers(); 

        VkBuffer globalUniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory globalUniformBufferMemory = VK_NULL_HANDLE;
        void* globalUniformBufferMapped = nullptr;
        void createGlobalUniformBuffer();

        // Swapchain Resources
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;
        std::vector<VkFramebuffer> swapChainFramebuffers;

        // Command & Sync
        VkCommandPool commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> commandBuffers;        
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
        uint32_t currentFrame = 0;

        // Descriptors
        static constexpr int MAX_TEXTURES = 100;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet;

        // Pipelines
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        VkPipeline transparentPipeline = VK_NULL_HANDLE;
        VkPipeline skyPipeline = VK_NULL_HANDLE;
        VkPipeline waterPipeline = VK_NULL_HANDLE;
        
        // Post Process / Bloom
        VkPipeline bloomPipeline = VK_NULL_HANDLE;
        VkPipeline compositePipeline = VK_NULL_HANDLE;
        VkRenderPass bloomRenderPass = VK_NULL_HANDLE;
        VkRenderPass compositeRenderPass = VK_NULL_HANDLE;
        VkFramebuffer bloomFramebuffer = VK_NULL_HANDLE;
        VkFramebuffer finalFramebuffer = VK_NULL_HANDLE;
        VkDescriptorSetLayout postProcessLayout = VK_NULL_HANDLE;
        VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet compositeDescriptorSet = VK_NULL_HANDLE;

        // Images / Textures
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        
        VkImage skyImage = VK_NULL_HANDLE;
        VkDeviceMemory skyImageMemory = VK_NULL_HANDLE;
        VkImageView skyImageView = VK_NULL_HANDLE;
        VkSampler skySampler = VK_NULL_HANDLE;

        VkImage textureImage = VK_NULL_HANDLE;
        VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
        VkImageView textureImageView = VK_NULL_HANDLE;
        VkSampler textureSampler = VK_NULL_HANDLE;
        std::vector<TextureResource> textureBank;
        std::unordered_map<std::string, int> textureMap;

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
        
        VkImage bloomBrightImage = VK_NULL_HANDLE;
        VkDeviceMemory bloomBrightImageMemory = VK_NULL_HANDLE;
        VkImageView bloomBrightImageView = VK_NULL_HANDLE;
        
        VkImage finalImage = VK_NULL_HANDLE;
        VkDeviceMemory finalImageMemory = VK_NULL_HANDLE;
        VkImageView finalImageView = VK_NULL_HANDLE;

        // Editor / Tools
        DisplayServer* display_ref;
        SDL_Window* window = nullptr;
        EditorUI editorUI;         
        ResourceCache cache;
        std::vector<Material> materialBank;
        std::map<std::string, uint32_t> materialMap; 
        std::unordered_map<std::string, uint32_t> meshMap;
        
        // Validation
        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        #ifdef NDEBUG
            const bool enableValidationLayers = false;
        #else
            const bool enableValidationLayers = true;
        #endif

        // Helpers
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        bool isDeviceSuitable(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkShaderModule createShaderModule(const std::vector<char>& code);
        
        // Creation Helpers
        bool createInstance();
        bool setupDebugMessenger();
        bool createSurface();
        bool pickPhysicalDevice();
        bool createLogicalDevice();
        bool createSwapChain();
        bool createImageViews();
        bool createDepthResources();
        bool createRenderPass();
        bool createFramebuffers();
        bool createCommandPool();
        bool createCommandBuffers();
        bool createSyncObjects();
        
        bool createDescriptorSetLayout();
        bool createDescriptorPool();
        bool createDescriptorSets();
        
        bool createGraphicsPipeline();
        bool createTransparentPipeline();
        bool createWaterPipeline();
        void createWaterMesh();
        
        bool createTextureSampler();
        void createDefaultTexture();
        bool createTextureImage(const std::string& path, VkImage& image, VkDeviceMemory& memory);
        // [FIX] Add declaration
        bool createTextureImageView();
        VkImageView createTextureImageView(VkImage& image);

        bool createViewportResources();
        bool createBloomResources();
        bool createBloomPipeline();
        bool createCompositePipeline();
        bool createHDRImage(const std::string& path, VkImage& image, VkDeviceMemory& memory);
        
        void updateCompositeDescriptors();
        void recreateSwapChain(SDL_Window* window);
        void cleanupSwapChain();
        
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        
        // [FIX] Add missing declarations
        bool createVertexBuffer(const std::vector<Vertex>& vertices, VkBuffer& buffer, VkDeviceMemory& memory);
        bool createIndexBuffer(const std::vector<uint32_t>& indices, VkBuffer& buffer, VkDeviceMemory& memory);
        void updateUniformBuffer(uint32_t currentImage, Scene* scene);
    };
}