#include <cstddef>
#include <cstdint>


#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "deps/vk_mem_alloc.h"

#include <array>
#include <set>
#include "scene/Scene.hpp"
#include "scene/components/ProceduralPlanetComponent.hpp"
#include <cstring>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include "modules/image/ImageLoader.hpp"
#include <SDL2/SDL_vulkan.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/intersect.hpp>       
#include <glm/gtx/matrix_decompose.hpp> 
#include "backends/imgui_impl_vulkan.h"
#include "modules/terrain/TerrainManager.hpp"
#include "servers/physics/PhysicsServer.hpp"
#include <vulkan/vulkan_core.h>  
#include "servers/display/DisplayServer.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "Vertex.hpp"
#include "IO/ConfigManager.hpp"


namespace Crescendo {
    
    static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
        size_t fileSize = (size_t) file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    RenderingServer::RenderingServer() {
        // Empty! 
    }

    bool RenderingServer::initialize(DisplayServer* display) {
        this->display_ref = display;
        this->window = display->get_window(); 

        // 1. Load configuration from the conf folder!
        this->config = ConfigManager::loadConfig("conf/engine_settings.toml");

        // 2. Apply it to your existing states
        this->renderSettings.enableSSAO = this->config.enableSSAO;
        this->renderSettings.enableSSR = this ->config.enableSSR;
        
        // Map MSAA int back to Vulkan Enum
        if (this->config.msaaSamples == 2) this->msaaSamples = VK_SAMPLE_COUNT_2_BIT;
        else if (this->config.msaaSamples == 4) this->msaaSamples = VK_SAMPLE_COUNT_4_BIT;
        else if (this->config.msaaSamples == 8) this->msaaSamples = VK_SAMPLE_COUNT_8_BIT;
        else this->msaaSamples =VK_SAMPLE_COUNT_1_BIT;

        postProcessSettings.exposure = 1.0f;
        postProcessSettings.gamma = 1.0f;
        postProcessSettings.bloomStrength = 0.04f;
        postProcessSettings.bloomThreshold = 1.0f;
        postProcessSettings.blurRadius = 1.0f;

        std::cout << "[1/5] Initializing Core Vulkan..." << std::endl;
        if (!createInstance()) return false;
        if (!setupDebugMessenger()) return false;
        if (!createSurface()) return false;
        if (!pickPhysicalDevice()) return false;
        if (!createLogicalDevice()) return false;

        std::cout << "[Check 1] Creating VMA Allocator..." << std::endl;
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        vulkanFunctions.vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
        
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = device;
        allocatorInfo.instance = instance;
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3; 
        
        if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
            std::cerr << "Failed to create VMA Allocator!" << std::endl;
            return false;
        }

        std::cout << "[2/5] Setting up Command Infrastructure..." << std::endl;
        if (!createSwapChain()) return false;
        if (!createImageViews()) return false;
        if (!createRenderPass()) return false;
        if (!createCommandPool()) return false;
        if (!createDepthResources()) return false;
        if (!createTextureSampler()) return false;
        if (!createDescriptorSetLayout()) return false;
        if (!createDescriptorPool()) return false;
        createStorageBuffers(); 
        createGlobalUniformBuffer();
        if (!createShadowResources()) return false; 
        if (!createTextureImage()) return false; 
        createTextureImage("assets/textures/speakersymbol.png", speakerTexture);
        // ---------------------------------------------------------
        // 1. BUILD COMPUTE PIPELINES FIRST
        // ---------------------------------------------------------
        if (!createComputePipelines()) return false;
        if (!createTerrainComputePipelines()) return false;

        // ---------------------------------------------------------
        // 2. FIRE THE LASER (Bake the Cubemap so skyImage exists!)
        // ---------------------------------------------------------
        TextureResource skyCubemap = generateCubemapFromHDR("assets/hdr/sky_cloudy2.hdr");
        if (skyCubemap.image.handle != VK_NULL_HANDLE) {
             skyImage = std::move(skyCubemap.image);
        } else {
             std::cerr << "[Fatal] Engine could not generate sky cubemap!" << std::endl;
             return false;
        }

        // ---------------------------------------------------------
        // 3. NOW BUILD DESCRIPTORS (Because skyImage is ready to be bound)
        // ---------------------------------------------------------
        if (!createViewportResources()) return false;
        if (!createDescriptorSets()) return false; 
        
        // --- UI & Viewport ---
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        editorUI.Initialize(this, this->window, instance, physicalDevice, device, graphicsQueue, indices.graphicsFamily.value(), renderPass, static_cast<uint32_t>(swapChainImages.size()));

        if (!createBloomResources()) return false;
        if (!createSSRResources()) return false;

        viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(viewportSampler, finalImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // --- Graphics Pipelines ---
        // Pass transparentRenderPass because it perfectly supports 4x MSAA without erasing the screen!
        symbolServer.Initialize(device, transparentRenderPass, descriptorSetLayout, symbolTextureLayout);
        if (!createGraphicsPipeline()) return false;
        if (!createWaterPipeline()) return false;       
        if (!createAtmospherePipeline()) return false; 
        if (!createTransparentPipeline()) return false;
        if (!createOpaquePipeline()) return false;
        if (!createBloomPipeline()) return false;
        if (!createCompositePipeline()) return false;
        if (!createShadowPipeline()) return false;
        if (!createSSRPipeline()) return false;

        // --- Bakerline ---
        if (!createBakeRenderPass()) return false;
        if (!createBakeFramebuffer()) return false;
        if (!createBakePipeline()) return false;
        
        // --- wireframe view ---
        if (!createOutlinePipeline()) return false;
        // if (!createBillboardPipeline()) return false;
       
        if (!createFramebuffers()) return false;

        // --- Assets ---
        std::cout << "[4/5] Loading Assets..." << std::endl;
               
        std::cout << "[5/5] Finalizing Synchronization..." << std::endl;
        if (!createSyncObjects()) return false;
        if (!createCommandBuffers()) return false;

        updateCompositeDescriptors();
        updateSSRDescriptors();

        mainCamera.SetPosition(glm::vec3(0.0f, -10.0f, 5.0f)); 
        mainCamera.SetRotation(glm::vec3(25.0f, 0.0f, 0.0f)); 

        std::cout << ">>> ENGINE READY! <<<" << std::endl;
        return true;
    }

