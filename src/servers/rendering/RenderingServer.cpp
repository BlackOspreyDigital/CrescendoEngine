#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "src/IO/Serializer.hpp"
#include "include/portable-file-dialogs.h"
#include <unordered_map>
#include "servers/rendering/RenderingServer.hpp"
#include "servers/display/DisplayServer.hpp"
#include "scene/Scene.hpp"
#include "Vertex.hpp"
#include <algorithm>
#include <SDL2/SDL_vulkan.h>
#include <SDL2/SDL_image.h>
#include <iostream>
#include <fstream>
#include <set>
#include <cstring> 
#include <array>
#include <chrono>
#include "src/IO/Serializer.hpp"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/intersect.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/common.hpp> // For abs/PI
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "deps/imgui/ImGuizmo.h"
#include "deps/tinygltf/tiny_gltf.h"
#include "deps/json/json.hpp"

namespace Crescendo {
    /*
    struct Ray {
        glm::vec3 origin;
        glm::vec3 direction;
    };

    Ray ScreenToWorldRay(glm::vec2 mousePos, glm::vec2 viewportSize, glm::mat4 view, glm::mat4 projection) {
        float x = (2.0f * mousePos.x) / viewportSize.x - 1.0f; 
        float y = 1.0f - (2.0f * mousePos.y) / viewportSize.y; 

        glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f); 

        glm::vec4 rayEye = glm::inverse(projection) * rayClip; 
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

        glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
        rayWorld = glm::normalize(rayWorld);

        Ray r;
        r.origin = glm::vec3(glm::inverse(view)[3]);
        r.direction = rayWorld;
        return r;
    }

    bool RayPlaneIntersection(Ray ray, glm::vec3 planeNormal, float planeHeight, glm::vec3& outIntersection) {
        float denom = glm::dot(planeNormal, ray.direction);

        if (abs(denom) > 1e-6) {
            glm::vec3 planeCenter = glm::vec3(0, 0, planeHeight);
            if (planeNormal.y == 1.0f) planeCenter = glm::vec3(0, planeHeight, 0);

            glm::vec3 p010 = planeCenter - ray.origin;
            float t= glm::dot(p010, planeNormal) / denom;

            if (t >= 0) {
                outIntersection = ray.origin + (ray.direction * t);
                return true;
            }
        }
        return false;
    }

    void TextCentered(std::string text) {
        auto windowWidth = ImGui::GetWindowSize().x;
        auto textWidth = ImGui::CalcTextSize(text.c_str()).x;

        ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
        ImGui::Text("%s", text.c_str());
    } */

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
        commandPool(VK_NULL_HANDLE),
        descriptorSet(VK_NULL_HANDLE), // [FIX] Initialize this to NULL!
        viewportImage(VK_NULL_HANDLE),
        viewportImageMemory(VK_NULL_HANDLE),
        viewportImageView(VK_NULL_HANDLE),
        viewportSampler(VK_NULL_HANDLE),
        viewportFramebuffer(VK_NULL_HANDLE),
        viewportRenderPass(VK_NULL_HANDLE),
        viewportDescriptorSet(VK_NULL_HANDLE) 
    {
    }

    // Reorganized during IMGUI refactor
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

        // [CRITICAL FIX] Create Descriptor Pool HERE (Before UI)
        // ImGui needs this to exist to allocate fonts and textures.
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

        // Now safe to call, because Pool exists!
        viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
            viewportSampler, 
            viewportImageView, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        // ---------------------------------------------------------
        // STEP 4: Pipelines
        // ---------------------------------------------------------
        if (!createGraphicsPipeline()) return false;
        if (!createGridPipeline()) return false;
        if (!createWaterPipeline()) return false;        
        if (!createTransparentPipeline()) return false;

        if (!createFramebuffers()) return false;

        // ---------------------------------------------------------
        // STEP 5: Assets & Descriptors
        // ---------------------------------------------------------
        std::cout << "[4/5] Loading Assets..." << std::endl;

        // 1. Default Texture
        createDefaultTexture();

