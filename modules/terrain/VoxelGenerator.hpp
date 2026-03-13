#pragma once

#include <vector>
#include <glm/glm.hpp>
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
        // 2. Make sure your GenerateChunk expects the settings!
        static ChunkData GenerateChunk(const glm::vec3& origin, int resolution, float size, const VoxelSettings& settings);

    private:
        // 3. Make sure EvaluateDensity expects the settings!
        static float EvaluateDensity(const glm::vec3& worldPos, const VoxelSettings& settings);
        static glm::vec3 VertexInterp(float isolevel, const glm::vec3& p1, const glm::vec3& p2, float valp1, float valp2);
    };

}
}