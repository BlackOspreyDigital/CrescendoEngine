#pragma once
#include "servers/rendering/IRenderer.hpp"
#include "servers/rendering/RenderTypes.hpp"
#include "servers/display/DisplayServer.hpp"
#include <string>
#include <vector>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
    #include <webgpu/webgpu_cpp.h>
#else
    // --- FINAL LSP MOCK ---
    namespace wgpu {
        struct ChainedStruct { uint32_t sType = 0; const ChainedStruct* next = nullptr; };
        enum class SType : uint32_t { 
            Invalid = 0, 
            EmscriptenSurfaceSourceCanvasHTMLSelector = 0x00010001, 
            ShaderSourceWGSL = 0x00010002 
        };
        struct EmscriptenSurfaceSourceCanvasHTMLSelector : ChainedStruct { const char* selector; };
        struct ShaderSourceWGSL : ChainedStruct { const char* code; }; 
        
        enum class RequestAdapterStatus : uint32_t { Success };
        enum class RequestDeviceStatus : uint32_t { Success };
        enum class CallbackMode : uint32_t { AllowSpontaneous };
        enum class TextureFormat : uint32_t { BGRA8Unorm, RGBA8Unorm, Depth24Plus };
        enum class TextureUsage : uint32_t { RenderAttachment };
        enum class CompositeAlphaMode : uint32_t { Auto };
        enum class PresentMode : uint32_t { Fifo };
        enum class LoadOp : uint32_t { Clear };
        enum class StoreOp : uint32_t { Store };
        enum class BufferUsage : uint32_t { None = 0, Vertex = 0x0004, Index = 0x0010, CopyDst = 0x0008 };
        inline BufferUsage operator|(BufferUsage a, BufferUsage b) { return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }

        enum class VertexFormat : uint32_t { Float32x3 };
        enum class VertexStepMode : uint32_t { Vertex };
        enum class BlendFactor : uint32_t { One, Zero };
        enum class BlendOperation : uint32_t { Add };
        enum class ColorWriteMask : uint32_t { All };
        enum class PrimitiveTopology : uint32_t { TriangleList };
        enum class CullMode : uint32_t { Back };
        enum class FrontFace : uint32_t { CCW };
        enum class CompareFunction : uint32_t { Less };
        enum class IndexFormat : uint32_t { Uint32 };

        const uint32_t kDepthSliceUndefined = 0xFFFFFFFF;

        #define MOCK_WGPU_TYPE(Name) class Name { public: Name() {} Name(std::nullptr_t) {} operator bool() const { return false; } };

        MOCK_WGPU_TYPE(CommandBuffer)
        MOCK_WGPU_TYPE(TextureView)
        MOCK_WGPU_TYPE(Buffer)
        MOCK_WGPU_TYPE(RenderPipeline)
        MOCK_WGPU_TYPE(ShaderModule)

        struct BufferDescriptor { uint64_t size; BufferUsage usage; bool mappedAtCreation = false; };
        class Texture { public: TextureView CreateView() { return TextureView(); } };
        struct SurfaceTexture { Texture texture; };

        class RenderPassEncoder { public: void End() {} void SetPipeline(RenderPipeline) {} void SetVertexBuffer(uint32_t, Buffer) {} void SetIndexBuffer(Buffer, IndexFormat) {} void DrawIndexed(uint32_t) {} void Draw(uint32_t) {} };
        class CommandEncoder { public: RenderPassEncoder BeginRenderPass(const void*) { return RenderPassEncoder(); } CommandBuffer Finish() { return CommandBuffer(); } };
        class Queue { public: void Submit(uint32_t, const CommandBuffer*) {} void WriteBuffer(Buffer, uint64_t, const void*, size_t) {} };
        
        struct ShaderModuleDescriptor { const ChainedStruct* nextInChain; };
        struct TextureDescriptor { TextureUsage usage; uint32_t size[3]; TextureFormat format; };

        class Device { public: Queue GetQueue() { return Queue(); } CommandEncoder CreateCommandEncoder() { return CommandEncoder(); } Buffer CreateBuffer(const BufferDescriptor*) { return Buffer(); } ShaderModule CreateShaderModule(const ShaderModuleDescriptor*) { return ShaderModule(); } RenderPipeline CreateRenderPipeline(const void*) { return RenderPipeline(); } Texture CreateTexture(const TextureDescriptor*) { return Texture(); } };
        
        // --- UPDATED MOCKS TO ACCEPT TYPED USERDATA ---
        class Adapter { public: template<typename F, typename T> void RequestDevice(const void*, CallbackMode, F&&, T) {} };
        class Surface { public: void Configure(const void*) {} void Unconfigure() {} void GetCurrentTexture(SurfaceTexture*) {} };
        struct SurfaceDescriptor { const ChainedStruct* nextInChain; };
        class Instance { public: Surface CreateSurface(const SurfaceDescriptor*) { return Surface(); } template<typename F, typename T> void RequestAdapter(const void*, CallbackMode, F&&, T) {} };

        struct Color { float r, g, b, a; };
        struct RenderPassColorAttachment { TextureView view; uint32_t depthSlice; LoadOp loadOp; StoreOp storeOp; Color clearValue; };
        struct RenderPassDepthStencilAttachment { TextureView view; float depthClearValue; LoadOp depthLoadOp; StoreOp depthStoreOp; };
        struct RenderPassDescriptor { uint32_t colorAttachmentCount; const RenderPassColorAttachment* colorAttachments; const RenderPassDepthStencilAttachment* depthStencilAttachment; };
        struct SurfaceConfiguration { Device device; TextureFormat format; TextureUsage usage; uint32_t width; uint32_t height; PresentMode presentMode; CompositeAlphaMode alphaMode; };

        static inline Instance CreateInstance(const void* = nullptr) { return Instance(); }
    }
#endif

namespace Crescendo {

    struct WebGPUMesh {
        wgpu::Buffer vertexBuffer = nullptr;
        wgpu::Buffer indexBuffer = nullptr;
        uint32_t indexCount = 0;
        std::string name;
    };

    class WebGPURenderer : public IRenderer {
    public:
        WebGPURenderer() = default;
        ~WebGPURenderer() override { shutdown(); }

        bool initialize(DisplayServer* displayServer) override;
        void render(Scene* scene, SceneManager* sceneManager, EngineState& state) override;
        ChunkBakeResult buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) override { return {}; }
        void shutdown() override;

        wgpu::Device device = nullptr;
        wgpu::Queue queue = nullptr;

        std::vector<WebGPUMesh> meshes;
        std::unordered_map<std::string, size_t> meshMap;

    private:
        void BuildPipeline(); 

        wgpu::Instance instance = nullptr;
        wgpu::Adapter adapter = nullptr;
        wgpu::Surface surface = nullptr;
        DisplayServer* display = nullptr;

        wgpu::RenderPipeline pipeline = nullptr;
        wgpu::Texture depthTexture = nullptr;
        wgpu::TextureView depthView = nullptr;
    };
}