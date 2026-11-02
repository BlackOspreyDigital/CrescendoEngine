#pragma once
#include "scene/Component.hpp"
#include "VoxelSettings.hpp"
#include <glm/glm.hpp>
#include <string>

namespace Crescendo::Voxel {
    class VoxelPlanetComponent : public Component {
    public:
        VoxelSettings settings;

        // Atmosphere scattering parameters
        float atmosphereCeiling = 1.02f;
        float atmosphereFloor = 0.0f;
        float atmosphereIntensity = 15.0f;
        glm::vec3 rayleigh = glm::vec3(0.015f, 0.035f, 0.075f);
        float mie = 0.003f;

        std::string GetName() const override { return "Voxel Planet"; }
        void DrawInspectorUI() override {}
    };
}