        // 2. Global Descriptors (Pool already created above!)
        // [FIX] Added braces check
        if (!createDescriptorSets()) { 
            std::cout << "!! Failed to create Global Descriptor Set" << std::endl; 
            return false; 
        }

        // 3. Load Game Assets
        createProceduralGrid(); 
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

        return true;
    }

    void RenderingServer::createProceduralGrid() {
        float size = 1000.0f;
        float z = 0.0f; 

        std::vector<Vertex> gridVertices = {
            { {-size, -size, z}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {-size, -size} }, 
            { { size, -size, z}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, { size, -size} }, 
            { { size,  size, z}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, { size,  size} }, 
            { {-size,  size, z}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {-size,  size} }  
        };

        std::vector<uint32_t> gridIndices = { 0, 1, 2, 2, 3, 0 };

        MeshResource gridMesh{}; 
        gridMesh.name = "Internal_Grid";
        gridMesh.indexCount = static_cast<uint32_t>(gridIndices.size());

        createVertexBuffer(gridVertices, gridMesh.vertexBuffer, gridMesh.vertexBufferMemory);
        createIndexBuffer(gridIndices, gridMesh.indexBuffer, gridMesh.indexBufferMemory);

        meshes.push_back(gridMesh);
        std::cout << "[System] Procedural Grid Mesh Generated." << std::endl;
    }

    void RenderingServer::createWaterMesh() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        int width = 1000;
        int depth = 1000;
        float spacing = 2.0f;
        
        float startX = -(width * spacing) / 2.0f;
        float startY = -(depth * spacing) / 2.0f;

        for (int z = 0; z <= depth; z++) {
            for (int x = 0; x <= width; x++) {
                Vertex v{};
                v.pos = glm::vec3(startX + x * spacing, startY + z * spacing, 0.0f);
                v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                v.color = glm::vec3(1.0f);
                v.texCoord = glm::vec2((float)x / width, (float)z / depth);
                vertices.push_back(v);
            }
        }

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
        appInfo.pApplicationName = "Crescendo Engine v0.2a";
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
        // 1. Create a 1x1 white pixel (R, G, B, A)
        uint32_t pixel = 0xFFFFFFFF; 
        
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, 4, 0, &data);
        memcpy(data, &pixel, 4);
        vkUnmapMemory(device, stagingBufferMemory);
        
        // 2. Create the Image Object (Using class member variables for storage)
        createImage(1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);
        
        // 3. Transition Layout & Copy Buffer
        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, textureImage, 1, 1);
        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        
        // 4. Create View & Sampler
        textureImageView = createTextureImageView(textureImage);
        createTextureSampler(); 

        // 5. Fill the Texture Bank with this Dummy Texture
        // This PREVENTS THE CRASH by ensuring every slot [0..99] is valid.
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
        
        // 1. Load with STB (Standard, Safe)
        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        if (!pixels) {
            // If this fails, we return false. 
            // The acquireTexture function will catch this and assign the Default Texture (ID 0).
            std::cerr << "[Texture] Warning: Failed to load image (STB): " << path << std::endl;
            return false;
        }

        // 2. Create Staging Buffer
        VkDeviceSize imageSize = texWidth * texHeight * 4;
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     stagingBuffer, stagingBufferMemory);

        // 3. Upload to Staging
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        // 4. Safe Cleanup
        stbi_image_free(pixels); 

        // 5. Create GPU Image
        createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, 
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        // 6. Copy & Transition
        transitionImageLayout(image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
        transitionImageLayout(image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 7. Cleanup Buffer
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
        // [FIX] We removed the UBO binding because we use Push Constants now.
        // We ONLY define the Texture Array at Binding 0.

        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0; // [CRITICAL] Textures must be Binding 0
        samplerLayoutBinding.descriptorCount = MAX_TEXTURES;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // Only pixel shader needs textures

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerLayoutBinding;

        return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) == VK_SUCCESS;
    }

    bool RenderingServer::createDescriptorPool() {
        // We need space for:
        // 1. Our Global Texture Array (1 Set, 100 Descriptors)
        // 2. ImGui Fonts & UI Elements (Requires its own sets, usually ~1000 is safe for UI)
        
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES + 1000 }, // Textures + ImGui Fonts
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 }, // ImGui often needs these
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        
        // [CRITICAL FIX] Increase Max Sets
        // Was 1. Now 1000+ to allow ImGui to allocate sets too.
        poolInfo.maxSets = 1000 * poolSizes.size(); 
        
        // [IMPORTANT] ImGui requires this flag to free its sets individually
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        return vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) == VK_SUCCESS;
    }

    bool RenderingServer::createDescriptorSets() {
       VkDescriptorSetAllocateInfo allocInfo{};
       allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
       allocInfo.descriptorPool = descriptorPool;
       allocInfo.descriptorSetCount = 1;
       allocInfo.pSetLayouts = &descriptorSetLayout;

       if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) return false;

       // Prepare info for ALL textures (initially pointing to default)
       std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
       for (int i = 0; i < MAX_TEXTURES; i++) {
           imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
           // Ensure textureBank was filled by createDefaultTexture() before this!
           imageInfos[i].imageView = textureBank[i].view; 
           imageInfos[i].sampler = textureSampler;
       }

       VkWriteDescriptorSet descriptorWrite{};
       descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
       descriptorWrite.dstSet = descriptorSet;
       descriptorWrite.dstBinding = 0;
       descriptorWrite.dstArrayElement = 0; // Start at index 0
       descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       
       // [CRITICAL] We must update the ENTIRE array, not just one.
       descriptorWrite.descriptorCount = static_cast<uint32_t>(MAX_TEXTURES); 
       descriptorWrite.pImageInfo = imageInfos.data();

       vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
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

       // [FIX] Ensure ID never exceeds 99 (MAX_TEXTURES - 1)
       // 0 is default, so we have 1..99 available.
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

       newTex.view = createTextureImageView(newTex.image);
       
       // [SAFETY] Double check bounds before array access
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

       // [SAFETY] Check descriptor set validity
       if (descriptorSet != VK_NULL_HANDLE) {
           vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
       } else {
           std::cerr << "[Render] Error: Attempted to update NULL Descriptor Set" << std::endl;
       }

       return newID;
    }

    bool RenderingServer::createGraphicsPipeline() {
        auto vertShaderCode = readFile("assets/shaders/vert.spv");
        auto fragShaderCode = readFile("assets/shaders/frag.spv");

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
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Changed for viewport test
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
        colorBlendAttachment.blendEnable = VK_FALSE;
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

        auto skyVertCode = readFile("assets/shaders/sky_vert.spv");
        auto skyFragCode = readFile("assets/shaders/sky_frag.spv");
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
        depthStencil.depthWriteEnable = VK_FALSE;
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
        auto vertShaderCode = readFile("assets/shaders/vert.spv");
        auto fragShaderCode = readFile("assets/shaders/frag.spv");

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

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &transparentPipeline) != VK_SUCCESS) {
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return true;
    }

    bool RenderingServer::createWaterPipeline() {
        auto vertShaderCode = readFile("assets/shaders/water_vert.spv");
        auto fragShaderCode = readFile("assets/shaders/water_frag.spv");

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

    bool RenderingServer::createGridPipeline() {
        auto vertShaderCode = readFile("assets/shaders/vert.spv");
        auto fragShaderCode = readFile("assets/shaders/grid.spv");

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

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gridPipeline) != VK_SUCCESS) {
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
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
    
    // [FIX] Ensure this helper is defined
    std::string normalizePath(const std::string& path) {
        std::string s = path;
        for (char &c : s) {
            if (c == '\\') c = '/';
        }
        return s;
    }

    // [FIX] Updated loadGLTF with Scene Safety Check
    void RenderingServer::loadGLTF(const std::string& filePath, Scene* scene) {
        // SAFETY CHECK: If scene is null, we cannot proceed
        if (scene == nullptr) {
            std::cerr << "[GLTF Error] 'scene' pointer is NULL! Cannot load model." << std::endl;
            return;
        }

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        std::cout << "[System] Loading GLTF: " << filePath << std::endl;

        bool ret = false;
        if (filePath.find(".glb") != std::string::npos) {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, filePath);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, filePath);
        }

        if (!warn.empty()) std::cout << "[GLTF Warn] " << warn << std::endl;
        if (!err.empty()) std::cerr << "[GLTF Err] " << err << std::endl;

        if (!ret) {
            std::cerr << "[GLTF Failed] Could not parse: " << filePath << std::endl;
            return;
        }

        // 1. Base Dir
        std::string baseDir = "";
        size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            baseDir = filePath.substr(0, lastSlash);
        }
        baseDir = normalizePath(baseDir);

        // 2. Load Textures
        for (const auto& tex : model.textures) {
            if (tex.source > -1) {
                const auto& img = model.images[tex.source];
                if (!img.uri.empty()) {
                    // 
                    std::string texPath = baseDir + "/" + decodeUri(img.uri);
                    acquireTexture(texPath); 
                }
            }
        }

        // 3. Meshes (Simplified loop for brevity, keep your vertex loading logic!)
        // Note: Assuming you have the vertex loading loop here. 
        // If you deleted it, paste the vertex loading block back from previous steps!
        // ... (Vertex Loading Logic) ...
        
        // 4. Process Nodes
        const tinygltf::Scene& gltfScene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
        for (size_t i = 0; i < gltfScene.nodes.size(); i++) {
            processGLTFNode(model, model.nodes[gltfScene.nodes[i]], nullptr, baseDir, scene);
        }
        
        std::cout << "[System] GLTF Loaded Successfully." << std::endl;
    }

    // [FIX] Updated processGLTFNode with Material Color Support
    void RenderingServer::processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene) {
        if (!scene) return; // Extra safety

        CBaseEntity* newEnt = scene->CreateEntity("prop_static"); 
        newEnt->targetName = node.name; 
        newEnt->textureID = 0; 

        if (parent) {
            newEnt->moveParent = parent;
            parent->children.push_back(newEnt);
            newEnt->origin = parent->origin; 
        }

        // Transform
        glm::vec3 translation(0.0f);
        glm::quat rotation = glm::identity<glm::quat>();
        glm::vec3 scale(1.0f);

        if (node.matrix.size() == 16) {
            glm::mat4 mat = glm::make_mat4(node.matrix.data());
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(mat, scale, rotation, translation, skew, perspective);
        } else {
            if (node.translation.size() == 3) translation = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            if (node.rotation.size() == 4) rotation = glm::quat((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]);
            if (node.scale.size() == 3) scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        }

        newEnt->origin += translation;
        newEnt->scale = scale;
        newEnt->angles = glm::degrees(glm::eulerAngles(rotation)); 

        if (node.mesh > -1) {
            const tinygltf::Mesh& mesh = model.meshes[node.mesh];
            
            for (size_t i = 0; i < mesh.primitives.size(); i++) {
                const auto& primitive = mesh.primitives[i];
                CBaseEntity* targetEnt = (i == 0) ? newEnt : scene->CreateEntity("prop_submesh");

                if (i > 0) {
                    targetEnt->moveParent = newEnt;
                    newEnt->children.push_back(targetEnt);
                    targetEnt->origin = newEnt->origin; 
                    targetEnt->angles = newEnt->angles;
                    targetEnt->scale = newEnt->scale;
                }

                // Material Logic
                if (primitive.material >= 0) {
                    const tinygltf::Material& mat = model.materials[primitive.material];
                    
                    targetEnt->roughness = (float)mat.pbrMetallicRoughness.roughnessFactor;
                    targetEnt->metallic = (float)mat.pbrMetallicRoughness.metallicFactor;

                    // [FIX] Load Base Color (Albedo)
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
                        std::string texPath = baseDir + "/" + decodeUri(img.uri);
                        
                        // Check map
                        if (textureMap.find(texPath) != textureMap.end()) {
                            targetEnt->textureID = textureMap[texPath];
                        } else {
                            // Fallback load
                            targetEnt->textureID = acquireTexture(texPath);
                        }
                    } else {
                        targetEnt->textureID = 0; // Use default if no texture
                    }
                }
                
                // Assign Mesh Index
                // Note: We use the simpler logic here. If you have the meshMap logic, keep it!
                 std::string meshKey = normalizePath(baseDir) + "_mesh_" + std::to_string(node.mesh) + "_" + std::to_string(i); 
                 if (meshMap.find(meshKey) != meshMap.end()) {
                     targetEnt->modelIndex = meshMap[meshKey];
                 }
            }
        }

        for (int childId : node.children) {
            processGLTFNode(model, model.nodes[childId], newEnt, baseDir, scene);
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
    // Render() / Draw Logic 
    // --------------------------------------------------------------------
    
    // [FIX] This is the ONLY definition of render now
    void RenderingServer::render(Scene* scene) {
        if (!scene) return;

        // ---------------------------------------------------------
        // 1. SYNC: Wait for Previous Frame
        // ---------------------------------------------------------
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        // ---------------------------------------------------------
        // 2. ACQUIRE IMAGE
        // ---------------------------------------------------------
        uint32_t imageIndex;
        // [FIX] Signal imageAvailableSemaphores[currentFrame] (NOT imageIndex)
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, 
                                                imageAvailableSemaphores[currentFrame], 
                                                VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain(display_ref->get_window());
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        // Only reset fences if we are actually going to submit work
        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        // ---------------------------------------------------------
        // 3. RECORD COMMANDS
        // ---------------------------------------------------------
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

        // Matrix Setup
        float aspectRatio = (float)swapChainExtent.width / (float)swapChainExtent.height;
        glm::mat4 view = mainCamera.GetViewMatrix();
        glm::mat4 proj = mainCamera.GetProjectionMatrix(aspectRatio);

        // =========================================================
        // PASS 1: 3D WORLD (Offscreen Viewport)
        // =========================================================
        VkRenderPassBeginInfo viewportPassInfo{};
        viewportPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        viewportPassInfo.renderPass = viewportRenderPass;
        viewportPassInfo.framebuffer = viewportFramebuffer;
        viewportPassInfo.renderArea.extent = swapChainExtent;

        std::array<VkClearValue, 2> viewportClearValues{};
        viewportClearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        viewportClearValues[1].depthStencil = {1.0f, 0};
        viewportPassInfo.clearValueCount = 2;
        viewportPassInfo.pClearValues = viewportClearValues.data();

        vkCmdBeginRenderPass(commandBuffers[currentFrame], &viewportPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            // Viewport & Scissor
            VkViewport viewport{};
            viewport.x = 0.0f; viewport.y = 0.0f;
            viewport.width = (float)swapChainExtent.width;
            viewport.height = (float)swapChainExtent.height;
            viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0}; scissor.extent = swapChainExtent;
            vkCmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);

            // --- SUB-PASS A: SKY ---
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
            glm::mat4 invViewProj = glm::inverse(proj * view);

            MeshPushConstants skyPush{};
            skyPush.renderMatrix = invViewProj;
            skyPush.camPos = glm::vec4(mainCamera.GetPosition(), 1.0f);
            skyPush.sunDir = glm::vec4(sunDirection, sunIntensity);
            skyPush.sunColor = glm::vec4(sunColor, 1.0f);

            vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants), &skyPush);
            vkCmdDraw(commandBuffers[currentFrame], 3, 1, 0, 0);

            // --- SUB-PASS B: OPAQUE OBJECTS ---
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            // [Check] Global Descriptor
            if (descriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            }

            if (!scene->entities.empty()) {
                for (auto* ent : scene->entities) {
                    if (!ent) continue;
                    if (ent->targetName == "prop_water") continue; // Skip water here

                    // [SAFETY] Bounds Check
                    if (ent->modelIndex >= meshes.size()) continue;

                    MeshResource& mesh = meshes[ent->modelIndex];
                    if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE) continue;

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

                    MeshPushConstants pushConst{};
                    pushConst.renderMatrix = proj * view * model;
                    pushConst.camPos = glm::vec4(mainCamera.GetPosition(), 1.0f);

                    int safeTexID = ent->textureID;
                    if (safeTexID < 0 || safeTexID >= MAX_TEXTURES) safeTexID = 0;

                    pushConst.pbrParams = glm::vec4((float)safeTexID, ent->roughness, ent->metallic, 0.0f);
                    pushConst.sunDir = glm::vec4(sunDirection, sunIntensity);
                    pushConst.sunColor = glm::vec4(sunColor, 1.0f);

                    vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants), &pushConst);
                    vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
                }
            }

            // --- SUB-PASS C: WATER ---
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);
            if (descriptorSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            }

            for (auto* ent : scene->entities) {
                if (!ent) continue;
                if (ent->targetName != "prop_water") continue; // Only water here

                if (ent->modelIndex >= meshes.size()) continue;

                MeshResource& mesh = meshes[ent->modelIndex];
                if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE) continue;

                VkBuffer vBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::scale(model, ent->scale);

                MeshPushConstants pushConst{};
                pushConst.renderMatrix = proj * view * model;
                pushConst.camPos = glm::vec4(mainCamera.GetPosition(), 1.0f);

                int safeWaterID = waterTextureID;
                if (safeWaterID < 0 || safeWaterID >= MAX_TEXTURES) safeWaterID = 0;

                pushConst.pbrParams = glm::vec4((float)safeWaterID, SDL_GetTicks() / 1000.0f, 0.0f, 0.0f);
                pushConst.sunDir = glm::vec4(sunDirection, sunIntensity);
                pushConst.sunColor = glm::vec4(sunColor, 1.0f);

                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants), &pushConst);
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
            }

        vkCmdEndRenderPass(commandBuffers[currentFrame]);

        // =========================================================
        // PASS 2: ONSCREEN (UI & ImGui)
        // =========================================================
        VkRenderPassBeginInfo screenPassInfo{};
        screenPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        screenPassInfo.renderPass = renderPass;
        screenPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        screenPassInfo.renderArea.offset = {0, 0};
        screenPassInfo.renderArea.extent = swapChainExtent;
        screenPassInfo.clearValueCount = 0; // Dont clear, just draw on top of what was there (or if loadOp is clear)

        vkCmdBeginRenderPass(commandBuffers[currentFrame], &screenPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            if (viewportDescriptorSet != VK_NULL_HANDLE) {
                editorUI.Prepare(scene, mainCamera, viewportDescriptorSet);
            } else {
                std::cerr << "[Render] Warning: Viewport Descriptor is NULL" << std::endl;
            }

            bool showConsole = true;
            gameConsole.Draw("Crescendo Console", &showConsole);
            editorUI.Render(commandBuffers[currentFrame]);

        vkCmdEndRenderPass(commandBuffers[currentFrame]);

        if (vkEndCommandBuffer(commandBuffers[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }

        // ---------------------------------------------------------
        // 4. SUBMIT & PRESENT (THE FIX IS HERE)
        // ---------------------------------------------------------
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        // Wait for the image to be available
        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

        // [CRITICAL FIX] Signal the semaphore for THIS FRAME, not the image index.
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command!");
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain(display_ref->get_window());
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
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
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else {
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

    RenderingServer::QueueFamilyIndices RenderingServer::findQueueFamilies(VkPhysicalDevice device) {
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

    RenderingServer::SwapChainSupportDetails RenderingServer::querySwapChainSupport(VkPhysicalDevice device) {
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
        // [FIX] Use actual SwapChain dimensions, not hardcoded 1920x1080
        uint32_t width = swapChainExtent.width;
        uint32_t height = swapChainExtent.height;

        // 1. Create Color Image
        createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, viewportImage, viewportImageMemory);

        transitionImageLayout(viewportImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        viewportImageView = createImageView(viewportImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

        // 2. Create Depth Image (Matched to SwapChain Size)
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        createImage(width, height, depthFormat, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, viewportDepthImage, viewportDepthImageMemory);
        
        viewportDepthImageView = createImageView(viewportDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // 3. Create Sampler 
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &viewportSampler) != VK_SUCCESS) return false;

        // [IMPORTANT] Add to ImGui (Only do this once or ensure cleanup!)
        viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(viewportSampler, viewportImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 4. Create Render Pass 
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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

        std::array<VkSubpassDependency, 2> dependencies;
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

        std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &viewportRenderPass) != VK_SUCCESS) return false;

        // 5. Create Framebuffer (Using Dynamic Width/Height)
        std::array<VkImageView, 2> fbAttachments = { viewportImageView, viewportDepthImageView };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = viewportRenderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
        framebufferInfo.pAttachments = fbAttachments.data();
        framebufferInfo.width = width;  // [CRITICAL FIX]
        framebufferInfo.height = height; // [CRITICAL FIX]
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &viewportFramebuffer) != VK_SUCCESS) return false;

        return true;
    }

    void SetCrescendoEditorStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        ImVec4 ashGreyDark   = ImVec4(0.10f, 0.10f, 0.11f, 1.00f); 
        ImVec4 ashGreyMedium = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
        ImVec4 ashGreyLight  = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
        ImVec4 goldOrange    = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
        ImVec4 goldHover     = ImVec4(1.00f, 0.80f, 0.30f, 1.00f);

        colors[ImGuiCol_WindowBg]             = ashGreyDark;   
        colors[ImGuiCol_ChildBg]              = ashGreyDark;
        colors[ImGuiCol_PopupBg]              = ashGreyDark;
        colors[ImGuiCol_MenuBarBg]            = ashGreyMedium;

        colors[ImGuiCol_TitleBg]              = ashGreyDark;
        colors[ImGuiCol_TitleBgActive]        = ashGreyMedium;
        colors[ImGuiCol_TitleBgCollapsed]     = ashGreyDark;
        colors[ImGuiCol_Header]               = ashGreyMedium;
        colors[ImGuiCol_HeaderHovered]        = goldOrange;
        colors[ImGuiCol_HeaderActive]         = goldOrange;

        colors[ImGuiCol_Text]                 = goldOrange;    
        colors[ImGuiCol_TextSelectedBg]       = ImVec4(1.00f, 0.65f, 0.00f, 0.35f);

        colors[ImGuiCol_FrameBg]              = ashGreyMedium;
        colors[ImGuiCol_FrameBgHovered]       = ashGreyLight;
        colors[ImGuiCol_FrameBgActive]        = ashGreyLight;
        
        colors[ImGuiCol_Button]               = ashGreyMedium;
        colors[ImGuiCol_ButtonHovered]        = goldHover;
        colors[ImGuiCol_ButtonActive]         = goldOrange;

        colors[ImGuiCol_SliderGrab]           = goldOrange;
        colors[ImGuiCol_SliderGrabActive]     = goldHover;
        colors[ImGuiCol_CheckMark]            = goldOrange;

        colors[ImGuiCol_Tab]                  = ashGreyDark;
        colors[ImGuiCol_TabHovered]           = goldHover;
        colors[ImGuiCol_TabActive]            = ashGreyMedium;
        colors[ImGuiCol_TabUnfocused]         = ashGreyDark;
        colors[ImGuiCol_TabUnfocusedActive]   = ashGreyMedium;
        colors[ImGuiCol_DockingPreview]       = ImVec4(1.00f, 0.65f, 0.00f, 0.70f);

        colors[ImGuiCol_Border]               = ashGreyMedium;
        colors[ImGuiCol_Separator]            = ashGreyMedium;

        style.WindowRounding = 5.0f;
        style.FrameRounding  = 3.0f;
        style.PopupRounding  = 5.0f;
    }

    void RenderingServer::recreateSwapChain(SDL_Window* window) {
        vkDeviceWaitIdle(device);

        cleanupSwapChain();
        
        // [ADD] Cleanup Viewport Resources explicitly so they can be rebuilt with new size
        if (viewportFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, viewportFramebuffer, nullptr); viewportFramebuffer = VK_NULL_HANDLE; }
        if (viewportImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, viewportImageView, nullptr); viewportImageView = VK_NULL_HANDLE; }
        if (viewportImage != VK_NULL_HANDLE) { vkDestroyImage(device, viewportImage, nullptr); viewportImage = VK_NULL_HANDLE; }
        if (viewportImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, viewportImageMemory, nullptr); viewportImageMemory = VK_NULL_HANDLE; }
        if (viewportDepthImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, viewportDepthImageView, nullptr); viewportDepthImageView = VK_NULL_HANDLE; }
        if (viewportDepthImage != VK_NULL_HANDLE) { vkDestroyImage(device, viewportDepthImage, nullptr); viewportDepthImage = VK_NULL_HANDLE; }
        if (viewportDepthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, viewportDepthImageMemory, nullptr); viewportDepthImageMemory = VK_NULL_HANDLE; }
        if (viewportRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(device, viewportRenderPass, nullptr); viewportRenderPass = VK_NULL_HANDLE; }
        // Note: Do NOT destroy viewportSampler here, it can be reused.

        createSwapChain();
        createImageViews();
        createFramebuffers();
        
        // [ADD] Recreate viewport with new swapChainExtent
        createViewportResources(); 
    }   

    void RenderingServer::cleanupSwapChain() {
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);
        depthImageView = VK_NULL_HANDLE;
        depthImage = VK_NULL_HANDLE;       

        for (auto framebuffer : swapChainFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        swapChainFramebuffers.clear();

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        swapChainImageViews.clear();

        if (viewportFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, viewportFramebuffer, nullptr);
            viewportFramebuffer = VK_NULL_HANDLE;
        }

        vkDestroySwapchainKHR(device, swapChain, nullptr);
    }

    void RenderingServer::shutdown() {
       if (device != VK_NULL_HANDLE) {
           vkDeviceWaitIdle(device);

            if (viewportDepthImageView != VK_NULL_HANDLE) vkDestroyImageView(device, viewportDepthImageView, nullptr);
            if (viewportDepthImage != VK_NULL_HANDLE) vkDestroyImage(device, viewportDepthImage, nullptr);
            if (viewportDepthImageMemory != VK_NULL_HANDLE) vkFreeMemory(device, viewportDepthImageMemory, nullptr);
           
            editorUI.Shutdown(device);

            if (skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, skyPipeline, nullptr);
            if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
            if (gridPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, gridPipeline, nullptr);
            if (transparentPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, transparentPipeline, nullptr);
            if (waterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, waterPipeline, nullptr);

            if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            
            if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
            
            if (viewportRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, viewportRenderPass, nullptr);

            for (auto& mesh : meshes) {
                if (mesh.vertexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
                    mesh.vertexBuffer = VK_NULL_HANDLE; 
                }
                if (mesh.vertexBufferMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, mesh.vertexBufferMemory, nullptr);
                    mesh.vertexBufferMemory = VK_NULL_HANDLE;
                }
                if (mesh.indexBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
                    mesh.indexBuffer = VK_NULL_HANDLE;
                }
                if (mesh.indexBufferMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, mesh.indexBufferMemory, nullptr);
                    mesh.indexBufferMemory = VK_NULL_HANDLE;
                }
            }
            meshes.clear(); 
            
            gameWorld.Clear(); 

            for (const auto& tex : textureBank) {
                if (tex.image != VK_NULL_HANDLE && tex.image != textureImage) {
                    vkDestroyImageView(device, tex.view, nullptr);
                    vkDestroyImage(device, tex.image, nullptr);
                    vkFreeMemory(device, tex.memory, nullptr);
                }
            }
            textureBank.clear();
            textureMap.clear();

            if (textureImageView != VK_NULL_HANDLE) vkDestroyImageView(device, textureImageView, nullptr);
            if (textureSampler != VK_NULL_HANDLE) vkDestroySampler(device, textureSampler, nullptr);
            if (textureImage != VK_NULL_HANDLE) vkDestroyImage(device, textureImage, nullptr);
            if (textureImageMemory != VK_NULL_HANDLE) vkFreeMemory(device, textureImageMemory, nullptr);

            if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);

            cleanupSwapChain(); 
            if (viewportImageView != VK_NULL_HANDLE) vkDestroyImageView(device, viewportImageView, nullptr);
            if (viewportSampler != VK_NULL_HANDLE) vkDestroySampler(device, viewportSampler, nullptr);
            if (viewportImage != VK_NULL_HANDLE) vkDestroyImage(device, viewportImage, nullptr);
            if (viewportImageMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, viewportImageMemory, nullptr);
                viewportImageMemory = VK_NULL_HANDLE; 
             }

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
