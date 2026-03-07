#include "ImageLoader.hpp"
#include <iostream>
#include <glm/gtc/constants.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Crescendo {

    void RawImageData::free() {
        if (pixels) { stbi_image_free(pixels); pixels = nullptr; }
        if (hdrPixels) { stbi_image_free(hdrPixels); hdrPixels = nullptr; }
    }

    RawImageData ImageLoader::loadStandardTexture(const std::string& path) {
        RawImageData data;
        data.pixels = stbi_load(path.c_str(), &data.width, &data.height, &data.channels, STBI_rgb_alpha);
        
        if (!data.pixels) {
            std::cerr << "[ImageLoader] Failed to load texture: " << path << std::endl;
        }
        return data;
    }

    RawImageData ImageLoader::loadHDRTexture(const std::string& path) {
        RawImageData data;
        data.isHDR = true;
        data.hdrPixels = stbi_loadf(path.c_str(), &data.width, &data.height, &data.channels, 4); // Force 4 channels for Vulkan
        
        if (!data.hdrPixels) {
            std::cerr << "[ImageLoader] Failed to load HDR: " << path << std::endl;
        }
        return data;
    }

    bool ImageLoader::extractHDRSunParams(const std::string& path, glm::vec3& outDir, glm::vec3& outColor, float& outIntensity) {
        int width, height, channels;
        float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 3);
        if (!data) return false;

        float maxLuminance = -1.0f;
        int maxIndex = 0;
        int maxX = 0, maxY = 0;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 3;
                float luminance = 0.2126f * data[idx] + 0.7152f * data[idx + 1] + 0.0722f * data[idx + 2]; 
                if (luminance > maxLuminance) {
                    maxLuminance = luminance;
                    maxIndex = idx;
                    maxX = x; maxY = y;
                }
            }
        }

        outIntensity = maxLuminance; 
        if (outIntensity > 0.0f) {
            outColor = glm::vec3(data[maxIndex], data[maxIndex + 1], data[maxIndex + 2]) / outIntensity;
        } else {
            outColor = glm::vec3(1.0f);
        }

        float u = (float)maxX / (float)width;
        float v = (float)maxY / (float)height;
        float theta = (u - 0.5f) * 2.0f * glm::pi<float>();
        float phi = (v - 0.5f) * glm::pi<float>();

        outDir.x = std::cos(phi) * std::cos(theta);
        outDir.y = std::cos(phi) * std::sin(theta);
        outDir.z = -std::sin(phi);
        outDir = glm::normalize(outDir);

        stbi_image_free(data);
        return true;
    }
}