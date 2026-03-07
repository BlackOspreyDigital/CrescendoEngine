#pragma once
#include <string>
#include <glm/glm.hpp>

namespace Crescendo {

    // The raw bytes that the RenderingServer actually wants!
    struct RawImageData {
        unsigned char* pixels = nullptr;
        float* hdrPixels = nullptr; // Used if the image is HDR
        int width = 0;
        int height = 0;
        int channels = 0;
        bool isHDR = false;

        // Helper to safely free the memory once the server is done with it
        void free(); 
    };

    class ImageLoader {
    public:
        // Reads PNG/JPG from disk
        static RawImageData loadStandardTexture(const std::string& path);
        
        // Reads HDR from disk
        static RawImageData loadHDRTexture(const std::string& path);
        
        // Mathematical extraction, purely CPU-side
        static bool extractHDRSunParams(const std::string& path, glm::vec3& outDir, glm::vec3& outColor, float& outIntensity);
    };
}