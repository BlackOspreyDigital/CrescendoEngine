#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
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
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "deps/imgui/ImGuizmo.h"
#include "deps/tinygltf/tiny_gltf.h"
#include "deps/json/json.hpp"

namespace Crescendo {

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

    RenderingServer::RenderingServer() : display_ref(nullptr) {}

    bool RenderingServer::initialize(DisplayServer* display) {
        display_ref = display;

        std::cout << "[1/5] Initializing Core Vulkan..." << std::endl;
        if (!createInstance()) return false;
        if (!setupDebugMessenger()) return false;
        if (!createSurface()) return false;
        if (!pickPhysicalDevice()) return false;
        if (!createLogicalDevice()) return false;

        std::cout << "[2/5] Setting up Command Infrastructure..." << std::endl;
        if (!createCommandPool()) return false;
        if (!createDescriptorSetLayout()) return false;
        
        std::cout << "[3/5] Creating SwapChain and Pipelines..." << std::endl;
        if (!createSwapChain()) return false;
        if (!createImageViews()) return false;
        if (!createDepthResources()) return false;
        if (!createRenderPass()) return false; // MAIN render pass (for screen)

        // =========================================================
        // CRITICAL SECTION: Viewport Refactor 
        // =========================================================
        
        // Interface first
        editorUI.Initialize(this, display->get_window(), instance, physicalDevice, device, graphicsQueue, renderPass, static_cast<uint32_t>(swapChainImages.size()));

        // Viewport second
        if (!createViewportResources()) return false;
        if (!createGraphicsPipeline()) return false;
        if (!createGridPipeline()) return false;
        if (!createWaterPipeline()) return false;        
        if (!createTransparentPipeline()) return false;
        if (!createFramebuffers()) return false;
    
        std::cout << "[4/5] Loading Assets (The Texture Stage)..." << std::endl;
        if (!createTextureImage()) { std::cout << "!! Failed at TextureImage" << std::endl; return false; }
        if (!createTextureImageView()) { std::cout << "!! Failed at TextureImageView" << std::endl; return false; }
        if (!createTextureSampler()) { std::cout << "!! Failed at TextureSampler" << std::endl; return false; }

        TextureResource defaultTex;
        defaultTex.image = textureImage;
        defaultTex.view = textureImageView;
        defaultTex.memory = textureImageMemory;

        textureBank.resize(MAX_TEXTURES);
        for (int i = 0; i < MAX_TEXTURES; i++) {
            textureBank[i] = defaultTex;
        }

        if (!createDescriptorPool()) return false;
        if (!createDescriptorSets()) { std::cout << "!! Failed at DescriptorSets" << std::endl; return false; }
        
        createProceduralGrid(); // Mesh Index 0
        createWaterMesh();

        this->waterTextureID = acquireTexture("assets/textures/water.png");

        if (this->waterTextureID == -1) {
            std::cout << "Warning: Water texture not found, using default." << std::endl;
            this->waterTextureID = 0;
        }

        std::cout << "[5/5] Finalizing Synchronization & ImGui..." << std::endl;
        if (!createCommandBuffers()) return false;
        if (!createSyncObjects()) return false;
        
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
        
        // FIX: Removed automatic entity creation. Main.cpp handles it now.
        std::cout << "[System] Procedural Grid Mesh Generated." << std::endl;
    }

    void RenderingServer::createWaterMesh() {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        
        int width = 1000; // 100x100 grid = 10,000 vertices
        int depth = 1000;
        float spacing = 2.0f; // Each square is 2 meters wide
        
        float startX = -(width * spacing) / 2.0f;
        float startY = -(depth * spacing) / 2.0f;

        // Generate Vertices
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

        // Generate Indices
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
        
        // Spawn it
        CBaseEntity* water = gameWorld.CreateEntity("prop_water");
        water->modelIndex = meshes.size() - 1;
        water->origin = glm::vec3(0, 0, -5.0f); // Slightly below ground
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
        
        // 1. Create and Fill Staging Buffer
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                    stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        // 2. Create GPU Image
        createImage(width, height, format, VK_IMAGE_TILING_OPTIMAL, 
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        // 3. Copy Command
        transitionImageLayout(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, image, width, height);
        transitionImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // --- THE FIX: WAIT FOR GPU TO FINISH ---
        // If we don't wait, the next line frees memory the GPU is still reading!
        vkQueueWaitIdle(graphicsQueue); 

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr); // This is where the crash was happening
    }   

