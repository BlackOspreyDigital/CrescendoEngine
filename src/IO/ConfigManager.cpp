#include "ConfigManager.hpp"
#include <iostream>
#include <fstream>

#include "deps/toml.hpp"

namespace Crescendo {

    EngineConfig ConfigManager::loadConfig(const std::string& filepath) {
        EngineConfig config; // start with defaults defined in struct

        try {
            // parse the file
            toml::table tbl = toml::parse_file(filepath);

            // read graphics
            config.msaaSamples = tbl["Graphics"]["msaa_samples"].value_or(config.msaaSamples);
            config.enableSSAO  = tbl["Graphics"]["enable_ssao"].value_or(config.enableSSAO);
            config.enableSSR   = tbl["Graphics"]["enable_ssr"].value_or(config.enableSSR);

            // read shadows
            config.shadowBiasConstant = tbl["Shadows"]["bias_constant"].value_or(config.shadowBiasConstant);
            config.shadowBiasSlope    = tbl["Shadows"]["bias_slope"].value_or(config.shadowBiasSlope);

            // read sun colors
            if (auto sunArr = tbl["Environment"]["sun_color"].as_array()) {
                config.sunColor.r = sunArr->get(0)->value_or(config.sunColor.r);
                config.sunColor.g = sunArr->get(1)->value_or(config.sunColor.g);
                config.sunColor.b = sunArr->get(2)->value_or(config.sunColor.b);
            }
            config.sunIntensity = tbl["Environment"]["sun_intensity"].value_or(config.sunIntensity);

            std::cout << "[Config] Successfully loaded: " << filepath << std::endl;
        }
        catch (const toml::parse_error& err) {
            std::cerr << "[Config] Parse error or file not found. Using defaults. " << err.description() << std::endl;
        }

        return config;
    }

    void ConfigManager::SaveConfig(const EngineConfig& config, const std::string& filepath) {
        // construct table in memory

     auto tbl = toml::table{
            { "Graphics", toml::table{
                { "msaa_samples", config.msaaSamples },
                { "enable_ssao", config.enableSSAO },
                { "enable_ssr", config.enableSSR }
            }},
            { "Shadows", toml::table{
                { "bias_constant", config.shadowBiasConstant },
                { "bias_slope", config.shadowBiasSlope }
            }},
            { "Environment", toml::table{
                { "sun_intensity", config.sunIntensity },
                { "sun_color", toml::array{config.sunColor.r, config.sunColor.g, config.sunColor.b} }
            }}
        };

        // Write to file
        std::ofstream file(filepath);
        if (file.is_open()) {
            file << tbl;
            file.close();
            std::cout << "[Config] Saved configuration to: " << filepath << std::endl;
        } else {
            std::cerr << "[Config] Failed to open file for writing: " << filepath << std::endl;
        }
    }
}