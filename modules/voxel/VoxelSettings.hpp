#pragma once
#include <glm/glm.hpp>

namespace Crescendo::Voxel {
    struct VoxelSettings {
        float radius = 50000.0f;
        float amplitude = 2000.0f;
        float frequency = 0.00005f;
        int octaves = 4;
        int maxLOD = 5;
        float splitThreshold = 1.25f;
    };
}
