#pragma once
#include "servers/rendering/IRenderer.hpp"
#include "servers/rendering/RenderTypes.hpp"
#include "servers/display/DisplayServer.hpp"
#include "servers/camera/Camera.hpp"
#include <string>
#include <vector>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
    #include <webgpu/webgpu_cpp.h>
#else
    namespace wgpu {
        enum class SType : uint32_t { Invalid = 0, EmscriptenSurfaceSourceCanvasHTMLSelector = 0x00010001, ShaderSourceWGSL = 0x00010002 };
        struct ChainedStruct { SType sType = SType::Invalid; const ChainedStruct* next = nullptr; };
        struct EmscriptenSurfaceSourceCanvasHTMLSelector : ChainedStruct { const char* selector; };
        struct ShaderSourceWGSL : ChainedStruct { const char* code; }; 
        
        enum class RequestAdapterStatus : uint32_t { Success };
        enum class RequestDeviceStatus : uint32_t { Success };
        enum class CallbackMode : uint32_t { AllowSpontaneous };
        enum class TextureFormat : uint32_t { BGRA8Unorm, RGBA8Unorm, Depth24Plus };
        enum class TextureUsage : uint32_t { RenderAttachment = 1, TextureBinding = 4, CopyDst = 8 };
        inline TextureUsage operator|(TextureUsage a, TextureUsage b) { return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }

        enum class CompositeAlphaMode : uint32_t { Auto };
        enum class PresentMode : uint32_t { Fifo };
        enum class LoadOp : uint32_t { Clear };
        enum class StoreOp : uint32_t { Store = 1, Discard = 2 };
        enum class BufferUsage : uint32_t { None = 0, Vertex = 0x0004, Index = 0x0010, CopyDst = 0x0008, Uniform = 0x0040 };
        inline BufferUsage operator|(BufferUsage a, BufferUsage b) { return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }

        enum class VertexFormat : uint32_t { Float32x3, Float32x2 };
        enum class VertexStepMode : uint32_t { Vertex };
        enum class BlendFactor : uint32_t { One, Zero };
        enum class BlendOperation : uint32_t { Add };
        enum class ColorWriteMask : uint32_t { All };
        enum class PrimitiveTopology : uint32_t { TriangleList };
        enum class CullMode : uint32_t { Back };
        enum class FrontFace : uint32_t { CCW };
        enum class CompareFunction : uint32_t { Less };
        enum class IndexFormat : uint32_t { Uint32 };
        
        enum class ShaderStage : uint32_t { Vertex = 1, Fragment = 2, Compute = 4 };
        inline ShaderStage operator|(ShaderStage a, ShaderStage b) { return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
        
        enum class BufferBindingType : uint32_t { Uniform = 0 };
        enum class TextureDimension : uint32_t { e1D = 1, e2D = 2, e3D = 3 }; 
        enum class TextureViewDimension : uint32_t { e2D = 1 };
        enum class TextureSampleType : uint32_t { Float = 1 };
        enum class SamplerBindingType : uint32_t { Filtering = 1 };
        enum class AddressMode : uint32_t { ClampToEdge = 1 };
        enum class FilterMode : uint32_t { Linear = 1 };
        enum class MipmapFilterMode : uint32_t { Linear = 1 };
        enum class TextureAspect : uint32_t { All = 1 };

        const uint32_t kDepthSliceUndefined = 0xFFFFFFFF;
        const uint64_t kWholeSize = 0xFFFFFFFFFFFFFFFF;

        #define MOCK_WGPU_TYPE(Name) class Name { public: Name() {} Name(std::nullptr_t) {} operator bool() const { return true; } };

        MOCK_WGPU_TYPE(CommandBuffer)
        MOCK_WGPU_TYPE(TextureView)
        MOCK_WGPU_TYPE(Buffer)
        MOCK_WGPU_TYPE(RenderPipeline)
        MOCK_WGPU_TYPE(ShaderModule)
        MOCK_WGPU_TYPE(BindGroupLayout)
        MOCK_WGPU_TYPE(BindGroup)
        MOCK_WGPU_TYPE(PipelineLayout)
        MOCK_WGPU_TYPE(Sampler)

        struct Extent3D { uint32_t width, height, depthOrArrayLayers; };
        struct BufferDescriptor { uint64_t size; BufferUsage usage; bool mappedAtCreation = false; };
        class Texture { public: Texture() {} Texture(std::nullptr_t) {} operator bool() const { return true; } TextureView CreateView() { return TextureView(); } };
        struct SurfaceTexture { Texture texture; };

        class RenderPassEncoder { public: RenderPassEncoder() {} RenderPassEncoder(std::nullptr_t) {} operator bool() const { return true; } void End() {} void SetPipeline(RenderPipeline) {} void SetVertexBuffer(uint32_t, Buffer) {} void SetIndexBuffer(Buffer, IndexFormat) {} void DrawIndexed(uint32_t) {} void Draw(uint32_t) {} void SetBindGroup(uint32_t, BindGroup) {} };
        class CommandEncoder { public: CommandEncoder() {} CommandEncoder(std::nullptr_t) {} operator bool() const { return true; } RenderPassEncoder BeginRenderPass(const void*) { return RenderPassEncoder(); } CommandBuffer Finish() { return CommandBuffer(); } };
        
        struct TexelCopyTextureInfo { Texture texture; uint32_t mipLevel; wgpu::Extent3D origin; TextureAspect aspect; };
        struct TexelCopyBufferLayout { uint64_t offset; uint32_t bytesPerRow; uint32_t rowsPerImage; };
        class Queue { public: Queue() {} Queue(std::nullptr_t) {} operator bool() const { return true; } void Submit(uint32_t, const CommandBuffer*) {} void WriteBuffer(Buffer, uint64_t, const void*, size_t) {} void WriteTexture(const TexelCopyTextureInfo*, const void*, size_t, const TexelCopyBufferLayout*, const Extent3D*) {} };

        struct ShaderModuleDescriptor { const ChainedStruct* nextInChain; };
        struct TextureDescriptor { TextureUsage usage; TextureDimension dimension; Extent3D size; TextureFormat format; uint32_t mipLevelCount; uint32_t sampleCount; };
        
        struct BufferBindingLayout { BufferBindingType type; bool hasDynamicOffset; uint64_t minBindingSize; };
        struct TextureBindingLayout { TextureSampleType sampleType; TextureViewDimension viewDimension; bool multisampled; };
        struct SamplerBindingLayout { SamplerBindingType type; };
        struct BindGroupLayoutEntry { uint32_t binding; ShaderStage visibility; BufferBindingLayout buffer; TextureBindingLayout texture; SamplerBindingLayout sampler; };
        struct BindGroupLayoutDescriptor { uint32_t entryCount; const BindGroupLayoutEntry* entries; };
        struct BindGroupEntry { uint32_t binding; Buffer buffer; uint64_t offset; uint64_t size; TextureView textureView; Sampler sampler; };
        struct BindGroupDescriptor { BindGroupLayout layout; uint32_t entryCount; const BindGroupEntry* entries; };
        struct PipelineLayoutDescriptor { uint32_t bindGroupLayoutCount; const BindGroupLayout* bindGroupLayouts; };
        struct SamplerDescriptor { AddressMode addressModeU; AddressMode addressModeV; AddressMode addressModeW; FilterMode magFilter; FilterMode minFilter; MipmapFilterMode mipmapFilter; };

        // --- NEW PIPELINE STRUCTS ---
        struct VertexAttribute { VertexFormat format; uint64_t offset; uint32_t shaderLocation; };
        struct VertexBufferLayout { uint64_t arrayStride; VertexStepMode stepMode; uint32_t attributeCount; const VertexAttribute* attributes; };
        struct ColorTargetState { TextureFormat format; ColorWriteMask writeMask; };
        struct FragmentState { ShaderModule module; const char* entryPoint; uint32_t targetCount; const ColorTargetState* targets; };
        struct DepthStencilState { TextureFormat format; bool depthWriteEnabled; CompareFunction depthCompare; };
        struct VertexState { ShaderModule module; const char* entryPoint; uint32_t bufferCount; const VertexBufferLayout* buffers; };
        struct PrimitiveState { PrimitiveTopology topology; CullMode cullMode; FrontFace frontFace; };
        struct RenderPipelineDescriptor { PipelineLayout layout; VertexState vertex; const FragmentState* fragment; PrimitiveState primitive; const DepthStencilState* depthStencil; };

        class Device { public: Device() {} Device(std::nullptr_t) {} operator bool() const { return true; } Queue GetQueue() { return Queue(); } CommandEncoder CreateCommandEncoder() { return CommandEncoder(); } Buffer CreateBuffer(const BufferDescriptor*) { return Buffer(); } ShaderModule CreateShaderModule(const ShaderModuleDescriptor*) { return ShaderModule(); } RenderPipeline CreateRenderPipeline(const void*) { return RenderPipeline(); } Texture CreateTexture(const TextureDescriptor*) { return Texture(); } BindGroupLayout CreateBindGroupLayout(const BindGroupLayoutDescriptor*) { return BindGroupLayout(); } BindGroup CreateBindGroup(const BindGroupDescriptor*) { return BindGroup(); } PipelineLayout CreatePipelineLayout(const PipelineLayoutDescriptor*) { return PipelineLayout(); } Sampler CreateSampler(const SamplerDescriptor*) { return Sampler(); } };
        class Adapter { public: Adapter() {} Adapter(std::nullptr_t) {} operator bool() const { return true; } template<typename F, typename T> void RequestDevice(const void*, CallbackMode, F&&, T) {} };
        class Surface { public: Surface() {} Surface(std::nullptr_t) {} operator bool() const { return true; } void Configure(const void*) {} void Unconfigure() {} void GetCurrentTexture(SurfaceTexture*) {} };
        struct SurfaceDescriptor { const ChainedStruct* nextInChain; };
        class Instance { public: Instance() {} Instance(std::nullptr_t) {} operator bool() const { return true; } Surface CreateSurface(const SurfaceDescriptor*) { return Surface(); } template<typename F, typename T> void RequestAdapter(const void*, CallbackMode, F&&, T) {} };

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
        bool initialized = false;
        bool isInitialized() const override { return this->initialized; }
        void render(Scene* scene, SceneManager* sceneManager, EngineState& state) override;
        ChunkBakeResult buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) override { return {}; }
        void shutdown() override;
        // --- Unified Camera Access ---
        Crescendo::Camera mainCamera; // Ensure camera tracking exists
        Camera* GetMainCamera() override { return &mainCamera; } // <-- Add this override

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
        wgpu::RenderPipeline bgPipeline = nullptr;
        wgpu::Texture depthTexture = nullptr;
        wgpu::TextureView depthView = nullptr;
        
        wgpu::Buffer uniformBuffer = nullptr;
        wgpu::BindGroup bindGroup = nullptr;
        wgpu::BindGroupLayout bindGroupLayout = nullptr;

        wgpu::Texture cardTexture = nullptr;
        wgpu::TextureView cardTextureView = nullptr;
        wgpu::Sampler cardSampler = nullptr;
    };
}