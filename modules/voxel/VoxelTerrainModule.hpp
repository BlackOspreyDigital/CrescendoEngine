#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <future>
#include <map>
#include <glm/glm.hpp>

#include "servers/rendering/IRenderer.hpp"
#include "servers/rendering/VulkanResources.hpp"

namespace Crescendo {
    
    class Scene;
    class Camera;
    class CBaseEntity;

    // The clean contract: No trees, no voxels, just raw GPU execution data!
    struct VoxelDrawPacket {
        VkBuffer vertexBuffer;
        VkBuffer indexBuffer;
        uint32_t indexCount;
        uint32_t entityGPUIndex;
    };

    class VoxelTerrainModule {
    public:
        VoxelTerrainModule() = default;
        ~VoxelTerrainModule() = default;

        bool Initialize(VkDevice device, VmaAllocator allocator);
        void Shutdown();

        // 1. Called once per frame BEFORE command recording
        // Handles 64-bit octree LODs, chunk queue sorting, and async Jolt physics threads
        void Update(Scene* scene, IRenderer* renderer, const Camera& camera);

        // 2. Called DURING command recording by the Rendering Server
        // Collects ready-to-draw leaf meshes without exposing spatial hierarchies
        void GatherOpaquePackets(Scene* scene, 
                                 const std::map<CBaseEntity*, uint32_t>& entityMap, 
                                 std::vector<VoxelDrawPacket>& outPackets);

        void GatherShadowPackets(Scene* scene, 
                                 const std::map<CBaseEntity*, uint32_t>& entityMap, 
                                 std::vector<VoxelDrawPacket>& outPackets);

        // Compute Pipeline accessors for background thread mesh baking
        int BuildChunkMesh(IRenderer* renderer, const TerrainComputePush& pushData);

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        VmaAllocator m_allocator = nullptr;

        // Relocated from RenderingServer.hpp!
        VkDescriptorSetLayout m_computeDescriptorLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_densityPipeline = VK_NULL_HANDLE;
        VkPipeline m_marchingCubesPipeline = VK_NULL_HANDLE;

        VulkanBuffer m_densityBuffer;
        VulkanBuffer m_computeVertexBuffer;
        VulkanBuffer m_computeIndexBuffer;
        
        VkDescriptorSet m_computeDescriptorSet = VK_NULL_HANDLE;

        bool CreateComputePipelines();
        void GenerateChunkGPU(VkCommandBuffer cmd, const TerrainComputePush& pushData);
    };
}