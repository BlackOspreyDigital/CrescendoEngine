#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <string>

// Bring in VMA and your VulkanBuffer wrapper
#include "deps/vk_mem_alloc.h"
#include "servers/rendering/RenderingServer.hpp" 

namespace Crescendo {

    // Ultra-lightweight vertex for 2D batching
    struct Vertex2D {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color;
        float texIndex; 

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex2D);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
            
            // Position
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex2D, pos);

            // UV
            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex2D, uv);

            // Color
            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex2D, color);

            // Texture Index
            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex2D, texIndex);

            return attributeDescriptions;
        }
    };

    class CanvasRenderer {
    public:
        CanvasRenderer();
        ~CanvasRenderer();

        void Initialize(VkDevice logicalDevice, VmaAllocator vmaAllocator, VkRenderPass renderPass, VkDescriptorSetLayout descriptorLayout);
        void Shutdown();

        // --- CPU BATCHING API ---
        void BeginFrame(); 
        void DrawRect(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
        void DrawImage(const glm::vec2& position, const glm::vec2& size, const glm::vec2& uv0, const glm::vec2& uv1, float textureID, const glm::vec4& tint = glm::vec4(1.0f));

        // --- GPU SUBMISSION ---
        void Render(VkCommandBuffer cmd, const glm::vec2& screenSize, VkDescriptorSet globalDescriptorSet);

    private:
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = nullptr;

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        // Dynamic GPU Buffers 
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        void* vertexMapped = nullptr;
        void* indexMapped = nullptr;

        const uint32_t MAX_VERTICES = 10000;
        const uint32_t MAX_INDICES = 15000;

        // CPU Buffers
        std::vector<Vertex2D> batchedVertices;
        std::vector<uint32_t> batchedIndices;
        
        // Internal Helpers
        std::vector<char> readFile(const std::string& filename);
        VkShaderModule createShaderModule(const std::vector<char>& code);
    };

} // namespace Crescendo