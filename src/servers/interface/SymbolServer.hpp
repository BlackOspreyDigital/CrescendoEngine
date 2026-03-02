#pragma once

#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.h>


#include <glm/glm.hpp>
#include <vector>

namespace Crescendo {

    struct SymbolPushConstant {
        glm::vec3 worldPosition;
        float scale;
        glm::vec3 cameraRight;
        float padding1;
        glm::vec3 cameraUp;
        float padding2;
    };

    class SymbolServer {
    public:
        SymbolServer() = default;
        ~SymbolServer() = default;

        // Must have all 4 arguments!
        bool Initialize(VkDevice device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout, VkDescriptorSetLayout textureSetLayout);

        void Cleanup(VkDevice device);
        void SubmitSymbol(const glm::vec3& position, float scale = 1.0f);
        void ClearSymbols();

        // Add 'VkDescriptorSet globalSet' right before the texture set
        void DrawSymbols(VkCommandBuffer cmd, const glm::vec3& camRight, const glm::vec3& camUp, VkDescriptorSet globalSet, VkDescriptorSet textureSet);

    private:
        VkPipelineLayout m_pipelineLayout;
        VkPipeline m_pipeline;

        std::vector<glm::vec3> m_queuedSymbols;
    };
}