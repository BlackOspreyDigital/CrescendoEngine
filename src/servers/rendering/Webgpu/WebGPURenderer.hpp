#pragma once
#include "servers/rendering/IRenderer.hpp"
#include "servers/rendering/RenderTypes.hpp"
#include "servers/display/DisplayServer.hpp"

#ifdef __EMSCRIPTEN__
    #include <webgpu/webgpu_cpp.h>
#else
    // --- ENHANCED LSP MOCK ---
    // This allows the IDE to compile the C++ syntax perfectly without needing Emscripten
    namespace wgpu {
        // A helper macro to create mock classes that can be assigned nullptr
        #define MOCK_WGPU_TYPE(Name) \
            class Name { \
            public: \
                Name() {} \
                Name(std::nullptr_t) {} \
                Name& operator=(std::nullptr_t) { return *this; } \
                operator bool() const { return false; } \
            };

        MOCK_WGPU_TYPE(Adapter)
        MOCK_WGPU_TYPE(Device)
        MOCK_WGPU_TYPE(Queue)
        
        struct SurfaceDescriptor { const void* nextInChain; };
        struct SurfaceDescriptorFromCanvasHTMLSelector { 
            const void* nextInChain; 
            const char* selector; 
        };
        struct SurfaceConfiguration {
            Device device; uint32_t format; uint32_t usage; uint32_t alphaMode;
            uint32_t width; uint32_t height; uint32_t presentMode;
        };

        class Surface {
        public:
            Surface() {}
            Surface(std::nullptr_t) {}
            Surface& operator=(std::nullptr_t) { return *this; }
            operator bool() const { return false; }
            void Configure(const SurfaceConfiguration*) {}
            void Unconfigure() {}
        };

        class Instance {
        public:
            Instance() {}
            Instance(std::nullptr_t) {}
            Instance& operator=(std::nullptr_t) { return *this; }
            operator bool() const { return false; }
            Surface CreateSurface(const SurfaceDescriptor*) { return Surface(); }
        };

        struct InstanceDescriptor {};
        Instance CreateInstance(const InstanceDescriptor*);
        
        enum class TextureFormat { BGRA8Unorm };
        enum class TextureUsage { RenderAttachment };
        enum class CompositeAlphaMode { Auto };
        enum class PresentMode { Fifo };
    }
    #undef MOCK_WGPU_TYPE
#endif



namespace Crescendo {

    class WebGPURenderer : public IRenderer {
    public:
        WebGPURenderer() = default;
        ~WebGPURenderer() override { shutdown(); }

        // Core IRenderer Interface
        bool initialize(DisplayServer* displayServer) override;
        void render(Scene* scene, SceneManager* sceneManager, EngineState& state) override;
        
        ChunkBakeResult buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) override;
        
        void shutdown() override;

    private:
        // --- CORE WEBGPU HANDLES ---
        wgpu::Instance instance = nullptr;
        wgpu::Adapter adapter = nullptr;
        wgpu::Device device = nullptr;
        wgpu::Queue queue = nullptr;
        wgpu::Surface surface = nullptr;

        // Display context
        DisplayServer* display = nullptr;

        // Initialization Helpers
        bool createInstanceAndSurface();
        bool requestAdapterAndDevice();
    };

}