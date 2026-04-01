#include "WebGPURenderer.hpp"
#include "servers/rendering/Vertex.hpp"
#include <iostream>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

namespace Crescendo {

    bool WebGPURenderer::initialize(DisplayServer* displayServer) {
        display = displayServer;
        instance = wgpu::CreateInstance(nullptr);

        wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc {};
        canvasDesc.sType = wgpu::SType::EmscriptenSurfaceSourceCanvasHTMLSelector;
        canvasDesc.selector = "#canvas";
        
        wgpu::SurfaceDescriptor surfaceDesc {};
        surfaceDesc.nextInChain = &canvasDesc;
        surface = instance.CreateSurface(&surfaceDesc);

        // PERFECTLY TYPED LAMBDAS
        instance.RequestAdapter(nullptr, wgpu::CallbackMode::AllowSpontaneous,
            [](wgpu::RequestAdapterStatus status, wgpu::Adapter res, const char* msg, WebGPURenderer* self) {
                if (status != wgpu::RequestAdapterStatus::Success) return;
                
                self->adapter = res;

                self->adapter.RequestDevice(nullptr, wgpu::CallbackMode::AllowSpontaneous,
                    [](wgpu::RequestDeviceStatus status, wgpu::Device res, const char* msg, WebGPURenderer* self) {
                        if (status != wgpu::RequestDeviceStatus::Success) return;
                        
                        self->device = res;
                        self->BuildPipeline(); 
                    }, self); 
            }, this); 

        return true; 
    }

    void WebGPURenderer::BuildPipeline() {
        queue = device.GetQueue();

        int width = 800, height = 600;
        if (display) display->get_window_size(&width, &height);

        wgpu::SurfaceConfiguration config {};
        config.device = device;
        config.format = wgpu::TextureFormat::RGBA8Unorm; 
        config.usage = wgpu::TextureUsage::RenderAttachment;
        config.width = static_cast<uint32_t>(width);
        config.height = static_cast<uint32_t>(height);
        config.presentMode = wgpu::PresentMode::Fifo;
        config.alphaMode = wgpu::CompositeAlphaMode::Auto;
        surface.Configure(&config);

        const char* wgsl = R"(
            @vertex
            fn vs_main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4f {
                var pos = array<vec2f, 3>(
                    vec2f(0.0, 0.5),
                    vec2f(-0.5, -0.5),
                    vec2f(0.5, -0.5)
                );
                return vec4f(pos[VertexIndex], 0.0, 1.0);
            }
            @fragment
            fn fs_main() -> @location(0) vec4f {
                return vec4f(1.0, 0.0, 0.0, 1.0);
            }
        )";

        wgpu::ShaderSourceWGSL wgslDesc{};
        wgslDesc.code = wgsl;
        wgslDesc.sType = wgpu::SType::ShaderSourceWGSL;
        wgpu::ShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &wgslDesc;
        wgpu::ShaderModule shader = device.CreateShaderModule(&shaderDesc);

        wgpu::PipelineLayoutDescriptor layoutDesc{};
        layoutDesc.bindGroupLayoutCount = 0;
        layoutDesc.bindGroupLayouts = nullptr;
        wgpu::PipelineLayout layout = device.CreatePipelineLayout(&layoutDesc);

        wgpu::ColorTargetState colorTarget{};
        colorTarget.format = wgpu::TextureFormat::RGBA8Unorm;
        colorTarget.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState fragmentState{};
        fragmentState.module = shader;
        fragmentState.entryPoint = "fs_main";
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        wgpu::RenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.layout = layout; 
        pipelineDesc.vertex.module = shader;
        pipelineDesc.vertex.entryPoint = "vs_main";
        pipelineDesc.vertex.bufferCount = 0; 
        pipelineDesc.vertex.buffers = nullptr;
        pipelineDesc.fragment = &fragmentState;
        pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipelineDesc.primitive.cullMode = wgpu::CullMode::None; 
        pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;

        pipeline = device.CreateRenderPipeline(&pipelineDesc);

        std::cout << "[WebGPURenderer] Hardware Bridge Fully Configured!" << std::endl;
    }

    void WebGPURenderer::render(Scene* scene, SceneManager* sceneManager, EngineState& state) {
        if (!device || !surface || !queue || !pipeline) return; 

        wgpu::SurfaceTexture surfaceTexture;
        surface.GetCurrentTexture(&surfaceTexture);
        if (!surfaceTexture.texture) return; 

        wgpu::TextureView view = surfaceTexture.texture.CreateView();
        if (!view) return; 
        
        wgpu::RenderPassColorAttachment colorAttachment {};
        colorAttachment.view = view;
#ifdef __EMSCRIPTEN__
        colorAttachment.depthSlice = wgpu::kDepthSliceUndefined;
#endif
        colorAttachment.loadOp = wgpu::LoadOp::Clear;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = { 0.1f, 0.15f, 0.2f, 1.0f }; 

        wgpu::RenderPassDescriptor renderPassDesc {};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        if (!encoder) return; 

        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPassDesc);
        
        if (pass) {
            pass.SetPipeline(pipeline);
            pass.Draw(3); // DRAW THE TRIANGLE
            pass.End();
        }

        wgpu::CommandBuffer commands = encoder.Finish();
        if (!commands) return; 
        
        queue.Submit(1, &commands);
    }

    void WebGPURenderer::shutdown() {
        if (surface) surface.Unconfigure();
        pipeline = nullptr; 
        queue = nullptr; device = nullptr; adapter = nullptr; surface = nullptr; instance = nullptr;
    }
}