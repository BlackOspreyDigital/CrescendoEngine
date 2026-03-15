#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "servers/rendering/Vertex.hpp" 

namespace Crescendo {
namespace Terrain {

    
    struct VoxelSettings {
        float radius = 12.0f;
        int octaves = 4;
        float amplitude = 3.0f;
        float frequency = 0.1f;
    };

    struct ChunkData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    class VoxelGenerator {
    public:
        // ADDED int lod = 0
        static ChunkData GenerateChunk(const glm::vec3& origin, int resolution, float size, const VoxelSettings& settings, int lod = 0);
        static void GenerateWaterSphere(float radius, int segments, int rings, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

        // ADDED int lod = 0
        static float EvaluateDensity(const glm::vec3& worldPos, const VoxelSettings& settings, int lod = 0);

    private:
        static glm::vec3 CalculateNormal(const glm::vec3& p, const VoxelSettings& settings); 
        static glm::vec3 VertexInterp(float isolevel, const glm::vec3& p1, const glm::vec3& p2, float valp1, float valp2);
    };

}
}