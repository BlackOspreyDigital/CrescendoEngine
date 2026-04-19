#include "WebGPURenderer.hpp"
#include "servers/rendering/Vertex.hpp"
#include "modules/gltf/AssetLoader.hpp" 
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
        if (!queue) { std::cout << "[Dawn Error] Failed to get queue!" << std::endl; return; }

        int width = 800, height = 600;
        if (display) display->get_window_size(&width, &height);
        if (width == 0 || height == 0) { width = 800; height = 600; } // Fallback to prevent 0-size texture crashes

        wgpu::SurfaceConfiguration config {};
        config.device = device;
        config.format = wgpu::TextureFormat::RGBA8Unorm; 
        config.usage = wgpu::TextureUsage::RenderAttachment;
        config.width = static_cast<uint32_t>(width);
        config.height = static_cast<uint32_t>(height);
        config.presentMode = wgpu::PresentMode::Fifo;
        config.alphaMode = wgpu::CompositeAlphaMode::Auto;
        surface.Configure(&config);

        wgpu::TextureDescriptor depthDesc{};
        depthDesc.usage = wgpu::TextureUsage::RenderAttachment;
        depthDesc.dimension = wgpu::TextureDimension::e2D;
        depthDesc.size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        depthDesc.format = wgpu::TextureFormat::Depth24Plus;
        depthDesc.mipLevelCount = 1; 
        depthDesc.sampleCount = 1;   
        
        depthTexture = device.CreateTexture(&depthDesc);
        if (!depthTexture) { std::cout << "[Dawn Error] Depth Texture creation failed!" << std::endl; return; }
        
        depthView = depthTexture.CreateView();
        if (!depthView) { std::cout << "[Dawn Error] Depth View creation failed!" << std::endl; return; }

        wgpu::BufferDescriptor uboDesc{};
        uboDesc.size = sizeof(glm::mat4);
        uboDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniformBuffer = device.CreateBuffer(&uboDesc);
        if (!uniformBuffer) { std::cout << "[Dawn Error] UBO creation failed!" << std::endl; return; }

        wgpu::BindGroupLayoutEntry bglEntry{};
        bglEntry.binding = 0;
        bglEntry.visibility = wgpu::ShaderStage::Vertex;
        bglEntry.buffer.type = wgpu::BufferBindingType::Uniform;
        bglEntry.buffer.minBindingSize = sizeof(glm::mat4);

        wgpu::BindGroupLayoutDescriptor bglDesc{};
        bglDesc.entryCount = 1;
        bglDesc.entries = &bglEntry;
        bindGroupLayout = device.CreateBindGroupLayout(&bglDesc);
        if (!bindGroupLayout) { std::cout << "[Dawn Error] BindGroupLayout failed!" << std::endl; return; }

        wgpu::BindGroupEntry bgEntry{};
        bgEntry.binding = 0;
        bgEntry.buffer = uniformBuffer;
        bgEntry.offset = 0;
        bgEntry.size = sizeof(glm::mat4);

        wgpu::BindGroupDescriptor bgDesc{};
        bgDesc.layout = bindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        bindGroup = device.CreateBindGroup(&bgDesc);
        if (!bindGroup) { std::cout << "[Dawn Error] BindGroup failed!" << std::endl; return; }

        // EXPLICIT mat4x4<f32> FOR WIDER DAWN COMPATIBILITY
        const char* wgsl = R"(
            struct UBO {
                mvp: mat4x4<f32>, 
            };
            @group(0) @binding(0) var<uniform> ubo: UBO;

            struct VertexInput {
                @location(0) pos: vec3f,
                @location(1) normal: vec3f,
            };
            struct VertexOutput {
                @builtin(position) position: vec4f,
                @location(0) color: vec3f,
            };
            @vertex
            fn vs_main(in: VertexInput) -> VertexOutput {
                var out: VertexOutput;
                out.position = ubo.mvp * vec4f(in.pos, 1.0);
                out.color = in.normal * 0.5 + vec3f(0.5, 0.5, 0.5);
                return out;
            }
            @fragment
            fn fs_main(in: VertexOutput) -> @location(0) vec4f {
                return vec4f(in.color, 1.0);
            }
        )";

        wgpu::ShaderSourceWGSL wgslDesc{};
        wgslDesc.code = wgsl;
        wgslDesc.sType = wgpu::SType::ShaderSourceWGSL;
        wgpu::ShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &wgslDesc;
        wgpu::ShaderModule shader = device.CreateShaderModule(&shaderDesc);
        if (!shader) { std::cout << "[Dawn Error] Shader Module failed!" << std::endl; return; }

        wgpu::PipelineLayoutDescriptor layoutDesc{};
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &bindGroupLayout;
        wgpu::PipelineLayout layout = device.CreatePipelineLayout(&layoutDesc);
        if (!layout) { std::cout << "[Dawn Error] Pipeline Layout failed!" << std::endl; return; }

        wgpu::VertexAttribute attributes[2];
        attributes[0].format = wgpu::VertexFormat::Float32x3;
        attributes[0].offset = 0;
        attributes[0].shaderLocation = 0;
        attributes[1].format = wgpu::VertexFormat::Float32x3;
        attributes[1].offset = sizeof(float) * 3; 
        attributes[1].shaderLocation = 1;

        wgpu::VertexBufferLayout vertexLayout{};
        vertexLayout.arrayStride = sizeof(Vertex);
        vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
        vertexLayout.attributeCount = 2;
        vertexLayout.attributes = attributes;

        wgpu::ColorTargetState colorTarget{};
        colorTarget.format = wgpu::TextureFormat::RGBA8Unorm;
        colorTarget.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState fragmentState{};
        fragmentState.module = shader;
        fragmentState.entryPoint = "fs_main";
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        wgpu::DepthStencilState depthStencil{};
        depthStencil.format = wgpu::TextureFormat::Depth24Plus;
        depthStencil.depthWriteEnabled = true;
        depthStencil.depthCompare = wgpu::CompareFunction::Less;

        wgpu::RenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.layout = layout; 
        pipelineDesc.vertex.module = shader;
        pipelineDesc.vertex.entryPoint = "vs_main";
        pipelineDesc.vertex.bufferCount = 1; 
        pipelineDesc.vertex.buffers = &vertexLayout;
        pipelineDesc.fragment = &fragmentState;
        pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipelineDesc.primitive.cullMode = wgpu::CullMode::Back; 
        pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
        pipelineDesc.depthStencil = &depthStencil;

        pipeline = device.CreateRenderPipeline(&pipelineDesc);
        if (!pipeline) { std::cout << "[Dawn Error] Pipeline failed!" << std::endl; return; }

        std::cout << "[WebGPURenderer] Hardware Bridge Fully Configured!" << std::endl;
    }

    void WebGPURenderer::render(Scene* scene, SceneManager* sceneManager, EngineState& state) {
        if (!device || !surface || !queue || !pipeline) return; 

        static bool initialModelLoaded = false;
        if (!initialModelLoaded) {
            Crescendo::AssetLoader::loadModel(this, "assets/systemsymbols/defaultplayer.glb", scene);
            initialModelLoaded = true;
        }

        int width = 800, height = 600;
        if (display) display->get_window_size(&width, &height);
        if (width == 0 || height == 0) return; // Prevent zero division
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);

        static float time = 0.0f;
        time += 0.016f; 

        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 mvp = projection * view * model;

        queue.WriteBuffer(uniformBuffer, 0, glm::value_ptr(mvp), sizeof(glm::mat4));

        wgpu::SurfaceTexture surfaceTexture;
        surface.GetCurrentTexture(&surfaceTexture);
        if (!surfaceTexture.texture) return; 

        wgpu::TextureView colorView = surfaceTexture.texture.CreateView();
        if (!colorView) return; 
        
        wgpu::RenderPassColorAttachment colorAttachment {};
        colorAttachment.view = colorView;
