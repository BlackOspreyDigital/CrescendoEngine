#include "modules/atmosphere/VolumetricAtmosphereModule.hpp"
#include "src/scene/Scene.hpp"
#include "servers/camera/Camera.hpp"

namespace Crescendo {

    bool VolumetricAtmosphereModule::Initialize(VkDevice device, VkFormat colorFormat, VkFormat depthFormat) {
        // We will port your createAtmospherePipeline() logic here next!
        return true; 
    }

    void VolumetricAtmosphereModule::Shutdown(VkDevice device) {
        if (m_atmospherePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_atmospherePipeline, nullptr);
            m_atmospherePipeline = VK_NULL_HANDLE;
        }
    }

    void VolumetricAtmosphereModule::RecordCommands(VkCommandBuffer cmd, 
                                                    VkPipelineLayout layout, 
                                                    Scene* scene, 
                                                    const Camera& camera, 
                                                    const glm::mat4& proj, 
                                                    const glm::mat4& view, 
                                                    VkExtent2D screenExtent, 
                                                    VkRenderPass renderPass, 
                                                    VkFramebuffer framebuffer) {
        if (!scene || m_atmospherePipeline == VK_NULL_HANDLE) return;

        // The exact atmosphere draw loop we ripped out of RenderingServer.cpp 
        // will get pasted cleanly right here!
    }

}