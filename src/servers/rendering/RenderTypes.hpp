#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// --- THE PLATFORM BRIDGE ---
#ifdef __EMSCRIPTEN__
    // WebGL uses simple integers (GLuint) to track memory on the GPU
    typedef unsigned int GPUBufferHandle;
#else
    // Desktop uses your custom Vulkan RAII wrappers
    #include "vulkan/VulkanResources.hpp"
    typedef VulkanBuffer GPUBufferHandle;
#endif

namespace Crescendo {
    
    struct MeshResource {
        std::string name;
        GPUBufferHandle vertexBuffer; // <--- Uses the Bridge!
        GPUBufferHandle indexBuffer;  // <--- Uses the Bridge!
        uint32_t indexCount;
        uint32_t textureID; // 0 default

        MeshResource() = default;
        MeshResource(const MeshResource&) = delete;
        MeshResource& operator=(const MeshResource&) = delete;
        MeshResource(MeshResource&& other) noexcept = default;
        MeshResource& operator=(MeshResource&& other) noexcept = default;
    };

    struct ChunkBakeResult {
        MeshResource generatedMesh;
        bool hasMesh = false;
        uint32_t physicsBodyID = 0;
        std::vector<float> collisionVerts;
        std::vector<uint32_t> collisionIndices;

        ChunkBakeResult() = default;
        ChunkBakeResult(const ChunkBakeResult&) = delete;
        ChunkBakeResult& operator=(const ChunkBakeResult&) = delete;
        ChunkBakeResult(ChunkBakeResult&&) noexcept = default;
        ChunkBakeResult& operator=(ChunkBakeResult&&) noexcept = default;
    };

    struct TerrainComputePush {
        alignas(16) glm::vec3 chunkOrigin;
        alignas(4)  float chunkSize;
        alignas(16) glm::vec3 planetCenter;
        alignas(4)  float planetRadius;
        alignas(4)  float amplitude;
        alignas(4)  float frequency;
        alignas(4)  int   octaves;
        alignas(4)  int   resolution;
        alignas(4)  int   lod;
    };
}