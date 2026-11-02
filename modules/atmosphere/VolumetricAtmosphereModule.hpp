#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Crescendo {
    
    class Scene;
    class Camera;

    class VolumetricAtmosphereModule {
    public:
        VolumetricAtmosphereModule() = default;
        ~VolumetricAtmosphereModule() = default;

        // Called during engine startup
        bool Initialize(VkDevice device, VkFormat colorFormat, VkFormat depthFormat);
        
        // Called during engine shutdown
        void Shutdown(VkDevice device);

        // Called by RenderingServer during the Transparent Pass
        void RecordCommands(VkCommandBuffer cmd, 
                            VkPipelineLayout layout, 
                            Scene* scene, 
                            const Camera& camera, 
                            const glm::mat4& proj, 
                            const glm::mat4& view, 
                            VkExtent2D screenExtent, 
                            VkRenderPass renderPass, 
                            VkFramebuffer framebuffer);

    private:
        VkPipeline m_atmospherePipeline = VK_NULL_HANDLE;
        
        // We will move createAtmospherePipeline() in here later!
    };
}