#ifdef __EMSCRIPTEN__
        colorAttachment.depthSlice = wgpu::kDepthSliceUndefined;
#endif
        colorAttachment.loadOp = wgpu::LoadOp::Clear;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = { 0.1f, 0.15f, 0.2f, 1.0f }; 

        // REQUIRED BY WEBGPU STANDARD: Depth24Plus cannot be 'Stored'
        wgpu::RenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = depthView;
        depthAttachment.depthClearValue = 1.0f;
        depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
        depthAttachment.depthStoreOp = wgpu::StoreOp::Discard; 

        wgpu::RenderPassDescriptor renderPassDesc {};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;
        renderPassDesc.depthStencilAttachment = &depthAttachment;

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        if (!encoder) return; 

        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPassDesc);
        
        if (pass) {
            pass.SetPipeline(pipeline);
            if (bindGroup) pass.SetBindGroup(0, bindGroup); 
            
            for (const auto& mesh : meshes) {
                if (mesh.vertexBuffer && mesh.indexBuffer) {
                    pass.SetVertexBuffer(0, mesh.vertexBuffer);
                    pass.SetIndexBuffer(mesh.indexBuffer, wgpu::IndexFormat::Uint32);
                    pass.DrawIndexed(mesh.indexCount);
                }
            }
            
            pass.End();
        }

        wgpu::CommandBuffer commands = encoder.Finish();
        if (!commands) return; 
        
        queue.Submit(1, &commands);
    }

    void WebGPURenderer::shutdown() {
        if (surface) surface.Unconfigure();
        pipeline = nullptr; depthView = nullptr; depthTexture = nullptr;
        uniformBuffer = nullptr; bindGroup = nullptr; bindGroupLayout = nullptr;
        queue = nullptr; device = nullptr; adapter = nullptr; surface = nullptr; instance = nullptr;
    }
}