// 1. Implementation Defines
#include <vulkan/vulkan_core.h>
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

// 2. Standard Library
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// 3. Third Party Libraries
#include <SDL2/SDL_vulkan.h>
#include <SDL2/SDL_image.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/intersect.hpp>       
#include <glm/gtx/matrix_decompose.hpp> 

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "ImGuizmo.h"     
#include "json.hpp"       
#include "tiny_gltf.h"    

// 4. Engine Headers 
#include "IO/Serializer.hpp"
#include "portable-file-dialogs.h" 
#include "scene/Scene.hpp"
#include "servers/display/DisplayServer.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "Vertex.hpp"

namespace Crescendo {
    
    // --------------------------------------------------------------------
    // LOGIC BEGIN
    // --------------------------------------------------------------------

    static void calculateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
        // 1. Initialize to zero (glm vectors don't always default to 0)
        for (auto& v : vertices) {
            v.tangent = glm::vec3(0.0f);
            v.bitangent = glm::vec3(0.0f);
        }

        // 2. Accumulate Tangents
        for (size_t i = 0; i < indices.size(); i += 3) {
            Vertex& v0 = vertices[indices[i]];
            Vertex& v1 = vertices[indices[i + 1]];
            Vertex& v2 = vertices[indices[i + 2]];

            glm::vec3 edge1 = v1.pos - v0.pos;
            glm::vec3 edge2 = v2.pos - v0.pos;
            glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
            glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

            float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

            // Safety for degenerate UVs
            if (std::isinf(f) || std::isnan(f)) f = 0.0f;

            glm::vec3 tangent, bitangent;

            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

            bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
            bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
            bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

            v0.tangent += tangent; v1.tangent += tangent; v2.tangent += tangent;
            v0.bitangent += bitangent; v1.bitangent += bitangent; v2.bitangent += bitangent;
        }