    bool RenderingServer::createInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Crescendo Engine v0.7a";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        uint32_t extensionCount = 0;
        SDL_Vulkan_GetInstanceExtensions(display_ref->get_window(), &extensionCount, nullptr);
        std::vector<const char*> extensions(extensionCount);
        SDL_Vulkan_GetInstanceExtensions(display_ref->get_window(), &extensionCount, extensions.data());
        
        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        return vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS;
    }

    bool RenderingServer::createSurface() {
        return display_ref->create_window_surface(instance, &surface) == VK_SUCCESS;
    }

    bool RenderingServer::createSwapChain() {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        
        // --- THE FIX: FORCE WAYLAND-SAFE V-SYNC ---
        // VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed safe fallback

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) return false;

        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
        return true;
    }

    bool RenderingServer::createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
            if (swapChainImageViews[i] == VK_NULL_HANDLE) return false;
        }
        return true;
    }

    bool RenderingServer::createDepthResources() {
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        
        // 1. Create the Image (RAII wrapper handles allocation and view)
        // Note: VK_IMAGE_ASPECT_DEPTH_BIT ensures the wrapper creates a depth view automatically.
        depthImage = VulkanImage(allocator, device, swapChainExtent.width, swapChainExtent.height, 
                                 depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

        // We no longer need a separate 'depthImageView' variable. 
        // We will access 'depthImage.view' directly in createFramebuffers.
        return true;
    }

    bool RenderingServer::createSSRResources() {
        uint32_t width = swapChainExtent.width;
        uint32_t height = swapChainExtent.height;

        // 1. SSR Target Image
        ssrImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_IMAGE_ASPECT_COLOR_BIT);
        transitionImageLayout(ssrImage.handle, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 2. Render Pass
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &ssrRenderPass) != VK_SUCCESS) return false;

        // 3. Framebuffer
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = ssrRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &ssrImage.view;
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        return vkCreateFramebuffer(device, &fbInfo, nullptr, &ssrFramebuffer) == VK_SUCCESS;
    }

    void RenderingServer::cmdTransitionImageLayout(VkCommandBuffer cmdbuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        // 1. Undefined -> attachment (Init/clear)
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        // 2. Attachment -> Shader Read (Post-Process Reading)
        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        // 3. Shader Read -> Attachment (Reset for next frame)
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else {
            // Fallback for other cases
            barrier.srcAccessMask = 0; // simplistic fallback
            barrier.dstAccessMask = 0;
            sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier(
            cmdbuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    }

    bool RenderingServer::createShadowResources() {
        // 1. Create Depth Image Array (4 Layers)
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = SHADOW_DIM;
        imageInfo.extent.height = SHADOW_DIM;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = SHADOW_CASCADES; // [CRITICAL] 4 Layers
        imageInfo.format = VK_FORMAT_D32_SFLOAT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        // [FIX] Manually populate the RAII wrapper members
        // The wrapper will still destroy these when it goes out of scope!
        shadowImage.allocator = allocator; 
        
        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &shadowImage.handle, &shadowImage.allocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        // 2. Create Global Sampler View (Array)
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = shadowImage.handle; // [FIX] Use .handle
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; 
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = SHADOW_CASCADES;

        if (vkCreateImageView(device, &viewInfo, nullptr, &shadowImageView) != VK_SUCCESS) return false;

        // 3. Create Individual Framebuffer Views
        shadowCascadeViews.resize(SHADOW_CASCADES);
        for (uint32_t i = 0; i < SHADOW_CASCADES; i++) {
            VkImageViewCreateInfo layerInfo = viewInfo;
            layerInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            layerInfo.subresourceRange.baseArrayLayer = i;
            layerInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &layerInfo, nullptr, &shadowCascadeViews[i]);
        }

        // 4. Create Shadow Sampler (PCF Ready)
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // Outside shadow map = lit
        samplerInfo.compareEnable = VK_TRUE; 
        samplerInfo.compareOp = VK_COMPARE_OP_GREATER; // Was LESS
        
        vkCreateSampler(device, &samplerInfo, nullptr, &shadowSampler);

        // 5. Create Render Pass (Depth Only)
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_D32_SFLOAT;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        // Synchronization Dependencies
        // [FIX] Zero-initialize the array to prevent garbage flags
        std::array<VkSubpassDependency, 2> dependencies = {};

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        vkCreateRenderPass(device, &renderPassInfo, nullptr, &shadowRenderPass);

        // 6. Create Framebuffers (One per Cascade)
        shadowFramebuffers.resize(SHADOW_CASCADES);
        for (size_t i = 0; i < SHADOW_CASCADES; i++) {
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = shadowRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &shadowCascadeViews[i];
            fbInfo.width = SHADOW_DIM;
            fbInfo.height = SHADOW_DIM;
            fbInfo.layers = 1;

            vkCreateFramebuffer(device, &fbInfo, nullptr, &shadowFramebuffers[i]);
        }
        
        return true;
        }

    void RenderingServer::calculateCascades(Scene* scene, Camera& camera, float aspectRatio, GlobalUniforms& globalData) {
        

        // 1. Read the exact distances from our TOML Config
        float nearClip = camera.nearClip;
        float farClip = this->config.shadowDistance; 
        float clipRange = farClip - nearClip;
        
        float lambda = this->config.cascadeSplitLambda; 
        
        // 2. Mathematically generate perfect split distances
        std::array<float, 5> cascadeLevels;
        for (uint32_t i = 0; i < 5; i++) {
            float p = (float)i / 4.0f; // 4 is SHADOW_CASCADES
            float logSplit = nearClip * std::pow(farClip / nearClip, p);
            float uniformSplit = nearClip + clipRange * p;
            cascadeLevels[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        }

        // Pass splits to GPU (for shader selection)
        globalData.cascadeSplits = glm::vec4(cascadeLevels[1], cascadeLevels[2], cascadeLevels[3], cascadeLevels[4]);

        glm::vec3 lightDir = glm::normalize(glm::vec3(scene->environment.sunDirection));

        for (uint32_t i = 0; i < 4; i++) {
            float nearPlane = cascadeLevels[i];
            float farPlane  = cascadeLevels[i + 1];

            glm::mat4 proj = glm::perspective(glm::radians(camera.fov), aspectRatio, nearPlane, farPlane);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 invCam = glm::inverse(proj * view);

            std::vector<glm::vec3> frustumCorners;
            for (int x = 0; x < 2; ++x) {
                for (int y = 0; y < 2; ++y) {
                    for (int z = 0; z < 2; ++z) {
                        glm::vec4 pt = invCam * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                        frustumCorners.push_back(glm::vec3(pt) / pt.w);
                    }
                }
            }

            glm::vec3 center = glm::vec3(0.0f);
            for (const auto& v : frustumCorners) {
                center += v;
            }
            center /= frustumCorners.size();

            // --- THE FIX ---
            // 1. Provide a safe "Up" vector so lookAt doesn't flip out when the sun is straight up
            glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
            if (std::abs(lightDir.z) > 0.999f) {
                up = glm::vec3(0.0f, 1.0f, 0.0f); 
            }

            // 2. Position the camera AT the sun (+lightDir) looking DOWN at the center!
            glm::mat4 lightView = glm::lookAt(center + lightDir, center, up);

            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            float minZ = std::numeric_limits<float>::max();
            float maxZ = std::numeric_limits<float>::lowest();

            for (const auto& v : frustumCorners) {
                glm::vec4 trf = lightView * glm::vec4(v, 1.0f);
                minX = std::min(minX, trf.x);
                maxX = std::max(maxX, trf.x);
                minY = std::min(minY, trf.y);
                maxY = std::max(maxY, trf.y);
                minZ = std::min(minZ, trf.z);
                maxZ = std::max(maxZ, trf.z);
            }

            // 3. Texel snapping to stabilize the shadow map edges
            float worldUnitsPerTexel = (maxX - minX) / SHADOW_DIM;
            minX = std::floor(minX / worldUnitsPerTexel) * worldUnitsPerTexel;
            maxX = std::floor(maxX / worldUnitsPerTexel) * worldUnitsPerTexel;
            minY = std::floor(minY / worldUnitsPerTexel) * worldUnitsPerTexel;
            maxY = std::floor(maxY / worldUnitsPerTexel) * worldUnitsPerTexel;

            // 4. Brute-force the light frustum depth to catch ALL shadow casters!
            // This completely fixes the shadows popping out when you look down.
            minZ -= 2000.0f;
            maxZ += 2000.0f;
            
            // Was: glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
            glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, maxZ, minZ);

            globalData.lightSpaceMatrices[i] = lightProj * lightView;
        }
    }

    VulkanImage RenderingServer::UploadTexture(void* pixels, int width, int height, VkFormat format) {
        VkDeviceSize imageSize = width * height * 4;

        // 1. Create Staging Buffer (RAII handles create & destroy)
        VulkanBuffer stagingBuffer(allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        // 2. Map & Copy
        void* data;
        vmaMapMemory(allocator, stagingBuffer.allocation, &data);
        memcpy(data, pixels, (size_t)imageSize);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        // 3. Create Target Image (RAII handles Image & View creation)
        VulkanImage newImage(allocator, device, width, height, format, 
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
                             VK_IMAGE_ASPECT_COLOR_BIT);

        // 4. Copy (Transitions & Blit)
        transitionImageLayout(newImage.handle, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer.handle, newImage.handle, width, height);
        transitionImageLayout(newImage.handle, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        return newImage; // Transfers ownership to caller
    }
    
    bool RenderingServer::createTextureImage() {
        RawImageData imgData = ImageLoader::loadStandardTexture("assets/textures/vikingemerald_default.png");
        
        // Fallback if file missing
        unsigned char fallback[4] = { 255, 0, 255, 255 }; // Magenta
        bool usedFallback = false;

        if (!imgData.pixels) {
            std::cout << "[Warning] Default texture missing. Using fallback." << std::endl;
            imgData.width = 1; 
            imgData.height = 1; 
            imgData.pixels = fallback;
            usedFallback = true;
        }

        // Send to VRAM!
        textureImage = UploadTexture(imgData.pixels, imgData.width, imgData.height, VK_FORMAT_R8G8B8A8_SRGB);

        // Only free if we actually allocated memory
        if (!usedFallback) imgData.free();

        // [CRITICAL] Resize the Bank!
        if (textureBank.size() < MAX_TEXTURES) {
            textureBank.resize(MAX_TEXTURES);
        }

        return true;
    }

    bool RenderingServer::createTextureImage(const std::string& path, VulkanImage& outImage) {
        RawImageData imgData = ImageLoader::loadStandardTexture(path);
        if (!imgData.pixels) return false;

        outImage = UploadTexture(imgData.pixels, imgData.width, imgData.height, VK_FORMAT_R8G8B8A8_UNORM);
        
        imgData.free();
        return true;
    }

    bool RenderingServer::createHDRImage(const std::string& path, VulkanImage& outImage) {
        RawImageData imgData = ImageLoader::loadHDRTexture(path);
        if (!imgData.hdrPixels) return false;

        VkDeviceSize imageSize = imgData.width * imgData.height * 4 * sizeof(float);
        
        // 1. Staging Buffer (RAII)
        VulkanBuffer staging(allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        void* mappedData;
        vmaMapMemory(allocator, staging.allocation, &mappedData);
        memcpy(mappedData, imgData.hdrPixels, static_cast<size_t>(imageSize));
        vmaUnmapMemory(allocator, staging.allocation);

        // Tell the module to free the CPU RAM
        imgData.free();

        // 2. Create Image (RAII)
        outImage = VulkanImage(allocator, device, imgData.width, imgData.height, VK_FORMAT_R32G32B32A32_SFLOAT, 
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
                               VK_IMAGE_ASPECT_COLOR_BIT);

        // 3. Copy
        transitionImageLayout(outImage.handle, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(staging.handle, outImage.handle, imgData.width, imgData.height);
        transitionImageLayout(outImage.handle, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        return true;
    }

    void RenderingServer::loadSkybox(const std::string& path, Scene* scene) {
        vkDeviceWaitIdle(device); 

        // Use the Compute GPU Baker!
        TextureResource newSky = generateCubemapFromHDR(path);
        
        if (newSky.image.handle != VK_NULL_HANDLE) {
            skyImage.destroy(); 
            skyImage = std::move(newSky.image);

           
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                VkDescriptorImageInfo skyInfo{};
                skyInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                skyInfo.imageView = skyImage.view;
                skyInfo.sampler = skySampler;

                VkWriteDescriptorSet skyWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                skyWrite.dstSet = descriptorSets[i]; // 'i' is now safely declared!
                skyWrite.dstBinding = 1;
                skyWrite.dstArrayElement = 0;
                skyWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                skyWrite.descriptorCount = 1;
                skyWrite.pImageInfo = &skyInfo;

                vkUpdateDescriptorSets(device, 1, &skyWrite, 0, nullptr);
            }
           
            
            std::cout << "[Engine] Successfully generated and loaded new HDR Cubemap: " << path << std::endl;

            // --- CPU SUN EXTRACTION ---
            if (scene) {
                glm::vec3 sunDir, sunColor;
                float sunInt;
                
                if (ImageLoader::extractHDRSunParams(path, sunDir, sunColor, sunInt)) {
                    sunInt = std::clamp(sunInt, 1.0f, 10.0f); 

                    scene->environment.sunDirection = sunDir;
                    scene->environment.sunColor = sunColor;
                    scene->environment.sunIntensity = sunInt;

                    // Update UI Entity
                    for (auto* ent : scene->entities) {
                        if (ent && ent->className == "env_sky") {
                            ent->albedoColor = sunColor;
                            ent->emission = sunInt; 
                            float pitch = std::asin(sunDir.z);
                            float yaw = std::atan2(sunDir.y, sunDir.x);
                            ent->angles = glm::degrees(glm::vec3(pitch, 0.0f, yaw));
                            break;
                        }
                    }
                    std::cout << "[Engine] HDR Sun Extracted -> Intensity: " << sunInt << std::endl;
                }
            }
        }
    }
    
    bool RenderingServer::createTextureSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        
        // Check limits
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
            return false;
        }

        // Also create the Sky Sampler while we are here (it's often the same)
        return vkCreateSampler(device, &samplerInfo, nullptr, &skySampler) == VK_SUCCESS;
    }

    bool RenderingServer::createDescriptorSetLayout() {
        // =========================================================
        // 1. MAIN SCENE LAYOUT (Set 0)
        // =========================================================
        
        // Create a vector of the SAME sampler repeated 100 times
        std::vector<VkSampler> immutableSamplers(MAX_TEXTURES, textureSampler);

        // Binding 0: Textures
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorCount = MAX_TEXTURES; // 100
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = immutableSamplers.data(); 
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 1: Skybox
        VkDescriptorSetLayoutBinding skyLayoutBinding{};
        skyLayoutBinding.binding = 1;
        skyLayoutBinding.descriptorCount = 1; 
        skyLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skyLayoutBinding.pImmutableSamplers = nullptr;
        skyLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 2: SSBO (Entity Data)
        VkDescriptorSetLayoutBinding ssboBinding{};
        ssboBinding.binding = 2;
        ssboBinding.descriptorCount = 1;
        ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssboBinding.pImmutableSamplers = nullptr;
        ssboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 3: Global Uniforms
        VkDescriptorSetLayoutBinding globalBinding{};
        globalBinding.binding = 3;
        globalBinding.descriptorCount = 1;
        globalBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        globalBinding.pImmutableSamplers = nullptr;

        // Binding 4: Shadow Map (Array)
        VkDescriptorSetLayoutBinding shadowBinding{};
        shadowBinding.binding = 4;
        shadowBinding.descriptorCount = 1;
        shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowBinding.pImmutableSamplers = nullptr;
        shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 5: Refraction Map
        VkDescriptorSetLayoutBinding refractionBinding{};
        refractionBinding.binding = 5;
        refractionBinding.descriptorCount = 1;
        refractionBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        refractionBinding.pImmutableSamplers = nullptr;
        refractionBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 6: Irradiance Map (Ambient Diffuse)
        VkDescriptorSetLayoutBinding irradianceBinding{};
        irradianceBinding.binding = 6;
        irradianceBinding.descriptorCount = 1;
        irradianceBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        irradianceBinding.pImmutableSamplers = nullptr;
        irradianceBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 7: Prefilter Map (Ambient Specular)
        VkDescriptorSetLayoutBinding prefilterBinding{};
        prefilterBinding.binding = 7;
        prefilterBinding.descriptorCount = 1;
        prefilterBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prefilterBinding.pImmutableSamplers = nullptr;
        prefilterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 8: BRDF LUT (Fresnel Look-Up Table)
        VkDescriptorSetLayoutBinding brdfBinding;
        brdfBinding.binding = 8;
        brdfBinding.descriptorCount = 1;
        brdfBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        brdfBinding.pImmutableSamplers = nullptr;
        brdfBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 9: Scene Depth Map
        VkDescriptorSetLayoutBinding depthBinding{};
        depthBinding.binding = 9;
        depthBinding.descriptorCount = 1;
        depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depthBinding.pImmutableSamplers = nullptr;
        depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;


        globalBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        
        std::array<VkDescriptorSetLayoutBinding, 10> bindings = { 
            samplerLayoutBinding, skyLayoutBinding, ssboBinding, globalBinding, 
            shadowBinding, refractionBinding, irradianceBinding, prefilterBinding, brdfBinding, depthBinding 
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            return false;
        }

        // =========================================================
        // 2. POST-PROCESS LAYOUT (Set 1)
        // =========================================================
        
        VkDescriptorSetLayoutBinding postBinding0{};
        postBinding0.binding = 0;
        postBinding0.descriptorCount = 1;
        postBinding0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postBinding0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding postBinding1{};
        postBinding1.binding = 1;
        postBinding1.descriptorCount = 1;
        postBinding1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postBinding1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        
        VkDescriptorSetLayoutBinding postBinding2{};
        postBinding2.binding = 2;
        postBinding2.descriptorCount = 1;
        postBinding2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postBinding2.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Make sure all 3 are in the array!
        std::array<VkDescriptorSetLayoutBinding, 3> postBindings = { postBinding0, postBinding1, postBinding2 };

        VkDescriptorSetLayoutCreateInfo postLayoutInfo{};
        postLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        postLayoutInfo.bindingCount = static_cast<uint32_t>(postBindings.size());
        postLayoutInfo.pBindings = postBindings.data();

        if (vkCreateDescriptorSetLayout(device, &postLayoutInfo, nullptr, &postProcessLayout) != VK_SUCCESS) {
            return false;
        }

        // =========================================================
        // 3. SSR LAYOUT (Set 2)
        // =========================================================

        std::array<VkDescriptorSetLayoutBinding, 3> ssrBindings{};

        // Binding 0: Scene Color
        ssrBindings[0].binding = 0;
        ssrBindings[0].descriptorCount = 1;
        ssrBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ssrBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 1: Normal + Roughness
        ssrBindings[1].binding = 1;
        ssrBindings[1].descriptorCount =1;
        ssrBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ssrBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 2: Depth Buffer
        ssrBindings[2].binding = 2;
        ssrBindings[2].descriptorCount =1;
        ssrBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ssrBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ssrLayoutInfo{};
        ssrLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ssrLayoutInfo.bindingCount = static_cast<uint32_t>(ssrBindings.size());
        ssrLayoutInfo.pBindings = ssrBindings.data();

        // --- SYMBOL TEXTURE LAYOUT ---
        VkDescriptorSetLayoutBinding symbolBinding{};
        symbolBinding.binding = 0;
        symbolBinding.descriptorCount = 1;
        symbolBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        symbolBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo symbolLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        symbolLayoutInfo.bindingCount = 1;
        symbolLayoutInfo.pBindings = &symbolBinding;
        vkCreateDescriptorSetLayout(device, &symbolLayoutInfo, nullptr, &symbolTextureLayout);
        
        if (vkCreateDescriptorSetLayout(device, &ssrLayoutInfo, nullptr, &ssrDescriptorLayout) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool RenderingServer::createDescriptorPool() {
       // UPGRADED to 4 to include Compute Storage Images
       std::array<VkDescriptorPoolSize, 4> poolSizes{}; 
       
       // 1. Uniform Buffers (Global UBOs)
       poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
       poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 10);

       // 2. Combined Image Samplers (Textures)
       poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * (MAX_TEXTURES + 10) + 100); 

       // 3. Storage Buffers (Entity Data)
       poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
       poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 5);

       // 4. STORAGE IMAGES (Compute Shader IBL Bakers)
       poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
       poolSizes[3].descriptorCount = 10; 

       VkDescriptorPoolCreateInfo poolInfo{};
       poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
       poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
       poolInfo.pPoolSizes = poolSizes.data();
       poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 5 + 50); 
       poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

       if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
           return false;
       }
       return true;
    }

    bool RenderingServer::createDescriptorSets() {
        // 1. Allocate Main Scene Sets (One per frame in flight)
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) return false;

        // -----------------------------------------------------------
        // PREPARE DESCRIPTOR WRITES FOR EVERY FRAME
        // -----------------------------------------------------------
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
            for (int j = 0; j < MAX_TEXTURES; j++) {
                imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos[j].sampler = VK_NULL_HANDLE;
                if (j < textureBank.size() && textureBank[j].image.handle != VK_NULL_HANDLE) {
                    imageInfos[j].imageView = textureBank[j].image.view; 
                } else {
                    imageInfos[j].imageView = textureImage.view; 
                }
            }

            VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            descriptorWrite.dstSet = descriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = static_cast<uint32_t>(MAX_TEXTURES);
            descriptorWrite.pImageInfo = imageInfos.data();

            VkDescriptorImageInfo skyImageInfo{};
            skyImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            skyImageInfo.imageView = skyImage.view; 
            skyImageInfo.sampler = skySampler;

            VkWriteDescriptorSet skyWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            skyWrite.dstSet = descriptorSets[i];
            skyWrite.dstBinding = 1;
            skyWrite.dstArrayElement = 0;
            skyWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            skyWrite.descriptorCount = 1;
            skyWrite.pImageInfo = &skyImageInfo;

            // [CRITICAL] Bind this frame's specific SSBO!
            VkDescriptorBufferInfo ssboInfo{};
            ssboInfo.buffer = entityStorageBuffers[i].handle;
            ssboInfo.offset = 0;
            ssboInfo.range = sizeof(EntityData) * MAX_ENTITIES;

            VkWriteDescriptorSet ssboWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            ssboWrite.dstSet = descriptorSets[i];
            ssboWrite.dstBinding = 2;
            ssboWrite.dstArrayElement = 0;
            ssboWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ssboWrite.descriptorCount = 1;
            ssboWrite.pBufferInfo = &ssboInfo;

            // [CRITICAL] Bind this frame's specific UBO!
            VkDescriptorBufferInfo globalInfo{};
            globalInfo.buffer = globalUniformBuffers[i].handle;
            globalInfo.offset = 0;
            globalInfo.range = sizeof(GlobalUniforms);

            VkWriteDescriptorSet globalWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            globalWrite.dstSet = descriptorSets[i];
            globalWrite.dstBinding = 3;
            globalWrite.dstArrayElement = 0;
            globalWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            globalWrite.descriptorCount = 1;
            globalWrite.pBufferInfo = &globalInfo;

            VkDescriptorImageInfo shadowInfo{};
            shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            shadowInfo.imageView = shadowImageView;
            shadowInfo.sampler = shadowSampler;

            VkWriteDescriptorSet shadowWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            shadowWrite.dstSet = descriptorSets[i];
            shadowWrite.dstBinding = 4;
            shadowWrite.dstArrayElement = 0;
            shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            shadowWrite.descriptorCount = 1;
            shadowWrite.pImageInfo = &shadowInfo;

            VkDescriptorImageInfo refInfo{};
            refInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            refInfo.imageView = refractionImageView;
            refInfo.sampler = refractionSampler;

            VkWriteDescriptorSet refWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            refWrite.dstSet = descriptorSets[i];
            refWrite.dstBinding = 5;
            refWrite.dstArrayElement = 0;
            refWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            refWrite.descriptorCount = 1;
            refWrite.pImageInfo = &refInfo;

            // STUB 6 & 7
            VkWriteDescriptorSet irradianceWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            irradianceWrite.dstSet = descriptorSets[i];
            irradianceWrite.dstBinding = 6;
            irradianceWrite.dstArrayElement = 0;
            irradianceWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            irradianceWrite.descriptorCount = 1;
            irradianceWrite.pImageInfo = &skyImageInfo; // placeholder

            VkWriteDescriptorSet prefilterWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            prefilterWrite.dstSet = descriptorSets[i];
            prefilterWrite.dstBinding = 7;
            prefilterWrite.dstArrayElement = 0;
            prefilterWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            prefilterWrite.descriptorCount = 1;
            prefilterWrite.pImageInfo = &skyImageInfo; // placeholder
            
            // Stub 8
            VkDescriptorImageInfo brdfInfo{};
            brdfInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            brdfInfo.imageView = textureImage.view; // Default texture fallback
            brdfInfo.sampler = textureSampler;

            VkWriteDescriptorSet brdfWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            brdfWrite.dstSet = descriptorSets[i];
            brdfWrite.dstBinding = 8;
            brdfWrite.dstArrayElement = 0;
            brdfWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            brdfWrite.descriptorCount = 1;
            brdfWrite.pImageInfo = &brdfInfo;

            // 9 Depth map
            VkDescriptorImageInfo depthInfo{};
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthInfo.imageView = viewportDepthImage.view; 
            depthInfo.sampler = viewportSampler;

            VkWriteDescriptorSet depthWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            depthWrite.dstSet = descriptorSets[i];
            depthWrite.dstBinding = 9;
            depthWrite.dstArrayElement = 0;
            depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            depthWrite.descriptorCount = 1;
            depthWrite.pImageInfo = &depthInfo;

            // Update the array size to 10 and add depthWrite to the end
            std::array<VkWriteDescriptorSet, 10> writes = {
                descriptorWrite, skyWrite, ssboWrite, globalWrite, shadowWrite, refWrite,
                irradianceWrite, prefilterWrite, brdfWrite, depthWrite
            };
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            
        } // End of the frames-in-flight loop

        // -----------------------------------------------------------
        // UPDATE SET 1 (Post Process)
        // -----------------------------------------------------------
        VkDescriptorSetAllocateInfo postAlloc{};
        postAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        postAlloc.descriptorPool = descriptorPool;
        postAlloc.descriptorSetCount = 1;
        postAlloc.pSetLayouts = &postProcessLayout;

        if (vkAllocateDescriptorSets(device, &postAlloc, &compositeDescriptorSet) != VK_SUCCESS) return false;

        // -----------------------------------------------------------
        // SSR ALLOCATE (Set 2) --- MOVED HERE!
        // -----------------------------------------------------------
        VkDescriptorSetAllocateInfo ssrAlloc{};
        ssrAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ssrAlloc.descriptorPool = descriptorPool;
        ssrAlloc.descriptorSetCount = 1;
        ssrAlloc.pSetLayouts = &ssrDescriptorLayout;

        if (vkAllocateDescriptorSets(device, &ssrAlloc, &ssrDescriptorSet) != VK_SUCCESS) return false;

        // --- SYMBOL DESCRIPTOR SET ---
        VkDescriptorSetAllocateInfo symbolAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        symbolAlloc.descriptorPool = descriptorPool;
        symbolAlloc.descriptorSetCount = 1;
        symbolAlloc.pSetLayouts = &symbolTextureLayout;
        vkAllocateDescriptorSets(device, &symbolAlloc, &symbolTextureSet);

        VkDescriptorImageInfo symbolInfo{};
        symbolInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        symbolInfo.imageView = speakerTexture.view;
        symbolInfo.sampler = textureSampler;

        VkWriteDescriptorSet symbolWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        symbolWrite.dstSet = symbolTextureSet;
        symbolWrite.dstBinding = 0;
        symbolWrite.dstArrayElement = 0;
        symbolWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        symbolWrite.descriptorCount = 1;
        symbolWrite.pImageInfo = &symbolInfo;
        vkUpdateDescriptorSets(device, 1, &symbolWrite, 0, nullptr);


        return true;
    }

    int RenderingServer::acquireMesh(const std::string& path, const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
        // 1. Check cache first (unless it's a dynamic procedural mesh)
        if (path != "PROCEDURAL" && cache.meshes.find(path) != cache.meshes.end()) {
            return cache.meshes[path];
        }

        // 2. Create the new MeshResource
        MeshResource newMesh;
        newMesh.name = name;
        newMesh.indexCount = static_cast<uint32_t>(indices.size());
        
        // 3. Send the raw vertices/indices to the Vulkan GPU buffers
        newMesh.vertexBuffer = createVertexBuffer(vertices);
        newMesh.indexBuffer = createIndexBuffer(indices);
        newMesh.textureID = 0; // Default white texture

        // 4. Store it in the engine's master list
        int meshID = static_cast<int>(meshes.size());
        meshes.push_back(std::move(newMesh));
        
        // 5. Cache it so we don't upload the same file twice
        if (path != "PROCEDURAL") {
            cache.meshes[path] = meshID;
            meshMap[path] = meshID;
        }

        return meshID;
    }

    int RenderingServer::acquireTexture(const std::string& path) {
        if (cache.textures.find(path) != cache.textures.end()) {
            return cache.textures[path];
        }

        int newID = static_cast<int>(textureMap.size()) + 1;
        if (newID >= MAX_TEXTURES) return 0;

        // 1. DELEGATED TO MODULE: The server doesn't know what a PNG is.
        // It just asks the ImageLoader for a struct of raw pixels.
        RawImageData imgData = ImageLoader::loadStandardTexture(path);
        if (!imgData.pixels) return 0;

        // 2. HARDWARE UPLOAD: Push the raw bytes to VRAM
        TextureResource newTex;
        newTex.image = UploadTexture(imgData.pixels, imgData.width, imgData.height, VK_FORMAT_R8G8B8A8_SRGB);
        
        // 3. CLEANUP: Tell the module to free the CPU RAM now that the GPU has it
        imgData.free();

        // 4. SERVER CACHING & DESCRIPTORS (Remains exactly the same!)
        if (newID < textureBank.size()) {
            textureBank[newID] = std::move(newTex);
        }

        cache.textures[path] = newID;
        textureMap[path] = newID;

        // Immediate Descriptor Update
        if (!descriptorSets.empty()) {
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = textureBank[newID].image.view;
                imageInfo.sampler = textureSampler;

                VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                descriptorWrite.dstSet = descriptorSets[i]; 
                descriptorWrite.dstBinding = 0;
                descriptorWrite.dstArrayElement = newID;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfo;

                vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
            }
        }

        return newID;
    }

    VkDescriptorSet RenderingServer::getImGuiTextureID(const std::string& path) {
        int id = acquireTexture(path);
        // Ensure the texture was loaded and exists in the bank
        if (id > 0 && id < textureBank.size() && textureBank[id].image.view != VK_NULL_HANDLE) {
            return ImGui_ImplVulkan_AddTexture(textureSampler, textureBank[id].image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        return VK_NULL_HANDLE;
    }

    //===============================================
    // PIPELINE LOGIC
    //===============================================

    bool RenderingServer::createGraphicsPipeline() {
        auto vertShaderCode = readFile("assets/shaders/shader.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/shader.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
        
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f; viewport.y = 0.0f;
        viewport.width = (float) swapChainExtent.width;
        viewport.height = (float) swapChainExtent.height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        
        
        // This ensures the model draws even if the winding order is inverted.
        rasterizer.cullMode = VK_CULL_MODE_NONE; 
        
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = msaaSamples;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE; 
        depthStencil.depthWriteEnable = VK_FALSE; 
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // Was LESS_OR_EQUAL

        // Infinity sky trick
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
        
        // Attachment 0: Scene Color
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[0].blendEnable = VK_TRUE;
        colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

        // Attachment 1: Normal G-Buffer (Clone Attachment 0 to bypass the independentBlend rule!)
        colorBlendAttachments[1] = colorBlendAttachments[0]; 

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 2; 
        colorBlending.pAttachments = colorBlendAttachments;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        
        // [FIX] Change this from sizeof(PushConsts) to 128.
        // The Skybox uses this same layout and needs 64 bytes.
        // If this is too small, the Skybox crashes and the Entity ID gets corrupted.
        pushConstant.size = 128; 
        
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; 
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
        
        // --- ONLY CREATE IF IT DOESN'T EXIST YET ---
        if (pipelineLayout == VK_NULL_HANDLE) {
            if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) return false;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = viewportRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) return false;

        auto skyVertCode = readFile("assets/shaders/sky.vert.spv");
        auto skyFragCode = readFile("assets/shaders/sky.frag.spv");
        VkShaderModule skyVertShaderModule = createShaderModule(skyVertCode);
        VkShaderModule skyFragShaderModule = createShaderModule(skyFragCode);
        
        VkPipelineShaderStageCreateInfo skyVertStageInfo{};
        skyVertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        skyVertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        skyVertStageInfo.module = skyVertShaderModule;
        skyVertStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo skyFragStageInfo{};
        skyFragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        skyFragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        skyFragStageInfo.module = skyFragShaderModule;
        skyFragStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo skyShaderStages[] = {skyVertStageInfo, skyFragStageInfo};

        rasterizer.cullMode = VK_CULL_MODE_NONE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthTestEnable = VK_FALSE; 
        VkPipelineVertexInputStateCreateInfo emptyInputState{};
        emptyInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = skyShaderStages;
        pipelineInfo.pVertexInputState = &emptyInputState; 
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create sky pipeline");
        }
        vkDestroyShaderModule(device, skyVertShaderModule, nullptr);
        vkDestroyShaderModule(device, skyFragShaderModule, nullptr);

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return true;
    }

    bool RenderingServer::createTransparentPipeline() {
        auto vertShaderCode = readFile("assets/shaders/shader.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/transparent.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        // ... (Shader Stages Setup remains the same) ...
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        // ... (Vertex Input Setup) ...
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f; viewport.y = 0.0f;
        viewport.width = (float)swapChainExtent.width;
        viewport.height = (float)swapChainExtent.height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; 
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = msaaSamples;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // Was LESS_OR_EQUAL

        VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
        
        // Attachment 0: Scene Color
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[0].blendEnable = VK_TRUE;
        colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

        // Attachment 1: Normal G-Buffer (Clone Attachment 0 to bypass the independentBlend rule!)
        colorBlendAttachments[1] = colorBlendAttachments[0]; 

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 2; 
        colorBlending.pAttachments = colorBlendAttachments;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout; // Uses the main layout
        pipelineInfo.renderPass = viewportRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &transparentPipeline) != VK_SUCCESS) {
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return true;
    }

    bool RenderingServer::createOpaquePipeline() {
        // --- 1. SHADER MODULES ---
        auto vertShaderCode = readFile("assets/shaders/shader.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/opaque.frag.spv"); 
        
        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        // --- 2. VERTEX INPUT ---
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapChainExtent};

        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // --- 3. RASTERIZER ---
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; 
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        // --- 4. MULTISAMPLING & DEPTH ---
        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = msaaSamples;

        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE; 
        depthStencil.depthWriteEnable = VK_TRUE; 
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // Was LESS_OR_EQUAL

        // --- 5. COLOR BLENDING ---
        VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[0].blendEnable = VK_FALSE; 

        colorBlendAttachments[1] = colorBlendAttachments[0]; // Normal G-Buffer

        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 2;
        colorBlending.pAttachments = colorBlendAttachments;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // --- 6. PIPELINE CREATION ---
        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout; 
        pipelineInfo.renderPass = viewportRenderPass; 
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &opaquePipeline) != VK_SUCCESS) {
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        
        return true;
    }

    bool RenderingServer::createWaterPipeline() {
        auto vertShaderCode = readFile("assets/shaders/water.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/water.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f; viewport.y = 0.0f;
        viewport.width = (float)swapChainExtent.width;
        viewport.height = (float)swapChainExtent.height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; 
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = msaaSamples;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE; 
        depthStencil.depthWriteEnable = VK_FALSE; 
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

       VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
        
        // Attachment 0: Scene Color
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[0].blendEnable = VK_TRUE;
        colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

        // Attachment 1: Normal G-Buffer (Clone Attachment 0 to bypass the independentBlend rule!)
        colorBlendAttachments[1] = colorBlendAttachments[0]; 

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 2; 
        colorBlending.pAttachments = colorBlendAttachments;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = viewportRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &waterPipeline) != VK_SUCCESS) {
             return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return true;
    }

    bool RenderingServer::createAtmospherePipeline() {
        auto vertShaderCode = readFile("assets/shaders/atmosphere.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/atmosphere.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo shaderStages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr}
        };

        // 1. THE VERTEX FIX: Tell Vulkan how to read your 3D Vertex structure!
        // The Atmosphere MUST have standard 3D vertex bindings!
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapChainExtent};

        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // 2. THE CULLING: Render both the inside and outside of the sphere
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; 
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = msaaSamples; 

        // 3. THE DEPTH: Test against the planet, but don't overwrite it
        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE; 
        depthStencil.depthWriteEnable = VK_FALSE; 
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        // 4. THE BLENDING: Pure Additive Blending (One + One)
        VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};

        // Primary Color Buffer
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[0].blendEnable = VK_TRUE;
        colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

        // G-Buffer Normal (We shouldn't write normals for transparent air)
        colorBlendAttachments[1] = colorBlendAttachments[0];
        

        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 2;
        colorBlending.pAttachments = colorBlendAttachments;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, (uint32_t)dynamicStates.size(), dynamicStates.data()};

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout; 
        pipelineInfo.renderPass = viewportRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &atmospherePipeline) != VK_SUCCESS) {
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return true;
    }

    bool RenderingServer::createCompositePipeline() {
        auto vertShaderCode = readFile("assets/shaders/fullscreen_vert.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/bloom_composite.frag.spv");
        
        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
        
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr};
        
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f};
        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT};
        
        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE; 

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        
        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, (uint32_t)dynamicStates.size(), dynamicStates.data()};
        
        if (compositePipelineLayout == VK_NULL_HANDLE) {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(PostProcessPushConstants);

            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;

            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &compositePipelineLayout) != VK_SUCCESS) {
                return false;
            }
        }
    
        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = compositePipelineLayout;
        pipelineInfo.renderPass = compositeRenderPass; 
        pipelineInfo.subpass = 0;
    
        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compositePipeline);
        
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
    
        return result == VK_SUCCESS;
    }

    bool RenderingServer::createShadowPipeline() {
        auto vertShaderCode = readFile("assets/shaders/shadow.vert.spv");
        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";
        
        // No Fragment Shader needed for Depth-Only pass!
        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo };
        
        // Vertex Input (We only need Position for shadows to save bandwidth)
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        
        // Use only Position (Location 0) for shadows if possible, but reusing standard layout is easier
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        
        // Viewport & Scissor (Dynamic)
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        
        // Rasterizer (Depth Bias is CRITICAL for Shadows)
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_TRUE; // Clamp depth to 0-1 range
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Render both sides for robust shadows
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_TRUE; 
        rasterizer.depthBiasConstantFactor = 1.25f; // Tweak these if you see acne
        rasterizer.depthBiasSlopeFactor = 1.75f;
        
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        
        // Depth Stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // Was LESS_OR_EQUAL
        
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS // Allow dynamic bias tweaking
        };
    
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
    
        // Push Constants for LightVP and Entity ID
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // Only Vertex needed
        pushConstant.offset = 0;
        pushConstant.size = sizeof(ShadowPushConsts); // 64 (mat4) + 4 (uint)
    
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    
        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) return false;
    
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 1; // Only Vertex Stage!
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = nullptr; // No color attachment
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = shadowPipelineLayout;
        pipelineInfo.renderPass = shadowRenderPass;
        pipelineInfo.subpass = 0;
    
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline) != VK_SUCCESS) return false;
    
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        return true;
    }

    bool RenderingServer::createSSRPipeline() {
        auto vertShaderCode = readFile("assets/shaders/fullscreen_vert.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/ssr.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};
        
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT};
        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, (uint32_t)dynamicStates.size(), dynamicStates.data()};

        // Layout & Push Constants
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SSRPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &ssrDescriptorLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &ssrPipelineLayout);

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = ssrPipelineLayout;
        pipelineInfo.renderPass = ssrRenderPass;
        pipelineInfo.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &ssrPipeline);

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return result == VK_SUCCESS;
    }

    bool RenderingServer::createBloomPipeline() {
        auto vertShaderCode = readFile("assets/shaders/fullscreen_vert.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/bloom_bright.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};
        
        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT};
        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, (uint32_t)dynamicStates.size(), dynamicStates.data()};

        // [FIX] Ensure Layout has Push Constants (even if unused by Bloom)
        // This makes it compatible with the Composite Pass which DOES use them.
        if (compositePipelineLayout == VK_NULL_HANDLE) {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(PostProcessPushConstants);

            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &postProcessLayout;
            layoutInfo.pushConstantRangeCount = 1; 
            layoutInfo.pPushConstantRanges = &pushConstantRange;

            if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &compositePipelineLayout) != VK_SUCCESS) {
                return false;
            }
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = compositePipelineLayout;
        pipelineInfo.renderPass = bloomRenderPass;
        pipelineInfo.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bloomPipeline);

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return result == VK_SUCCESS;
    }

    bool RenderingServer::createBakePipeline() {
        //1. Load the compiled SPIR-V shaders
        auto vertShaderCode = readFile("assets/shaders/bake.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/bake.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        // 2. Vertex Input ( Must match vertex struct)
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // 3. Viewport and Scissor (Locked to our Lightmap Size)
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)LIGHTMAP_SIZE;
        viewport.height = (float)LIGHTMAP_SIZE;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {LIGHTMAP_SIZE, LIGHTMAP_SIZE};

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // 4. Rasterizer (CRITICAL: Disable culling for baking!)
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Capture everything
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 5. Dual Color Blending (One for Position, One for Normal)
        // We disable blending completely because we just want to overwrite the pixels with raw data.
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        // We have TWO attachments in our bake render pass
        VkPipelineColorBlendAttachmentState blendAttachments[] = { colorBlendAttachment, colorBlendAttachment };

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 2;
        colorBlending.pAttachments = blendAttachments;

        // 6. Push Constants (To pass the modelMatrix)
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0; // No textures/descriptors needed for the bake pass!
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &bakePipelineLayout) != VK_SUCCESS) {
            std::cerr << "Failed to create bake pipeline layout!" << std::endl;
            return false;
        }

        // 7. Depth Testing Disabled
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE; 
        depthStencil.depthWriteEnable = VK_FALSE;

        // 8. Build the actual Pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = bakePipelineLayout;
        pipelineInfo.renderPass = bakeRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &bakePipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create bake graphics pipeline!" << std::endl;
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return true;
    }

    bool RenderingServer::createComputePipelines() {
        // 1. Create the Descriptor Set Layout
        // Binding 0: Sampler2D (Input)
        VkDescriptorSetLayoutBinding inputBinding{};
        inputBinding.binding = 0;
        inputBinding.descriptorCount = 1;
        inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        inputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        // Binding 1: ImageCube (Output/Storage)
        VkDescriptorSetLayoutBinding outputBinding{};
        outputBinding.binding = 1;
        outputBinding.descriptorCount = 1;
        outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = { inputBinding, outputBinding };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorLayout) != VK_SUCCESS) return false;

        // 2. Create the Pipeline Layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &computeDescriptorLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) return false;

        // 3. Load the Shader & Build the Pipeline
        auto compShaderCode = readFile("assets/shaders/equirect2cube.comp.spv");
        VkShaderModule compShaderModule = createShaderModule(compShaderCode);

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = computePipelineLayout;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = compShaderModule;
        pipelineInfo.stage.pName = "main";

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &equirectToCubePipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create Compute Pipeline!" << std::endl;
            return false;
        }

        vkDestroyShaderModule(device, compShaderModule, nullptr);
        return true;
    }

    bool RenderingServer::createTerrainComputePipelines() {
        // 1. Descriptor Set Layout
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &terrainComputeDescriptorLayout) != VK_SUCCESS) return false;
        
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(TerrainComputePush);
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &terrainComputeDescriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
        
        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &terrainComputePipelineLayout) != VK_SUCCESS) return false;
        
        // 2. Load the Compute Shaders
        auto densityCode = readFile("assets/shaders/terrain_density.comp.spv");
        auto marchingCode = readFile("assets/shaders/terrain_marching_cubes.comp.spv");
        
        VkShaderModule densityModule = createShaderModule(densityCode);
        VkShaderModule marchingModule = createShaderModule(marchingCode);
        
        // Setup Density Pipeline
        VkPipelineShaderStageCreateInfo densityStage{};
        densityStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        densityStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        densityStage.module = densityModule;
        densityStage.pName = "main";
        
        VkComputePipelineCreateInfo computeInfo{};
        computeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computeInfo.layout = terrainComputePipelineLayout;
        computeInfo.stage = densityStage;
        
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &densityComputePipeline);
        
        // Swap to Marching Cubes Pipeline
        VkPipelineShaderStageCreateInfo marchingStage{};
        marchingStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        marchingStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        marchingStage.module = marchingModule;
        marchingStage.pName = "main";
        
        computeInfo.stage = marchingStage;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &marchingCubesComputePipeline);
        
        vkDestroyShaderModule(device, densityModule, nullptr);
        vkDestroyShaderModule(device, marchingModule, nullptr);
        
        // 3. Allocate the VRAM Workspace (SSBOs)
        const VkDeviceSize DENSITY_SIZE = 33 * 33 * 33 * sizeof(float);
        const VkDeviceSize MAX_VERTS_SIZE = 50 * 1024 * 1024;
        const VkDeviceSize MAX_INDICES_SIZE = 10 * 1024 * 1024;
        const VkDeviceSize COUNTER_SIZE = 2 * sizeof(uint32_t);

        // (Your existing GPU-Only allocations)
        densityBuffer = VulkanBuffer(allocator, DENSITY_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        computeVertexBuffer = VulkanBuffer(allocator, MAX_VERTS_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        computeIndexBuffer = VulkanBuffer(allocator, MAX_INDICES_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        // --- NEW: VMA 3.0 HOST-VISIBLE STAGING BUFFERS (FOR JOLT PHYSICS) ---
        VkBufferCreateInfo sVertInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        sVertInfo.size = MAX_VERTS_SIZE;
        sVertInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo sAlloc{};
        sAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        sAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT; 
        
        // [FIX] Give the RAII wrappers the allocator so they know how to destroy themselves!
        stagingVertBuffer.allocator = allocator;
        stagingIndexBuffer.allocator = allocator;
        counterBuffer.allocator = allocator;

        vmaCreateBuffer(allocator, &sVertInfo, &sAlloc, &stagingVertBuffer.handle, &stagingVertBuffer.allocation, nullptr);
        
        

        VkBufferCreateInfo sIndInfo = sVertInfo;
        sIndInfo.size = MAX_INDICES_SIZE;
        vmaCreateBuffer(allocator, &sIndInfo, &sAlloc, &stagingIndexBuffer.handle, &stagingIndexBuffer.allocation, nullptr);
        
        VkBufferCreateInfo counterBufInfo{};
        counterBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        counterBufInfo.size = COUNTER_SIZE;
        counterBufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo counterAllocInfo{};
        counterAllocInfo.usage = VMA_MEMORY_USAGE_AUTO; 
        counterAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT; 

        vmaCreateBuffer(allocator, &counterBufInfo, &counterAllocInfo, &counterBuffer.handle, &counterBuffer.allocation, nullptr);
        
        // 4. Bind the Buffers to the Descriptor Set
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &terrainComputeDescriptorLayout;
        
        vkAllocateDescriptorSets(device, &allocInfo, &terrainComputeDescriptorSet);
        
        VkDescriptorBufferInfo dInfo{};
        dInfo.buffer = densityBuffer.handle;
        dInfo.offset = 0;
        dInfo.range = VK_WHOLE_SIZE;
        
        VkDescriptorBufferInfo vInfo{};
        vInfo.buffer = computeVertexBuffer.handle;
        vInfo.offset = 0;
        vInfo.range = VK_WHOLE_SIZE;
        
        VkDescriptorBufferInfo iInfo{};
        iInfo.buffer = computeIndexBuffer.handle;
        iInfo.offset = 0;
        iInfo.range = VK_WHOLE_SIZE;
        
        VkDescriptorBufferInfo cInfo{};
        cInfo.buffer = counterBuffer.handle;
        cInfo.offset = 0;
        cInfo.range = VK_WHOLE_SIZE;
        
        std::array<VkWriteDescriptorSet, 4> writes{};
        
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = terrainComputeDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &dInfo;
        
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = terrainComputeDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &vInfo;
        
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = terrainComputeDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &iInfo;
        
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = terrainComputeDescriptorSet;
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &cInfo;
        
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        
        return true;
    }

    bool RenderingServer::createOutlinePipeline() {
        auto vertShaderCode = readFile("assets/shaders/shader.vert.spv"); 
        auto fragShaderCode = readFile("assets/shaders/outline.frag.spv"); 
        
        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo shaderStages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr}
        };

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE; // Force Wireframe
        rasterizer.lineWidth = 1.0f; 
        rasterizer.cullMode = VK_CULL_MODE_NONE;       // Draw backfaces too for a technical look
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        
        rasterizer.depthBiasEnable = VK_TRUE;          // Pull the wireframe toward the camera
        rasterizer.depthBiasConstantFactor = -2.0f;
        rasterizer.depthBiasSlopeFactor = -2.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = msaaSamples;

        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_FALSE; 
        depthStencil.depthWriteEnable = VK_FALSE; // No need to write depth for lines
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // Was LESS_OR_EQUAL

        VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[0].blendEnable = VK_FALSE; 
        colorBlendAttachments[1] = colorBlendAttachments[0]; 

        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 2;
        colorBlending.pAttachments = colorBlendAttachments;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, (uint32_t)dynamicStates.size(), dynamicStates.data()};

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout; 
        pipelineInfo.renderPass = viewportRenderPass; 
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outlinePipeline) != VK_SUCCESS) {
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        
        return true;
    }

    bool RenderingServer::createSkyPipeline() {
        auto vertShaderCode = readFile("assets/shaders/sky.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/sky.frag.spv");
            
        // The Skybox MUST have an empty vertex input!
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        // (No binding descriptions here!)
        return true;
    }

    void RenderingServer::generateChunkGPU(VkCommandBuffer cmd, const TerrainComputePush& pushData) {
        // 1. Reset the vertex/index counters to 0
        vkCmdFillBuffer(cmd, counterBuffer.handle, 0, 2 * sizeof(uint32_t), 0);

        // Barrier: Wait for the fill to finish before the shader tries to write to it
        VkBufferMemoryBarrier fillBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        fillBarrier.buffer = counterBuffer.handle;
        fillBarrier.offset = 0; fillBarrier.size = VK_WHOLE_SIZE;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &fillBarrier, 0, nullptr);

        // 2. Bind the Descriptor Set
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, terrainComputePipelineLayout, 0, 1, &terrainComputeDescriptorSet, 0, nullptr);

        // 3. PASS 1: Density Generation
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, densityComputePipeline);
        vkCmdPushConstants(cmd, terrainComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TerrainComputePush), &pushData);
        
        // Dispatch (Resolution / 8 because our shader uses local_size = 8)
        uint32_t groups = (pushData.resolution / 8) + 1;
        vkCmdDispatch(cmd, groups, groups, groups);

        // Barrier: Wait for Density Pass to finish before Marching Cubes starts
        VkBufferMemoryBarrier densityBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        densityBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        densityBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        densityBarrier.buffer = densityBuffer.handle;
        densityBarrier.offset = 0; densityBarrier.size = VK_WHOLE_SIZE;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &densityBarrier, 0, nullptr);

        // 4. PASS 2: Marching Cubes
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, marchingCubesComputePipeline);
        vkCmdDispatch(cmd, groups, groups, groups);

        // Barrier: Ensure vertices are fully written before the Graphics queue tries to draw them
        std::array<VkBufferMemoryBarrier, 2> geomBarriers{};
        geomBarriers[0] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, computeVertexBuffer.handle, 0, VK_WHOLE_SIZE};
        geomBarriers[1] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, computeIndexBuffer.handle, 0, VK_WHOLE_SIZE};
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 2, geomBarriers.data(), 0, nullptr);
    }

    ChunkBakeResult RenderingServer::buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) {
        ChunkBakeResult result;
        VkCommandPool localPool;
        
        // 1. DISPATCH
        VkCommandBuffer cmd = beginAsyncCommands(localPool);
        // Plug the GPU memory into the shader! 
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, terrainComputePipelineLayout, 0, 1, &terrainComputeDescriptorSet, 0, nullptr);
                
        // Pass 1: Density (Padded to 33x33x33 to prevent boundary walls)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, densityComputePipeline);
        vkCmdPushConstants(cmd, terrainComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TerrainComputePush), &pushData);
        vkCmdDispatch(cmd, 5, 5, 5); // <--- CHANGED THIS TO 5!
        
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        
        // Pass 2: Marching Cubes (Still 32x32x32 cubes)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, marchingCubesComputePipeline);
        vkCmdPushConstants(cmd, terrainComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TerrainComputePush), &pushData);
        vkCmdDispatch(cmd, 4, 4, 4); // <--- CHANGED TO 4 (Same as 32 / 8)
        
        endAsyncCommands(cmd, localPool);
        
        // 2. Read back vertex/index counts
        uint32_t* counters;
        vmaMapMemory(allocator, counterBuffer.allocation, (void**)&counters);
        uint32_t vertexCount = counters[0];
        uint32_t indexCount = counters[1];
        vmaUnmapMemory(allocator, counterBuffer.allocation);

        // Safe memory overflow
        // Clamp the vertices so we never write past the end of the staging buffer!
        const uint32_t MAX_SAFE_VERTICES = 60000; 
        if (vertexCount > MAX_SAFE_VERTICES) vertexCount = MAX_SAFE_VERTICES;
        if (indexCount > MAX_SAFE_VERTICES * 3) indexCount = MAX_SAFE_VERTICES * 3;
        // ----------------------------------------------

        if (vertexCount == 0 || indexCount == 0) {
            // Still need to reset counters if the chunk was empty air
            cmd = beginAsyncCommands(localPool);

            vkCmdFillBuffer(cmd, counterBuffer.handle, 0, 8, 0);

            endAsyncCommands(cmd, localPool);
            return result;
        }

        // 3. Allocate permanent VRAM (For Graphics)
        VkDeviceSize vertSize = vertexCount * sizeof(Vertex);
        VkDeviceSize indSize = indexCount * sizeof(uint32_t);

        MeshResource newMesh;
        newMesh.name = "GPU_Chunk";
        newMesh.indexCount = indexCount;
        newMesh.textureID = 0;
        newMesh.vertexBuffer = VulkanBuffer(allocator, vertSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        newMesh.indexBuffer = VulkanBuffer(allocator, indSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        // Exact-Size Dynamic Staging Buffers
        VkBuffer tempVertBuf = VK_NULL_HANDLE, tempIndBuf = VK_NULL_HANDLE;
        VmaAllocation tempVertAlloc = VK_NULL_HANDLE, tempIndAlloc = VK_NULL_HANDLE;

        if (needsCollision) {
            // Create raw VMA buffers matched EXACTLY to the vertSize and indSize
            VkBufferCreateInfo vInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            vInfo.size = vertSize; vInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VkBufferCreateInfo iInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            iInfo.size = indSize; iInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

            vmaCreateBuffer(allocator, &vInfo, &allocInfo, &tempVertBuf, &tempVertAlloc, nullptr);
            vmaCreateBuffer(allocator, &iInfo, &allocInfo, &tempIndBuf, &tempIndAlloc, nullptr);
        }
        // ---------------------------------------------------------

        // 4. Data Transfer AND Counter Reset
        cmd = beginAsyncCommands(localPool);

        // Copy the geometry to GPU memory
        VkBufferCopy vCopy{0, 0, vertSize}, iCopy{0, 0, indSize};
        vkCmdCopyBuffer(cmd, computeVertexBuffer.handle, newMesh.vertexBuffer.handle, 1, &vCopy);
        vkCmdCopyBuffer(cmd, computeIndexBuffer.handle, newMesh.indexBuffer.handle, 1, &iCopy);

        if (needsCollision) {
            // Copy to our exactly-sized temporary staging buffers
            vkCmdCopyBuffer(cmd, computeVertexBuffer.handle, tempVertBuf, 1, &vCopy);
            vkCmdCopyBuffer(cmd, computeIndexBuffer.handle, tempIndBuf, 1, &iCopy);
        }

        // Reset counters to prevent CPU stall
        vkCmdFillBuffer(cmd, counterBuffer.handle, 0, 8, 0);
        endAsyncCommands(cmd, localPool);

        // 5. Data Extraction
        if (needsCollision) {
            const int STRIDE = sizeof(Vertex) / sizeof(float);
            float* rawVerts;
            vmaMapMemory(allocator, tempVertAlloc, (void**)&rawVerts);
            result.collisionVerts.assign(rawVerts, rawVerts + (vertexCount * STRIDE));
            vmaUnmapMemory(allocator, tempVertAlloc);
        
            uint32_t* rawInds;
            vmaMapMemory(allocator, tempIndAlloc, (void**)&rawInds);
            result.collisionIndices.assign(rawInds, rawInds + indexCount);
            vmaUnmapMemory(allocator, tempIndAlloc);

            // --- CLEANUP: Destroy the temporary buffers immediately! ---
            vmaDestroyBuffer(allocator, tempVertBuf, tempVertAlloc);
            vmaDestroyBuffer(allocator, tempIndBuf, tempIndAlloc);
        }

        result.generatedMesh = std::move(newMesh);
        result.hasMesh = true;
        return result;
    }
   
    //===============================================
    // RENDER STAGING
    //===============================================

    bool RenderingServer::createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = VK_FORMAT_D32_SFLOAT;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        
        return vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) == VK_SUCCESS;
    }

    bool RenderingServer::createFramebuffers() {
        swapChainFramebuffers.resize(swapChainImageViews.size());

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            std::array<VkImageView, 2> attachments = {
                swapChainImageViews[i],
                depthImage.view
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    bool RenderingServer::createCommandPool() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        // 1. The Main Render Command Pool
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            return false;
        }

        // 2. The Background Async Command Pool
        VkCommandPoolCreateInfo asyncPoolInfo{};
        asyncPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        asyncPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        asyncPoolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        if (vkCreateCommandPool(device, &asyncPoolInfo, nullptr, &asyncCommandPool) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool RenderingServer::createCommandBuffers() {
        commandBuffers.resize(swapChainFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;        
        allocInfo.commandPool = commandPool;    
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;    
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();
        
        return vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) == VK_SUCCESS;
    }

    bool RenderingServer::createSyncObjects() {
        uint32_t imageCount = static_cast<uint32_t>(swapChainImages.size());

        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        
        imageAvailableSemaphores.resize(imageCount);
        renderFinishedSemaphores.resize(imageCount);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) return false;
        }

        for (size_t i = 0; i < imageCount; i++) {
            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }
    
    void RenderingServer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    void RenderingServer::copyBufferAsync(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        // Create a local pool specifically for this memory copy
        VkCommandPool localPool;
        VkCommandBuffer commandBuffer = beginAsyncCommands(localPool);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        // Queue mutex to safely submit, then it destroys the local pool!
        endAsyncCommands(commandBuffer, localPool);
    }

    VulkanBuffer RenderingServer::createVertexBuffer(const std::vector<Vertex>& vertices) {
        VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();
        
        // Staging (RAII)
        VulkanBuffer staging(allocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        
        void* data;
        vmaMapMemory(allocator, staging.allocation, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vmaUnmapMemory(allocator, staging.allocation);

        // GPU Buffer (RAII)
        VulkanBuffer buffer(allocator, bufferSize, 
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
                            0);

        copyBuffer(staging.handle, buffer.handle, bufferSize);
        return buffer;
    }

    VulkanBuffer RenderingServer::createIndexBuffer(const std::vector<uint32_t>& indices) {
        VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();
        
        VulkanBuffer staging(allocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        
        void* data;
        vmaMapMemory(allocator, staging.allocation, &data);
        memcpy(data, indices.data(), (size_t)bufferSize);
        vmaUnmapMemory(allocator, staging.allocation);

        VulkanBuffer buffer(allocator, bufferSize, 
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
                            0);

        copyBuffer(staging.handle, buffer.handle, bufferSize);
        return buffer;
    }

    void RenderingServer::updateUniformBuffer(uint32_t currentImage, Scene* scene) {
    }

    void RenderingServer::createGlobalUniformBuffer() {
        VkDeviceSize bufferSize = sizeof(GlobalUniforms);
        globalUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        globalUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            globalUniformBuffers[i] = VulkanBuffer(allocator, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            if (vmaMapMemory(allocator, globalUniformBuffers[i].allocation, &globalUniformBuffersMapped[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to map Global Uniform Buffer memory!");
            }
        }
    }

    void RenderingServer::createStorageBuffers() {
        VkDeviceSize bufferSize = sizeof(EntityData) * MAX_ENTITIES;
        entityStorageBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        entityStorageBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            entityStorageBuffers[i] = VulkanBuffer(allocator, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            if (vmaMapMemory(allocator, entityStorageBuffers[i].allocation, &entityStorageBuffersMapped[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to map Entity Storage Buffer memory!");
            }
        }
    }

    // --------------------------------------------------------------------
    // TOOLING (OFFSCREEN)
    // --------------------------------------------------------------------

    bool RenderingServer::createBakeRenderPass() {
        VkAttachmentDescription attachments[2] = {};

        // Attachment 0: World Position (32-bit floats)
        attachments[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Attachment 1: World Normal (32-bit floats)
        attachments[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference posRef{};
        posRef.attachment = 0;
        posRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference normRef{};
        normRef.attachment = 1;
        normRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReferences[2] = { posRef, normRef };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colorReferences;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &bakeRenderPass) != VK_SUCCESS) {
            std::cerr << "Failed to create bake render pass!" << std::endl;
            return false;
        }
        return true;
    }

    bool RenderingServer::createBakeFramebuffer() {
    
        // 1. The usage flags: Render to it (Attachment) and Read from it later (Sampled)
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        // 2. Allocate Images and Views in ONE line using your custom RAII constructor!
        positionBakeImage = VulkanImage(allocator, device, LIGHTMAP_SIZE, LIGHTMAP_SIZE, 
                                        VK_FORMAT_R32G32B32A32_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT);

        normalBakeImage = VulkanImage(allocator, device, LIGHTMAP_SIZE, LIGHTMAP_SIZE, 
                                      VK_FORMAT_R32G32B32A32_SFLOAT, usage, VK_IMAGE_ASPECT_COLOR_BIT);

        // 3. Tie them together into the Framebuffer
        // Notice we access the view using the `.view` property of your wrapper
        VkImageView attachments[2] = { positionBakeImage.view, normalBakeImage.view };

        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = bakeRenderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = attachments; // This fixes the 'undeclared identifier' typo too!
        fbInfo.width = LIGHTMAP_SIZE;
        fbInfo.height = LIGHTMAP_SIZE;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &bakeFramebuffer) != VK_SUCCESS) {
            std::cerr << "Failed to create Bake Framebuffer!" << std::endl;
            return false;
        }

        return true;
    }

    VkCommandBuffer RenderingServer::beginAsyncCommands(VkCommandPool& outLocalPool) {
        // 1. Create a dedicated pool JUST for this specific background thread
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; 
        
        // Grab the queue family index dynamically so it doesn't crash!
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        vkCreateCommandPool(device, &poolInfo, nullptr, &outLocalPool);

        // 2. Allocate the buffer from the new local pool
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = outLocalPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void RenderingServer::endAsyncCommands(VkCommandBuffer commandBuffer, VkCommandPool localPool) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence;
        vkCreateFence(device, &fenceInfo, nullptr, &fence);

        {
            // Traffic Light: Protect the actual Queue submission
            std::lock_guard<std::mutex> lock(queueMutex);
            vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence); 
        }

        // Wait for the background GPU work to finish
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);

        // Clean up the local pool (which automatically frees the command buffer!)
        vkDestroyCommandPool(device, localPool, nullptr);
    }

    // --------------------------------------------------------------------
    // Render() / THE RENDER LOOP
    // --------------------------------------------------------------------

    void RenderingServer::render(Scene* scene, SceneManager* sceneManager, EngineState& engineState) {
        if (!scene) return;
                    
        // --- SAFELY REBUILD PIPELINES BEFORE THE FRAME STARTS ---
        if (msaaNeedsRebuild) {
            SetMSAASamples(pendingMsaaSamples);
            msaaNeedsRebuild = false;
        }
            
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(window); return; }
        
        // Pass the state reference to the UI!
        editorUI.Prepare(scene, sceneManager, mainCamera, viewportDescriptorSet, engineState);
        
        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        // ---------------------------------------------------------
        // PHASE 0: UPLOAD ENTITY DATA TO GPU (SSBO)
        // ---------------------------------------------------------
        EntityData* gpuData = (EntityData*)entityStorageBuffersMapped[currentFrame];
        int entityCount = 0;

        // Map used to link an Entity Pointer to its GPU Index
        std::map<CBaseEntity*, uint32_t> entityGPUIndices;

        // Get 64-Bit Camera
        glm::dvec3 cameraWorldPos = mainCamera.Position;

        for (auto* ent : scene->entities) {
            if (!ent || entityCount >= MAX_ENTITIES) continue;

            EntityData& data = gpuData[entityCount];

            // 1. Calculate the difference in 64-bit double space
            glm::dvec3 entityWorldPos = ent->origin;
            glm::dvec3 relativePos = entityWorldPos - cameraWorldPos;

            // 2. Cast it down to 32-bit float using the glm::vec3 constructor!
            glm::vec3 renderPos = glm::vec3(relativePos);

            // 3. Upload to GPU
            data.pos = glm::vec4(renderPos, 1.0f);
            data.rot   = glm::vec4(glm::radians(ent->angles), 0.0f);
            data.scale = glm::vec4(ent->scale, 1.0f);

            // Material & Volume logic remains the same...
            int texID = (ent->textureID > 0) ? ent->textureID : 0;
            if (texID == 0 && ent->modelIndex < meshes.size() && meshes[ent->modelIndex].textureID > 0) {
                texID = meshes[ent->modelIndex].textureID;
            }
        
            data.albedoTint   = glm::vec4(ent->albedoColor, (float)texID);
            data.sphereBounds = glm::vec4(0.0f); // Placeholder if you aren't using culling yet
            data.pbrParams    = glm::vec4(ent->roughness, ent->metallic, ent->emission, ent->normalStrength);
            data.volumeParams = glm::vec4(ent->transmission, ent->thickness, ent->attenuationDistance, ent->ior);
            data.volumeColor  = glm::vec4(ent->attenuationColor, 0.0f);
            data.volumeParams = glm::vec4(ent->transmission, ent->thickness, ent->attenuationDistance, ent->ior);
            data.volumeColor  = glm::vec4(ent->attenuationColor, (float)ent->normalTextureID); 
            data.advancedPbr = glm::vec4(ent->clearcoat, ent->clearcoatRoughness, ent->sheen, (float)ent->ormTextureID);       
            data.extendedPbr = glm::vec4(ent->subsurface, ent->specular, ent->specularTint, ent->anisotropic);
            data.padding1 = glm::vec4(0.0f);
            data.padding2 = glm::vec4(0.0f);

            entityGPUIndices[ent] = entityCount;
            entityCount++;
        }

        // ---------------------------------------------------------
        // RENDER COMMANDS
        // ---------------------------------------------------------

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

        float aspectRatio = 1.0f;
        glm::vec2 viewportSize = editorUI.GetViewportSize();
        if (viewportSize.x > 0 && viewportSize.y > 0) aspectRatio = viewportSize.x / viewportSize.y;
        
        // <--- 3. STRIP TRANSLATION SO CAMERA IS AT (0,0,0) IN RENDER SPACE --->
        glm::mat4 view = mainCamera.GetViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(mainCamera.fov), aspectRatio, mainCamera.farClip, mainCamera.nearClip);
        proj[1][1] *= -1.0f; // The Vulkan Y-flip
        
        glm::mat4 vp = proj * view;

        // 2. Sun Logic (Grab defaults from EditorUI/Scene Environment)
        // strip this for the new refactor 
        glm::vec3 sunDirection = glm::normalize(scene->environment.sunDirection);
        glm::vec3 sunColor = scene->environment.sunColor;
        float sunIntensity = scene->environment.sunIntensity;
        
        // Look for our environment entity to sync settings
        for (auto* ent : scene->entities) {
            if (ent && ent->className == "env_sky") {
                // Sync GI Colors
                scene->environment.skyColor = ent->albedoColor; 
                scene->environment.groundColor = ent->attenuationColor; 
                
                // Update Sun from this entity's rotation
                glm::mat4 rotMat = glm::mat4_cast(glm::quat(glm::radians(ent->angles)));
                sunDirection = glm::normalize(glm::vec3(rotMat * glm::vec4(0, 0, 1, 0)));
                scene->environment.sunDirection = sunDirection; 
                break;
            }
        }

        // =========================================================
        // 2. UPDATE GLOBAL UNIFORMS (GUB)
        // =========================================================
        GlobalUniforms globalData{};
        globalData.viewProj = vp;
        globalData.view = view;
        globalData.proj = proj;
        // Cast to vec3 before packing it into the vec4
        globalData.cameraPos = glm::vec4(glm::vec3(mainCamera.Position), 1.0f);
        
        // Now the sliders will perfectly push into the shader!
        globalData.sunDirection = glm::vec4(sunDirection, sunIntensity);
        globalData.sunColor = glm::vec4(sunColor, 1.0f);
        // Explicitly cast the strongly-typed enum to a float for the shader packing!
        float skyTypeFloat = static_cast<float>(scene->environment.skyType);
        globalData.params = glm::vec4(SDL_GetTicks() / 1000.0f, skyTypeFloat, viewportSize.x, viewportSize.y);
        globalData.fogColor = scene->environment.fogColor;
        if (!scene->environment.enableFog) {
            globalData.fogColor.w = 0.0f; // Force density to exactly 0 to turn it off!
        }
        globalData.fogParams   = scene->environment.fogParams;
        globalData.skyColor    = glm::vec4(scene->environment.skyColor, 1.0f);
        globalData.groundColor = glm::vec4(scene->environment.groundColor, 1.0f);
        
        // --- EXTRACT POINT LIGHTS ---
        globalData.pointLightParams.x = 0; // Reset count
        for (auto* ent : scene->entities) {
            if (ent && ent->className == "light_point" && globalData.pointLightParams.x < 16) {
                int idx = globalData.pointLightParams.x; // Current array index
                globalData.pointLights[idx].positionAndRadius = glm::vec4(ent->origin, ent->scale.x);
                globalData.pointLights[idx].colorAndIntensity = glm::vec4(ent->albedoColor, ent->emission);
                globalData.pointLightParams.x++;
            }
        }

        calculateCascades(scene, mainCamera, aspectRatio, globalData);

        // Upload to GPU (Binding 3)
        memcpy(globalUniformBuffersMapped[currentFrame], &globalData, sizeof(GlobalUniforms));

        // ---> 2. THE PASTED ENTITY SORTING BLOCK <---
        std::vector<CBaseEntity*> opaqueList;
        std::vector<std::pair<float, CBaseEntity*>> transPairs;
        // Cast to vec3 for the sorting distance checks
        glm::vec3 camPos = glm::vec3(mainCamera.Position);

        for (auto* ent : scene->entities) {
            if (!ent || ent->modelIndex >= meshes.size() || ent->className == "prop_water") continue;
            if (ent->transmission > 0.0f) {
                // Cast the entity origin down to a 32-bit float just for the distance check
                glm::vec3 entPosFloat = glm::vec3(ent->origin);
                float distSq = glm::dot(entPosFloat - camPos, entPosFloat - camPos);
                transPairs.push_back({distSq, ent});
            } else {
                opaqueList.push_back(ent);
            }
        }
        std::sort(transPairs.begin(), transPairs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<CBaseEntity*> transparentList;
        for (auto& p : transPairs) transparentList.push_back(p.second);

        // =========================================================
        // PASS 0: CASCADED SHADOW MAPS (Depth Only)
        // =========================================================
        vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
        vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

        // Loop through all 4 shadow slices
        for (uint32_t i = 0; i < SHADOW_CASCADES; i++) {
            VkRenderPassBeginInfo shadowPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            shadowPassInfo.renderPass = shadowRenderPass;
            shadowPassInfo.framebuffer = shadowFramebuffers[i];
            shadowPassInfo.renderArea.extent = {SHADOW_DIM, SHADOW_DIM};
            
            VkClearValue clearDepth;
            clearDepth.depthStencil = {0.0f};
            shadowPassInfo.clearValueCount = 1;
            shadowPassInfo.pClearValues = &clearDepth;

            vkCmdBeginRenderPass(commandBuffers[currentFrame], &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{0.0f, 0.0f, (float)SHADOW_DIM, (float)SHADOW_DIM, 0.0f, 1.0f};
            vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);
            
            VkRect2D scissor{{0, 0}, {SHADOW_DIM, SHADOW_DIM}};
            vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);

            // Lower the multiplier so the UI sliders don't rip the shadows off the models
            float scaledConstantBias = scene->environment.shadowBiasConstant * 100.0f;
            
            vkCmdSetDepthBias(commandBuffers[currentFrame], scaledConstantBias, 0.0f, scene->environment.shadowBiasSlope);

            // Draw all OPAQUE entities into the shadow map
            for (auto* ent : opaqueList) {
                MeshResource& mesh = meshes[ent->modelIndex];
                if (mesh.vertexBuffer.handle == VK_NULL_HANDLE) continue;

                VkBuffer vBuffers[] = { mesh.vertexBuffer.handle };
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
                                    
                ShadowPushConsts push{};
                push.lightSpaceMatrix = globalData.lightSpaceMatrices[i]; // The specific math for this slice!
                push.entityIndex = entityGPUIndices[ent];
                
                vkCmdPushConstants(commandBuffers[currentFrame], shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConsts), &push);
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
            }

            vkCmdEndRenderPass(commandBuffers[currentFrame]);
        }

        // =========================================================
        // PASS 1: OFFSCREEN SCENE (HDR) -> viewportFramebuffer
        // =========================================================
        VkRenderPassBeginInfo viewportPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        viewportPassInfo.renderPass = viewportRenderPass;
        viewportPassInfo.framebuffer = viewportFramebuffer;
        viewportPassInfo.renderArea.extent = swapChainExtent;

        bool useMSAA = msaaSamples != VK_SAMPLE_COUNT_1_BIT;
        std::vector<VkClearValue> clearValues;

        if (useMSAA) {
            clearValues.resize(6);
            // Changed from 0.1f to 0.0f
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};      
            clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};      
            clearValues[2].depthStencil = {0.0f, 0};            
            clearValues[3].color = {{0.0f, 0.0f, 0.0f, 0.0f}};      
            clearValues[4].color = {{0.0f, 0.0f, 0.0f, 0.0f}};      
            clearValues[5].depthStencil = {0.0f, 0};            
        } else {
            clearValues.resize(3);
            // Changed from 0.1f to 0.0f
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};      
            clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};      
            clearValues[2].depthStencil = {0.0f, 0};            
        }

        viewportPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        viewportPassInfo.pClearValues = clearValues.data();
        
        vkCmdBeginRenderPass(commandBuffers[currentFrame], &viewportPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            
            VkViewport viewport{0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f};
            vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, swapChainExtent};
            vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);
    
           // -----------------------------------------------------------------
            // DRAW SKYBOX (SIM SCALE DEEP SPACE)
            // -----------------------------------------------------------------
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
            {
                // 1. The 112-Byte struct
                struct SkyboxPush {
                    glm::mat4 invViewProj;  // 64 bytes
                    glm::vec4 sunDirection; // 16 bytes (xyz = dir, w = intensity)
                    glm::vec4 zenithColor;  // 16 bytes
                    glm::vec4 horizonColor; // 16 bytes
                } skyPush;

                glm::mat4 viewNoTrans = glm::mat4(glm::mat3(view));
                skyPush.invViewProj = glm::inverse(proj * viewNoTrans);

                // 2. True Deep Space 
                glm::vec3 sunDir = scene->environment.sunDirection;
                float sunIntensity = 20.0f; 
                
                // Pitch black void. The ONLY blue you will ever see now comes 
                // mathematically from the Rayleigh scattering of your planets!
                glm::vec3 zenith = glm::vec3(0.0f, 0.0f, 0.0f);  
                glm::vec3 horizon = glm::vec3(0.0f, 0.0f, 0.005f); // Tiny baseline so stars still render

                // 3. Pack everything into the struct
                skyPush.sunDirection = glm::vec4(sunDir, sunIntensity);
                skyPush.zenithColor = glm::vec4(zenith, 1.0f);
                skyPush.horizonColor = glm::vec4(horizon, 1.0f);
                
                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                   0, sizeof(SkyboxPush), &skyPush);
            }
            vkCmdDraw(commandBuffers[currentFrame], 3, 1, 0, 0);
                    
            // -----------------------------------------------------------------
            // DRAW ENTITIES (OPAQUE & TRANSPARENT)
            // -----------------------------------------------------------------
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
            
            // Generic Draw Helper
            auto DrawList = [&](const std::vector<CBaseEntity*>& list) {
                for (auto* ent : list) {
                    MeshResource& mesh = meshes[ent->modelIndex];
                    if (mesh.vertexBuffer.handle == VK_NULL_HANDLE) continue;

                    VkBuffer vBuffers[] = { mesh.vertexBuffer.handle };
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
                                        
                    PushConsts push{};
                    push.entityIndex = entityGPUIndices[ent];
                    vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts), &push);
                    vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
                }
            };

            // -----------------------------------------------------------------
            // 2. DRAW OPAQUE & OCTREES
            // -----------------------------------------------------------------
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
            
            // First, draw standard opaque models (like the player, ships, etc)
            DrawList(opaqueList);

            // Traverse and stream the Procedural Planets! [Updated GPU method]
            for (auto* ent : scene->entities) {
                if (ent && ent->HasComponent<ProceduralPlanetComponent>()) {
                    auto planet = ent->GetComponent<ProceduralPlanetComponent>();
                    if (!planet->rootNode) continue;

                    // 1. Cull the tree and queue up missing chunks
                    // Wrap ent->origin in a glm::vec3 cast!
                    planet->rootNode->Update(camPos - glm::vec3(ent->origin), planet->lodSplitThreshold, planet->chunkManager.get());

                    // 2. Sort the queue so chunks closest to the camera generate FIRST
                    auto& queue = planet->chunkManager->chunkQueue;
                    std::sort(queue.begin(), queue.end(), [&](Crescendo::Terrain::OctreeNode* a, Crescendo::Terrain::OctreeNode* b) {
                        // Cast the 64-bit origin down to a 32-bit vec3 just for the distance check
                    float distA = glm::length((camPos - glm::vec3(ent->origin)) - a->center);
                    float distB = glm::length((camPos - glm::vec3(ent->origin)) - b->center);
                        return distA > distB; // > Descending order: Furthest at front, Closest at back
                    });

                    // 3. Process the Bake Queue (Launch Async Threads!)
                    int chunksLaunched = 0;
                    while (!queue.empty() && chunksLaunched < 1) { 
                        auto* node = queue.back(); // Grab the closest chunk (O(1) fast!)
                        queue.pop_back();          // Instantly remove it from the back

                        // Mark as generating so we don't accidentally queue it again
                        node->isGenerating = true; 

                        TerrainComputePush pushData{}; 
                        pushData.chunkOrigin = node->center - glm::vec3(node->size / 2.0f);
                        pushData.chunkSize = node->size;
                        pushData.planetCenter = glm::vec3(0.0f);
                        pushData.planetRadius = planet->settings.radius;
                        pushData.amplitude = planet->settings.amplitude;
                        pushData.frequency = planet->settings.frequency;
                        pushData.octaves = planet->settings.octaves;
                        pushData.resolution = 32;
                        pushData.lod = node->lod;

                        bool needsCollision = (node->lod <= 1);
                        
                        // --- THE TRULY ASYNC LAUNCH ---
                        auto* physicsServer = scene->physics; // Grab the pointer for the background thread

                        // Notice the variables added inside the [ ] brackets!
                        node->pendingBakeResult = std::async(std::launch::async, [this, pushData, needsCollision, physicsServer]() -> Crescendo::ChunkBakeResult {
                            
                            // 1. GPU Compute (Runs in background)
                            ChunkBakeResult result = this->buildChunkMesh(pushData, needsCollision);
                            
                            // 2. Jolt Physics (Runs in background!)
                            if (needsCollision && result.hasMesh && physicsServer) {
                                int stride = sizeof(Vertex) / sizeof(float);
                                result.physicsBodyID = physicsServer->CreateTerrainCollider(result.collisionVerts, result.collisionIndices, pushData.chunkOrigin, stride);
                                
                                // OPTIMIZATION: Clear the heavy RAM arrays since Jolt has the data now!
                                result.collisionVerts.clear();
                                result.collisionIndices.clear();
                            }
                            
                            return result; // Hand the completely finished package back
                        });
                        
                        chunksLaunched++;
                    }

                    // 4. Check for ANY finished background threads and integrate them!
                    planet->rootNode->CheckForFinishedMeshes(this, scene, planet->rootNode->center - glm::vec3(planet->rootNode->size / 2.0f));

                    // 4. Recursive Octree Streaming Lambda
                    auto drawOctree = [&](auto& self, Crescendo::Terrain::OctreeNode* node) -> void {
                        if (!node) return;
                        if (!node->isVisible) return; // Cull the dark side only

                        // 1. Check if the high-res children are fully baked yet
                        bool childrenReady = false;
                        if (!node->isLeaf) {
                            childrenReady = true;
                            for (auto& child : node->children) {
                                // If even one child is missing its mesh, they aren't ready!
                                if (child && child->meshID == -1) { 
                                    childrenReady = false;
                                    break;
                                }
                            }
                        }

                        // 2. DRAW THE PARENT IF: It is a leaf, OR its children are still generating in the background!
                        if (node->isLeaf || !childrenReady) {
                            if (node->meshID >= 0) { 
                                MeshResource& mesh = meshes[node->meshID];
                                if (mesh.vertexBuffer.handle != VK_NULL_HANDLE) {
                                    VkBuffer vBuffers[] = { mesh.vertexBuffer.handle };
                                    VkDeviceSize offsets[] = {0};
                                    vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                                    vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
                                    
                                    PushConsts push{};
                                    push.entityIndex = entityGPUIndices[ent];
                                    vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts), &push);
                                    
                                    vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
                                }
                            }
                        } 
                        
                        // 3. ONLY recurse and draw the children if they are all 100% ready to go
                        if (childrenReady && !node->isLeaf) {
                            for (auto& child : node->children) {
                                self(self, child.get());
                            }
                        }
                    };

                    // 5. Fire the draw calls!
                    drawOctree(drawOctree, planet->rootNode.get());

                    
                }
            }

            // Close the opaque pass so the depth buffer transitions to READ_ONLY
            vkCmdEndRenderPass(commandBuffers[currentFrame]);

            // -----------------------------------------------------------------
            // 2.5 DRAW VOLUMETRIC ATMOSPHERE (In the Read-Only Transparent Pass!)
            // -----------------------------------------------------------------
            for (auto* ent : scene->entities) {
                if (ent && ent->HasComponent<ProceduralPlanetComponent>()) {
                    auto planet = ent->GetComponent<ProceduralPlanetComponent>();
                    if (planet->atmosphereMeshID != -1) {
                        
                        VkRenderPassBeginInfo transPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                        transPassInfo.renderPass = transparentRenderPass; 
                        transPassInfo.framebuffer = viewportFramebuffer; 
                        transPassInfo.renderArea.extent = swapChainExtent;
                        
                        vkCmdBeginRenderPass(commandBuffers[currentFrame], &transPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                        
                        vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, atmospherePipeline);
                        
                        AtmospherePush atmoPush{};
                        atmoPush.vp = proj * view; 
                        
                        float innerRadius = planet->settings.radius + planet->atmosphereFloor;
                        float outerRadius = planet->settings.radius * planet->atmosphereCeiling;

                        // 1. THE RTE MATH: Subtract the 64-bit camera position from the 64-bit planet origin, 
                        // THEN cast the small difference to a 32-bit float.
                        glm::vec3 relativePlanetCenter = glm::vec3(ent->origin - mainCamera.Position);

                        atmoPush.sunDirection_planetRadius = glm::vec4(sunDirection, innerRadius);
                        
                        // 2. Pass the camera-relative position to the shader
                        atmoPush.planetCenter_atmosphereRadius = glm::vec4(relativePlanetCenter, outerRadius);
                        
                        // 3. Because the universe moves around the camera, the camera is ALWAYS at zero in the shader!
                        atmoPush.cameraPos_sunIntensity = glm::vec4(0.0f, 0.0f, 0.0f, planet->atmosphereIntensity);
                        
                        atmoPush.rayleigh_mie = glm::vec4(planet->rayleigh, planet->mie);

                        vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                           0, sizeof(AtmospherePush), &atmoPush);

                        MeshResource& atmoMesh = meshes[planet->atmosphereMeshID];
                        if (atmoMesh.vertexBuffer.handle != VK_NULL_HANDLE) {
                            VkBuffer vBuffers[] = { atmoMesh.vertexBuffer.handle };
                            VkDeviceSize offsets[] = {0};
                            vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                            vkCmdBindIndexBuffer(commandBuffers[currentFrame], atmoMesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
                            vkCmdDrawIndexed(commandBuffers[currentFrame], atmoMesh.indexCount, 1, 0, 0, 0);
                        }
                        
                        vkCmdEndRenderPass(commandBuffers[currentFrame]);
                    }
                }
            }
            
            // -----------------------------------------------------------------
            // 3. ITERATIVE REFRACTION (TRANSPARENT PASS)
            // -----------------------------------------------------------------
            
            // Wrap your entire mip-mapping and barrier block in a reusable lambda!
            auto UpdateRefractionTexture = [&]() {
                VkImageMemoryBarrier barriers[2] = {};
                barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[0].image = viewportImage.handle;
                barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barriers[1].image = refractionImage.handle;
                barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, refractionMipLevels, 0, 1};
                barriers[1].srcAccessMask = 0;
                barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(commandBuffers[currentFrame], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

                VkImageBlit blit{};
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {(int32_t)swapChainExtent.width, (int32_t)swapChainExtent.height, 1};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {(int32_t)swapChainExtent.width, (int32_t)swapChainExtent.height, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

                vkCmdBlitImage(commandBuffers[currentFrame], viewportImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, refractionImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(commandBuffers[currentFrame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barriers[0]);

                int32_t mipWidth = swapChainExtent.width;
                int32_t mipHeight = swapChainExtent.height;
                VkImageMemoryBarrier mipBarrier{};
                mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                mipBarrier.image = refractionImage.handle;
                mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                mipBarrier.subresourceRange.baseArrayLayer = 0;
                mipBarrier.subresourceRange.layerCount = 1;
                mipBarrier.subresourceRange.levelCount = 1;

                for (uint32_t i = 1; i < refractionMipLevels; i++) {
                    mipBarrier.subresourceRange.baseMipLevel = i - 1;
                    mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    mipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    mipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                    vkCmdPipelineBarrier(commandBuffers[currentFrame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

                    VkImageBlit mipBlit{};
                    mipBlit.srcOffsets[0] = {0, 0, 0};
                    mipBlit.srcOffsets[1] = {mipWidth, mipHeight, 1};
                    mipBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    mipBlit.srcSubresource.mipLevel = i - 1;
                    mipBlit.srcSubresource.baseArrayLayer = 0;
                    mipBlit.srcSubresource.layerCount = 1;
                    mipBlit.dstOffsets[0] = {0, 0, 0};
                    mipBlit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
                    mipBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    mipBlit.dstSubresource.mipLevel = i;
                    mipBlit.dstSubresource.baseArrayLayer = 0;
                    mipBlit.dstSubresource.layerCount = 1;

                    vkCmdBlitImage(commandBuffers[currentFrame], refractionImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, refractionImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &mipBlit, VK_FILTER_LINEAR);

                    mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    vkCmdPipelineBarrier(commandBuffers[currentFrame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

                    if (mipWidth > 1) mipWidth /= 2;
                    if (mipHeight > 1) mipHeight /= 2;
                }

                mipBarrier.subresourceRange.baseMipLevel = refractionMipLevels - 1;
                mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(commandBuffers[currentFrame], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &mipBarrier);
            };

            // --- THE MAGIC LOOP ---
            // Draw glass objects one by one, snapshotting the screen between them!
            for (auto* transEnt : transparentList) {
                // 1. Snapshot the screen (including any previously drawn glass!)
                UpdateRefractionTexture();

                // 2. Open the Render Pass
                VkRenderPassBeginInfo transPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                transPassInfo.renderPass = transparentRenderPass; 
                transPassInfo.framebuffer = viewportFramebuffer; 
                transPassInfo.renderArea.extent = swapChainExtent;
                vkCmdBeginRenderPass(commandBuffers[currentFrame], &transPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                // 3. Draw exactly ONE transparent entity
                vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline);
                vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
                
                std::vector<CBaseEntity*> singleList = { transEnt };
                DrawList(singleList);

                // 4. Close the pass so the next object can snapshot it
                vkCmdEndRenderPass(commandBuffers[currentFrame]);
            }

            // -----------------------------------------------------------------
            // 4. WATER OBJECTS
            // -----------------------------------------------------------------
            // If we have water, we need to do one last snapshot so water refracts the glass!
            bool hasWater = false;
            std::vector<CBaseEntity*> waterList;
            for (auto* ent : scene->entities) {
                if (ent && ent->className == "prop_water") {
                    waterList.push_back(ent);
                    hasWater = true;
                }
            }

            if (hasWater) {
                UpdateRefractionTexture();

                VkRenderPassBeginInfo waterPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                waterPassInfo.renderPass = transparentRenderPass; 
                waterPassInfo.framebuffer = viewportFramebuffer; 
                waterPassInfo.renderArea.extent = swapChainExtent;
                vkCmdBeginRenderPass(commandBuffers[currentFrame], &waterPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);
                vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

                DrawList(waterList);
                vkCmdEndRenderPass(commandBuffers[currentFrame]);
            }

        // -----------------------------------------------------------------
        // 5. EDITOR SYMBOLS & OUTLINES (Drawn last so they overlay the scene!)
        // -----------------------------------------------------------------
        VkRenderPassBeginInfo symbolPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        symbolPassInfo.renderPass = transparentRenderPass; 
        symbolPassInfo.framebuffer = viewportFramebuffer; 
        symbolPassInfo.renderArea.extent = swapChainExtent;

        vkCmdBeginRenderPass(commandBuffers[currentFrame], &symbolPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // Ensure Vulkan knows the screen size!
        VkViewport symbolViewport{0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f};
        vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &symbolViewport);
        VkRect2D symbolScissor{{0, 0}, swapChainExtent};
        vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &symbolScissor);

        // --- THE OUTLINE DRAW CALL ---
        int selectedIndex = editorUI.GetSelectedObjectIndex();

        // MASSIVE SAFETY CHECK: Ensure index is valid AND the pointer isn't null!
        if (editorUI.GetShowSelectionOutline() && selectedIndex >= 0 && selectedIndex < scene->entities.size() && scene->entities[selectedIndex] != nullptr) {
            
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

            // A recursive lambda to dig through the entity and all its children
            auto drawOutline = [&](auto& self, CBaseEntity* ent) -> void {
                if (!ent) return; // ASan caught this previously failing because ent was a dangling pointer
                
                // If this specific node has a mesh, draw it!
                if (ent->modelIndex >= 0 && ent->modelIndex < meshes.size()) {
                    MeshResource& mesh = meshes[ent->modelIndex];
                    if (mesh.vertexBuffer.handle != VK_NULL_HANDLE) {
                        VkBuffer vBuffers[] = { mesh.vertexBuffer.handle };
                        VkDeviceSize offsets[] = {0};
                        vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                        vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);

                        PushConsts push{};
                        push.entityIndex = entityGPUIndices[ent]; // <--- If ent is freed memory, this crashes!
                        vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConsts), &push);

                        vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
                    }
                }
                
                // Recursively check all children
                for (CBaseEntity* child : ent->children) {
                    // One more safety check just in case the scene graph got mangled
                    if (child != nullptr) {
                        self(self, child);
                    }
                }
            };

            // Fire the laser!
            drawOutline(drawOutline, scene->entities[selectedIndex]);
        }

        // --- DRAW SYMBOLS ---
        symbolServer.DrawSymbols(commandBuffers[currentFrame], mainCamera.Right, mainCamera.Up, descriptorSets[currentFrame], symbolTextureSet);
        
        vkCmdEndRenderPass(commandBuffers[currentFrame]);

        // =========================================================
        // PASS 2: SCREEN SPACE REFLECTIONS (SSR)
        // =========================================================
        {
            VkRenderPassBeginInfo ssrPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            ssrPassInfo.renderPass = ssrRenderPass;
            ssrPassInfo.framebuffer = ssrFramebuffer;
            ssrPassInfo.renderArea.extent = swapChainExtent;
            
            // Clear to black (no reflections) so if disabled, it doesn't leave garbage on screen
            VkClearValue ssrClear = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
            ssrPassInfo.clearValueCount = 1;
            ssrPassInfo.pClearValues = &ssrClear;

            vkCmdBeginRenderPass(commandBuffers[currentFrame], &ssrPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            
            if (renderSettings.enableSSR) {
                // Determine resolution scale
                float scale = renderSettings.halfResSSR ? 0.5f : 1.0f;
                uint32_t currentWidth = static_cast<uint32_t>(swapChainExtent.width * scale);
                uint32_t currentHeight = static_cast<uint32_t>(swapChainExtent.height * scale);

                VkViewport ssrViewport{0.0f, 0.0f, (float)currentWidth, (float)currentHeight, 0.0f, 1.0f};
                vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &ssrViewport);
                
                VkRect2D ssrScissor{{0, 0}, {currentWidth, currentHeight}};
                vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &ssrScissor);
                
                vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, ssrPipeline);
                vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        ssrPipelineLayout, 0, 1, &ssrDescriptorSet, 0, nullptr);

                SSRPushConstants ssrPush{};
                ssrPush.proj = proj;
                ssrPush.view = view;
                ssrPush.invProj = glm::inverse(proj);
                ssrPush.invView = glm::inverse(view);

                vkCmdPushConstants(commandBuffers[currentFrame], ssrPipelineLayout,
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSRPushConstants), &ssrPush);

                vkCmdDraw(commandBuffers[currentFrame], 3, 1, 0, 0);
            }
            
            vkCmdEndRenderPass(commandBuffers[currentFrame]);
        }        
        
        // [BLOOM EXTRACT]
        {
            VkRenderPassBeginInfo bloomPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            bloomPassInfo.renderPass = bloomRenderPass;
            bloomPassInfo.framebuffer = bloomFramebuffer;
            bloomPassInfo.renderArea.extent.width = swapChainExtent.width / 4;
            bloomPassInfo.renderArea.extent.height = swapChainExtent.height / 4;
            VkClearValue bloomClear = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
            bloomPassInfo.clearValueCount = 1;
            bloomPassInfo.pClearValues = &bloomClear;

            vkCmdBeginRenderPass(commandBuffers[currentFrame], &bloomPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport bloomViewport{0.0f, 0.0f, (float)swapChainExtent.width / 4.0f, (float)swapChainExtent.height / 4.0f, 0.0f, 1.0f};
            vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &bloomViewport);
            VkRect2D bloomScissor{{0, 0}, {swapChainExtent.width / 4, swapChainExtent.height / 4}};
            vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &bloomScissor);
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    compositePipelineLayout, 0, 1, &compositeDescriptorSet, 0, nullptr);
            vkCmdDraw(commandBuffers[currentFrame], 3, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffers[currentFrame]);
        }
    
        // ---------------------------------------------------------
        // POST-PROCESSING
        // ---------------------------------------------------------
    
        VkRenderPassBeginInfo compositePassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        compositePassInfo.renderPass = compositeRenderPass;  
        compositePassInfo.framebuffer = finalFramebuffer;    
        compositePassInfo.renderArea.extent = swapChainExtent;
        compositePassInfo.clearValueCount = 1;
        compositePassInfo.pClearValues = &clearValues[0]; 
    
        vkCmdBeginRenderPass(commandBuffers[currentFrame], &compositePassInfo, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport compositeViewport{0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f};
            vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &compositeViewport);
            vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    compositePipelineLayout, 0, 1, &compositeDescriptorSet, 0, nullptr);
            
            postProcessSettings.ssaoUVScale = renderSettings.halfResSSAO ? 0.5f : 1.0f;
            postProcessSettings.ssrUVScale = renderSettings.halfResSSR ? 0.5f : 1.0f;

            vkCmdPushConstants(commandBuffers[currentFrame], compositePipelineLayout,
                                VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostProcessPushConstants), &postProcessSettings);
            vkCmdDraw(commandBuffers[currentFrame], 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffers[currentFrame]);
            
        // ---------------------------------------------------------
        // UI & SWAPCHAIN
        // ---------------------------------------------------------
        VkRenderPassBeginInfo swapChainPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        swapChainPassInfo.renderPass = renderPass; 
        swapChainPassInfo.framebuffer = swapChainFramebuffers[imageIndex]; 
        swapChainPassInfo.renderArea.extent = swapChainExtent;
        swapChainPassInfo.clearValueCount = 2; 
        
        swapChainPassInfo.pClearValues = clearValues.data();
            
        vkCmdBeginRenderPass(commandBuffers[currentFrame], &swapChainPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            editorUI.Render(commandBuffers[currentFrame]);
        vkCmdEndRenderPass(commandBuffers[currentFrame]);
            
        if (vkEndCommandBuffer(commandBuffers[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }
    
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        { 
            std::lock_guard<std::mutex> lock(queueMutex);
            if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
                throw std::runtime_error("failed to submit draw command buffer!");
            } 
        
    
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;
    
        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        }
    
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain(window);
        }
        symbolServer.ClearSymbols();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    VkCommandBuffer RenderingServer::beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void RenderingServer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void RenderingServer::transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layerCount; // <--- The magic 6 faces!

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    TextureResource RenderingServer::UploadCubemap(void* pixels, size_t totalSize, uint32_t width, uint32_t height, uint32_t mipLevels) {
        TextureResource resource{};
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;

        // 1. Staging Buffer
        VulkanBuffer stagingBuffer(allocator, totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        void* data;
        vmaMapMemory(allocator, stagingBuffer.allocation, &data);
        memcpy(data, pixels, totalSize);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        // 2. Create the Image (6 Array Layers)
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 6;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &resource.image.handle, &resource.image.allocation, nullptr) != VK_SUCCESS) {
            std::cerr << "[Vulkan Error] Failed to allocate Cubemap Image!" << std::endl;
            return resource;
        }

        // 3. Mathematically calculate the mipmap copy regions (No GLI needed)
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        size_t offset = 0;

        for(uint32_t face = 0; face < 6; face++) {
            for (uint32_t level = 0; level < mipLevels; level++) {

                // Halve the resolution for each mip level
                uint32_t mipWidth = std::max(1u, width >> level);
                uint32_t mipHeight = std::max(1u, height >> level);

                VkBufferImageCopy region{};
                region.bufferOffset = offset;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = level;
                region.imageSubresource.baseArrayLayer = face;
                region.imageSubresource.layerCount = 1;
                region.imageExtent.width = mipWidth;
                region.imageExtent.height = mipHeight;
                region.imageExtent.depth = 1;

                bufferCopyRegions.push_back(region);

                // Advance the offset (R16G16B16_SFLOAT is 8 bytes per pixel)
                offset += (mipWidth * mipHeight * 8);
            }
        }

        // 4. GPU Copy
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        transitionImageLayout(commandBuffer, resource.image.handle, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, 6);
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.handle, resource.image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
        transitionImageLayout(commandBuffer, resource.image.handle, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels, 6);
        endSingleTimeCommands(commandBuffer);

        // 5. Create Image View
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = resource.image.handle;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;

        vkCreateImageView(device, &viewInfo, nullptr, &resource.image.view);

        return resource;
    }

    TextureResource RenderingServer::generateCubemapFromHDR(const std::string& hdrPath) {
        TextureResource resource{};
        uint32_t cubeSize = 1024; // 1024x1024 pixels per face for a crisp skybox

        // 1. Load the flat HDR using your existing function
        VulkanImage flatHDR;
        if (!createHDRImage(hdrPath, flatHDR)) {
            std::cerr << "[Compute] Failed to load HDR for IBL: " << hdrPath << std::endl;
            return resource;
        }

        // 2. Create the target Cubemap (Must have STORAGE_BIT for compute writing)
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
        
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {cubeSize, cubeSize, 1};
        imageInfo.mipLevels = 1; 
        imageInfo.arrayLayers = 6; // 6 faces!
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &resource.image.handle, &resource.image.allocation, nullptr) != VK_SUCCESS) {
            return resource;
        }
        resource.image.allocator = allocator;
        resource.image.device = device;

        // Create the Cubemap View
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = resource.image.handle;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;
        vkCreateImageView(device, &viewInfo, nullptr, &resource.image.view);

        // 3. Map memory via Descriptor Set
        VkDescriptorSetAllocateInfo allocSetInfo{};
        allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocSetInfo.descriptorPool = descriptorPool;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &computeDescriptorLayout;
        
        VkDescriptorSet computeSet;
        vkAllocateDescriptorSets(device, &allocSetInfo, &computeSet);

        VkDescriptorImageInfo inputInfo{};
        inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        inputInfo.imageView = flatHDR.view;
        inputInfo.sampler = skySampler; 

        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Compute writing layout
        outputInfo.imageView = resource.image.view;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = computeSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &inputInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = computeSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &outputInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // 4. FIRE THE LASER
        VkCommandBuffer cmd = beginSingleTimeCommands();

        // Transition target to GENERAL for writing
        transitionImageLayout(cmd, resource.image.handle, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1, 6);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, equirectToCubePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeSet, 0, nullptr);

        // Spawn 64x64 grids of 16x16 worker blocks across 6 faces!
        vkCmdDispatch(cmd, cubeSize / 16, cubeSize / 16, 6);

        // Transition target to SHADER_READ_ONLY for the main render pass to sample
        transitionImageLayout(cmd, resource.image.handle, format, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 6);

        endSingleTimeCommands(cmd);

        // 5. Cleanup
        flatHDR.destroy();
        vkFreeDescriptorSets(device, descriptorPool, 1, &computeSet);

        std::cout << "[Compute] HDR successfully baked to Cubemap: " << hdrPath << std::endl;
        return resource;
    }

    void RenderingServer::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } 
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } 
        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else {
            std::cerr << "[Error] Unsupported Layout Transition: " << oldLayout << " -> " << newLayout << std::endl;
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);
    }

    void RenderingServer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(commandBuffer);
    }

    VkImageView RenderingServer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture image view!");
        }
        return imageView;
    }

    uint32_t RenderingServer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("failed to find suitable memory type!");
    }

    bool RenderingServer::pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0) return false;

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                break;
            }
        }
        return (physicalDevice != VK_NULL_HANDLE);
    }
    
    bool RenderingServer::createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
        deviceFeatures.depthClamp = VK_TRUE;

        deviceFeatures.fillModeNonSolid = VK_TRUE;
        
        // [CRITICAL] Enable this so shaders can use "texSampler[textureID]"
        deviceFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE; 

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;

        const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) return false;

        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
        return true;
    }

    QueueFamilyIndices RenderingServer::findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphicsFamily = i;
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) indices.presentFamily = i;
            if (indices.isComplete()) break;
            i++;
        }
        return indices;
    }

    bool RenderingServer::isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);
        return indices.isComplete();
    }

    bool RenderingServer::setupDebugMessenger() {
        if (!enableValidationLayers) return true;
        return true;
    }

    SwapChainSupportDetails RenderingServer::querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }
        return details;
    }

    VkSurfaceFormatKHR RenderingServer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR RenderingServer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D RenderingServer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != UINT32_MAX) return capabilities.currentExtent;
        int width, height;
        display_ref->get_window_size(&width, &height);
        VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
    }

    VkShaderModule RenderingServer::createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) return VK_NULL_HANDLE;
        return shaderModule;
    }

    bool RenderingServer::createViewportResources() {
        uint32_t width = swapChainExtent.width;
        uint32_t height = swapChainExtent.height;
        refractionMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

        if (viewportRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, viewportRenderPass, nullptr);
        if (transparentRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, transparentRenderPass, nullptr);
        if (compositeRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, compositeRenderPass, nullptr);

        bool useMSAA = msaaSamples != VK_SAMPLE_COUNT_1_BIT;

        // --- 1. Viewport Render Pass (Dynamic MSAA/1x) ---
        VkAttachmentDescription2 colorTarget{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
        colorTarget.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        colorTarget.samples = msaaSamples;
        colorTarget.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorTarget.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorTarget.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorTarget.finalLayout = useMSAA ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription2 normalTarget = colorTarget;

        VkAttachmentDescription2 depthTarget{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
        depthTarget.format = VK_FORMAT_D32_SFLOAT;
        depthTarget.samples = msaaSamples;
        depthTarget.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthTarget.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthTarget.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthTarget.finalLayout = useMSAA ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference2 colorRefs[2] = {};
        colorRefs[0] = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0};
        colorRefs[1] = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0};

        VkAttachmentReference2 depthRef{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0};

        std::vector<VkAttachmentDescription2> attachments = { colorTarget, normalTarget, depthTarget };

        VkSubpassDescription2 subpass{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colorRefs;
        subpass.pDepthStencilAttachment = &depthRef;

        VkAttachmentReference2 resolveRefs[2] = {};
        VkAttachmentReference2 depthResolveRef{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
        VkSubpassDescriptionDepthStencilResolve depthResolveInfo{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE};

        if (useMSAA) {
            VkAttachmentDescription2 colorResolve{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
            colorResolve.format = VK_FORMAT_R16G16B16A16_SFLOAT;
            colorResolve.samples = VK_SAMPLE_COUNT_1_BIT;
            colorResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorResolve.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentDescription2 normalResolve = colorResolve;

            VkAttachmentDescription2 depthResolve{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
            depthResolve.format = VK_FORMAT_D32_SFLOAT;
            depthResolve.samples = VK_SAMPLE_COUNT_1_BIT;
            depthResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthResolve.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            attachments.push_back(colorResolve);
            attachments.push_back(normalResolve);
            attachments.push_back(depthResolve);

            resolveRefs[0] = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0};
            resolveRefs[1] = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0};
            depthResolveRef = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0};

            depthResolveInfo.depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
            depthResolveInfo.stencilResolveMode = VK_RESOLVE_MODE_NONE;
            depthResolveInfo.pDepthStencilResolveAttachment = &depthResolveRef;

            subpass.pResolveAttachments = resolveRefs;
            subpass.pNext = &depthResolveInfo;
        }

        std::array<VkSubpassDependency2, 2> dependencies = {};
        dependencies[0].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo2 rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
        rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        rpInfo.pAttachments = attachments.data();
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        rpInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass2(device, &rpInfo, nullptr, &viewportRenderPass) != VK_SUCCESS) return false;

        // --- 2. Transparent Render Pass ---
        std::vector<VkAttachmentDescription2> loadAttachments = attachments;
        
        // Color Target
        loadAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        loadAttachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        // Normal Target
        loadAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        loadAttachments[1].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        // Depth Target (MSAA or 1x)
        // If MSAA is off, it must be READ_ONLY so the shader can sample it.
        // If MSAA is on, it must stay ATTACHMENT_OPTIMAL to do transparent depth testing.
        VkImageLayout transparentDepthLayout = useMSAA ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        
        loadAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        loadAttachments[2].initialLayout = transparentDepthLayout;
        loadAttachments[2].finalLayout = transparentDepthLayout;

        VkAttachmentReference2 transDepthRef{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 2, transparentDepthLayout, 0};
        
        VkSubpassDescription2 transSubpass = subpass;
        transSubpass.pDepthStencilAttachment = &transDepthRef;

        if (useMSAA) {
            // Color Resolve
            loadAttachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            loadAttachments[3].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            
            // Normal Resolve
            loadAttachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            loadAttachments[4].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Depth Resolve (The 1x Depth Buffer we sample in the shader!)
            // We must preserve its data from the opaque pass and keep it perfectly READ_ONLY.
            loadAttachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            loadAttachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            loadAttachments[5].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            loadAttachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            // Remove depth resolve from this subpass so we don't overwrite it while reading it
            transSubpass.pNext = nullptr; 
        }

        VkRenderPassCreateInfo2 transRpInfo = rpInfo;
        transRpInfo.pAttachments = loadAttachments.data();
        transRpInfo.pSubpasses = &transSubpass;

        if (vkCreateRenderPass2(device, &transRpInfo, nullptr, &transparentRenderPass) != VK_SUCCESS) return false;

        // --- 3. Composite Render Pass (Final Output LDR) ---
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription compositeSubpass{};
        compositeSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        compositeSubpass.colorAttachmentCount = 1;
        compositeSubpass.pColorAttachments = &colorRef;

        VkSubpassDependency compositeDependency{};
        compositeDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        compositeDependency.dstSubpass = 0;
        compositeDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        compositeDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        compositeDependency.srcAccessMask = 0;
        compositeDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo compositeRpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        compositeRpInfo.attachmentCount = 1;
        compositeRpInfo.pAttachments = &colorAttachment;
        compositeRpInfo.subpassCount = 1;
        compositeRpInfo.pSubpasses = &compositeSubpass;
        compositeRpInfo.dependencyCount = 1;
        compositeRpInfo.pDependencies = &compositeDependency;

        if (vkCreateRenderPass(device, &compositeRpInfo, nullptr, &compositeRenderPass) != VK_SUCCESS) return false;

        // --- 4. Allocate Images ---
        if (useMSAA) {
            colorImageMSAA = VulkanImage(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, msaaSamples);
            normalImageMSAA = VulkanImage(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, msaaSamples);
            depthImageMSAA = VulkanImage(allocator, device, width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, msaaSamples);
        }

        // Scene Images (HDR 1x)
        viewportImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        viewportNormalImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        // Refraction Map (Mipped)
        VkImageCreateInfo refInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        refInfo.imageType = VK_IMAGE_TYPE_2D;
        refInfo.extent.width = width;
        refInfo.extent.height = height;
        refInfo.extent.depth = 1;
        refInfo.mipLevels = refractionMipLevels;
        refInfo.arrayLayers = 1;
        refInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        refInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        refInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        refInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        refInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        refInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        refractionImage.allocator = allocator;
        refractionImage.device = device;
        if (vmaCreateImage(allocator, &refInfo, &allocInfo, &refractionImage.handle, &refractionImage.allocation, nullptr) != VK_SUCCESS) return false;

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = refractionImage.handle;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = refractionMipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &refractionImageView) != VK_SUCCESS) return false;

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = (float)refractionMipLevels;
        vkCreateSampler(device, &samplerInfo, nullptr, &refractionSampler);

        finalImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        transitionImageLayout(finalImage.handle, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        viewportDepthImage = VulkanImage(allocator, device, width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

        // --- 5. FRAMEBUFFERS ---
        std::vector<VkImageView> fbAttachments;
        if (useMSAA) {
            fbAttachments = { colorImageMSAA.view, normalImageMSAA.view, depthImageMSAA.view, viewportImage.view, viewportNormalImage.view, viewportDepthImage.view };
        } else {
            fbAttachments = { viewportImage.view, viewportNormalImage.view, viewportDepthImage.view };
        }

        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = viewportRenderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
        fbInfo.pAttachments = fbAttachments.data();
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &viewportFramebuffer) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo compositeFbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        compositeFbInfo.renderPass = compositeRenderPass;
        compositeFbInfo.attachmentCount = 1;
        compositeFbInfo.pAttachments = &finalImage.view;
        compositeFbInfo.width = width;
        compositeFbInfo.height = height;
        compositeFbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &compositeFbInfo, nullptr, &finalFramebuffer) != VK_SUCCESS) return false;

        VkSamplerCreateInfo vSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        vSamplerInfo.magFilter = VK_FILTER_LINEAR;
        vSamplerInfo.minFilter = VK_FILTER_LINEAR;
        vSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vSamplerInfo.anisotropyEnable = VK_FALSE;
        vSamplerInfo.maxAnisotropy = 1.0f;
        vSamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        vkCreateSampler(device, &vSamplerInfo, nullptr, &viewportSampler);

        return true;
    }

    void RenderingServer::updateCompositeDescriptors() {
       // Added ssrImage check!
       if (viewportImage.view == VK_NULL_HANDLE || bloomBrightImage.view == VK_NULL_HANDLE || ssrImage.view == VK_NULL_HANDLE) return;

       VkDescriptorImageInfo compositeInfos[3] = {}; 
       
       // Binding 0: The 3D Scene
       compositeInfos[0].sampler = viewportSampler;
       compositeInfos[0].imageView = viewportImage.view;
       compositeInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

       // Binding 1: The Bloom Brightness Buffer
       compositeInfos[1].sampler = viewportSampler;
       compositeInfos[1].imageView = bloomBrightImage.view;
       compositeInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

       // --- ADD BINDING 2 (SSR) ---
       compositeInfos[2].sampler = viewportSampler;
       compositeInfos[2].imageView = ssrImage.view;
       compositeInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

       VkWriteDescriptorSet postWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
       postWrite.dstSet = compositeDescriptorSet;
       postWrite.dstBinding = 0;
       postWrite.dstArrayElement = 0;
       postWrite.descriptorCount = 3; 
       postWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       postWrite.pImageInfo = compositeInfos;

       vkUpdateDescriptorSets(device, 1, &postWrite, 0, nullptr);
    }

    void RenderingServer::updateSSRDescriptors() {
        if (viewportImage.view == VK_NULL_HANDLE || viewportNormalImage.view == VK_NULL_HANDLE || viewportDepthImage.view == VK_NULL_HANDLE) return;

        std::array<VkDescriptorImageInfo, 3> ssrInfos{};

        // Binding 0: Scene Color (HDR)
        ssrInfos[0].sampler = viewportSampler;
        ssrInfos[0].imageView = viewportImage.view;
        ssrInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Binding 1: Normal + Roughness G-Buffer
        ssrInfos[1].sampler = viewportSampler;
        ssrInfos[1].imageView = viewportNormalImage.view;
        ssrInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Binding 2: Depth Buffer
        ssrInfos[2].sampler = viewportSampler;
        ssrInfos[2].imageView = viewportDepthImage.view;
        ssrInfos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 3> writes{};
        for(int i = 0; i < 3; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = ssrDescriptorSet;
            writes[i].dstBinding = i;
            writes[i].dstArrayElement = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &ssrInfos[i];
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    bool RenderingServer::createBloomResources() {
       uint32_t width = swapChainExtent.width / 4;
       uint32_t height = swapChainExtent.height / 4;
        
       // 1. Create the Bright Image
       bloomBrightImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R8G8B8A8_SRGB,
           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
           VK_IMAGE_ASPECT_COLOR_BIT);
        
       // Transition to READ_ONLY
       transitionImageLayout(bloomBrightImage.handle, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
       // 2. Create Bloom Render Pass
       VkAttachmentDescription colorAttachment{};
       colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
       colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
       colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
       colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
       colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
       colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
       VkAttachmentReference colorAttachmentRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
       VkSubpassDescription subpass{0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1, &colorAttachmentRef};
        
       VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
       renderPassInfo.attachmentCount = 1;
       renderPassInfo.pAttachments = &colorAttachment;
       renderPassInfo.subpassCount = 1;
       renderPassInfo.pSubpasses = &subpass;
        
       if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &bloomRenderPass) != VK_SUCCESS) return false;
        
       // 3. Create Framebuffer
       VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
       fbInfo.renderPass = bloomRenderPass;
       fbInfo.attachmentCount = 1;
       // Use the RAII view
       fbInfo.pAttachments = &bloomBrightImage.view;
       fbInfo.width = width;
       fbInfo.height = height;
       fbInfo.layers = 1;
        
       return vkCreateFramebuffer(device, &fbInfo, nullptr, &bloomFramebuffer) == VK_SUCCESS;
    }

    // --------------------------------------------------------------------
    // SERVER JANITOR LOOP
    // --------------------------------------------------------------------

    void RenderingServer::recreateSwapChain(SDL_Window* window) {
        int width = 0, height = 0;
        SDL_Vulkan_GetDrawableSize(window, &width, &height);
        
        // 1. Handle Minimization (Pause until window is visible again)
        while (width == 0 || height == 0) {
            SDL_Vulkan_GetDrawableSize(window, &width, &height);
            SDL_WaitEvent(nullptr);
        }

        vkDeviceWaitIdle(device);

        // 2. Cleanup Old Resources
        cleanupSwapChain();

        // 3. Recreate Base Swapchain
        createSwapChain();
        createImageViews();
        createDepthResources();
        // (RenderPass is usually not destroyed in cleanupSwapChain, so we reuse it)
        createFramebuffers();

        // 4. Recreate Custom Offscreen Resources (HDR, Bloom, Final)
        createViewportResources();
        createSSRResources();    
        createBloomResources();  
        updateSSRDescriptors();       
        updateCompositeDescriptors(); 

        // 5. Update ImGui Descriptor
        // We  must create a new one pointing to the NEW finalImageView.
        if (finalImage.view != VK_NULL_HANDLE) {
            viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
                viewportSampler, 
                finalImage.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkDescriptorImageInfo refInfo{};
            refInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            refInfo.imageView = refractionImageView;
            refInfo.sampler = refractionSampler;

            VkWriteDescriptorSet refWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            refWrite.dstSet = descriptorSets[i]; 
            refWrite.dstBinding = 5;
            refWrite.dstArrayElement = 0;
            refWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            refWrite.descriptorCount = 1;
            refWrite.pImageInfo = &refInfo;

            // New Depth Map Update (Binding 9)
            VkDescriptorImageInfo depthInfo{};
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthInfo.imageView = viewportDepthImage.view;
            depthInfo.sampler = viewportSampler;

            VkWriteDescriptorSet depthWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            depthWrite.dstSet = descriptorSets[i]; 
            depthWrite.dstBinding = 9;
            depthWrite.dstArrayElement = 0;
            depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            depthWrite.descriptorCount = 1;
            depthWrite.pImageInfo = &depthInfo;

            vkUpdateDescriptorSets(device, 1, &depthWrite, 0, nullptr);
        }
        
    } 

    void RenderingServer::cleanupSwapChain() {
        // 1. Destroy Framebuffers
        if (viewportFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, viewportFramebuffer, nullptr);
        if (bloomFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, bloomFramebuffer, nullptr);
        if (finalFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, finalFramebuffer, nullptr);
        if (ssrFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, ssrFramebuffer, nullptr); 
        for (auto framebuffer : swapChainFramebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);

        // 2. Destroy Images
        depthImage.destroy();
        refractionImage.destroy();
        viewportImage.destroy();
        viewportNormalImage.destroy();
        viewportDepthImage.destroy();

        // --- CLEAN UP MSAA IMAGES ---
        colorImageMSAA.destroy();
        normalImageMSAA.destroy();
        depthImageMSAA.destroy();

        bloomBrightImage.destroy();
        finalImage.destroy();
        ssrImage.destroy();
        
        // 3. Destroy Manual Views & Samplers
        if (refractionImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, refractionImageView, nullptr); refractionImageView = VK_NULL_HANDLE; }
        if (refractionSampler != VK_NULL_HANDLE) { vkDestroySampler(device, refractionSampler, nullptr); refractionSampler = VK_NULL_HANDLE; }
        if (viewportSampler != VK_NULL_HANDLE) { vkDestroySampler(device, viewportSampler, nullptr); viewportSampler = VK_NULL_HANDLE; }

        // 4. Swapchain
        for (auto imageView : swapChainImageViews) vkDestroyImageView(device, imageView, nullptr);
        if (swapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapChain, nullptr);
            swapChain = VK_NULL_HANDLE;
        }
        
        // 5. Destroy Offscreen Render Passes
        if (viewportRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, viewportRenderPass, nullptr); viewportRenderPass = VK_NULL_HANDLE; }
        if (transparentRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, transparentRenderPass, nullptr); transparentRenderPass = VK_NULL_HANDLE; } // [FIX] Fix Memory Leak!
        if (compositeRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, compositeRenderPass, nullptr); compositeRenderPass = VK_NULL_HANDLE; }
        if (bloomRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, bloomRenderPass, nullptr); bloomRenderPass = VK_NULL_HANDLE; }
        if (ssrRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, ssrRenderPass, nullptr); ssrRenderPass = VK_NULL_HANDLE; } 
    }

    void RenderingServer::SetMSAASamples(VkSampleCountFlagBits newSamples) {

        if (msaaSamples == newSamples) return;

        vkDeviceWaitIdle(device);
        msaaSamples = newSamples;

        if (graphicsPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, graphicsPipeline,nullptr); graphicsPipeline = VK_NULL_HANDLE; }
        if (skyPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, skyPipeline, nullptr); skyPipeline = VK_NULL_HANDLE; }
        if (opaquePipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, opaquePipeline, nullptr);opaquePipeline = VK_NULL_HANDLE; }
        if (transparentPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, transparentPipeline, nullptr); transparentPipeline = VK_NULL_HANDLE; }
        if (waterPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, waterPipeline, nullptr); waterPipeline = VK_NULL_HANDLE; }
        if (atmospherePipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, atmospherePipeline, nullptr); atmospherePipeline = VK_NULL_HANDLE; }

        recreateSwapChain(window);

        createGraphicsPipeline();
        createOpaquePipeline();
        createTransparentPipeline();
        createWaterPipeline();
        createAtmospherePipeline();

        std::cout << "[Engine] MSAA successfully changed to " << newSamples << " samples." << std::endl;
    }

    void RenderingServer::shutdown() {
                
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            // 1. DESTROY TOP LEVEL
            symbolServer.Cleanup(device);
            editorUI.Shutdown(device);
                    
            // 2. DESTROY PIPELINES (With Null Checks!)
            if (skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, skyPipeline, nullptr);
            if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
            if (transparentPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, transparentPipeline, nullptr);
            if (opaquePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, opaquePipeline, nullptr);
            if (waterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, waterPipeline, nullptr);
            if (atmospherePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, atmospherePipeline, nullptr);
            if (bloomPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, bloomPipeline, nullptr);
            if (compositePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, compositePipeline, nullptr);
            if (shadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, shadowPipeline, nullptr);
            if (ssrPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, ssrPipeline, nullptr);
            if (equirectToCubePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, equirectToCubePipeline, nullptr);
            if (outlinePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, outlinePipeline, nullptr);
            
            if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (compositePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
            if (shadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
            if (ssrPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, ssrPipelineLayout, nullptr);
            if (computePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
            
            if (symbolTextureLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, symbolTextureLayout, nullptr);
            if (postProcessLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, postProcessLayout, nullptr);
            if (ssrDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, ssrDescriptorLayout, nullptr);
            if (computeDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, computeDescriptorLayout, nullptr);
            if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        
            if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
            if (shadowRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, shadowRenderPass, nullptr);
        
            if (densityComputePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, densityComputePipeline, nullptr);
            if (marchingCubesComputePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, marchingCubesComputePipeline, nullptr);
            if (terrainComputePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, terrainComputePipelineLayout, nullptr);
            if (terrainComputeDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, terrainComputeDescriptorLayout, nullptr);
        
            // 3. DESTROY SHADOWS
            for (auto fb : shadowFramebuffers) {
                if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
            }
            shadowFramebuffers.clear();
            for (auto view : shadowCascadeViews) {
                if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
            }
            shadowCascadeViews.clear();
            if (shadowImageView != VK_NULL_HANDLE) vkDestroyImageView(device, shadowImageView, nullptr);
            if (shadowSampler != VK_NULL_HANDLE) vkDestroySampler(device, shadowSampler, nullptr);
            shadowImage.destroy();
            
            // 4. DESTROY EVERY SINGLE IMAGE (Updated with the missing ones!)
            speakerTexture.destroy(); 
            skyImage.destroy(); 
            textureImage.destroy(); 
            positionBakeImage.destroy(); 
            normalBakeImage.destroy(); 
                    
            // 5. DESTROY BUFFERS
            densityBuffer.destroy();
            computeVertexBuffer.destroy();
            computeIndexBuffer.destroy();
            counterBuffer.destroy();
            stagingVertBuffer.destroy();
            stagingIndexBuffer.destroy();
        
            // Explicitly destroy the RAII buffers using the correct '.handle' property!
            for (auto& mesh : meshes) {
                if (mesh.vertexBuffer.handle != VK_NULL_HANDLE) mesh.vertexBuffer.destroy();
                if (mesh.indexBuffer.handle != VK_NULL_HANDLE) mesh.indexBuffer.destroy();
            }
            meshes.clear(); 

            for (auto& tex : textureBank) tex.image.destroy();
            textureBank.clear();
            textureMap.clear();
        
            if (bakePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, bakePipeline, nullptr);
            if (bakePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, bakePipelineLayout, nullptr);
            if (bakeFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, bakeFramebuffer, nullptr);
            if (bakeRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, bakeRenderPass, nullptr);
            
            if (textureSampler != VK_NULL_HANDLE) vkDestroySampler(device, textureSampler, nullptr);
            if (skySampler != VK_NULL_HANDLE) vkDestroySampler(device, skySampler, nullptr);
            if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            
            for (auto& buf : entityStorageBuffers) buf.destroy();
            entityStorageBuffers.clear();

            for (auto& buf : globalUniformBuffers) buf.destroy();
            globalUniformBuffers.clear();
        
            cleanupSwapChain();
            
            for (size_t i = 0; i < imageAvailableSemaphores.size(); i++) { 
                if (renderFinishedSemaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
                if (imageAvailableSemaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            }
            for (size_t i = 0; i < inFlightFences.size(); i++) {
                if (inFlightFences[i] != VK_NULL_HANDLE) vkDestroyFence(device, inFlightFences[i], nullptr);
            }
            
            if (asyncCommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, asyncCommandPool, nullptr);
            if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        
            if (allocator != nullptr) {
                vmaDestroyAllocator(allocator);
                allocator = nullptr;
            }
        
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
    
        // 6. THESE MUST ALWAYS RUN IF INSTANCE EXISTS
        if (instance != VK_NULL_HANDLE) {
            if (surface != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(instance, surface, nullptr);
                surface = VK_NULL_HANDLE;
            }
            
            if (debugMessenger != VK_NULL_HANDLE) {
                auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
                if (func != nullptr) func(instance, debugMessenger, nullptr);
                debugMessenger = VK_NULL_HANDLE;
            }
            
            vkDestroyInstance(instance, nullptr); 
            instance = VK_NULL_HANDLE;
        }
        
        std::cout << "[RenderingServer] Shutdown Complete." << std::endl;
    }
}

// This shit genuinely insane || Osprey "Far Above"