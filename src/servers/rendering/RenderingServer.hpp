#pragma once

#include <cstdint>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include "servers/rendering/Vertex.hpp"
#include "Material.hpp"
#include "tiny_obj_loader.h"
#include <map>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include "VulkanResources.hpp"
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include <SDL2/SDL.h>
#include <imgui.h>
#include "servers/camera/Camera.hpp"
#include "scene/GameWorld.hpp"
#include "scene/CarController.hpp"
#include "servers/interface/EditorUI.hpp"


// [VMA Forward Declarations]
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;
typedef uint32_t VmaAllocationCreateFlags;

namespace tinygltf { class Model; class Node; }

namespace Crescendo {
    class DisplayServer;
    class Scene;
    
    struct MeshResource {
        std::string name;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        uint32_t indexCount;
        uint32_t textureID; // 0 default

        // Default Constructor
        MeshResource() = default;
        MeshResource(const MeshResource&) = delete;
        MeshResource& operator=(const MeshResource&) = delete;
        MeshResource(MeshResource&& other) noexcept = default;
        MeshResource& operator=(MeshResource&& other) noexcept = default;
    };

    struct TextureResource {
        VulkanImage image;
        uint32_t id;
        TextureResource() = default;
        TextureResource(const TextureResource&) = delete;
        TextureResource& operator=(const TextureResource&) = delete;
        TextureResource(TextureResource&& other) noexcept = default;
        TextureResource& operator=(TextureResource&& other) noexcept = default;
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
    
    struct EntityData {
        glm::mat4 modelMatrix;      
        glm::vec4 sphereBounds;     
        glm::vec4 albedoTint;       
        glm::vec4 pbrParams;        
        glm::vec4 volumeParams;     
        glm::vec4 volumeColor;      
    };

    struct GlobalUniforms {
        glm::mat4 viewProj;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 cameraPos;
        glm::vec4 sunDirection;
        glm::vec4 sunColor;
        glm::vec4 params;
        glm::mat4 lightSpaceMatrices[4]; // One matrix per cascade
        glm::vec4 cascadeSplits;         // Split distances
    };

    struct PushConsts {
        uint32_t entityIndex;   
    };

    struct ShadowPushConsts {
        glm::mat4 lightVP;
        uint32_t entityIndex;
    };

    struct SkyboxPushConsts {
        glm::mat4 invViewProj;  
    };
    
    struct PostProcessPushConstants {
       float exposure;
       float gamma;
       float bloomStrength;
       float bloomThreshold;
       float blurRadius;
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
        
        // [UPDATED] Returns RAII Object
        VulkanImage UploadTexture(void* pixels, int width, int height, VkFormat format);
        
        GameWorld* GetWorld() { return &gameWorld; }

        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

        void loadModel(const std::string& path, Scene* scene);
        void loadGLTF(const std::string& filePath, Scene* scene);
        void processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene, glm::mat4 parentMatrix = glm::mat4(1.0f));
        void loadMaterialsFromOBJ(const std::string& baseDir, const std::vector<tinyobj::material_t>& materials);

        bool isPlayMode = false;
        CarController* activeCar = nullptr;
        
        PostProcessPushConstants postProcessSettings{ 
            0.0f,  // exposure
            2.2f,  // gamma
            1.0f, // bloomStrength
            1.0f   // bloomThreshold
        };
        
        Camera mainCamera;
        GameWorld gameWorld;
        std::vector<MeshResource> meshes;
        int waterTextureID = 0;
        Console gameConsole;

        // Constants
        const uint32_t SHADOW_DIM = 2048; 
        const uint32_t SHADOW_CASCADES = 4;

    private:
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;

        VmaAllocator allocator = nullptr;
        
        // --- [SSBO & UBO] RAII ---
        static constexpr size_t MAX_ENTITIES = 10000;
        VulkanBuffer entityStorageBuffer; 
        void* entityStorageBufferMapped = nullptr;
        void createStorageBuffers(); 

        VulkanBuffer globalUniformBuffer; 
        void* globalUniformBufferMapped = nullptr;
        void createGlobalUniformBuffer();

        // [UPDATED] Helpers returning RAII objects
        VulkanBuffer createVertexBuffer(const std::vector<Vertex>& vertices);
        VulkanBuffer createIndexBuffer(const std::vector<uint32_t>& indices);