        // 3. Orthogonalize & Normalize (Gram-Schmidt)
        for (auto& v : vertices) {
            if (glm::length(v.tangent) > 0.0001f) {
                // Re-orthogonalize tangent with respect to normal
                v.tangent = glm::normalize(v.tangent - v.normal * glm::dot(v.normal, v.tangent));
            } else {
                 // Fallback for missing/bad UVs
                 v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            }

            if (glm::length(v.bitangent) > 0.0001f) {
                 v.bitangent = glm::normalize(v.bitangent);
            } else {
                 v.bitangent = glm::cross(v.normal, v.tangent);
            }
        }
    }
    
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

    RenderingServer::RenderingServer() : 
        display_ref(nullptr),
        window(nullptr),
        currentFrame(0),
        instance(VK_NULL_HANDLE),
        descriptorSet(VK_NULL_HANDLE), 
        commandPool(VK_NULL_HANDLE),
        viewportImage(VK_NULL_HANDLE),
        viewportImageMemory(VK_NULL_HANDLE),
        viewportImageView(VK_NULL_HANDLE),
        viewportSampler(VK_NULL_HANDLE),
        viewportFramebuffer(VK_NULL_HANDLE),
        viewportRenderPass(VK_NULL_HANDLE),
        viewportDescriptorSet(VK_NULL_HANDLE) 
    {
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

        std::cout << "[2/5] Setting up Command Infrastructure..." << std::endl;
        if (!createSwapChain()) return false;
        if (!createImageViews()) return false;
        if (!createRenderPass()) return false;
        if (!createCommandPool()) return false;
        if (!createDepthResources()) return false;
        if (!createDescriptorSetLayout()) return false;
        if (!createDescriptorPool()) return false;

        // ---------------------------------------------------------
        // STEP 3: UI, Fonts & Viewport
        // ---------------------------------------------------------
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        editorUI.Initialize(
            this, 
            this->window, 
            instance, 
            physicalDevice, 
            device, 
            graphicsQueue,
            indices.graphicsFamily.value(),
            renderPass, 
            static_cast<uint32_t>(swapChainImages.size())
        );

        if (!createViewportResources()) return false;

        if (!createBloomResources()) {
            std::cerr << "!! Failed to create Bloom resources" << std::endl;
            return false;
        }

        viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
            viewportSampler, 
            finalImageView, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        // ---------------------------------------------------------
        // STEP 4: Pipelines
        // ---------------------------------------------------------
        if (!createGraphicsPipeline()) return false;
        if (!createWaterPipeline()) return false;        
        if (!createTransparentPipeline()) return false;
        if (!createBloomPipeline()) return false;
        if (!createCompositePipeline()) return false;

        if (!createFramebuffers()) return false;

        // ---------------------------------------------------------
        // STEP 5: Assets & Descriptors
        // ---------------------------------------------------------
        std::cout << "[4/5] Loading Assets..." << std::endl;

        if (createHDRImage("assets/hdr/sky_cloudy.hdr", skyImage, skyImageMemory)) {
            skyImageView = createImageView(skyImage, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

            // Linear sampler for smooth sky look
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            vkCreateSampler(device, &samplerInfo, nullptr, &skySampler);

            // update descriptor sets here to point Binding 1 to this skySampler!
        }

        // 1. Default Texture
        createDefaultTexture();

        // 2. Global Descriptors (Pool already created above!)
        if (!createDescriptorSets()) { 
            std::cout << "!! Failed to create Global Descriptor Set" << std::endl; 
            return false; 
        }

        // 3. Load Game Assets
        
        createWaterMesh();

        // Even if this fails, acquireTexture now returns 0 (Default).
        this->waterTextureID = acquireTexture("assets/textures/water.png");

        if (this->waterTextureID == 0) {
            std::cout << "[System] Note: Water texture failed to load (or used default)." << std::endl;
        } else {
            std::cout << "[System] Water Texture Loaded! ID: " << this->waterTextureID << std::endl;
        }

        // ---------------------------------------------------------
        // STEP 7: Final Sync & Command Buffers
        // ---------------------------------------------------------
        std::cout << "[5/5] Finalizing Synchronization & ImGui..." << std::endl;

        if (!createSyncObjects()) return false;
        if (!createCommandBuffers()) return false;

        std::cout << ">>> ENGINE READY! <<<" << std::endl;
        gameConsole.AddLog("[Render] Viewport resolution: %dx%d\n", swapChainExtent.width, swapChainExtent.height);

        updateCompositeDescriptors();

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
        
        // This will no longer crash because indices.size() > 0
        createVertexBuffer(vertices, waterMesh.vertexBuffer, waterMesh.vertexBufferMemory);
        createIndexBuffer(indices, waterMesh.indexBuffer, waterMesh.indexBufferMemory);
        
        meshes.push_back(waterMesh);
        
        CBaseEntity* water = gameWorld.CreateEntity("prop_water");
        water->modelIndex = meshes.size() - 1;
        water->origin = glm::vec3(0, 0, -5.0f);
    }

    bool RenderingServer::createInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Crescendo Engine v0.3a";
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
        createImage(swapChainExtent.width, swapChainExtent.height, depthFormat, 
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);

        depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        return true;
    }

    void RenderingServer::UploadTexture(void* pixels, int width, int height, VkFormat format, VkImage& image, VkDeviceMemory& memory) {
        VkDeviceSize imageSize = width * height * 4;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                    stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        createImage(width, height, format, VK_IMAGE_TILING_OPTIMAL, 
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        transitionImageLayout(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, image, width, height);
        transitionImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkQueueWaitIdle(graphicsQueue); 

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }   

    void RenderingServer::createDefaultTexture() {
        // White reflects 100% light, making bloom explode. Grey is a neutral material.
        unsigned char pixels[4] = { 128, 128, 128, 255 };
        
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, 4, 0, &data);
        memcpy(data, pixels, 4);
        vkUnmapMemory(device, stagingBufferMemory);
        
        // [FIX] Use UNORM (Raw values) instead of SRGB
        createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);
        
        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, textureImage, 1, 1);
        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        
        // [FIX] Use UNORM View
        textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
        createTextureSampler(); 

        TextureResource defaultTex;
        defaultTex.image = textureImage;
        defaultTex.view = textureImageView;
        defaultTex.memory = textureImageMemory;
        
        textureBank.resize(MAX_TEXTURES);
        for (int i = 0; i < MAX_TEXTURES; i++) {
            textureBank[i] = defaultTex;
        }
        
        std::cout << "[System] Default Texture Generated." << std::endl;
    }

    bool RenderingServer::createTextureImage(const std::string& path, VkImage& image, VkDeviceMemory& memory) {
        int texWidth, texHeight, texChannels;
        
        // Load with STB
        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels) {
            std::cerr << "[Texture] Warning: Failed to load image (STB): " << path << std::endl;
            return false;
        }

        VkDeviceSize imageSize = texWidth * texHeight * 4;
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        stbi_image_free(pixels); 

        // [FIX] Use UNORM instead of SRGB
        createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, 
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        transitionImageLayout(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
        transitionImageLayout(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return true;
    }

    bool RenderingServer::createTextureImageView() {
        textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
        return (textureImageView != VK_NULL_HANDLE);
    }

    bool RenderingServer::createTextureSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        return vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) == VK_SUCCESS;
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

            if (!mat.diffuse_texname.empty()) {
                std::string texturePath;

                if (std::ifstream(mat.diffuse_texname).good()) {
                    texturePath = mat.diffuse_texname;
                }
                else if (std::ifstream(baseDir + mat.diffuse_texname).good()) {
                    texturePath = baseDir + mat.diffuse_texname;
                }
                else if (std::ifstream("assets/textures/" + mat.diffuse_texname).good()) {
                    texturePath = "assets/textures/" + mat.diffuse_texname;
                }
                
                if (!texturePath.empty()) {
                    newMat.textureID = acquireTexture(texturePath);
                } else {
                    newMat.textureID = 0; 
                }

                materialMap[mat.name] = static_cast<uint32_t>(materialBank.size());
                materialBank.push_back(newMat);
            }
        }
    }

    VkImageView RenderingServer::createTextureImageView(VkImage& image) {
        return createImageView(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    bool RenderingServer::createDescriptorSetLayout() {
        // =========================================================
        // 1. MAIN SCENE LAYOUT (Set 0)
        // =========================================================
        
        // Binding 0: Texture Array (Material Textures)
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorCount = 100; // MAX_TEXTURES
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 1: Skybox Texture
        VkDescriptorSetLayoutBinding skyLayoutBinding{};
        skyLayoutBinding.binding = 1;
        skyLayoutBinding.descriptorCount = 1; 
        skyLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skyLayoutBinding.pImmutableSamplers = nullptr;
        skyLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = { samplerLayoutBinding, skyLayoutBinding };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data(); 

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            return false;
        }

        // =========================================================
        // 2. POST-PROCESS LAYOUT (Set 1) [THIS WAS MISSING!]
        // =========================================================
        
        // Binding 0: Scene/Bright Color Input
        VkDescriptorSetLayoutBinding postBinding0{};
        postBinding0.binding = 0;
        postBinding0.descriptorCount = 1;
        postBinding0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        postBinding0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 1: Bloom Blur Input
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

        // [CRITICAL] This assigns the handle that was previously NULL!
        if (vkCreateDescriptorSetLayout(device, &postLayoutInfo, nullptr, &postProcessLayout) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    bool RenderingServer::createDescriptorPool() {
       std::array<VkDescriptorPoolSize, 2> poolSizes{};
       
       // Pool for Uniform Buffers (if you use them)
       poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
       poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

       // Pool for Textures
       poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       // We need 100 for the array + 1 for the sky * Frames (buffer)
       poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 105); 

       VkDescriptorPoolCreateInfo poolInfo{};
       poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
       poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
       poolInfo.pPoolSizes = poolSizes.data();
       poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 2);

       if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
           return false;
       }
       return true;
    }

    bool RenderingServer::createDescriptorSets() {
       // 1. Allocate Main Scene Set (Set 0)
       VkDescriptorSetAllocateInfo allocInfo{};
       allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
       allocInfo.descriptorPool = descriptorPool;
       allocInfo.descriptorSetCount = 1;
       allocInfo.pSetLayouts = &descriptorSetLayout;

       if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) return false;

       // 2. Write Binding 0 (Texture Array)
       std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
       for (int i = 0; i < MAX_TEXTURES; i++) {
           imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
           imageInfos[i].imageView = textureBank[i].view; 
           imageInfos[i].sampler = textureSampler;
       }

       VkWriteDescriptorSet descriptorWrite{};
       descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
       descriptorWrite.dstSet = descriptorSet;
       descriptorWrite.dstBinding = 0;
       descriptorWrite.dstArrayElement = 0;
       descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       descriptorWrite.descriptorCount = static_cast<uint32_t>(MAX_TEXTURES);
       descriptorWrite.pImageInfo = imageInfos.data();

       // 3. Write Binding 1 (Sky Texture)
       VkDescriptorImageInfo skyImageInfo{};
       skyImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       skyImageInfo.imageView = skyImageView;
       skyImageInfo.sampler = skySampler;

       VkWriteDescriptorSet skyWrite{};
       skyWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
       skyWrite.dstSet = descriptorSet;
       skyWrite.dstBinding = 1; 
       skyWrite.dstArrayElement = 0;
       skyWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       skyWrite.descriptorCount = 1;
       skyWrite.pImageInfo = &skyImageInfo;

       // Update both bindings
       std::array<VkWriteDescriptorSet, 2> writes = {descriptorWrite, skyWrite};
       vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

       // 4. Allocate Post-Process Set (Set 1)
       VkDescriptorSetAllocateInfo postAlloc{};
       postAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
       postAlloc.descriptorPool = descriptorPool;
       postAlloc.descriptorSetCount = 1;
       // [CRITICAL] This now points to a VALID layout created in step 1
       postAlloc.pSetLayouts = &postProcessLayout;

       if (vkAllocateDescriptorSets(device, &postAlloc, &compositeDescriptorSet) != VK_SUCCESS) return false;

       return true;
    }

    int RenderingServer::acquireMesh(const std::string& path, const std::string& name, 
                                     const std::vector<Vertex>& vertices, 
                                     const std::vector<uint32_t>& indices) {
        // 1. Generate Unique Key
        // Example: "assets/models/duck.gltf_DuckMesh"
        std::string key = path + "_" + name;

        // 2. CHECK CACHE (Return existing ID if found)
        if (cache.meshes.find(key) != cache.meshes.end()) {
            return cache.meshes[key];
        }

        // 3. CREATE NEW MESH RESOURCE
        MeshResource newMesh{};
        newMesh.name = name;
        newMesh.indexCount = static_cast<uint32_t>(indices.size());
        
        createVertexBuffer(vertices, newMesh.vertexBuffer, newMesh.vertexBufferMemory);
        createIndexBuffer(indices, newMesh.indexBuffer, newMesh.indexBufferMemory);
        
        // 4. STORE & CACHE
        meshes.push_back(newMesh);
        
        // Get the index of the mesh we just added
        int newIndex = static_cast<int>(meshes.size()) - 1;
        cache.meshes[key] = newIndex;

        return newIndex;
    }

    int RenderingServer::acquireTexture(const std::string& path) {
       if (cache.textures.find(path) != cache.textures.end()) {
           return cache.textures[path];
       }

       int newID = static_cast<int>(textureMap.size()) + 1;
       
       if (newID >= MAX_TEXTURES) {
           std::cerr << "[Cache] Texture Bank Full! (Max " << MAX_TEXTURES << "). Using Default." << std::endl;
           return 0;
       }

       TextureResource newTex; 
       if (!createTextureImage(path, newTex.image, newTex.memory)) {
           std::cerr << "[Cache] Failed to load: " << path << " (Using Default)" << std::endl;
           cache.textures[path] = 0; 
           return 0; 
       }

       // [FIX] Restored to use UNORM (Raw Color)
       // This matches the format used in createTextureImage now.
       newTex.view = createImageView(newTex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
       
       if (newID < textureBank.size()) {
           textureBank[newID] = newTex; 
       }

       cache.textures[path] = newID; 
       textureMap[path] = newID; 

       VkDescriptorImageInfo imageInfo{};
       imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       imageInfo.imageView = newTex.view; 
       imageInfo.sampler = textureSampler;

       VkWriteDescriptorSet descriptorWrite{};
       descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
       descriptorWrite.dstSet = descriptorSet; 
       descriptorWrite.dstBinding = 0;
       descriptorWrite.dstArrayElement = newID; 
       descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       descriptorWrite.descriptorCount = 1;
       descriptorWrite.pImageInfo = &imageInfo;

       if (descriptorSet != VK_NULL_HANDLE) {
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
        
        // [FIX] Assign these! The compiler warned they were unused.
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
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
        pushConstant.size = sizeof(MeshPushConstants);
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
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; 
        
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(MeshPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;

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
                depthImageView
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

    // --------------------------------------------------------------------
    // OBJ(Wavefront) Loading & Handling 
    // --------------------------------------------------------------------

    void RenderingServer::loadModel(const std::string& path) {

        if (meshMap.find(path) != meshMap.end()) {
            uint32_t existingIndex = meshMap[path];

            CBaseEntity* ent = gameWorld.CreateEntity("prop_dynamic");
            ent->modelIndex = existingIndex;
            ent->targetName = path.substr(path.find_last_of("/\\") + 1);

            std::cout << "[System] Instanced existing mesh: " << ent->targetName << std::endl;
            return;
        }

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        std::string baseDir = path.substr(0, path.find_last_of("/\\")) + "/";
        
        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), baseDir.c_str())) {
            std::cerr << "Failed to load model: " << warn << err << std::endl;
            return;
        }

        loadMaterialsFromOBJ(baseDir, materials);

        std::vector<Vertex> tempVertices;
        std::vector<uint32_t> tempIndices;
        std::unordered_map<Vertex, uint32_t> uniqueVertices{};

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex{};

                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                if (index.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    };
                } else {
                    vertex.normal = {0.0f, 0.0f, 1.0f}; 
                }

                if (index.texcoord_index >= 0) {
                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1] 
                    };
                }

                vertex.color = {1.0f, 1.0f, 1.0f};

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(tempVertices.size());
                    tempVertices.push_back(vertex);
                }
                tempIndices.push_back(uniqueVertices[vertex]);

                int matId = shape.mesh.material_ids[0];
                if (matId >= 0 && static_cast<size_t>(matId) < materials.size()) {
                    vertex.color = {materials[matId].diffuse[0], materials[matId].diffuse[1], materials[matId].diffuse[2]};
                }
            }
        }

        MeshResource newMesh{}; 
        newMesh.name = path.substr(path.find_last_of("/\\") + 1);
        newMesh.indexCount = static_cast<uint32_t>(tempIndices.size());

        // [FIX] Calculate Tangents before upload
        calculateTangents(tempVertices, tempIndices);

        createVertexBuffer(tempVertices, newMesh.vertexBuffer, newMesh.vertexBufferMemory);
        createIndexBuffer(tempIndices, newMesh.indexBuffer, newMesh.indexBufferMemory);

        meshes.push_back(newMesh);
        
        CBaseEntity* ent = gameWorld.CreateEntity("prop_dynamic");
        ent->modelIndex = meshes.size() - 1;
        ent->targetName = newMesh.name;

        if (!shapes.empty() && !shapes[0].mesh.material_ids.empty()) {
            int localMatID = shapes[0].mesh.material_ids[0];
            if (localMatID >= 0) {
                std::string matName = materials[localMatID].name;

                if (materialMap.find(matName) != materialMap.end()) {
                    uint32_t globalMatID = materialMap[matName];
                    ent->textureID = materialBank[globalMatID].textureID;
                }
            }
        }
        
        std::cout << ">>> Imported & Spawned: " << newMesh.name << std::endl;
    } 

    // --------------------------------------------------------------------
    // GLTF Loading & Handling 
    // --------------------------------------------------------------------
    
    // [HELPER] Standardize slashes to forward slashes
    std::string normalizePath(const std::string& path) {
        std::string s = path;
        for (char &c : s) {
            if (c == '\\') c = '/';
        }
        return s;
    }

    // [HELPER] Decode URL characters (e.g. "My%20Texture.png" -> "My Texture.png")
    std::string decodeUri(const std::string& uri) {
        std::string result;
        for (size_t i = 0; i < uri.length(); i++) {
            if (uri[i] == '%' && i + 2 < uri.length()) {
                std::string hex = uri.substr(i + 1, 2);
                char c = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
                result += c;
                i += 2;
            } else if (uri[i] == '+') {
                result += ' ';
            } else {
                result += uri[i];
            }
        }
        return result;
    }
    
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

        // =========================================================
        // MESH LOADING SECTION
        // =========================================================
        for (size_t i = 0; i < model.meshes.size(); i++) {
            const auto& mesh = model.meshes[i];

            for (size_t j = 0; j < mesh.primitives.size(); j++) {
                const auto& primitive = mesh.primitives[j];
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                // Helper for memory-safe attribute access
                auto getAttrData = [&](const std::string& name, int& stride) -> const uint8_t* {
                    auto it = primitive.attributes.find(name);
                    if (it == primitive.attributes.end()) return nullptr;
                    const auto& acc = model.accessors[it->second];
                    const auto& view = model.bufferViews[acc.bufferView];
                    stride = acc.ByteStride(view);
                    return &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
                };

                // 1. Get Master Vertex Count
                auto posIt = primitive.attributes.find("POSITION");
                if (posIt == primitive.attributes.end()) continue;
                const auto& posAccessor = model.accessors[posIt->second];
                int posCount = posAccessor.count;

                // 2. Setup Base Pointers and Strides
                int posStride = 0, normStride = 0, texStride = 0, tanStride = 0;
                const uint8_t* posBase = getAttrData("POSITION", posStride);
                const uint8_t* normBase = getAttrData("NORMAL", normStride);
                const uint8_t* texBase = getAttrData("TEXCOORD_0", texStride);
                const uint8_t* tanBase = getAttrData("TANGENT", tanStride); // [NEW] Tangent Data

                vertices.reserve(posCount);

                // 3. Process Vertices
                for (int v = 0; v < posCount; v++) {
                    Vertex vert{};

                    // --- POSITION ---
                    const float* p = reinterpret_cast<const float*>(posBase + (v * posStride));
                    vert.pos = { p[0], p[1], p[2] };

                    // --- NORMAL ---
                    if (normBase) {
                        const float* n = reinterpret_cast<const float*>(normBase + (v * normStride));
                        vert.normal = { n[0], n[1], n[2] };
                    } else {
                        vert.normal = { 0.0f, 0.0f, 1.0f };
                    }

                    // --- UV ---
                    if (texBase) {
                        const auto& acc = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                        if (acc.componentType == 5126) { // FLOAT
                            const float* t = reinterpret_cast<const float*>(texBase + (v * texStride));
                            vert.texCoord = { t[0], t[1] };
                        } else if (acc.componentType == 5123) { // USHORT
                            const uint16_t* t = reinterpret_cast<const uint16_t*>(texBase + (v * texStride));
                            vert.texCoord = { t[0] / 65535.0f, t[1] / 65535.0f };
                        }
                    }

                    // --- TANGENT & BITANGENT [CRITICAL FIX] ---
                    if (tanBase) {
                        // Case A: Model has baked tangents (Ideal)
                        const float* t = reinterpret_cast<const float*>(tanBase + (v * tanStride));
                        vert.tangent = { t[0], t[1], t[2] };
                        float sigma = t[3]; // W component stores sign
                        vert.bitangent = glm::cross(vert.normal, vert.tangent) * sigma;
                    } else {
                        // Case B: No tangents? Generate defaults to prevent NaN/Invisibility
                        // This ensures the TBN matrix in the shader is valid (even if not perfect)
                        vert.tangent = { 1.0f, 0.0f, 0.0f }; 
                        vert.bitangent = { 0.0f, 1.0f, 0.0f };
                        
                        // Optional: Improved fallback (Gram-Schmidt)
                        // glm::vec3 c1 = glm::cross(vert.normal, glm::vec3(0.0, 0.0, 1.0)); 
                        // glm::vec3 c2 = glm::cross(vert.normal, glm::vec3(0.0, 1.0, 0.0)); 
                        // vert.tangent = (glm::length(c1) > glm::length(c2)) ? glm::normalize(c1) : glm::normalize(c2);
                        // vert.bitangent = glm::normalize(glm::cross(vert.normal, vert.tangent));
                    }

                    vert.color = { 1.0f, 1.0f, 1.0f };
                    vertices.push_back(vert);
                }

                // 4. Fill Indices
                if (primitive.indices > -1) {
                    const auto& acc = model.accessors[primitive.indices];
                    const auto& view = model.bufferViews[acc.bufferView];
                    const uint8_t* idxData = &model.buffers[view.buffer].data[acc.byteOffset + view.byteOffset];
                    int idxStride = acc.ByteStride(view);

                    for (size_t k = 0; k < acc.count; k++) {
                        if (acc.componentType == 5125) // UINT
                            indices.push_back(*(const uint32_t*)(idxData + k * idxStride));
                        else if (acc.componentType == 5123) // USHORT
                            indices.push_back(*(const uint16_t*)(idxData + k * idxStride));
                        else if (acc.componentType == 5121) // UBYTE
                            indices.push_back(*(const uint8_t*)(idxData + k * idxStride));
                    }
                }

                // 5. Create GPU Resources
                MeshResource newMesh{};
                newMesh.name = baseDir + "_mesh_" + std::to_string(i) + "_" + std::to_string(j);
                newMesh.indexCount = static_cast<uint32_t>(indices.size());
                createVertexBuffer(vertices, newMesh.vertexBuffer, newMesh.vertexBufferMemory);
                createIndexBuffer(indices, newMesh.indexBuffer, newMesh.indexBufferMemory);

                size_t globalIndex = meshes.size();
                meshes.push_back(newMesh);

                // Create key for node mapping
                std::string meshKey = newMesh.name; // Use name as key
                meshMap[meshKey] = globalIndex;
            }
        }

        // =========================================================
        // NODE PROCESSING
        // =========================================================
        const auto& gltfScene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
        for (int nodeIdx : gltfScene.nodes) {
            processGLTFNode(model, model.nodes[nodeIdx], nullptr, baseDir, scene);
        }
    }

    // Updated processGLTFNode with new positions from push constants
    // We will be pushing a Charlie branch to experiment with Global Illumination and Ray-Tracing
    // Updates expected soon.

    // [CHANGE] Added 'parentMatrix' parameter (defaults to Identity in header)
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
        glm::vec3 skew; 
        glm::vec4 perspective;
        glm::vec3 worldPos;
        glm::quat worldRot;
        glm::vec3 worldScale;

        glm::decompose(globalMat, worldScale, worldRot, worldPos, skew, perspective);

        newEnt->origin = worldPos;
        newEnt->scale  = worldScale;
        newEnt->angles = glm::degrees(glm::eulerAngles(worldRot));

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
                                    UploadTexture((void*)img.image.data(), img.width, img.height, format, newTex.image, newTex.memory);
                                    newTex.view = createImageView(newTex.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
                                    success = true;
                                } 
                                else if (!img.uri.empty()) {
                                    if (createTextureImage(texKey, newTex.image, newTex.memory)) {
                                         newTex.view = createImageView(newTex.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
                                         success = true;
                                    }
                                }

                                if (success) {
                                    textureBank[newID] = newTex;
                                    textureMap[texKey] = newID;
                                    cache.textures[texKey] = newID; 
                                    targetEnt->textureID = newID;

                                    VkDescriptorImageInfo imageInfo{};
                                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                    imageInfo.imageView = newTex.view; 
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
    
    void RenderingServer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    bool RenderingServer::createVertexBuffer(const std::vector<Vertex>& vertices, VkBuffer& buffer, VkDeviceMemory& memory) {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stagingBuffer, stagingBufferMemory);
        
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, memory);

        copyBuffer(stagingBuffer, buffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return true;
    }

    bool RenderingServer::createIndexBuffer(const std::vector<uint32_t>& indices, VkBuffer& buffer, VkDeviceMemory& memory) {
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
        
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stagingBuffer, stagingBufferMemory);
        
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, memory);
                    
        copyBuffer(stagingBuffer, buffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return true;
    }

    void RenderingServer::updateUniformBuffer(uint32_t currentImage, Scene* scene) {
    }

    // --------------------------------------------------------------------
    // Render() / THE RENDER LOOP
    // --------------------------------------------------------------------

    // [REPLACE THE ENTIRE render() FUNCTION WITH THIS]
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
        }
    
        editorUI.Prepare(scene, mainCamera, viewportDescriptorSet);
        
        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);  
        
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        if (vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }
        
        float aspectRatio = 1.0f;
        glm::vec2 viewportSize = editorUI.GetViewportSize();
        if (viewportSize.x > 0 && viewportSize.y > 0) { aspectRatio = viewportSize.x / viewportSize.y; }
        glm::mat4 view = mainCamera.GetViewMatrix();
        glm::mat4 proj = mainCamera.GetProjectionMatrix(aspectRatio);
            
        // --- SUN LOGIC ---
        glm::vec3 sunDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.5f)); 
        glm::vec3 sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
        float sunIntensity = 1.0f;
        
        for (auto* sunEnt : scene->entities) {
            if (sunEnt && sunEnt->targetName == "Sun") {
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(sunEnt->angles.x), glm::vec3(1, 0, 0));
                rotMat = glm::rotate(rotMat, glm::radians(sunEnt->angles.y), glm::vec3(0, 1, 0));
                rotMat = glm::rotate(rotMat, glm::radians(sunEnt->angles.z), glm::vec3(0, 0, 1));
                sunDirection = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
                break; 
            }
        }
        
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
        
        transitionImageLayout(viewportImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        
        vkCmdBeginRenderPass(commandBuffers[currentFrame], &viewportPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            
            VkViewport viewport{0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f};
            vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, swapChainExtent};
            vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);
    
            // -----------------------------------------------------------------
            // DRAW SKYBOX
            // -----------------------------------------------------------------
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            {
                glm::mat4 viewNoTrans = glm::mat4(glm::mat3(view)); 
                MeshPushConstants skyPush{};
                skyPush.renderMatrix = glm::inverse(proj * viewNoTrans); 
                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                    0, sizeof(MeshPushConstants), &skyPush);
            }
            vkCmdDraw(commandBuffers[currentFrame], 3, 1, 0, 0);
        
            // =========================================================
            // PHASE 1: OPAQUE OBJECTS (Draw First -> Write Depth)
            // =========================================================
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            VkPipeline currentPipeline = graphicsPipeline;
            vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            
            for (auto* ent : scene->entities) {
                if (!ent || ent->modelIndex >= meshes.size()) continue;
                if (ent->className == "prop_water") continue; 
                
                // [CRITICAL FIX] Skip Glass Objects in this pass!
                // We must draw solid internals BEFORE the glass cover.
                if (ent->transmission > 0.0f) continue; 

                MeshResource& mesh = meshes[ent->modelIndex];
                if (mesh.vertexBuffer == VK_NULL_HANDLE) continue;
                
                // --- Pipeline Switching ---
                VkPipeline targetPipeline = graphicsPipeline;       
                if (currentPipeline != targetPipeline) {
                    vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
                    currentPipeline = targetPipeline;
                }
                
                // --- Binding ---
                VkBuffer vBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                
                // --- Transform ---
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0,0,1));
                model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0,1,0));
                model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1,0,0));
                model = glm::scale(model, ent->scale);
                
                // --- Push Constants ---
                MeshPushConstants push{};
                push.renderMatrix = proj * view * model; 
                push.modelMatrix  = model;               
                
                // [FIX] Force Opaque Mode (0.0) for this pass
                push.camPos = glm::vec4(mainCamera.GetPosition(), 0.0f);

                int selectedTexID = (ent->textureID > 0) ? ent->textureID : mesh.textureID;
                int safeTextureID = (selectedTexID < 0) ? 0 : selectedTexID;
            
                push.pbrParams = glm::vec4((float)safeTextureID, ent->roughness, ent->metallic, ent->emission);
                push.sunDir = glm::vec4(sunDirection, sunIntensity);
                push.sunColor = glm::vec4(sunColor, ent->normalStrength);
                
                // [FIX] Standard Color Tint
                push.albedoTint = glm::vec4(ent->albedoColor, 1.0f);
                
                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                    0, sizeof(MeshPushConstants), &push);
                
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
            }

            // =========================================================
            // PHASE 2: GLASS / TRANSMISSION OBJECTS (Draw Second)
            // =========================================================
            // These objects have (ent->transmission > 0.0f)
            
            for (auto* ent : scene->entities) {
                if (!ent || ent->modelIndex >= meshes.size()) continue;
                
                // [CRITICAL] ONLY process Glass here (Skip Opaque & Water)
                if (ent->transmission <= 0.0f) continue; 
                if (ent->className == "prop_water") continue;

                MeshResource& mesh = meshes[ent->modelIndex];
                if (mesh.vertexBuffer == VK_NULL_HANDLE) continue;

                // Use the same graphics pipeline, just different constants
                VkPipeline targetPipeline = graphicsPipeline;       
                if (currentPipeline != targetPipeline) {
                    vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
                    currentPipeline = targetPipeline;
                }
            
                VkBuffer vBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0,0,1));
                model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0,1,0));
                model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1,0,0));
                model = glm::scale(model, ent->scale);
            
                MeshPushConstants push{};
                push.renderMatrix = proj * view * model; 
                push.modelMatrix  = model;               

                // [FIX] Send Transmission Factor (camPos.w)
                push.camPos = glm::vec4(mainCamera.GetPosition(), ent->transmission);

                int selectedTexID = (ent->textureID > 0) ? ent->textureID : mesh.textureID;
                int safeTextureID = (selectedTexID < 0) ? 0 : selectedTexID;

                push.pbrParams = glm::vec4((float)safeTextureID, ent->roughness, ent->metallic, ent->emission);
                push.sunDir = glm::vec4(sunDirection, sunIntensity);
                push.sunColor = glm::vec4(sunColor, ent->normalStrength);

                // [FIX] Pack Attenuation Data into AlbedoTint
                // RGB = Attenuation Color, W = Attenuation Distance
                push.albedoTint = glm::vec4(ent->attenuationColor, ent->attenuationDistance);
            
                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                    0, sizeof(MeshPushConstants), &push);
                
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
            }
        
            // =========================================================
            // PHASE 3: WATER OBJECTS
            // =========================================================
            for (auto* ent : scene->entities) {
                if (!ent || ent->modelIndex >= meshes.size()) continue;
                if (ent->className != "prop_water") continue; 
            
                VkPipeline targetPipeline = waterPipeline;
                if (currentPipeline != targetPipeline) {
                    vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
                    currentPipeline = targetPipeline;
                }
            
                MeshResource& mesh = meshes[ent->modelIndex];
                VkBuffer vBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::scale(model, ent->scale);
            
                MeshPushConstants push{};
                push.renderMatrix = proj * view * model; 
                push.modelMatrix  = model;               
                
                // Water uses standard opaque mode for now, handled by its own shader logic
                push.camPos = glm::vec4(mainCamera.GetPosition(), 0.0f);

                int selectedTexID = (ent->textureID > 0) ? ent->textureID : mesh.textureID;
                int safeTextureID = (selectedTexID < 0) ? 0 : selectedTexID;
                push.pbrParams = glm::vec4((float)safeTextureID, ent->roughness, ent->metallic, ent->emission);
            
                if (safeTextureID == 0 && this->waterTextureID > 0) {
                    push.pbrParams.x = (float)this->waterTextureID;
                }
                float time = (SDL_GetTicks() / 1000.0f) * 0.5f; 
                push.pbrParams.w = time; 
                
                push.sunDir = glm::vec4(sunDirection, sunIntensity);
                push.sunColor = glm::vec4(sunColor, ent->normalStrength);
                push.albedoTint = glm::vec4(ent->albedoColor, 1.0f);
            
                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, 
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                                    0, sizeof(MeshPushConstants), &push);
                
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
            }
        
            vkCmdEndRenderPass(commandBuffers[currentFrame]);
        
        // ---------------------------------------------------------
        // TRANSITION: Scene Image -> Readable by Bloom
        // ---------------------------------------------------------
        transitionImageLayout(viewportImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        transitionImageLayout(bloomBrightImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        
        // =========================================================
        // PASS 1.5: BLOOM EXTRACTION
        // =========================================================
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
        // TRANSITION: Bloom Image -> Readable by Composite
        // ---------------------------------------------------------
        transitionImageLayout(bloomBrightImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
        // =========================================================
        // PASS 2: POST-PROCESSING (Composite) -> finalFramebuffer
        // =========================================================
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
            
        // =========================================================
        // PASS 3: UI & SWAPCHAIN -> Screen
        // =========================================================
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

    void RenderingServer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image!");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory!");
        }

        vkBindImageMemory(device, image, imageMemory, 0);
        }

        bool RenderingServer::createHDRImage(const std::string& path, VkImage& image, VkDeviceMemory& memory) {
        int width, height, nrComponents;
        float* data = stbi_loadf(path.c_str(), &width, &height, &nrComponents, 4);

        if (!data) {
            std::cerr << "[Rendering] Failed to load HDR image: " << path << std::endl;
            return false;
        }

        VkDeviceSize imageSize = width * height * 4 * sizeof(float);

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
            stagingBuffer, stagingBufferMemory);

        void* mappedData;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &mappedData);
        memcpy(mappedData, data, imageSize);
        vkUnmapMemory(device, stagingBufferMemory);

        stbi_image_free(data);

        createImage(width, height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        transitionImageLayout(image, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, image, width, height);
        transitionImageLayout(image, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return true;
    }

    void RenderingServer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory");
        }

        vkBindBufferMemory(device, buffer, bufferMemory, 0);
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

        // 1. SCENE IMAGE (HDR)
        createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, viewportImage, viewportImageMemory);

        // Transition HDR image
        transitionImageLayout(viewportImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        viewportImageView = createImageView(viewportImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

        // 2. FINAL IMAGE (LDR) - [FIX] Add Transition Here!
        createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, finalImage, finalImageMemory);

        // [CRITICAL FIX] Transition Final Image so ImGui doesn't crash reading Undefined layout
        transitionImageLayout(finalImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        finalImageView = createImageView(finalImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

        // 3. DEPTH IMAGE
        createImage(width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, viewportDepthImage, viewportDepthImageMemory); // [FIX] Removed extra args

        viewportDepthImageView = createImageView(viewportDepthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

        // 4. RENDER PASS 1: 3D SCENE
        VkAttachmentDescription attachments[2] = {};

        // Color
        attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Depth
        attachments[1].format = VK_FORMAT_D32_SFLOAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &viewportRenderPass) != VK_SUCCESS) return false;

        std::array<VkImageView, 2> fbAttachments = { viewportImageView, viewportDepthImageView };
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = viewportRenderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments.data();
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;
        vkCreateFramebuffer(device, &fbInfo, nullptr, &viewportFramebuffer);

        // 5. RENDER PASS 2: COMPOSITE
        VkAttachmentDescription compositeAttachment{};
        compositeAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
        compositeAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        compositeAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        compositeAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        compositeAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        compositeAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        compositeAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        compositeAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference compositeRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription compositeSubpass{};
        compositeSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        compositeSubpass.colorAttachmentCount = 1;
        compositeSubpass.pColorAttachments = &compositeRef;
        
        VkRenderPassCreateInfo compositePassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        compositePassInfo.attachmentCount = 1;
        compositePassInfo.pAttachments = &compositeAttachment;
        compositePassInfo.subpassCount = 1;
        compositePassInfo.pSubpasses = &compositeSubpass;

        if (vkCreateRenderPass(device, &compositePassInfo, nullptr, &compositeRenderPass) != VK_SUCCESS) return false;

        VkFramebufferCreateInfo compositeFbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        compositeFbInfo.renderPass = compositeRenderPass;
        compositeFbInfo.attachmentCount = 1;
        compositeFbInfo.pAttachments = &finalImageView;
        compositeFbInfo.width = width;
        compositeFbInfo.height = height;
        compositeFbInfo.layers = 1;
        vkCreateFramebuffer(device, &compositeFbInfo, nullptr, &finalFramebuffer);

        // [FIX] CREATE SAMPLER (Previous step missed this inside the function body?)
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(device, &samplerInfo, nullptr, &viewportSampler) != VK_SUCCESS) return false;

        return true;
    }

    void RenderingServer::updateCompositeDescriptors() {
       // Only update if resources exist
       if (viewportImageView == VK_NULL_HANDLE || bloomBrightImageView == VK_NULL_HANDLE) return;

       VkDescriptorImageInfo compositeInfos[2] = {};
       // Binding 0: The 3D Scene
       compositeInfos[0].sampler = viewportSampler;
       compositeInfos[0].imageView = viewportImageView;
       compositeInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

       // Binding 1: The Bloom Brightness Buffer
       compositeInfos[1].sampler = viewportSampler;
       compositeInfos[1].imageView = bloomBrightImageView;
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

       // 1. Create the Bright Image (Target for bloom extraction)
       createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bloomBrightImage, bloomBrightImageMemory);
       
       // [FIX] ADD THIS LINE HERE -----------------------------------------
       // Transition the bloom image to READ_ONLY so the Composite Pass doesn't crash
       // when it tries to sample it (even if it's empty/black for now).
       transitionImageLayout(bloomBrightImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
       // ------------------------------------------------------------------

       bloomBrightImageView = createImageView(bloomBrightImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

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
       fbInfo.pAttachments = &bloomBrightImageView;
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
        // We must create a new one pointing to the NEW finalImageView.
        if (finalImageView != VK_NULL_HANDLE) {
            viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
                viewportSampler, 
                finalImageView, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }
    }

    void RenderingServer::cleanupSwapChain() {
        // 1. Destroy Standard Swapchain Resources
        for (auto framebuffer : swapChainFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }

        if (swapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapChain, nullptr);
            swapChain = VK_NULL_HANDLE;
        }

        // 2. Destroy Main Depth Resources (Created in createDepthResources)
        if (depthImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, depthImageView, nullptr); depthImageView = VK_NULL_HANDLE; }
        if (depthImage != VK_NULL_HANDLE) { vkDestroyImage(device, depthImage, nullptr); depthImage = VK_NULL_HANDLE; }
        if (depthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, depthImageMemory, nullptr); depthImageMemory = VK_NULL_HANDLE; }

        // =========================================================
        // CLEANUP CUSTOM OFFSCREEN RESOURCES
        // =========================================================

        // [FIX] Destroy Viewport Sampler
        if (viewportSampler != VK_NULL_HANDLE) { vkDestroySampler(device, viewportSampler, nullptr); viewportSampler = VK_NULL_HANDLE; }

        // A. Framebuffers
        if (viewportFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, viewportFramebuffer, nullptr); viewportFramebuffer = VK_NULL_HANDLE; }
        if (finalFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, finalFramebuffer, nullptr); finalFramebuffer = VK_NULL_HANDLE; }
        
        // [FIX] Destroy Bloom Framebuffer
        if (bloomFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, bloomFramebuffer, nullptr); bloomFramebuffer = VK_NULL_HANDLE; }

        // B. Render Passes
        if (viewportRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, viewportRenderPass, nullptr); viewportRenderPass = VK_NULL_HANDLE; }
        if (compositeRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, compositeRenderPass, nullptr); compositeRenderPass = VK_NULL_HANDLE; }
        
        // [FIX] Destroy Bloom RenderPass
        if (bloomRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, bloomRenderPass, nullptr); bloomRenderPass = VK_NULL_HANDLE; }

        // C. Image Views
        if (viewportImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, viewportImageView, nullptr); viewportImageView = VK_NULL_HANDLE; }
        if (viewportDepthImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, viewportDepthImageView, nullptr); viewportDepthImageView = VK_NULL_HANDLE; }
        if (finalImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, finalImageView, nullptr); finalImageView = VK_NULL_HANDLE; }
        
        // [FIX] Destroy Bloom Views
        if (bloomBrightImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, bloomBrightImageView, nullptr); bloomBrightImageView = VK_NULL_HANDLE; }

        // D. Images & Memory
        if (viewportImage != VK_NULL_HANDLE) { vkDestroyImage(device, viewportImage, nullptr); viewportImage = VK_NULL_HANDLE; }
        if (viewportImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, viewportImageMemory, nullptr); viewportImageMemory = VK_NULL_HANDLE; }

        if (viewportDepthImage != VK_NULL_HANDLE) { vkDestroyImage(device, viewportDepthImage, nullptr); viewportDepthImage = VK_NULL_HANDLE; }
        if (viewportDepthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, viewportDepthImageMemory, nullptr); viewportDepthImageMemory = VK_NULL_HANDLE; }

        if (finalImage != VK_NULL_HANDLE) { vkDestroyImage(device, finalImage, nullptr); finalImage = VK_NULL_HANDLE; }
        if (finalImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, finalImageMemory, nullptr); finalImageMemory = VK_NULL_HANDLE; }

        // [FIX] Destroy Bloom Images
        if (bloomBrightImage != VK_NULL_HANDLE) { vkDestroyImage(device, bloomBrightImage, nullptr); bloomBrightImage = VK_NULL_HANDLE; }
        if (bloomBrightImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, bloomBrightImageMemory, nullptr); bloomBrightImageMemory = VK_NULL_HANDLE; }
    }

    void RenderingServer::shutdown() {
       if (device != VK_NULL_HANDLE) {
           vkDeviceWaitIdle(device);
          
           editorUI.Shutdown(device);

           // 2. Destroy Pipelines
           if (skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, skyPipeline, nullptr);
           if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
           
           if (transparentPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, transparentPipeline, nullptr);
           if (waterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, waterPipeline, nullptr);
           if (bloomPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, bloomPipeline, nullptr);
           if (compositePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, compositePipeline, nullptr);

           // 3. Destroy Layouts
           if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
           if (compositePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
           if (postProcessLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, postProcessLayout, nullptr);
           
           // 4. Destroy Main RenderPass
           // [NOTE] viewportRenderPass and bloomRenderPass are handled in cleanupSwapChain()!
           // if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
           
           // 5. Cleanup Meshes
           for (auto& mesh : meshes) {
               if (mesh.vertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, mesh.vertexBuffer, nullptr); mesh.vertexBuffer = VK_NULL_HANDLE; }
               if (mesh.vertexBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(device, mesh.vertexBufferMemory, nullptr); mesh.vertexBufferMemory = VK_NULL_HANDLE; }
               if (mesh.indexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, mesh.indexBuffer, nullptr); mesh.indexBuffer = VK_NULL_HANDLE; }
               if (mesh.indexBufferMemory != VK_NULL_HANDLE) { vkFreeMemory(device, mesh.indexBufferMemory, nullptr); mesh.indexBufferMemory = VK_NULL_HANDLE; }
           }
           meshes.clear(); 
           gameWorld.Clear(); 
        
           // 6. Cleanup Textures
           for (const auto& tex : textureBank) {
               if (tex.image != VK_NULL_HANDLE && tex.image != textureImage) {
                   vkDestroyImageView(device, tex.view, nullptr);
                   vkDestroyImage(device, tex.image, nullptr);
                   vkFreeMemory(device, tex.memory, nullptr);
               }
           }
           textureBank.clear();
           textureMap.clear();
        
           // 7. Cleanup Default Texture
           if (textureImageView != VK_NULL_HANDLE) vkDestroyImageView(device, textureImageView, nullptr);
           if (textureSampler != VK_NULL_HANDLE) vkDestroySampler(device, textureSampler, nullptr);
           if (textureImage != VK_NULL_HANDLE) vkDestroyImage(device, textureImage, nullptr);
           if (textureImageMemory != VK_NULL_HANDLE) vkFreeMemory(device, textureImageMemory, nullptr);
        
           // 8. Cleanup Descriptors
           if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
           
           
           
           if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);

           // Cleanup Sky Resources
           if (skySampler != VK_NULL_HANDLE) vkDestroySampler(device, skySampler, nullptr);
           if (skyImageView != VK_NULL_HANDLE) vkDestroyImageView(device, skyImageView, nullptr);
           if (skyImage != VK_NULL_HANDLE) vkDestroyImage(device, skyImage, nullptr);
           if (skyImageMemory != VK_NULL_HANDLE) vkFreeMemory(device, skyImageMemory, nullptr);

           // 9. Cleanup Swapchain
           cleanupSwapChain();

           // 10. Cleanup Sync Objects

           if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);

           for (size_t i = 0; i < imageAvailableSemaphores.size(); i++) { 
               vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
               vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
           }
           for (size_t i = 0; i < inFlightFences.size(); i++) {
               vkDestroyFence(device, inFlightFences[i], nullptr);
           }
        
           if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
        
           vkDestroyDevice(device, nullptr);
           device = VK_NULL_HANDLE;
       }

       if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
       if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    }
}
