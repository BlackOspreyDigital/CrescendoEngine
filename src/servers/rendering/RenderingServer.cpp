
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
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
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <SDL2/SDL_vulkan.h>
#include <SDL2/SDL_image.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/intersect.hpp>       
#include <glm/gtx/matrix_decompose.hpp> 
#include "backends/imgui_impl_vulkan.h"
#include "tiny_gltf.h"  
#include <vulkan/vulkan_core.h>  
#include "servers/display/DisplayServer.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "Vertex.hpp"

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

        std::cout << "[1/5] Initializing Core Vulkan..." << std::endl;
        if (!createInstance()) return false;
        if (!setupDebugMessenger()) return false;
        if (!createSurface()) return false;
        if (!pickPhysicalDevice()) return false;
        if (!createLogicalDevice()) return false;

        // --- CHECK 1: VMA INIT (DYNAMIC) ---
        std::cout << "[Check 1] Creating VMA Allocator..." << std::endl;
        
        // 1. Configure Function Pointers
        VmaVulkanFunctions vulkanFunctions = {};
        
        // Root pointers
        vulkanFunctions.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        vulkanFunctions.vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
        
        // [CRITICAL FIX] We must NOT set these manually if we want VMA to fetch them!
        // But since VMA is dynamic, we just give it the root pointers and tell it to use 1.0 logic to be safe
        // Or better yet, we rely on the volk/loader if available.
        // Since we are raw, we will try VK_API_VERSION_1_0 to disable the check for `vkGetBufferMemoryRequirements2`
        
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = device;
        allocatorInfo.instance = instance;
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;
        
        // [FIX] Downgrade to 1.0 to satisfy the missing function pointer assertion
        // This forces VMA to use `vkGetBufferMemoryRequirements` (standard) instead of the `...2KHR` extension
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0; 
        
        if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
            std::cerr << "Failed to create VMA Allocator!" << std::endl;
            return false;
        }

        // --- CHECK 2: COMMANDS ---
        std::cout << "[Check 2] Creating Command Infrastructure..." << std::endl;
        if (!createSwapChain()) return false;
        if (!createImageViews()) return false;
        if (!createRenderPass()) return false;
        
        // CRITICAL: Pool must be created BEFORE textures/resources
        if (!createCommandPool()) {
             std::cerr << "Failed to create Command Pool!" << std::endl;
             return false;
        }

        // --- CHECK 3: RESOURCES ---
        std::cout << "[Check 3] Creating Resources..." << std::endl;
        if (!createDepthResources()) return false;
        if (!createShadowResources()) return false;
        if (!createTextureImage()) return false;
        
        

        // --- CHECK 4: SAMPLERS ---
        std::cout << "[Check 4] Creating Samplers..." << std::endl;
        if (!createTextureSampler()) {
            std::cerr << "!! Failed to create Texture Sampler" << std::endl;
            return false;
        }

        // [FIX] ADD THIS BLOCK HERE! ----------------------------------
        // This creates the default texture AND resizes the bank to MAX_TEXTURES
        if (!createTextureImage()) {
            std::cerr << "Failed to create default texture resources!" << std::endl;
            return false;
        }

        // Load Skybox Image
        if (createHDRImage("assets/hdr/sky_cloudy.hdr", skyImage)) {
            // RAII wrapper automatically creates skyImage.view
        } else {
            std::cout << "[Warning] Skybox HDR not found." << std::endl;
        }

        createWaterMesh();
        this->waterTextureID = acquireTexture("assets/textures/water.png");

        // --- CHECK 5: DESCRIPTORS ---
        std::cout << "[Check 5] Creating Descriptors..." << std::endl;
        if (!createDescriptorSetLayout()) return false;

        createStorageBuffers(); 
        createGlobalUniformBuffer();
        
        if (!createDescriptorPool()) return false;
        if (!createDescriptorSets()) { 
            std::cout << "!! Failed to create Global Descriptor Set" << std::endl; 
            return false; 
        }

        // --- CHECK 6: UI & PIPELINES ---
        std::cout << "[Check 6] Initializing UI & Pipelines..." << std::endl;
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        editorUI.Initialize(this, this->window, instance, physicalDevice, device, graphicsQueue, indices.graphicsFamily.value(), renderPass, static_cast<uint32_t>(swapChainImages.size()));

        if (!createViewportResources()) return false;
        if (!createBloomResources()) return false;
        if (!createGraphicsPipeline()) return false;
        if (!createWaterPipeline()) return false;        
        if (!createTransparentPipeline()) return false;
        if (!createBloomPipeline()) return false;
        if (!createCompositePipeline()) return false;
        if (!createFramebuffers()) return false;

        // --- CHECK 7: SYNC ---
        std::cout << "[Check 7] Final Sync..." << std::endl;
        if (!createSyncObjects()) return false;
        if (!createCommandBuffers()) return false;

        updateCompositeDescriptors();

        // [ADD THIS] Set a default camera position to see the water
        mainCamera.SetPosition(glm::vec3(0.0f, -10.0f, 5.0f)); // Back 10, Up 5
        mainCamera.SetRotation(glm::vec3(25.0f, 0.0f, 0.0f));  // Look down 25 degrees

        std::cout << ">>> ENGINE READY! <<<" << std::endl;
        return true;
    }

    void RenderingServer::createWaterMesh() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        int width = 1000;
        int depth = 1000;
        float spacing = 2.0f;
        
        float startX = -(width * spacing) / 2.0f;
        float startY = -(depth * spacing) / 2.0f;

        // 1. Generate Vertices
        for (int z = 0; z <= depth; z++) {
            for (int x = 0; x <= width; x++) {
                Vertex v{};
                v.pos = glm::vec3(startX + x * spacing, startY + z * spacing, 0.0f);
                v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                v.color = glm::vec3(1.0f);
                v.texCoord = glm::vec2((float)x / width, (float)z / depth);
                
                // Manually set tangents for the flat plane
                v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                v.bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
                
                vertices.push_back(v);
            }
        }

        // 2. Generate Indices (This was likely missing!)
        for (int z = 0; z < depth; z++) {
            for (int x = 0; x < width; x++) {
                int topLeft = (z * (width + 1)) + x;
                int topRight = topLeft + 1;
                int bottomLeft = ((z + 1) * (width + 1)) + x;
                int bottomRight = bottomLeft + 1;

                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }

        MeshResource waterMesh{};
        waterMesh.name = "Internal_Water";
        waterMesh.indexCount = indices.size();
        
        
        
        // meshes.push_back(waterMesh);  <-- BAD
        meshes.push_back(std::move(waterMesh)); // <-- GOOD
        
        CBaseEntity* water = gameWorld.CreateEntity("prop_water");
        water->modelIndex = meshes.size() - 1;
        water->origin = glm::vec3(0, 0, -5.0f);
    }

    bool RenderingServer::createInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Crescendo Engine v0.5a";
        appInfo.apiVersion = VK_API_VERSION_1_0;

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
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

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
        samplerInfo.compareOp = VK_COMPARE_OP_LESS; // Hardware PCF
        
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
        int texWidth, texHeight, texChannels;
        // Load the default texture
        stbi_uc* pixels = stbi_load("assets/textures/vikingemerald_default.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VkDeviceSize imageSize = texWidth * texHeight * 4;

        // Fallback if file missing
        unsigned char fallback[4] = { 255, 0, 255, 255 }; // Magenta
        if (!pixels) {
            std::cout << "[Warning] Default texture missing. Using fallback." << std::endl;
            texWidth = 1; texHeight = 1; imageSize = 4;
            pixels = (stbi_uc*)fallback;
        }

        // 1. Staging Buffer (RAII)
        VulkanBuffer stagingBuffer(
            allocator,
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        // 2. Map & Copy
        void* data;
        if (vmaMapMemory(allocator, stagingBuffer.allocation, &data) != VK_SUCCESS) return false;
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        if (pixels != fallback) stbi_image_free(pixels);

        // 3. Create Image
        textureImage = VulkanImage(allocator, device, texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, 
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_IMAGE_ASPECT_COLOR_BIT);

        // 4. Transition & Copy
        transitionImageLayout(textureImage.handle, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer.handle, textureImage.handle, texWidth, texHeight);
        transitionImageLayout(textureImage.handle, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 5. [CRITICAL] Resize the Bank!
        // This prevents the "Assertion failed" crash when loading other textures
        if (textureBank.size() < MAX_TEXTURES) {
            textureBank.resize(MAX_TEXTURES);
        }

        return true;
    }

    // Add this implementation to fix the Linker Error
    bool RenderingServer::createTextureImage(const std::string& path, VulkanImage& outImage) {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels) {
            std::cerr << "Failed to load texture file: " << path << std::endl;
            return false;
        }

        VkDeviceSize imageSize = texWidth * texHeight * 4;

        // 1. Staging Buffer (RAII)
        VulkanBuffer stagingBuffer(
            allocator,
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        // 2. Map & Copy
        void* data;
        if (vmaMapMemory(allocator, stagingBuffer.allocation, &data) != VK_SUCCESS) {
            stbi_image_free(pixels);
            return false;
        }
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        stbi_image_free(pixels);

        // 3. Create Image (RAII Move Assignment)
        // Note: We use UNORM for standard textures, unlike the SFLOAT used in HDR
        outImage = VulkanImage(allocator, device, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_IMAGE_ASPECT_COLOR_BIT);

        // 4. Transition & Copy
        transitionImageLayout(outImage.handle, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer.handle, outImage.handle, texWidth, texHeight);
        transitionImageLayout(outImage.handle, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        return true;
    }

    // [FIX] Update signature to take VulkanImage&
    bool RenderingServer::createHDRImage(const std::string& path, VulkanImage& outImage) {
        int width, height, nrComponents;
        float* data = stbi_loadf(path.c_str(), &width, &height, &nrComponents, 4);
        if (!data) return false;

        VkDeviceSize imageSize = width * height * 4 * sizeof(float);
        
        // 1. Staging Buffer (RAII)
        VulkanBuffer staging(allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        void* mappedData;
        vmaMapMemory(allocator, staging.allocation, &mappedData);
        memcpy(mappedData, data, static_cast<size_t>(imageSize));
        vmaUnmapMemory(allocator, staging.allocation);

        stbi_image_free(data);

        // 2. Create Image (RAII) - Move it into the outImage
        outImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, 
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
                               VK_IMAGE_ASPECT_COLOR_BIT);

        // 3. Copy
        transitionImageLayout(outImage.handle, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(staging.handle, outImage.handle, width, height);
        transitionImageLayout(outImage.handle, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        return true;
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

    void RenderingServer::loadMaterialsFromOBJ(const std::string& baseDir, const std::vector<tinyobj::material_t>& materials) {
        for (const auto& mat : materials) {
            if (materialMap.find(mat.name) != materialMap.end()) {
                continue;
            }

            Material newMat;
            newMat.name = mat.name;
            newMat.albedoColor = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);

            if (mat.shininess > 0.0f) {
                newMat.roughness = 1.0f - std::clamp(mat.shininess / 1000.0f, 0.0f, 1.0f);
            } else {
                newMat.roughness = 0.9f; 
            }
            newMat.metallic = 0.0f; 

            // [FIXED] Single, clean texture loading block
            if (!mat.diffuse_texname.empty()) {
                std::string texturePath;

                // Check paths in order of likelihood
                if (std::ifstream(mat.diffuse_texname).good()) {
                    texturePath = mat.diffuse_texname;
                }
                else if (std::ifstream(baseDir + "/" + mat.diffuse_texname).good()) {
                    texturePath = baseDir + "/" + mat.diffuse_texname;
                }
                else if (std::ifstream("assets/textures/" + mat.diffuse_texname).good()) {
                    texturePath = "assets/textures/" + mat.diffuse_texname;
                }

                if (!texturePath.empty()) {
                    newMat.textureID = acquireTexture(texturePath);
                } else {
                    newMat.textureID = 0; // Default texture
                }
            } else {
                newMat.textureID = 0;
            } 

            materialMap[mat.name] = static_cast<uint32_t>(materialBank.size());
            materialBank.push_back(newMat);
        }
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
        // [FIX] Point to the vector of samplers
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

        globalBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        
        std::array<VkDescriptorSetLayoutBinding, 5> bindings = { // Updated size to 5
            samplerLayoutBinding, skyLayoutBinding, ssboBinding, globalBinding, shadowBinding
        };

        // Update layoutInfo.bindingCount =4 

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

        std::array<VkDescriptorSetLayoutBinding, 2> postBindings = { postBinding0, postBinding1 };

        VkDescriptorSetLayoutCreateInfo postLayoutInfo{};
        postLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        postLayoutInfo.bindingCount = static_cast<uint32_t>(postBindings.size());
        postLayoutInfo.pBindings = postBindings.data();

        if (vkCreateDescriptorSetLayout(device, &postLayoutInfo, nullptr, &postProcessLayout) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool RenderingServer::createDescriptorPool() {
       // Merged duplicate types and increased counts
       std::array<VkDescriptorPoolSize, 3> poolSizes{};
       
       // 1. Uniform Buffers (Global UBOs)
       poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
       poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 10);

       // 2. Combined Image Samplers (Textures)
       // Calculation: (Global Array (100) + Skybox (1)) * Frames + ImGui Viewport + ImGui Fonts + Buffer
       poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * (MAX_TEXTURES + 10) + 100); 

       // 3. Storage Buffers (Entity Data)
       poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
       poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 5);

       VkDescriptorPoolCreateInfo poolInfo{};
       poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
       poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
       poolInfo.pPoolSizes = poolSizes.data();
       
       // [CRITICAL FIX] Increase Max Sets
       // We need sets for:
       // - Global Resources (1 per frame)
       // - ImGui Fonts (1 total)
       // - Viewport Texture (1 total)
       // - Future post-process passes?
       poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 5 + 50); 

       // [IMPORTANT] Allow sets to be freed (Required for ImGui and dynamic resizing)
       poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

       if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
           return false;
       }
       return true;
    }

    bool RenderingServer::createDescriptorSets() {

        // 1. Allocate Main Scene Set (Set 0)
        std::vector<VkDescriptorSetLayout> layouts(1, descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) return false;

        // -----------------------------------------------------------
        // PREPARE DESCRIPTOR WRITES
        // -----------------------------------------------------------

        std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
        for (int i = 0; i < MAX_TEXTURES; i++) {

            // [FIX] Check if the bank slot has a valid image loaded
            if (i < textureBank.size() && textureBank[i].image.handle != VK_NULL_HANDLE) {
                imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                // Access the view from the bank
                imageInfos[i].imageView = textureBank[i].image.view; 

                imageInfos[i].sampler = VK_NULL_HANDLE;
            } 
            else {
                // [FIX] Slot is empty, point to the Default/Fallback texture
                imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                // Access the view from the member variable 'textureImage'
                imageInfos[i].imageView = textureImage.view; 

                imageInfos[i].sampler = VK_NULL_HANDLE;
            }
        }

        // Descriptor WRITE
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = static_cast<uint32_t>(MAX_TEXTURES);
        descriptorWrite.pImageInfo = imageInfos.data();

        // Binding 1: Sky Texture
        VkDescriptorImageInfo skyImageInfo{};
        skyImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // [FIX] Use skyImage.view instead of skyImageView
        skyImageInfo.imageView = skyImage.view; 
        skyImageInfo.sampler = skySampler;

        VkWriteDescriptorSet skyWrite{};
        skyWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        skyWrite.dstSet = descriptorSet;
        skyWrite.dstBinding = 1;
        skyWrite.dstArrayElement = 0;
        skyWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skyWrite.descriptorCount = 1;
        skyWrite.pImageInfo = &skyImageInfo;

        // Binding 2: SSBO (Entity Data)
        VkDescriptorBufferInfo ssboInfo{};
        ssboInfo.buffer = entityStorageBuffer.handle;
        ssboInfo.offset = 0;
        ssboInfo.range = sizeof(EntityData) * MAX_ENTITIES;

        VkWriteDescriptorSet ssboWrite{};
        ssboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ssboWrite.dstSet = descriptorSet;
        ssboWrite.dstBinding = 2;
        ssboWrite.dstArrayElement = 0;
        ssboWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssboWrite.descriptorCount = 1;
        ssboWrite.pBufferInfo = &ssboInfo;

        // Binding 3: Global Uniforms (GUB)
        VkDescriptorBufferInfo globalInfo{};
        globalInfo.buffer = globalUniformBuffer.handle;
        globalInfo.offset = 0;
        globalInfo.range = sizeof(GlobalUniforms);

        VkWriteDescriptorSet globalWrite{};
        globalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        globalWrite.dstSet = descriptorSet;
        globalWrite.dstBinding = 3;
        globalWrite.dstArrayElement = 0;
        globalWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        globalWrite.descriptorCount = 1;
        globalWrite.pBufferInfo = &globalInfo;

        // Binding 4: Shadow Map
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowImageView;
        shadowInfo.sampler = shadowSampler;

        VkWriteDescriptorSet shadowWrite{};
        shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrite.dstSet = descriptorSet;
        shadowWrite.dstBinding = 4;
        shadowWrite.dstArrayElement = 0;
        shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowWrite.descriptorCount = 1;
        shadowWrite.pImageInfo = &shadowInfo;

        std::array<VkWriteDescriptorSet, 5> writes = { 
        descriptorWrite, skyWrite, ssboWrite, globalWrite, shadowWrite 
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // -----------------------------------------------------------
        // UPDATE SET 1 (Post Process)
        // -----------------------------------------------------------
        VkDescriptorSetAllocateInfo postAlloc{};
        postAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        postAlloc.descriptorPool = descriptorPool;
        postAlloc.descriptorSetCount = 1;
        postAlloc.pSetLayouts = &postProcessLayout;

        if (vkAllocateDescriptorSets(device, &postAlloc, &compositeDescriptorSet) != VK_SUCCESS) return false;

        return true;
    }

    int RenderingServer::acquireMesh(const std::string& path, const std::string& name, 
                                     const std::vector<Vertex>& vertices, 
                                     const std::vector<uint32_t>& indices) {
        std::string key = path + "_" + name;
        if (cache.meshes.find(key) != cache.meshes.end()) return cache.meshes[key];

        MeshResource newMesh;
        newMesh.name = name;
        newMesh.indexCount = static_cast<uint32_t>(indices.size());
        
        // [RAII] Direct assignment
        newMesh.vertexBuffer = createVertexBuffer(vertices);
        newMesh.indexBuffer = createIndexBuffer(indices);
        
        meshes.push_back(std::move(newMesh));
        
        int newIndex = static_cast<int>(meshes.size()) - 1;
        cache.meshes[key] = newIndex;
        return newIndex;
    }

    int RenderingServer::acquireTexture(const std::string& path) {
       if (cache.textures.find(path) != cache.textures.end()) {
           return cache.textures[path];
       }

       int newID = static_cast<int>(textureMap.size()) + 1;
       if (newID >= MAX_TEXTURES) return 0;

       int texWidth, texHeight, texChannels;
       stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
       if (!pixels) {
           std::cerr << "Failed to load texture: " << path << std::endl;
           return 0;
       }

       // Create RAII Resource
       TextureResource newTex;
       newTex.image = UploadTexture(pixels, texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM);
       stbi_image_free(pixels);

       // Use std::move() because RAII objects cannot be copied
       if (newID < textureBank.size()) {
           textureBank[newID] = std::move(newTex); 
       }

       cache.textures[path] = newID;
       textureMap[path] = newID;

       // [CRITICAL FIX] Only update descriptor if the set exists!
       // During initialization, this is VK_NULL_HANDLE, so we skip it.
       // createDescriptorSets() will handle the binding later.
       if (descriptorSet != VK_NULL_HANDLE) {
           VkDescriptorImageInfo imageInfo{};
           imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
           imageInfo.imageView = textureBank[newID].image.view;
           imageInfo.sampler = VK_NULL_HANDLE;

           VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
           descriptorWrite.dstSet = descriptorSet;
           descriptorWrite.dstBinding = 0;
           descriptorWrite.dstArrayElement = newID;
           descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
           descriptorWrite.descriptorCount = 1;
           descriptorWrite.pImageInfo = &imageInfo;

           vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
       }

       return newID;
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
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE; // changed for viewport test
        depthStencil.depthWriteEnable = VK_TRUE; // changed for viewport test
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

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
        
        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) return false;

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

    // [CLEANUP] Remove unused 'pipelineLayoutInfo' to fix warnings
    bool RenderingServer::createTransparentPipeline() {
        auto vertShaderCode = readFile("assets/shaders/shader.vert.spv");
        auto fragShaderCode = readFile("assets/shaders/shader.frag.spv");

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

        // ... (Input Assembly, Viewport, Rasterizer, Multisample, DepthStencil, ColorBlend - KEEP THESE) ...
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
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // [FIX] Removed unused 'VkPipelineLayoutCreateInfo pipelineLayoutInfo'
        // We reuse the 'pipelineLayout' created in createGraphicsPipeline

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
        rasterizer.cullMode = VK_CULL_MODE_NONE; 
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE; 
        depthStencil.depthWriteEnable = VK_TRUE; 
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE; 
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        /*
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; 
        
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        
        // [FIX] Change from sizeof(PushConsts) to 128
        // This covers both the tiny ID (4 bytes) and the Skybox Matrix (64 bytes)
        pushConstantRange.size = 128;
        */

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
        
        // [FIX] Reuse existing layout if created by Bloom
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
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        
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

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        return vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS;
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

    void RenderingServer::loadModel(const std::string& filePath) {
        // Check if file exists
        std::ifstream f(filePath.c_str());
        if (!f.good()) {
            std::cerr << "[Error] File not found: " << filePath << std::endl;
            return;
        }

        // Route based on extension
        if (filePath.find(".glb") != std::string::npos || filePath.find(".gltf") != std::string::npos) {
            // Assumes 'scene' is your active scene member variable. 
            // If you don't store it, you might need to pass it, but usually the server knows the active scene.
            // Based on your previous code, you usually pass the active scene from the main loop, 
            // but for a simple UI load, we often target the primary scene.
            
            // NOTE: If 'scene' isn't a class member, use the pointer to the main scene you created in Init.
            // For now, let's assume you have a pointer or pass 'this->currentScene' if you have one.
            // If you don't have a 'currentScene' member, you'll need to update the header to store it.
            
            // TEMPORARY FIX: Assuming you pass it or have a getter. 
            // If this fails, we need to see how you store the Scene pointer.
            // loadGLTF(filePath, this->activeScene); 
        } 
        else if (filePath.find(".obj") != std::string::npos) {
            // loadOBJ(filePath, ...); 
            std::cout << "[Loader] OBJ loading not yet refactored." << std::endl;
        }
    }

    // --------------------------------------------------------------------
    // GLTF Loading & Handling (UPDATED)
    // --------------------------------------------------------------------

    // [HELPER] Standardize slashes
    std::string normalizePath(const std::string& path) {
        std::string s = path;
        for (char &c : s) if (c == '\\') c = '/';
        return s;
    }

    // [HELPER] Decode URL characters
    std::string decodeUri(const std::string& uri) {
        std::string result;
        for (size_t i = 0; i < uri.length(); i++) {
            if (uri[i] == '%' && i + 2 < uri.length()) {
                std::string hex = uri.substr(i + 1, 2);
                char c = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                result += c;
                i += 2;
            } else if (uri[i] == '+') result += ' ';
            else result += uri[i];
        }
        return result;
    }
    
    // 1. UPDATED LOADGLTF (Tangents + Safe Buffers) Added missing Texture function at bottom

    void RenderingServer::loadGLTF(const std::string& filePath, Scene* scene) {
        if (scene == nullptr) return;

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        bool ret = (filePath.find(".glb") != std::string::npos) ? 
                   loader.LoadBinaryFromFile(&model, &err, &warn, filePath) : 
                   loader.LoadASCIIFromFile(&model, &err, &warn, filePath);

        if (!ret) { std::cerr << "[GLTF Error] " << err << std::endl; return; }

        std::string baseDir = "";
        size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash != std::string::npos) baseDir = filePath.substr(0, lastSlash);
        baseDir = normalizePath(baseDir);

        // --- MESH LOADING ---
        for (size_t i = 0; i < model.meshes.size(); i++) {
            const auto& mesh = model.meshes[i];

            for (size_t j = 0; j < mesh.primitives.size(); j++) {
                const auto& primitive = mesh.primitives[j];
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                auto getAttrData = [&](const std::string& name, int& stride) -> const uint8_t* {
                    auto it = primitive.attributes.find(name);
                    if (it == primitive.attributes.end()) return nullptr;
                    const auto& acc = model.accessors[it->second];
                    const auto& view = model.bufferViews[acc.bufferView];
                    stride = acc.ByteStride(view);
                    return &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
                };

                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) continue;
                int posCount = model.accessors[posIt->second].count;

                int posStride = 0, normStride = 0, tex0Stride = 0, tex1Stride = 0, tanStride = 0;
                const uint8_t* posBase = getAttrData("POSITION", posStride);
                const uint8_t* normBase = getAttrData("NORMAL", normStride);
                const uint8_t* tex0Base = getAttrData("TEXCOORD_0", tex0Stride); // Use tex0Stride
                const uint8_t* tex1Base = getAttrData("TEXCOORD_1", tex1Stride); // Use tex1Stride
                const uint8_t* tanBase = getAttrData("TANGENT", tanStride);

                vertices.reserve(posCount);

                for (int v = 0; v < posCount; v++) {
                    Vertex vert{};
                    const float* p = reinterpret_cast<const float*>(posBase + (v * posStride));
                    vert.pos = { p[0], p[1], p[2] };

                    if (normBase) {
                        const float* n = reinterpret_cast<const float*>(normBase + (v * normStride));
                        vert.normal = { n[0], n[1], n[2] };
                    } else vert.normal = { 0.0f, 0.0f, 1.0f };

                    // UV 0
                    if (tex0Base) {
                        const auto& acc = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                        if (acc.componentType == 5126) { 
                            const float* t = reinterpret_cast<const float*>(tex0Base + (v * tex0Stride));
                            vert.texCoord = { t[0], t[1] };
                        } else if (acc.componentType == 5123) {
                            const uint16_t* t = reinterpret_cast<const uint16_t*>(tex0Base + (v * tex0Stride));
                            vert.texCoord = { t[0] / 65535.0f, t[1] / 65535.0f };
                        }
                    }
                    // UV 1
                    if (tex1Base) {
                        const auto& acc = model.accessors[primitive.attributes.at("TEXCOORD_1")];
                        if (acc.componentType == 5126) { 
                            const float* t = reinterpret_cast<const float*>(tex1Base + (v * tex1Stride));
                            vert.texCoord = { t[0], t[1] };
                        } else if (acc.componentType == 5123) { 
                            const uint16_t* t = reinterpret_cast<const uint16_t*>(tex1Base + (v * tex1Stride));
                            vert.texCoord = { t[0] / 65535.0f, t[1] / 65535.0f };
                        }
                    } else {
                        vert.texCoord1 = { 0.0f, 0.0f }; // Default
                    }

                    // [CRITICAL] Tangent Calculation for Normal Mapping
                    if (tanBase) {
                        const float* t = reinterpret_cast<const float*>(tanBase + (v * tanStride));
                        vert.tangent = { t[0], t[1], t[2] };
                        vert.bitangent = glm::cross(vert.normal, vert.tangent) * t[3];
                    } else {
                        // Safe Fallback
                        vert.tangent = { 1.0f, 0.0f, 0.0f };
                        vert.bitangent = { 0.0f, 1.0f, 0.0f };
                    }

                    vert.color = { 1.0f, 1.0f, 1.0f };
                    vertices.push_back(vert);
                }

                if (primitive.indices > -1) {
                    const auto& acc = model.accessors[primitive.indices];
                    const auto& view = model.bufferViews[acc.bufferView];
                    const uint8_t* idxData = &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
                    int idxStride = acc.ByteStride(view);

                    for (size_t k = 0; k < acc.count; k++) {
                        if (acc.componentType == 5125) indices.push_back(*(const uint32_t*)(idxData + k * idxStride));
                        else if (acc.componentType == 5123) indices.push_back(*(const uint16_t*)(idxData + k * idxStride));
                        else if (acc.componentType == 5121) indices.push_back(*(const uint8_t*)(idxData + k * idxStride));
                    }
                }

                MeshResource newMesh{};
                newMesh.name = baseDir + "_mesh_" + std::to_string(i) + "_" + std::to_string(j);
                newMesh.indexCount = static_cast<uint32_t>(indices.size());
                newMesh.vertexBuffer = createVertexBuffer(vertices);
                newMesh.indexBuffer = createIndexBuffer(indices);

                size_t globalIndex = meshes.size();
                meshes.push_back(std::move(newMesh));
                meshMap[newMesh.name] = globalIndex;
            }
        }

        // --- NODE PROCESSING START ---
        const auto& gltfScene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
        for (int nodeIdx : gltfScene.nodes) {
            // [FIX] Pass Identity Matrix to start the chain
            processGLTFNode(model, model.nodes[nodeIdx], nullptr, baseDir, scene, glm::mat4(1.0f));
        }
    }

    void RenderingServer::processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene, glm::mat4 parentMatrix) {
        if (!scene) return; 

        CBaseEntity* newEnt = scene->CreateEntity("prop_static"); 
        newEnt->targetName = node.name; 
        newEnt->textureID = 0; 

        if (parent) {
            newEnt->moveParent = parent;
            parent->children.push_back(newEnt);
        }

        // 1. Calculate LOCAL Matrix (T * R * S)
        glm::vec3 localTranslation(0.0f);
        glm::quat localRotation = glm::identity<glm::quat>();
        glm::vec3 localScale(1.0f);
        glm::mat4 localMat(1.0f);

        if (node.matrix.size() == 16) {
            localMat = glm::make_mat4(node.matrix.data());
        } else {
            if (node.translation.size() == 3) 
                localTranslation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            if (node.rotation.size() == 4) 
                localRotation = glm::quat((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
            if (node.scale.size() == 3) 
                localScale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);

            localMat = glm::translate(glm::mat4(1.0f), localTranslation) * glm::mat4(localRotation) * glm::scale(glm::mat4(1.0f), localScale);
        }

        // 2. Calculate GLOBAL Matrix (Parent * Local)
        // [CRITICAL] This applies the accumulated transform from the root down to this node.
        glm::mat4 globalMat = parentMatrix * localMat;

        // 3. Decompose Global Matrix to get World Transform
        glm::vec3 worldScale;
        glm::quat worldRot;
        glm::vec3 worldPos;
        glm::vec3 skew; 
        glm::vec4 perspective;

        glm::decompose(globalMat, worldScale, worldRot, worldPos, skew, perspective);

        // [FIX] Convert GLTF (Right-Handed Y-Up) to Engine (Z-Up)
        // Swap Y and Z, and invert the new Z to maintain orientation
        newEnt->origin = glm::vec3(worldPos.x, -worldPos.z, worldPos.y);

        // Apply a -90 degree X-axis rotation to orient the model correctly in Z-Up space
        glm::quat correction = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));
        newEnt->angles = glm::degrees(glm::eulerAngles(correction * worldRot));

        newEnt->scale = worldScale;

        if (node.mesh > -1) {
            const tinygltf::Mesh& mesh = model.meshes[node.mesh];
            
            for (size_t i = 0; i < mesh.primitives.size(); i++) {
                const auto& primitive = mesh.primitives[i];
                CBaseEntity* targetEnt = (i == 0) ? newEnt : scene->CreateEntity("prop_submesh");

                if (i > 0) {
                    targetEnt->moveParent = newEnt;
                    newEnt->children.push_back(targetEnt);
                    // Submeshes inherit the node's resolved world transform directly
                    targetEnt->origin = newEnt->origin; 
                    targetEnt->angles = newEnt->angles;
                    targetEnt->scale  = newEnt->scale;
                }

                // --- Material Logic ---
                if (primitive.material >= 0) {
                    const tinygltf::Material& mat = model.materials[primitive.material];
                    
                    targetEnt->normalStrength = 0.0f;
                    targetEnt->roughness = (float)mat.pbrMetallicRoughness.roughnessFactor;
                    targetEnt->metallic = (float)mat.pbrMetallicRoughness.metallicFactor;
                
                    // [ADD] Emissive Factor (Fixes Dark Watch Hands)
                    if (mat.emissiveFactor.size() == 3) {
                         float r = (float)mat.emissiveFactor[0];
                         float g = (float)mat.emissiveFactor[1];
                         float b = (float)mat.emissiveFactor[2];
                         float maxEmit = std::max(r, std::max(g, b));
                         if (maxEmit > 0.0f) targetEnt->emission = maxEmit * 5.0f;
                    }

                    // [ADD] Parse Extensions (Glass/Transmission)
                    if (mat.extensions.find("KHR_materials_transmission") != mat.extensions.end()) {
                        const auto& ext = mat.extensions.at("KHR_materials_transmission");
                        if (ext.Has("transmissionFactor")) targetEnt->transmission = (float)ext.Get("transmissionFactor").Get<double>();
                    }

                    if (mat.extensions.find("KHR_materials_volume") != mat.extensions.end()) {
                        const auto& ext = mat.extensions.at("KHR_materials_volume");
                        if (ext.Has("thicknessFactor")) targetEnt->thickness = (float)ext.Get("thicknessFactor").Get<double>();
                        if (ext.Has("attenuationDistance")) targetEnt->attenuationDistance = (float)ext.Get("attenuationDistance").Get<double>();
                        if (ext.Has("attenuationColor")) {
                            auto c = ext.Get("attenuationColor");
                            targetEnt->attenuationColor = glm::vec3(c.Get(0).Get<double>(), c.Get(1).Get<double>(), c.Get(2).Get<double>());
                        }
                    }

                    if (mat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                        targetEnt->albedoColor = glm::vec3(
                            (float)mat.pbrMetallicRoughness.baseColorFactor[0],
                            (float)mat.pbrMetallicRoughness.baseColorFactor[1],
                            (float)mat.pbrMetallicRoughness.baseColorFactor[2]
                        );
                    }

                    int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
                    if (texIndex >= 0) {
                        const tinygltf::Texture& tex = model.textures[texIndex];
                        const tinygltf::Image& img = model.images[tex.source];
                        
                        std::string texKey;
                        if (!img.uri.empty()) {
                            texKey = baseDir + "/" + decodeUri(img.uri);
                        } else {
                            texKey = "EMBEDDED_" + std::to_string(tex.source) + "_" + node.name;
                        }
                        
                        if (textureMap.find(texKey) != textureMap.end()) {
                            targetEnt->textureID = textureMap[texKey];
                        } else {
                            int newID = static_cast<int>(textureMap.size()) + 1;
                            
                            if (newID < MAX_TEXTURES) {
                                TextureResource newTex;
                                bool success = false;
                                VkFormat format = VK_FORMAT_R8G8B8A8_UNORM; 

                                if (!img.image.empty()) {
                                    // [FIX] Use RAII UploadTexture
                                    newTex.image = UploadTexture((void*)img.image.data(), img.width, img.height, format);
                                    // View is auto-created in newTex.image.view
                                    success = true;
                                } 
                                else if (!img.uri.empty()) {
                                    // [FIX] Use updated createTextureImage signature
                                    if (createTextureImage(texKey, newTex.image)) {
                                         success = true;
                                    }
                                }

                                if (success) {
                                    // [FIX] Move ownership to bank
                                    textureBank[newID] = std::move(newTex);
                                    
                                    textureMap[texKey] = newID;
                                    cache.textures[texKey] = newID; 
                                    targetEnt->textureID = newID;

                                    VkDescriptorImageInfo imageInfo{};
                                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                    // [FIX] Access view via .image.view
                                    imageInfo.imageView = textureBank[newID].image.view;
                                    imageInfo.sampler = textureSampler;

                                    VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                                    descriptorWrite.dstSet = descriptorSet; 
                                    descriptorWrite.dstBinding = 0;
                                    descriptorWrite.dstArrayElement = newID; 
                                    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                                    descriptorWrite.descriptorCount = 1;
                                    descriptorWrite.pImageInfo = &imageInfo;
                                    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
                                    
                                    std::cout << "[GLTF] Loaded Texture: " << texKey << " (ID: " << newID << ")" << std::endl;
                                } else {
                                    std::cerr << "[GLTF] Failed to load texture: " << texKey << std::endl;
                                }
                            }
                        }
                    } else {
                        targetEnt->textureID = 0; 
                    }
                }
                
                 std::string meshKey = normalizePath(baseDir) + "_mesh_" + std::to_string(node.mesh) + "_" + std::to_string(i); 
                 if (meshMap.find(meshKey) != meshMap.end()) {
                     targetEnt->modelIndex = meshMap[meshKey];
                 }
            }
        }

        // [CHANGE] Recursion: Pass the NEW Global Matrix to children
        for (int childId : node.children) {
            processGLTFNode(model, model.nodes[childId], newEnt, baseDir, scene, globalMat);
        }
    }

    

    // ---------------------------------------------------------------------------------------------
    
    void RenderingServer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
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

        // [FIX] REMOVED "VMA_ALLOCATION_CREATE_MAPPED_BIT" to prevent double-mapping
        globalUniformBuffer = VulkanBuffer(
            allocator, 
            bufferSize, 
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT 
        );

        if (vmaMapMemory(allocator, globalUniformBuffer.allocation, &globalUniformBufferMapped) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map Global Uniform Buffer memory!");
        }
    }

    void RenderingServer::createStorageBuffers() {
        VkDeviceSize bufferSize = sizeof(EntityData) * MAX_ENTITIES;
        
        // [FIX] REMOVED "VMA_ALLOCATION_CREATE_MAPPED_BIT" to prevent double-mapping
        entityStorageBuffer = VulkanBuffer(
            allocator, 
            bufferSize, 
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT 
        );
    
        if (vmaMapMemory(allocator, entityStorageBuffer.allocation, &entityStorageBufferMapped) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map Entity Storage Buffer memory!");
        }
    }

    // --------------------------------------------------------------------
    // Render() / THE RENDER LOOP
    // --------------------------------------------------------------------

    void RenderingServer::render(Scene* scene) {
        if (!scene) return;
        
        // 1. Sync
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, 
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
            
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain(window);
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }
    
        editorUI.Prepare(scene, mainCamera, viewportDescriptorSet);
        
        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);  

        // ---------------------------------------------------------
        // PHASE 0: UPLOAD ENTITY DATA TO GPU (SSBO)
        // ---------------------------------------------------------
        EntityData* gpuData = (EntityData*)entityStorageBufferMapped;
        int entityCount = 0;

        // Map used to link an Entity Pointer to its GPU Index
        std::map<CBaseEntity*, uint32_t> entityGPUIndices;

        for (auto* ent : scene->entities) {
            if (!ent || entityCount >= MAX_ENTITIES) continue;

            // 1. Calculate Transforms
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, ent->origin);
            model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0,0,1)); // Fixed 'angels' typo
            model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0,1,0));
            model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1,0,0));
            model = glm::scale(model, ent->scale);

            // 2. Pack Data
            EntityData& data = gpuData[entityCount];
            data.modelMatrix = model;

            // Material & Volume
            int texID = (ent->textureID > 0) ? ent->textureID : 0;
            // [FIX] Corrected '==' to '=' 
            if (texID == 0 && ent->modelIndex < meshes.size() && meshes[ent->modelIndex].textureID > 0) {
                texID = meshes[ent->modelIndex].textureID;
            }

            data.albedoTint   = glm::vec4(ent->albedoColor, (float)texID);
            data.pbrParams    = glm::vec4(ent->roughness, ent->metallic, ent->emission, ent->normalStrength);
            data.volumeParams = glm::vec4(ent->transmission, ent->thickness, ent->attenuationDistance, ent->ior);
            data.volumeColor  = glm::vec4(ent->attenuationColor, 0.0f);
            
            // Store index for the Draw Loop
            entityGPUIndices[ent] = entityCount;
            entityCount++;
        }

        // ---------------------------------------------------------
        // RENDER COMMANDS
        // ---------------------------------------------------------

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        if (vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        // --- 1. CALCULATE MATRICES & SUN (Existing Code) ---
        float aspectRatio = 1.0f;
        glm::vec2 viewportSize = editorUI.GetViewportSize();
        if (viewportSize.x > 0 && viewportSize.y > 0) aspectRatio = viewportSize.x / viewportSize.y; 

        glm::mat4 view = mainCamera.GetViewMatrix();
        glm::mat4 proj = mainCamera.GetProjectionMatrix(aspectRatio);
        proj[1][1] *= -1; 
        glm::mat4 vp = proj * view;

        // 2. Sun Logic
        glm::vec3 sunDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.5f)); 
        glm::vec3 sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
        float sunIntensity = 1.0f;
        
        for (auto* sunEnt : scene->entities) {
            if (sunEnt && sunEnt->targetName == "Sun") {
                // Calculate sun direction from entity rotation
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(sunEnt->angles.x), glm::vec3(1, 0, 0));
                rotMat = glm::rotate(rotMat, glm::radians(sunEnt->angles.y), glm::vec3(0, 1, 0));
                rotMat = glm::rotate(rotMat, glm::radians(sunEnt->angles.z), glm::vec3(0, 0, 1));
                sunDirection = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
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
        globalData.cameraPos = glm::vec4(mainCamera.GetPosition(), 1.0f);
        globalData.sunDirection = glm::vec4(sunDirection, sunIntensity);
        globalData.sunColor = glm::vec4(sunColor, 1.0f);
        globalData.params = glm::vec4(SDL_GetTicks() / 1000.0f, 0.0f, viewportSize.x, viewportSize.y);

        // Upload to GPU (Binding 3)
        memcpy(globalUniformBufferMapped, &globalData, sizeof(GlobalUniforms));

        // =========================================================
        // PASS 1: OFFSCREEN SCENE (HDR) -> viewportFramebuffer
        // =========================================================
        VkRenderPassBeginInfo viewportPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        viewportPassInfo.renderPass = viewportRenderPass;
        viewportPassInfo.framebuffer = viewportFramebuffer;
        viewportPassInfo.renderArea.extent = swapChainExtent;
        
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}}; 
        clearValues[1].depthStencil = {1.0f, 0};
        viewportPassInfo.clearValueCount = 2;
        viewportPassInfo.pClearValues = clearValues.data();
        
        // Transition image for writing
        //transitionImageLayout(viewportImage.handle, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        
        vkCmdBeginRenderPass(commandBuffers[currentFrame], &viewportPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            
            VkViewport viewport{0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f};
            vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, swapChainExtent};
            vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);
    
            // -----------------------------------------------------------------
            // DRAW SKYBOX
            // -----------------------------------------------------------------
            // vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
            // vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                //                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            {
                // [FIX] Calculate and Push Skybox Matrix
                //glm::mat4 viewNoTrans = glm::mat4(glm::mat3(view));
                
                // Use the struct defined in .hpp (SkyboxPushConsts)
                //SkyboxPushConsts skyPush{}; 
                //skyPush.invViewProj = glm::inverse(proj * viewNoTrans);
                
                //vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                //                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                //                   0, sizeof(SkyboxPushConsts), &skyPush);
            }
            // vkCmdDraw(commandBuffers[currentFrame], 3, 1, 0, 0);
        
            // -----------------------------------------------------------------
            // DRAW ENTITIES (OPAQUE & TRANSPARENT)
            // -----------------------------------------------------------------
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            
            auto DrawPass = [&](bool isTransparentPass) {
                for (auto* ent : scene->entities) {
                    if (!ent || ent->modelIndex >= meshes.size()) continue;
                    if (ent->className == "prop_water") continue; 

                    bool entIsTransparent = (ent->transmission > 0.0f);
                    if (entIsTransparent != isTransparentPass) continue;

                    MeshResource& mesh = meshes[ent->modelIndex];
                    if (mesh.vertexBuffer.handle == VK_NULL_HANDLE) continue; // [FIX] Add .handle

                    VkBuffer vBuffers[] = { mesh.vertexBuffer.handle }; // [FIX] Define this!
                    VkDeviceSize offsets[] = {0};

                    vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
                                        
                    PushConsts push{};
                    push.entityIndex = entityGPUIndices[ent]; // Just the ID!

                    vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                        0, sizeof(PushConsts), &push);
                    
                    vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
                }
            };

            // Phase 1: Opaque
            DrawPass(false);

            // Phase 2: Transparent
            DrawPass(true);

            // =========================================================
            // PHASE 3: WATER OBJECTS
            // =========================================================
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            for (auto* ent : scene->entities) {
                if (!ent || ent->className != "prop_water") continue;
                if (ent->modelIndex >= meshes.size()) continue;

                MeshResource& mesh = meshes[ent->modelIndex];
                if (mesh.vertexBuffer.handle == VK_NULL_HANDLE) continue;

                VkBuffer vBuffers[] = { mesh.vertexBuffer.handle };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);

                PushConsts push{};
                push.entityIndex = entityGPUIndices[ent];

                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                    0, sizeof(PushConsts), &push);
                
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
        } // End of Water Loop

        // [FIX 1] END the Viewport Render Pass here!
        // This closes the main 3D rendering so we can transition images safely.
        vkCmdEndRenderPass(commandBuffers[currentFrame]);
        // [FIX 4] Transition Bloom Result for reading
        //cmdTransitionImageLayout(commandBuffers[currentFrame], bloomBrightImage.handle, VK_FORMAT_R8G8B8A8_SRGB, 
        //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
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
    
        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command!");
        }
    
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;
    
        result = vkQueuePresentKHR(presentQueue, &presentInfo);
    
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain(window);
        }
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

        // =========================================================
        // 1. CREATE MISSING RENDER PASSES
        // =========================================================
        
        // Cleanup if they already exist (safe for resize)
        if (viewportRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, viewportRenderPass, nullptr);
        if (compositeRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, compositeRenderPass, nullptr);

        // --- Viewport Render Pass (HDR Scene + Depth) ---
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT; // HDR Format
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // Ready for sampling

            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = VK_FORMAT_D32_SFLOAT;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
            subpass.pDepthStencilAttachment = &depthRef;

            // Dependencies for sync
            // [FIX] Zero-initialize the array to prevent garbage flags!
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

            std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
            VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            rpInfo.pAttachments = attachments.data();
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
            rpInfo.pDependencies = dependencies.data();

            if (vkCreateRenderPass(device, &rpInfo, nullptr, &viewportRenderPass) != VK_SUCCESS) {
                std::cerr << "Failed to create Viewport RenderPass!" << std::endl;
                return false;
            }
        }

        // --- Composite Render Pass (Final Output LDR) ---
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB; // Final LDR Format
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

            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments = &colorAttachment;
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 1;
            rpInfo.pDependencies = &dependency;

            if (vkCreateRenderPass(device, &rpInfo, nullptr, &compositeRenderPass) != VK_SUCCESS) {
                std::cerr << "Failed to create Composite RenderPass!" << std::endl;
                return false;
            }
        }

        // =========================================================
        // 2. CREATE IMAGES (Existing Code)
        // =========================================================

        // 1. SCENE IMAGE (HDR)
        viewportImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 
            VK_IMAGE_ASPECT_COLOR_BIT);

        // 2. REFRACTION (Custom Mips)
        {
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
        }

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

        // 3. FINAL IMAGE (LDR)
        finalImage = VulkanImage(allocator, device, width, height, VK_FORMAT_R8G8B8A8_SRGB, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_IMAGE_ASPECT_COLOR_BIT);
        
        transitionImageLayout(finalImage.handle, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 4. DEPTH IMAGE
        viewportDepthImage = VulkanImage(allocator, device, width, height, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);

        // 5. FRAMEBUFFERS
        // NOTE: viewportRenderPass is now valid!
        std::array<VkImageView, 2> fbAttachments = { viewportImage.view, viewportDepthImage.view };
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = viewportRenderPass; 
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments.data();
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &viewportFramebuffer) != VK_SUCCESS) return false;

        // NOTE: compositeRenderPass is now valid!
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
       // Only update if resources exist
       if (viewportImage.view == VK_NULL_HANDLE || bloomBrightImage.view == VK_NULL_HANDLE) return;

       VkDescriptorImageInfo compositeInfos[2] = {};
       // Binding 0: The 3D Scene
       compositeInfos[0].sampler = viewportSampler;
       compositeInfos[0].imageView = viewportImage.view;
       compositeInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

       // Binding 1: The Bloom Brightness Buffer
       compositeInfos[1].sampler = viewportSampler;
       compositeInfos[1].imageView = bloomBrightImage.view;
       compositeInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

       VkWriteDescriptorSet postWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
       postWrite.dstSet = compositeDescriptorSet;
       postWrite.dstBinding = 0;
       postWrite.dstArrayElement = 0;
       postWrite.descriptorCount = 2;
       postWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       postWrite.pImageInfo = compositeInfos;

       vkUpdateDescriptorSets(device, 1, &postWrite, 0, nullptr);
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

    bool RenderingServer::createBloomResources() {
       uint32_t width = swapChainExtent.width / 4;
       uint32_t height = swapChainExtent.height / 4;
        
       // 1. Create the Bright Image
       // [FIX] Use RAII Constructor
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
       // [FIX] Use the RAII view
       fbInfo.pAttachments = &bloomBrightImage.view;
       fbInfo.width = width;
       fbInfo.height = height;
       fbInfo.layers = 1;
        
       return vkCreateFramebuffer(device, &fbInfo, nullptr, &bloomFramebuffer) == VK_SUCCESS;
    }

    // --------------------------------------------------------------------
    // SERVER CLEANUP
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
        // (RenderPass is usually not destroyed in cleanupSwapChain, so we reuse it)
        createFramebuffers();

        // 4. Recreate Custom Offscreen Resources (HDR, Bloom, Final)
        createViewportResources();

        // 5. Update ImGui Descriptor
        // Since we destroyed the 'finalImageView', the old descriptor set is invalid.
        // We  must create a new one pointing to the NEW finalImageView.
        if (finalImage.view != VK_NULL_HANDLE) {
            viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
                viewportSampler, 
                finalImage.view, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }
    }

    void RenderingServer::cleanupSwapChain() {
        // 1. Destroy Framebuffers
        if (viewportFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, viewportFramebuffer, nullptr);
        if (bloomFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, bloomFramebuffer, nullptr);
        if (finalFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, finalFramebuffer, nullptr);
        for (auto framebuffer : swapChainFramebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);

        // 2. Destroy Images
        depthImage.destroy();
        refractionImage.destroy();
        viewportImage.destroy();
        viewportDepthImage.destroy();
        bloomBrightImage.destroy();
        finalImage.destroy();

        // 3. Destroy Manual Views & SAMPLERS [FIX]
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
        if (compositeRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, compositeRenderPass, nullptr); compositeRenderPass = VK_NULL_HANDLE; }
        // [FIX] Destroy Bloom Render Pass
        if (bloomRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, bloomRenderPass, nullptr); bloomRenderPass = VK_NULL_HANDLE; }
    }

    void RenderingServer::shutdown() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            editorUI.Shutdown(device);
        
            // 1. Destroy Pipelines & Layouts
            if (skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, skyPipeline, nullptr);
            if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
            if (transparentPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, transparentPipeline, nullptr);
            if (waterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, waterPipeline, nullptr);
            if (bloomPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, bloomPipeline, nullptr);
            if (compositePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, compositePipeline, nullptr);
            if (shadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, shadowPipeline, nullptr);

            if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (compositePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
            if (shadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
            if (postProcessLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, postProcessLayout, nullptr);
            
            // 2. Destroy RenderPasses
            if (shadowRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, shadowRenderPass, nullptr);
            if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);

            // 3. Cleanup Shadow Resources
            for (auto fb : shadowFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
            shadowFramebuffers.clear();
            for (auto view : shadowCascadeViews) vkDestroyImageView(device, view, nullptr);
            shadowCascadeViews.clear();
            if (shadowImageView != VK_NULL_HANDLE) vkDestroyImageView(device, shadowImageView, nullptr);
            if (shadowSampler != VK_NULL_HANDLE) vkDestroySampler(device, shadowSampler, nullptr);
            shadowImage.destroy();
            
            // 4. Cleanup RAII Images
            skyImage.destroy();
            textureImage.destroy();
            
            // 5. Cleanup Meshes & Textures
            meshes.clear(); 
            gameWorld.Clear();
            textureBank.clear();
            textureMap.clear();
            
            // 6. Cleanup Samplers
            if (textureSampler != VK_NULL_HANDLE) vkDestroySampler(device, textureSampler, nullptr);
            if (skySampler != VK_NULL_HANDLE) vkDestroySampler(device, skySampler, nullptr);
            
            // 7. Cleanup Descriptors & Pools [FIX]
            if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr); // <-- THIS WAS MISSING
            
            // 8. Cleanup SSBO & UBO
            if (entityStorageBufferMapped) {
                vmaUnmapMemory(allocator, entityStorageBuffer.allocation);
                entityStorageBufferMapped = nullptr;
            }
            if (globalUniformBufferMapped) {
                vmaUnmapMemory(allocator, globalUniformBuffer.allocation);
                globalUniformBufferMapped = nullptr;
            }
            entityStorageBuffer.destroy();
            globalUniformBuffer.destroy(); 
                
            // 9. Cleanup Swapchain
            cleanupSwapChain();
        
            // 10. Cleanup Sync Objects
            for (size_t i = 0; i < imageAvailableSemaphores.size(); i++) { 
                vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
                vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            }
            for (size_t i = 0; i < inFlightFences.size(); i++) {
                vkDestroyFence(device, inFlightFences[i], nullptr);
            }
            
            if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);

            // 11. Destroy Allocator
            if (allocator != nullptr) {
                vmaDestroyAllocator(allocator);
                allocator = nullptr;
            }
        
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debugMessenger != VK_NULL_HANDLE) {
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func != nullptr) func(instance, debugMessenger, nullptr);
        }
        
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);    
    }
}