    bool RenderingServer::createTextureImage() {
        SDL_Surface* surface = IMG_Load("assets/textures/default.png");
        if (!surface) {
            std::cerr << "Failed to load texture image!" << IMG_GetError() << std::endl;
            return false;
        }

        SDL_Surface* temp =  SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
        VkDeviceSize imageSize = temp->w * temp->h * 4;
        uint32_t texWidth = temp->w;
        uint32_t texHeight = temp->h;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                 stagingBuffer, stagingBufferMemory);
        
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, temp->pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);
        SDL_FreeSurface(temp);
        SDL_FreeSurface(surface);
        
        createImage(texWidth, texHeight,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    textureImage, textureImageMemory);

        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, textureImage, texWidth, texHeight);
        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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
        samplerInfo.maxAnisotropy = 16.0f; // Might need to check limits
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
            
            // FIX: Map OBJ "Diffuse" to PBR "Albedo"
            newMat.albedoColor = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
            
            // FIX: Map OBJ "Shininess" to PBR "Roughness"
            // OBJ Shininess is usually 0-1000 (High = Shiny/Smooth)
            // PBR Roughness is 0-1 (High = Matte/Rough)
            // We invert it so high shininess becomes low roughness.
            if (mat.shininess > 0.0f) {
                newMat.roughness = 1.0f - std::clamp(mat.shininess / 1000.0f, 0.0f, 1.0f);
            } else {
                newMat.roughness = 0.9f; // Default to matte
            }
            
            newMat.metallic = 0.0f; // OBJs are rarely metallic by default

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

    bool RenderingServer::createTextureImage(const std::string& path, VkImage& image, VkDeviceMemory& memory) {
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) {
            std::cerr << "Failed to load texture: " << path << " | " << IMG_GetError() << std::endl;
            return false;
        }

        SDL_Surface* temp = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
        VkDeviceSize imageSize = temp->w * temp->h * 4;
        uint32_t texWidth = temp->w;
        uint32_t texHeight = temp->h;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     stagingBuffer, stagingBufferMemory);
        
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, temp->pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);
        
        SDL_FreeSurface(temp);
        SDL_FreeSurface(surface);
        
        createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);

        transitionImageLayout(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, image, texWidth, texHeight);
        transitionImageLayout(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return true;
    }

    VkImageView RenderingServer::createTextureImageView(VkImage& image) {
        return createImageView(image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    bool RenderingServer::createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorCount = MAX_TEXTURES;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerLayoutBinding;

        return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) == VK_SUCCESS;
    }

    bool RenderingServer::createDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = MAX_TEXTURES;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 10;

        return vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) == VK_SUCCESS;
    }

    bool RenderingServer::createDescriptorSets() {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) return false;

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
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = static_cast<uint32_t>(MAX_TEXTURES);
        descriptorWrite.pImageInfo = imageInfos.data();

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
        return true;
    }

    int RenderingServer::acquireTexture(const std::string& path) {
        if (textureMap.find(path) != textureMap.end()) {
            return textureMap[path];
        }

        // 1. Check Bank Limits
        int newID = textureMap.size() + 1; // Start at 1 (0 is default)
        if (newID >= MAX_TEXTURES) {
            std::cerr << "Texture Bank Full" << std::endl;
            return 0;
        }

        TextureResource newTex;
        
        // 2. Load the Image Data (Creates VkImage & VkDeviceMemory)
        createTextureImage(path, newTex.image, newTex.memory);

        // 3. FIX #1: Create the View! (This was missing)
        // We must generate the VkImageView so the shader can read it.
        newTex.view = createTextureImageView(newTex.image);

        // Store in bank
        textureBank[newID] = newTex; 
        textureMap[path] = newID;

        // 4. Update the Descriptor Set
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = newTex.view; // Now this is valid!
        imageInfo.sampler = textureSampler;

        VkWriteDescriptorSet descriptorWrite {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        
        // 5. FIX #2: Update the correct slot!
        // Previously this was '0', which overwrote the default texture repeatedly.
        descriptorWrite.dstArrayElement = newID; 
        
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

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
        // Update renderpass to work with new viewport renderpass 1/9/25 
        // Vertex Input
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
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
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

        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(MeshPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};

       
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout; // Using the set layout
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
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = viewportRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) return false;

        // ------------------------------
        // Sky logic I will eventually move this to its own setup like a sky node
        // ------------------------------
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

        // disable culling for sky (full screen quad)
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        // disable depth write (sky is background)
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthTestEnable = VK_FALSE; // always draw
        // no vertex input (generated in shader)
        VkPipelineVertexInputStateCreateInfo emptyInputState{};
        emptyInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = skyShaderStages;
        pipelineInfo.pVertexInputState = &emptyInputState; // empty
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

        // ... [Shader Stages Setup - Same as GraphicsPipeline] ...
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

        // Vertex Input
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

        // Viewport & Scissor
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
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Ensure backfaces are drawn for glass!
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // --- KEY CHANGE 1: DISABLE DEPTH WRITE ---
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;  // Still check depth!
        depthStencil.depthWriteEnable = VK_FALSE; // <--- DON'T WRITE DEPTH
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        // --- KEY CHANGE 2: ENABLE BLENDING ---
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

        // ... [Shader Stages Setup - Same as GraphicsPipeline] ...
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

        // Vertex Input
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

        // Viewport & Scissor
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
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Ensure backfaces are drawn for glass!
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // --- KEY CHANGE 1: DISABLE DEPTH WRITE ---
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;  // Still check depth!
        depthStencil.depthWriteEnable = VK_FALSE; // <--- DON'T WRITE DEPTH
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        // --- KEY CHANGE 2: ENABLE BLENDING ---
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
        auto fragShaderCode = readFile("assets/shaders/grid.spv"); // <--- CUSTOM SHADER

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

        // 2. Vertex Input (Same as standard objects)
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // 3. Input Assembly
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // 4. Viewport & Scissor
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

        // 5. Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL; 
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Important: Don't cull the grid!
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        // 6. Multisampling
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 7. Depth Stencil
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE; // TRANSPARENCY: Don't write to depth buffer
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        // 8. Color Blending (CRITICAL FOR GRID TRANSPARENCY)
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE; // <--- ENABLE BLENDING
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

        // 9. Pipeline Layout & Creation
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
        
        // Reuse the existing layout!
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

        // Fences pace the CPU/GPU and stay at MAX_FRAMES_IN_FLIGHT
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        
        // Semaphores must match the image count to prevent the validation errors you saw earlier
        imageAvailableSemaphores.resize(imageCount);
        renderFinishedSemaphores.resize(imageCount);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        // Create Fences
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) return false;
        }

        // Create Semaphores - Use imageCount here!
        for (size_t i = 0; i < imageCount; i++) {
            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    void RenderingServer::loadModel(const std::string& path) {

        // check cache
        if (meshMap.find(path) != meshMap.end()) {
            uint32_t existingIndex = meshMap[path];

            CBaseEntity* ent = gameWorld.CreateEntity("prop_dynamic");
            ent->modelIndex = existingIndex;
            ent->targetName = path.substr(path.find_last_of("/\\") + 1);

            std::cout << "[System] Instanced existing mesh: " << ent->targetName << std::endl;
            return;
        }

        // load new model if not cached
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

        // Local buffers to hold geometry before uploading
        std::vector<Vertex> tempVertices;
        std::vector<uint32_t> tempIndices;
        std::unordered_map<Vertex, uint32_t> uniqueVertices{};

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Vertex vertex{};

                // 1. Position
                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                // 2. Normals (Check if they exist first)
                if (index.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    };
                } else {
                    // FALLBACK: If your OBJ has no normals, this makes it point UP.
                    // Try changing this to match your Z-Up system if needed: {0.0f, 0.0f, 1.0f}
                    vertex.normal = {0.0f, 0.0f, 1.0f}; 
                }

                // 3. Texture Coordinates
                if (index.texcoord_index >= 0) {
                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1] // Flip Y for Vulkan
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

        // 4. Create and Initialize MeshResource (prevents 0x67 garbage crash)
        MeshResource newMesh{}; 
        newMesh.name = path.substr(path.find_last_of("/\\") + 1);
        newMesh.indexCount = static_cast<uint32_t>(tempIndices.size());

        // 5. Upload to GPU
        createVertexBuffer(tempVertices, newMesh.vertexBuffer, newMesh.vertexBufferMemory);
        createIndexBuffer(tempIndices, newMesh.indexBuffer, newMesh.indexBufferMemory);

        // 6. Mesh Init
        meshes.push_back(newMesh);
        
        CBaseEntity* ent = gameWorld.CreateEntity("prop_dynamic");
        ent->modelIndex = meshes.size() - 1;
        ent->targetName = newMesh.name;

        if (!shapes.empty() && !shapes[0].mesh.material_ids.empty()) {
            int localMatID = shapes[0].mesh.material_ids[0];
            if (localMatID >= 0) {
                std::string matName = materials[localMatID].name;

                // global ID lookup
                if (materialMap.find(matName) != materialMap.end()) {
                    uint32_t globalMatID = materialMap[matName];

                    // texture ID assign from mat obj
                    ent->textureID = materialBank[globalMatID].textureID;
                }
            }
        }
        
        std::cout << ">>> Imported & Spawned: " << newMesh.name << std::endl;
    } 

    // --------------------------------------------------------------------
    // GLTF Loading & Handling 
    // --------------------------------------------------------------------

    // --- 1. URI HELPER (Handles spaces in filenames) ---
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

    // --- REVERTED TO YOUR ORIGINAL LOGIC (With Scene* fix) ---
    void RenderingServer::processGLTFNode(tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene) {
        
        // ADAPTATION: Used 'scene' instead of 'GetWorld()' since we passed it in
        CBaseEntity* newEnt = scene->CreateEntity("prop_static"); 
        newEnt->targetName = node.name; 

        if (parent) {
            newEnt->moveParent = parent;
            parent->children.push_back(newEnt);
            newEnt->origin = parent->origin; 
        }

        if (node.translation.size() == 3) {
            newEnt->origin += glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        }
        if (node.scale.size() == 3) {
            newEnt->scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        }

        if (node.mesh >= 0) {
            tinygltf::Mesh& mesh = model.meshes[node.mesh];
            
            // Loop through primitives (sub-meshes)
            for (const auto& primitive : mesh.primitives) {
                
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;
                const float* positionBuffer = nullptr;
                const float* normalBuffer = nullptr;
                const float* texBuffer = nullptr;
                size_t vertexCount = 0;

                // ATTRIBUTE EXTRACTION
                if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("POSITION")];
                    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                    positionBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
                    vertexCount = accessor.count;
                }
                if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("NORMAL")];
                    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                    normalBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
                }
                if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                    texBuffer = reinterpret_cast<const float*>(&model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
                }

                // VERTEX PACKING
                for (size_t i = 0; i < vertexCount; i++) {
                    Vertex v{};
                    v.pos = glm::vec3(positionBuffer[i * 3], positionBuffer[i * 3 + 1], positionBuffer[i * 3 + 2]);
                    if (normalBuffer) v.normal = glm::vec3(normalBuffer[i * 3], normalBuffer[i * 3 + 1], normalBuffer[i * 3 + 2]);
                    if (texBuffer) v.texCoord = glm::vec2(texBuffer[i * 2], texBuffer[i * 2 + 1]);
                    v.color = {1.0f, 1.0f, 1.0f};
                    vertices.push_back(v);
                }

                // INDEX EXTRACTION
                if (primitive.indices >= 0) {
                    const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
                    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + view.byteOffset]);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + view.byteOffset]);
                        for (size_t i = 0; i < accessor.count; i++) indices.push_back(buf[i]);
                    }
                }
                
                // MESH RESOURCE CREATION
                MeshResource newMeshResource;
                newMeshResource.name = node.name.empty() ? "GLTF_Mesh" : node.name;
                newMeshResource.indexCount = indices.size();

                // CALL EXISTING RENDERER HELPERS
                createVertexBuffer(vertices, newMeshResource.vertexBuffer, newMeshResource.vertexBufferMemory);
                createIndexBuffer(indices, newMeshResource.indexBuffer, newMeshResource.indexBufferMemory);

                meshes.push_back(newMeshResource);
                newEnt->modelIndex = meshes.size() - 1;

                // MATERIAL & TEXTURE LOGIC
                if (primitive.material >= 0) {
                    tinygltf::Material& mat = model.materials[primitive.material];

                    newEnt->roughness = (float)mat.pbrMetallicRoughness.roughnessFactor;
                    newEnt->metallic = (float)mat.pbrMetallicRoughness.metallicFactor;
                    
                    if (mat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
                        newEnt->albedoColor = glm::vec3(
                            mat.pbrMetallicRoughness.baseColorFactor[0],
                            mat.pbrMetallicRoughness.baseColorFactor[1],
                            mat.pbrMetallicRoughness.baseColorFactor[2]
                        );
                    }

                    int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
                    if (texIndex >= 0) {
                        tinygltf::Texture& tex = model.textures[texIndex];
                        if (tex.source >= 0) {
                            tinygltf::Image& img = model.images[tex.source];
                            if (!img.uri.empty()) {
                                std::string decodedUri = decodeUri(img.uri);
                                std::string fullPath = baseDir + decodedUri;
                                int loadedID = acquireTexture(fullPath);

                                if (loadedID == -1) {
                                    newEnt->textureID = 0; 
                                } else {
                                    newEnt->textureID = loadedID;
                                }
                            }
                        }
                    } else {
                        newEnt->textureID = 0; 
                    }
                }
            }
        }

        for (int childIndex : node.children) {
            processGLTFNode(model, model.nodes[childIndex], newEnt, baseDir, scene);
        }
    }
    
    // --- MAIN LOADER ---
    void RenderingServer::loadGLTF(const std::string& filePath, Scene* scene) {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        bool ret = false;
        if (filePath.find(".glb") != std::string::npos) {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, filePath);
        } else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, filePath);
        }

        if (!warn.empty()) std::cout << "[GLTF WARN] " << warn << std::endl;
        if (!err.empty()) std::cout << "[GLTF ERR] " << err << std::endl;

        if (!ret) {
            std::cout << "Failed to parse GLTF: " << filePath << std::endl;
            return;
        }

        std::string baseDir = "";
        size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            baseDir = filePath.substr(0, lastSlash + 1);
        }
        
        const tinygltf::Scene& gltfScene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];

        for (int nodeIndex : gltfScene.nodes) {
            // Pass scene to the helper
            processGLTFNode(model, model.nodes[nodeIndex], nullptr, baseDir, scene);
        }
        
        std::cout << "Successfully loaded GLTF Scene!" << std::endl;
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
        // Refactored for new mesh buffer indexing system

        // staging buffer
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stagingBuffer, stagingBufferMemory);
        
        // mem map
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        // gpu buffer
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, memory);

        // copy gpu buffer
        copyBuffer(stagingBuffer, buffer, bufferSize);

        // cleanup staging resources
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
        // We handle aspect ratio in render() now, so this can be empty for now
    }

    void RenderingServer::render(Scene* scene) {
        // [1. Synchronization & Acquire Image]
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(display_ref->get_window()); return; }
        
        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        // 1. Calculate Matrices
        float aspectRatio = 1920.0f / 1080.0f; // Viewport/Window Aspect Ratio
        glm::mat4 view = mainCamera.GetViewMatrix();
        glm::mat4 proj = mainCamera.GetProjectionMatrix(aspectRatio); 
        proj[1][1] *= -1; // Y- Vulkan Flip

        // [Update Uniforms]
        updateUniformBuffer(currentFrame, scene);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

        // =========================================================
        // PASS 1: OFFSCREEN (Render The 3D World)
        // =========================================================
        VkRenderPassBeginInfo viewportPassInfo{};
        viewportPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        viewportPassInfo.renderPass = viewportRenderPass;
        viewportPassInfo.framebuffer = viewportFramebuffer;
        viewportPassInfo.renderArea.extent = {1920, 1080};
        
        // --- FIX: ARRAY SIZE 1, ONLY ACCESS [0] ---
        std::array<VkClearValue, 2> viewportClearValues{}; 
        viewportClearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        viewportClearValues[1].depthStencil = {1.0f, 0}; // If this exists, array MUST be 2.

        viewportPassInfo.clearValueCount = 1; // Only clear the first one (Color)
        viewportPassInfo.pClearValues = viewportClearValues.data();

        vkCmdBeginRenderPass(commandBuffers[currentFrame], &viewportPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            // --- 1A. OPAQUE OBJECTS (The Logic from the missing 'drawFrame') ---
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            for (auto* ent : scene->entities) {
                // Skip transparent objects (Water) for now
                if (!ent || ent->targetName.find("water") != std::string::npos) continue; 
                if (ent->modelIndex < 0 || ent->modelIndex >= meshes.size()) continue;

                MeshResource& mesh = meshes[ent->modelIndex];
                VkBuffer vBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // Calc Matrices
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::rotate(model, glm::radians(ent->angles.z), glm::vec3(0,0,1));
                model = glm::rotate(model, glm::radians(ent->angles.y), glm::vec3(0,1,0));
                model = glm::rotate(model, glm::radians(ent->angles.x), glm::vec3(1,0,0));
                model = glm::scale(model, ent->scale);

                MeshPushConstants pushConst{};
                pushConst.renderMatrix = proj * view * model;
                pushConst.camPos = glm::vec4(mainCamera.GetPosition(), 1.0f);
                // Default PBR params (TextureID, Roughness, Metallic, Time)
                pushConst.pbrParams = glm::vec4((float)ent->textureID, ent->roughness, ent->metallic, 0.0f);
                pushConst.sunDir = glm::vec4(sunDirection, sunIntensity);
                pushConst.sunColor = glm::vec4(sunColor, 1.0f);

                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants), &pushConst);
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
            }

            // --- 1B. TRANSPARENT OBJECTS (Water/Glass) ---
            // Crucial: This must be INSIDE Pass 1 to appear in the texture!
            vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);

            for (auto* ent : scene->entities) {
                // Only draw Water
                if (!ent || ent->targetName.find("water") == std::string::npos) continue;

                MeshResource& mesh = meshes[ent->modelIndex];
                VkBuffer vBuffers[] = { mesh.vertexBuffer };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffers[currentFrame], mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, ent->origin);
                model = glm::scale(model, ent->scale); // Water usually doesn't rotate, but you can add it if needed

                MeshPushConstants pushConst{};
                pushConst.renderMatrix = proj * view * model;
                pushConst.camPos = glm::vec4(mainCamera.GetPosition(), 1.0f);

                float time = SDL_GetTicks() / 1000.0f;
                pushConst.pbrParams = glm::vec4((float)this->waterTextureID, 0.1f, 0.8f, time); 
                pushConst.sunDir = glm::vec4(sunDirection, sunIntensity);
                pushConst.sunColor = glm::vec4(sunColor, 1.0f);

                vkCmdPushConstants(commandBuffers[currentFrame], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPushConstants), &pushConst);
                vkCmdDrawIndexed(commandBuffers[currentFrame], mesh.indexCount, 1, 0, 0, 0);
            }

        vkCmdEndRenderPass(commandBuffers[currentFrame]); // End Pass 1 (Texture is now painted)


        // =========================================================
        // PASS 2: ONSCREEN (Render The UI)
        // =========================================================
        VkRenderPassBeginInfo screenPassInfo{};
        screenPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        screenPassInfo.renderPass = renderPass;
        screenPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        screenPassInfo.renderArea.extent = swapChainExtent;
        
        std::array<VkClearValue, 2> screenClearValues{};
        screenClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        screenClearValues[1].depthStencil = {1.0f, 0};
        screenPassInfo.clearValueCount = 2;
        screenPassInfo.pClearValues = screenClearValues.data();

        vkCmdBeginRenderPass(commandBuffers[currentFrame], &screenPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            // Draw the Editor (which displays the Pass 1 texture)
            editorUI.Draw(commandBuffers[currentFrame], scene, mainCamera, viewportDescriptorSet);

        vkCmdEndRenderPass(commandBuffers[currentFrame]);


        // =========================================================
        // [3. End & Submit]
        // =========================================================
        vkEndCommandBuffer(commandBuffers[currentFrame]);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
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

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(presentQueue, &presentInfo);
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
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
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
        // Simplified setup for brevity, assuming standard validation layer usage
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
        // FIFO Is vsync no need for mailbox mode as its uncapped and sends engine to 5k fps
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
        // --- 1. Create Image (The Texture) ---
        createImage(1920, 1080, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, viewportImage, viewportImageMemory);

        // --- 2. Create ImageView ---
        viewportImageView = createImageView(viewportImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

        // --- 3. Create Sampler ---
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

        // --- 4. Register with ImGui ---
        viewportDescriptorSet = ImGui_ImplVulkan_AddTexture(viewportSampler, viewportImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // --- 5. Create Render Pass (Offscreen) ---
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // Ready for ImGui

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

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

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &viewportRenderPass) != VK_SUCCESS) return false;

        // --- 6. Create Framebuffer ---
        VkImageView attachments[] = { viewportImageView };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = viewportRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = 1920;
        framebufferInfo.height = 1080;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &viewportFramebuffer) != VK_SUCCESS) return false;

        return true;
    }

    

    void SetCrescendoEditorStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // --- COLOR PALETTE ---
        // Ash Grey: Very dark, slight blue tint for depth
        ImVec4 ashGreyDark   = ImVec4(0.10f, 0.10f, 0.11f, 1.00f); 
        ImVec4 ashGreyMedium = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
        ImVec4 ashGreyLight  = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);

        // Gold/Orange: Pure gold for text and highlights
        ImVec4 goldOrange    = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
        ImVec4 goldHover     = ImVec4(1.00f, 0.80f, 0.30f, 1.00f);

        // --- BACKGROUND OVERRIDES ---
        colors[ImGuiCol_WindowBg]             = ashGreyDark;   // The main panel background
        colors[ImGuiCol_ChildBg]              = ashGreyDark;
        colors[ImGuiCol_PopupBg]              = ashGreyDark;
        colors[ImGuiCol_MenuBarBg]            = ashGreyMedium;

        // --- TITLE & HEADER (This is usually where the light grey hides) ---
        colors[ImGuiCol_TitleBg]              = ashGreyDark;
        colors[ImGuiCol_TitleBgActive]        = ashGreyMedium;
        colors[ImGuiCol_TitleBgCollapsed]     = ashGreyDark;
        colors[ImGuiCol_Header]               = ashGreyMedium;
        colors[ImGuiCol_HeaderHovered]        = goldOrange;
        colors[ImGuiCol_HeaderActive]         = goldOrange;

        // --- TEXT ---
        colors[ImGuiCol_Text]                 = goldOrange;    // Use Gold for main text
        colors[ImGuiCol_TextSelectedBg]       = ImVec4(1.00f, 0.65f, 0.00f, 0.35f);

        // --- INTERACTIVE ELEMENTS ---
        colors[ImGuiCol_FrameBg]              = ashGreyMedium;
        colors[ImGuiCol_FrameBgHovered]       = ashGreyLight;
        colors[ImGuiCol_FrameBgActive]        = ashGreyLight;
        
        colors[ImGuiCol_Button]               = ashGreyMedium;
        colors[ImGuiCol_ButtonHovered]        = goldHover;
        colors[ImGuiCol_ButtonActive]         = goldOrange;

        colors[ImGuiCol_SliderGrab]           = goldOrange;
        colors[ImGuiCol_SliderGrabActive]     = goldHover;
        colors[ImGuiCol_CheckMark]            = goldOrange;

        // --- TABS & DOCKING ---
        colors[ImGuiCol_Tab]                  = ashGreyDark;
        colors[ImGuiCol_TabHovered]           = goldHover;
        colors[ImGuiCol_TabActive]            = ashGreyMedium;
        colors[ImGuiCol_TabUnfocused]         = ashGreyDark;
        colors[ImGuiCol_TabUnfocusedActive]   = ashGreyMedium;
        colors[ImGuiCol_DockingPreview]       = ImVec4(1.00f, 0.65f, 0.00f, 0.70f);

        // --- BORDERS ---
        colors[ImGuiCol_Border]               = ashGreyMedium;
        colors[ImGuiCol_Separator]            = ashGreyMedium;

        // Rounding for a modern look
        style.WindowRounding = 5.0f;
        style.FrameRounding  = 3.0f;
        style.PopupRounding  = 5.0f;
    }

    void RenderingServer::recreateSwapChain(SDL_Window* window) {
        vkDeviceWaitIdle(device);
        cleanupSwapChain();
        createSwapChain(); // No args needed now, uses internal ref
        createImageViews();
        createFramebuffers();
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
           
           // --- 1. NEW: Editor Cleanup ---
           // This handles ImGui, Fonts, and internal UI textures automatically.
           editorUI.Shutdown(device);

           // --- 2. RenderPass & Pipelines ---
           if (skyPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, skyPipeline, nullptr);
           if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
           if (gridPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, gridPipeline, nullptr);
           if (transparentPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, transparentPipeline, nullptr);
           if (waterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, waterPipeline, nullptr);

           if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
           
           if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
           
           // KEEP: We still own the viewport render pass
           if (viewportRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, viewportRenderPass, nullptr);

           // --- 3. Mesh Resources ---
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
           
           // If you still have GameWorld defined in header, keep this. 
           // If you moved fully to Scene*, you can remove it.
           gameWorld.Clear(); 

           // --- 4. Texture Assets ---
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

           // --- 5. Descriptors ---
           if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
           if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);

           // --- 6. Viewport & Swapchain ---
           cleanupSwapChain(); 
           if (viewportImageView != VK_NULL_HANDLE) vkDestroyImageView(device, viewportImageView, nullptr);
           if (viewportSampler != VK_NULL_HANDLE) vkDestroySampler(device, viewportSampler, nullptr);
           if (viewportImage != VK_NULL_HANDLE) vkDestroyImage(device, viewportImage, nullptr);
           if (viewportImageMemory != VK_NULL_HANDLE) {
               vkFreeMemory(device, viewportImageMemory, nullptr);
               viewportImageMemory = VK_NULL_HANDLE; // Reset to avoid double free
            }

           // --- 7. Sync Objects ---
           for (size_t i = 0; i < imageAvailableSemaphores.size(); i++) { 
               vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
               vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
           }
           for (size_t i = 0; i < inFlightFences.size(); i++) {
               vkDestroyFence(device, inFlightFences[i], nullptr);
           }

           // --- 8. Command Infrastructure ---
           if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);

           vkDestroyDevice(device, nullptr);
           device = VK_NULL_HANDLE;
       }

       if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
       if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    }
}