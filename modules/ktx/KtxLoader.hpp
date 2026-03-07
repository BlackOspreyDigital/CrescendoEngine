#pragma once
#include <string>
#include <vulkan/vulkan_core.h>

namespace Crescendo {
    class RenderingServer;

    // The raw bytes for a 6-sided Cubemap with Mipmaps
    struct RawCubemapData {
        void* pixels = nullptr;
        size_t totalSize = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipLevels = 0;
        
        void* internalPtr = nullptr; // Keeps the GLI object alive in memory
        
        void free(); 
    };

    class KtxLoader {
    public:
        // Existing libktx standard texture loader
        static int loadTexture(RenderingServer* renderer, const std::string& path);
        
        // NEW: Parses a .ktx file and extracts the 6 faces
        static RawCubemapData loadCubemap(const std::string& path);
    };
}