        // Swapchain Resources (Keep Raw)
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
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        // Pipelines
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline graphicsPipeline = VK_NULL_HANDLE;
        VkPipeline transparentPipeline = VK_NULL_HANDLE;
        VkPipeline skyPipeline = VK_NULL_HANDLE;
        VkPipeline waterPipeline = VK_NULL_HANDLE;
        
        // Post Process
        VkPipeline bloomPipeline = VK_NULL_HANDLE;
        VkPipeline compositePipeline = VK_NULL_HANDLE;
        VkRenderPass bloomRenderPass = VK_NULL_HANDLE;
        VkRenderPass compositeRenderPass = VK_NULL_HANDLE;
        VkFramebuffer bloomFramebuffer = VK_NULL_HANDLE;
        VkFramebuffer finalFramebuffer = VK_NULL_HANDLE;
        VkDescriptorSetLayout postProcessLayout = VK_NULL_HANDLE;
        VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet compositeDescriptorSet = VK_NULL_HANDLE;

        // --- IMAGES / TEXTURES (RAII) ---
        // Note: Default views are accessed via .image.view (e.g. depthImage.view)
        
        VulkanImage depthImage; 

        VulkanImage refractionImage;
        VkImageView refractionImageView = VK_NULL_HANDLE; // Keep (Custom view)
        VkSampler refractionSampler = VK_NULL_HANDLE;
        uint32_t refractionMipLevels = 1;
                
        VulkanImage skyImage;
        VkSampler skySampler = VK_NULL_HANDLE;
        
        VulkanImage textureImage; // Default white texture
        VkSampler textureSampler = VK_NULL_HANDLE;
        
        std::vector<TextureResource> textureBank;
        std::unordered_map<std::string, int> textureMap;

        // Viewport (HDR)
        VulkanImage viewportImage;
        VkSampler viewportSampler = VK_NULL_HANDLE;
        VkFramebuffer viewportFramebuffer = VK_NULL_HANDLE;
        VkRenderPass viewportRenderPass = VK_NULL_HANDLE;
        VkDescriptorSet viewportDescriptorSet = VK_NULL_HANDLE;
        
        // Viewport Depth
        VulkanImage viewportDepthImage;
        
        // Bloom
        VulkanImage bloomBrightImage;
        
        // Final Composite (LDR)
        VulkanImage finalImage;

        DisplayServer* display_ref;
        SDL_Window* window = nullptr;
        EditorUI editorUI;         
        ResourceCache cache;
        std::vector<Material> materialBank;
        std::map<std::string, uint32_t> materialMap; 
        std::unordered_map<std::string, uint32_t> meshMap;
        
        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        #ifdef NDEBUG
            const bool enableValidationLayers = false;
        #else
            const bool enableValidationLayers = true;
        #endif

        // Internal Helpers
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        bool isDeviceSuitable(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkShaderModule createShaderModule(const std::vector<char>& code);
        
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
        bool createTextureImage();
        // You have the string version, but you need the void/bool version too:
        
        bool createTextureImage(const std::string& path, VulkanImage& outImage);

        bool createTextureSampler();         
        
        bool createViewportResources();
        bool createBloomResources();
        bool createBloomPipeline();
        bool createCompositePipeline();
        bool createHDRImage(const std::string& path, VulkanImage& outImage);
        void updateCompositeDescriptors();
        void recreateSwapChain(SDL_Window* window);
        void cleanupSwapChain();
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        void renderShadows(Scene* scene, const glm::vec3& lightDir, GlobalUniforms& globalUBO);
        
        // Frustum helper (Declaration only)
        std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
        
        void updateUniformBuffer(uint32_t currentImage, Scene* scene);

        // --- SHADOW RESOURCES ---
        // Shadows are complex; we keep the RAII image but manual views for the array layers
        VulkanImage shadowImage; 
        VkImageView shadowImageView = VK_NULL_HANDLE; // Array View (for sampling)
        std::vector<VkImageView> shadowCascadeViews;  // Individual Layer Views (for rendering)
        VkSampler shadowSampler = VK_NULL_HANDLE;
        
        VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> shadowFramebuffers; 
        VkPipeline shadowPipeline = VK_NULL_HANDLE;    
        VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

        // Transitions
        void cmdTransitionImageLayout(VkCommandBuffer cmdbuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
        
        bool createShadowResources();
        bool createShadowPipeline();
        
    };
    
}