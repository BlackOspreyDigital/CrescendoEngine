#pragma once

#include <string>
#include <glm/glm.hpp>

namespace Crescendo {

    // This struct holds the data we will read/write
    struct EngineConfig {
        //Graphics Settings
        int msaaSamples = 4;
        bool enableSSAO = true;
        bool enableSSR  = true;

        float shadowBiasConstant = 0.015f;
        float shadowBiasSlope = 1.75f;
        float cascadeSplitLambda = 0.95f;
        float shadowDistance = 150.0f;

        glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.9f);
        float sunIntensity = 5.0f;
    };

    class ConfigManager {
    public:
        // Load settings from file
        static EngineConfig loadConfig(const std::string& filepath);

        // save current settings to file
        static void SaveConfig(const EngineConfig& config, const std::string& filepath);
    };
}