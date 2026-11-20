#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Crescendo {

    struct FoliageInstance {
        glm::vec3 position = glm::vec3(0.0f);
        float yawRadians = 0.0f;
        glm::vec3 scale = glm::vec3(1.0f);
        float windPhase = 0.0f;
        glm::vec4 tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // rgb = color multiplier, a = wind/subsurface weight
    };

    struct FoliageMeshPart {
        int lod0MeshID = -1;
        int lod1MeshID = -1;       // Optional far-LOD / crossed-impostor mesh (-1 reuses lod0MeshID)
        int textureID = 0;         // Albedo / alpha-cutout texture ID (0 = white/vertex color)
        int normalTextureID = 0;   // Optional tangent-space normal map ID
        float roughness = 0.88f;
        float subsurface = 0.42f;  // Sunlight translucency / backlight wrap
        float alphaCutoff = 0.45f; // Cutout threshold when textureID > 0
        bool doubleSided = true;   // true = VK_CULL_MODE_NONE (leaf cards); false = back-culled (trunks)
        bool castShadows = true;
    };

    struct FoliageBatch {
        std::string name;
        std::vector<FoliageMeshPart> parts;
        float lodSwitchDist = 175.0f;
        float maxDrawDist = 950.0f;
        float boundingRadius = 3.5f;
        float windStrength = 0.16f;
        float windFrequency = 1.6f;
        std::vector<FoliageInstance> instances;
    };

} // namespace Crescendo
