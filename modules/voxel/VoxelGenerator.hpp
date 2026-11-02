#pragma once
#include <glm/glm.hpp>
#include <vector>
#include "VoxelSettings.hpp"
#include "servers/rendering/Vertex.hpp"

namespace Crescendo::Voxel {
    struct ChunkMesh {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        bool hasGeometry = false;
    };

    class VoxelGenerator {
    public:
        static float EvaluateDensity(const glm::vec3& worldPos, const VoxelSettings& settings);
        static ChunkMesh GeneratePlanetMesh(const VoxelSettings& settings, int resolution = 32);
        static ChunkMesh GenerateSphereMesh(float radius, int resolution = 32);
    };
}
