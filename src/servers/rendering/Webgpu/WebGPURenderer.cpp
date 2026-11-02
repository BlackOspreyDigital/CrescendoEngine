#include "WebGPURenderer.hpp"
#include "servers/rendering/Vertex.hpp"
#include "modules/gltf/AssetLoader.hpp" 
#include "scene/Scene.hpp"
#include "IO/SceneManager.hpp"
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <webgpu/webgpu.h>
    // Emscripten exposes the webgpu device getter via webgpu_cpp or standard interop:
    extern "C" WGPUDevice emscripten_webgpu_get_device();
#endif

// --- INTERACTION STATE ---
static bool isScraping = false;
static float interactiveRip = -0.1f; // Starts below 0.0 so the foil is 100% solid on load
static double lastMouseY = 0.0;

// Tracking for the Parallax Tilt
static float tiltTargetX = 0.0f;
static float tiltTargetY = 0.0f;
static float currentTiltX = 0.0f;
static float currentTiltY = 0.0f;

#ifdef __EMSCRIPTEN__
// --- EMSCRIPTEN MOUSE CALLBACKS ---
EM_BOOL on_mouse_down(int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData) {
    isScraping = true;
    return EM_TRUE;
}

EM_BOOL on_mouse_up(int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData) {
    isScraping = false;
    return EM_TRUE;
}

EM_BOOL on_mouse_move(int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData) {
    double canvasWidth, canvasHeight;
    emscripten_get_element_css_size("#canvas", &canvasWidth, &canvasHeight);
    
    // 1. Constantly track normalized mouse position (-1.0 to 1.0) for the tilt effect
    tiltTargetX = (mouseEvent->targetX / canvasWidth) * 2.0f - 1.0f;
    tiltTargetY = (mouseEvent->targetY / canvasHeight) * 2.0f - 1.0f;

    // 2. Handle the TCG foil rip
    if (isScraping) {
        double deltaY = mouseEvent->targetY - lastMouseY;
        lastMouseY = mouseEvent->targetY;
        
        // ONE-WAY TEAR: Only progress if the mouse is dragging downward!
        if (deltaY > 0.0) {
            interactiveRip += (deltaY / canvasHeight) * 2.5f; 
        }
        
        if (interactiveRip > 1.2f) interactiveRip = 1.2f;
    }
    return EM_TRUE;
}
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

        #ifdef __EMSCRIPTEN__
        std::cout << "[WebGPURenderer] Requesting Adapter..." << std::endl;
        
        instance.RequestAdapter(nullptr, wgpu::CallbackMode::AllowSpontaneous,
            [](wgpu::RequestAdapterStatus status, wgpu::Adapter res, const char* msg, WebGPURenderer* self) {
                if (status == wgpu::RequestAdapterStatus::Success) {
                    self->adapter = res;
                } else {
                    std::cout << "[Dawn Error] Adapter failed: " << (msg ? msg : "unknown") << std::endl;
                }
            }, this);

        // Freeze C++ execution here until the browser resolves the adapter
        while (!this->adapter) {
            emscripten_sleep(10); 
        }

        std::cout << "[WebGPURenderer] Requesting Device..." << std::endl;
        
        this->adapter.RequestDevice(nullptr, wgpu::CallbackMode::AllowSpontaneous,
            [](wgpu::RequestDeviceStatus devStatus, wgpu::Device devRes, const char* devMsg, WebGPURenderer* selfInner) {
                if (devStatus == wgpu::RequestDeviceStatus::Success) {
                    selfInner->device = devRes;
                } else {
                    std::cout << "[Dawn Error] Device failed: " << (devMsg ? devMsg : "unknown") << std::endl;
                }
            }, this);

        // Freeze C++ execution here until the browser resolves the device
        while (!this->device) {
            emscripten_sleep(10);
        }

        std::cout << "[WebGPURenderer] WebGPU Context Acquired!" << std::endl;
        this->queue = this->device.GetQueue();
        this->BuildPipeline();
        emscripten_set_mousedown_callback("#canvas", nullptr, false, on_mouse_down);
        emscripten_set_mouseup_callback("#canvas", nullptr, false, on_mouse_up);
        emscripten_set_mousemove_callback("#canvas", nullptr, false, on_mouse_move);
        #endif
        
        this->initialized = true;
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
        uboDesc.size = sizeof(glm::mat4) + sizeof(glm::vec4) + (sizeof(float) * 4);
        uboDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniformBuffer = device.CreateBuffer(&uboDesc);
        if (!uniformBuffer) { std::cout << "[Dawn Error] UBO creation failed!" << std::endl; return; }

        // --- 1. CREATE THE SAMPLER & TEXTURE VIA STB_IMAGE ---
        wgpu::SamplerDescriptor samplerDesc{};
        samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
        samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
        samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
        samplerDesc.magFilter = wgpu::FilterMode::Linear;
        samplerDesc.minFilter = wgpu::FilterMode::Linear;
        samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        cardSampler = device.CreateSampler(&samplerDesc);
        stbi_set_flip_vertically_on_load(false);

        // Load the actual image file (Force 4 channels for RGBA8Unorm)
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load("assets/HoloTestCard.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

        static uint8_t fallbackPixel[4] = { 255, 0, 255, 255 }; // Magenta fallback
        if (!pixels) {
            std::cout << "[Dawn Warning] Failed to load card texture via stb_image! Using fallback." << std::endl;
            texWidth = 1;
            texHeight = 1;
            pixels = fallbackPixel;
        } else {
            // DEBUG PRINT: Let's verify Emscripten actually packed the new 1024 file!
            std::cout << "[STB Debug] Successfully loaded texture: " << texWidth << "x" << texHeight << " | Channels: " << texChannels << std::endl;
            std::cout << "[STB Debug] First Pixel [RGBA]: " 
                      << (int)pixels[0] << ", " << (int)pixels[1] << ", " 
                      << (int)pixels[2] << ", " << (int)pixels[3] << std::endl;
        }

        wgpu::TextureDescriptor texDesc{};
        texDesc.dimension = wgpu::TextureDimension::e2D;
        texDesc.size = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 }; 
        texDesc.format = wgpu::TextureFormat::RGBA8Unorm;
        texDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        cardTexture = device.CreateTexture(&texDesc);
        cardTextureView = cardTexture.CreateView();

        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = cardTexture;
        destination.mipLevel = 0;
        destination.origin = { 0, 0, 0 };
        destination.aspect = wgpu::TextureAspect::All;

        wgpu::TexelCopyBufferLayout sourceDataLayout{};
        sourceDataLayout.offset = 0;
        sourceDataLayout.bytesPerRow = 4 * texWidth; // 4096 for 1024 width (already 256-aligned!)
        sourceDataLayout.rowsPerImage = texHeight;

        wgpu::Extent3D writeSize = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };
        
        // Pass the exact tight stb_image size here so we don't read out-of-bounds
        queue.WriteTexture(&destination, pixels, (4 * texWidth * texHeight), &sourceDataLayout, &writeSize);

        // Free the host memory if we successfully allocated it via STB
        if (pixels != fallbackPixel) {
            stbi_image_free(pixels);
        }

        // --- 2. THE NEW BIND GROUP LAYOUT (3 Entries) ---
        wgpu::BindGroupLayoutEntry bglEntries[3] = {};
        
        bglEntries[0].binding = 0;
        bglEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        bglEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        bglEntries[0].buffer.minBindingSize = sizeof(glm::mat4) + sizeof(glm::vec4) + (sizeof(float) * 4);

        bglEntries[1].binding = 1;
        bglEntries[1].visibility = wgpu::ShaderStage::Fragment;
        bglEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
        bglEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;

        bglEntries[2].binding = 2;
        bglEntries[2].visibility = wgpu::ShaderStage::Fragment;
        bglEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

        wgpu::BindGroupLayoutDescriptor bglDesc{};
        bglDesc.entryCount = 3;
        bglDesc.entries = bglEntries;
        bindGroupLayout = device.CreateBindGroupLayout(&bglDesc);
        if (!bindGroupLayout) { std::cout << "[Dawn Error] BindGroupLayout failed!" << std::endl; return; }


        // --- 3. THE NEW BIND GROUP (3 Entries) ---
        wgpu::BindGroupEntry bgEntries[3] = {};
        
        bgEntries[0].binding = 0;
        bgEntries[0].buffer = uniformBuffer;
        bgEntries[0].offset = 0;
        bgEntries[0].size = sizeof(glm::mat4) + sizeof(glm::vec4) + (sizeof(float) * 4);

        bgEntries[1].binding = 1;
        bgEntries[1].textureView = cardTextureView;

        bgEntries[2].binding = 2;
        bgEntries[2].sampler = cardSampler;

        wgpu::BindGroupDescriptor bgFinalDesc{};
        bgFinalDesc.layout = bindGroupLayout;
        bgFinalDesc.entryCount = 3;
        bgFinalDesc.entries = bgEntries;
        bindGroup = device.CreateBindGroup(&bgFinalDesc);
        if (!bindGroup) { std::cout << "[Dawn Error] BindGroup failed!" << std::endl; return; }

        // EXPLICIT mat4x4<f32> FOR WIDER DAWN COMPATIBILITY
        const char* wgsl = R"(
            struct UBO {
                mvp: mat4x4<f32>,
                eyePos: vec4f, 
                time: f32,
                ripProgress: f32,
                pad1: f32, pad2: f32,
            };
            @group(0) @binding(0) var<uniform> ubo: UBO;
            @group(0) @binding(1) var cardTexture: texture_2d<f32>;
            @group(0) @binding(2) var cardSampler: sampler;

            struct VertexInput {
                @location(0) pos: vec3f,
                @location(1) normal: vec3f,
                @location(2) uv: vec2f,
            };

            struct VertexOutput {
                @builtin(position) position: vec4f,
                @location(0) worldPos: vec3f,
                @location(1) normal: vec3f,
                @location(2) uv: vec2f,
            };

            @vertex
            fn vs_main(in: VertexInput) -> VertexOutput {
                var out: VertexOutput;
                out.position = ubo.mvp * vec4f(in.pos, 1.0);
                out.worldPos = in.pos;
                out.normal = in.normal;
                out.uv = in.uv;
                return out;
            }

            // --- NOISE MATH FOR DISSOLVE ---
            // --- DEUS EX GEOMETRIC NOISE ---
            fn hash(p: vec2f) -> f32 {
                var p2 = fract(p * vec2f(123.34, 456.21));
                p2 += dot(p2, p2 + 45.32);
                return fract(p2.x * p2.y);
            }

            fn noise(p: vec2f) -> f32 {
                let i = floor(p);
                let f = fract(p);
                // The cubic curve gives us the sharp, shattered glass edges!
                let u = f * f * (3.0 - 2.0 * f);
                return mix(
                    mix(hash(i + vec2f(0.0, 0.0)), hash(i + vec2f(1.0, 0.0)), u.x),
                    mix(hash(i + vec2f(0.0, 1.0)), hash(i + vec2f(1.0, 1.0)), u.x),
                    u.y
                );
            }

            fn fbm(p_in: vec2f) -> f32 {
                var p = p_in;
                var v = 0.0;
                var a = 0.5;
                let shift = vec2f(100.0);
                let rot = mat2x2<f32>(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
                for (var i = 0; i < 4; i++) {
                    v += a * noise(p);
                    p = rot * p * 2.0 + shift;
                    a *= 0.5;
                }
                return v;
            }

            @fragment
            fn fs_main(in: VertexOutput) -> @location(0) vec4f {
                let baseColor = textureSample(cardTexture, cardSampler, in.uv);
                if (baseColor.a < 0.05) { discard; }

                let tearNoise = fbm(in.uv * 5.0); 

                // 1. Lighting & View vectors
                let foilBase = vec3f(0.04, 0.05, 0.07); // Dark industrial card backing
                let lightDir = normalize(vec3f(1.0, 1.0, 2.0));
                let normal = normalize(in.normal);
                let NdotL = max(dot(normal, lightDir), 0.2);

                let viewDir = normalize(vec3f(0.0, 0.0, 5.0) - in.worldPos);
                let NdotV = max(dot(normal, viewDir), 0.0);
                
                // Specular gloss highlight
                let halfVector = normalize(lightDir + viewDir);
                let specular = pow(max(dot(normal, halfVector), 0.0), 48.0);

                // 2. Smooth Holographic Gradient Ramp
                let fresnel = pow(1.0 - NdotV, 2.0);
                
                // Mix between clean cyan, electric gold, and magenta based on the viewing angle + noise
                let t = fract(fresnel * 1.5 + tearNoise * 0.5);
                let color1 = vec3f(0.0, 0.8, 0.9);   // Cyber Cyan
                let color2 = vec3f(0.9, 0.7, 0.1);   // Gold Foil
                let color3 = vec3f(0.9, 0.1, 0.5);   // Hot Magenta
                
                var holoColor = mix(color1, color2, smoothstep(0.0, 0.5, t));
                holoColor = mix(holoColor, color3, smoothstep(0.5, 1.0, t));

                // 3. Combine base, smooth holographic gradient, and glossy specular reflection
                let litFoil = (foilBase * NdotL) + (holoColor * fresnel * 0.6) + (vec3f(1.0) * specular * 1.5);

                let edgeWidth = 0.06;
                let threshold = ubo.ripProgress;

                // The Dissolve Logic (Now starts completely sealed because interactiveRip defaults to -0.1)
                if (tearNoise < threshold) {
                    return vec4f(baseColor.rgb, baseColor.a);
                } else if (tearNoise < threshold + edgeWidth) {
                    let intensity = 1.0 - ((tearNoise - threshold) / edgeWidth);
                    let edgeGlow = mix(vec3f(0.8, 0.1, 0.0), vec3f(1.0, 0.9, 0.2), intensity);
                    return vec4f(edgeGlow, 1.0);
                } else {
                    return vec4f(litFoil, 1.0);
                }
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

        wgpu::VertexAttribute attributes[3];
        
        // Location 0: Position
        attributes[0].format = wgpu::VertexFormat::Float32x3; 
        attributes[0].offset = offsetof(Vertex, pos);
        attributes[0].shaderLocation = 0;

        // Location 1: Normal
        attributes[1].format = wgpu::VertexFormat::Float32x3; 
        attributes[1].offset = offsetof(Vertex, normal); 
        attributes[1].shaderLocation = 1;

        // Location 2: UV (texCoord)
        attributes[2].format = wgpu::VertexFormat::Float32x2; 
        attributes[2].offset = offsetof(Vertex, texCoord); 
        attributes[2].shaderLocation = 2;

        wgpu::VertexBufferLayout vertexLayout{};
        vertexLayout.arrayStride = sizeof(Vertex); 
        vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
        vertexLayout.attributeCount = 3;
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
        pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
        pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
        pipelineDesc.depthStencil = &depthStencil;

        pipeline = device.CreateRenderPipeline(&pipelineDesc);
        if (!pipeline) { std::cout << "[Dawn Error] Pipeline failed!" << std::endl; return; }

        std::cout << "[WebGPURenderer] Hardware Bridge Fully Configured!" << std::endl;

        // ==========================================
        // PROCEDURAL BACKGROUND PIPELINE
        // ==========================================
        const char* bgWgsl = R"(
            struct UBO {
                mvp: mat4x4<f32>,
                eyePos: vec4f, 
                time: f32,
                ripProgress: f32, // Replace pad3 with ripProgress here!
                pad1: f32, pad2: f32,
            };
            @group(0) @binding(0) var<uniform> ubo: UBO;

            struct BgOutput {
                @builtin(position) position: vec4f,
                @location(0) uv: vec2f,
            };

            // Generate a full-screen triangle without vertex buffers
            @vertex
            fn vs_bg(@builtin(vertex_index) v_idx: u32) -> BgOutput {
                var pos = array<vec2f, 3>(
                    vec2f(-1.0, -1.0), 
                    vec2f(3.0, -1.0), 
                    vec2f(-1.0, 3.0)
                );
                var out: BgOutput;
                out.position = vec4f(pos[v_idx], 0.99999, 1.0); // Push to far clip plane
                out.uv = pos[v_idx] * 0.5 + 0.5;
                return out;
            }

            // --- NOISE MATH ---
            fn hash(p: vec2f) -> f32 {
                var p3 = fract(vec3f(p.x, p.y, p.x) * 0.1313);
                p3 += dot(p3, p3.yzx + 3.333);
                return fract((p3.x + p3.y) * p3.z);
            }

            fn noise(p: vec2f) -> f32 {
                let i = floor(p);
                let f = fract(p);
                
                // Quintic interpolation curve for buttery smooth blending
                let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
                
                let a = hash(i + vec2f(0.0, 0.0));
                let b = hash(i + vec2f(1.0, 0.0));
                let c = hash(i + vec2f(0.0, 1.0));
                let d = hash(i + vec2f(1.0, 1.0));

                return mix(
                    mix(a, b, u.x),
                    mix(c, d, u.x),
                    u.y
                );
            }

            fn fbm(p_in: vec2f) -> f32 {
                var p = p_in;
                var v = 0.0;
                var a = 0.5;
                let shift = vec2f(100.0);
                let rot = mat2x2<f32>(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
                for (var i = 0; i < 4; i++) {
                    v += a * noise(p);
                    p = rot * p * 2.0 + shift;
                    a *= 0.5;
                }
                return v;
            }

            // --- FRAGMENT SHADER ---
            @fragment
            fn fs_bg(in: BgOutput) -> @location(0) vec4f {
                var uv = in.uv * 3.0;
                uv.y -= ubo.time * 0.15; // Scroll up
                uv.x += sin(ubo.time * 0.1 + uv.y * 0.5) * 0.2; // Warping

                let n = fbm(uv);
                let darkBg = vec3f(0.02, 0.03, 0.05);
                let ashColor = vec3f(0.15, 0.08, 0.06);
                let emberColor = vec3f(1.0, 0.4, 0.1);

                var finalColor = mix(darkBg, ashColor, smoothstep(0.1, 0.7, n));
                finalColor += emberColor * (pow(n, 4.0) * 2.0); // Sparks

                // Vignette
                let centerDist = distance(in.uv, vec2f(0.5));
                finalColor *= smoothstep(0.8, 0.2, centerDist);

                return vec4f(finalColor, 1.0);
            }
        )";

        wgpu::ShaderSourceWGSL bgWgslDesc{};
        bgWgslDesc.code = bgWgsl;
        bgWgslDesc.sType = wgpu::SType::ShaderSourceWGSL;
        wgpu::ShaderModuleDescriptor bgShaderDesc{};
        bgShaderDesc.nextInChain = &bgWgslDesc;
        wgpu::ShaderModule bgShader = device.CreateShaderModule(&bgShaderDesc);

        wgpu::FragmentState bgFragmentState{};
        bgFragmentState.module = bgShader;
        bgFragmentState.entryPoint = "fs_bg";
        bgFragmentState.targetCount = 1;
        bgFragmentState.targets = &colorTarget;

        wgpu::DepthStencilState bgDepthStencil{};
        bgDepthStencil.format = wgpu::TextureFormat::Depth24Plus;
        bgDepthStencil.depthWriteEnabled = false; // Background shouldn't block geometry
        bgDepthStencil.depthCompare = wgpu::CompareFunction::LessEqual; // Crucial for 0.99999 depth

        wgpu::RenderPipelineDescriptor bgPipelineDesc{};
        bgPipelineDesc.layout = layout; 
        bgPipelineDesc.vertex.module = bgShader;
        bgPipelineDesc.vertex.entryPoint = "vs_bg";
        bgPipelineDesc.vertex.bufferCount = 0; 
        bgPipelineDesc.fragment = &bgFragmentState;
        bgPipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        bgPipelineDesc.depthStencil = &bgDepthStencil;

        bgPipeline = device.CreateRenderPipeline(&bgPipelineDesc);

    }
    
    int WebGPURenderer::acquireMesh(const std::string& path, const std::string& name, 
                                    const std::vector<Vertex>& vertices, 
                                    const std::vector<uint32_t>& indices) {
        // 1. Create and upload the Vertex Buffer
        wgpu::BufferDescriptor vertDesc{};
        vertDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        vertDesc.size = vertices.size() * sizeof(Vertex);
        vertDesc.mappedAtCreation = false;
                                    
        wgpu::Buffer vBuffer = device.CreateBuffer(&vertDesc);
        queue.WriteBuffer(vBuffer, 0, vertices.data(), vertDesc.size);
                                    
        // 2. Create and upload the Index Buffer
        wgpu::BufferDescriptor indDesc{};
        indDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        indDesc.size = indices.size() * sizeof(uint32_t);
        indDesc.mappedAtCreation = false;
                                    
        wgpu::Buffer iBuffer = device.CreateBuffer(&indDesc);
        queue.WriteBuffer(iBuffer, 0, indices.data(), indDesc.size);
                                    
        // 3. Package into WebGPUMesh
        WebGPUMesh newMesh;
        newMesh.name = name;
        newMesh.vertexBuffer = vBuffer;
        newMesh.indexBuffer = iBuffer;
        newMesh.indexCount = static_cast<uint32_t>(indices.size());
                                    
        // 4. Store and return the ID
        meshes.push_back(newMesh);
        return static_cast<int>(meshes.size() - 1);
    }

    int WebGPURenderer::acquireTexture(const std::string& texturePath) {
        // We will implement this properly later when we handle the VFS texture extraction. 
        // For now, return 0 (the default fallback texture ID).
        return 0; 
    }

    void WebGPURenderer::render(Scene* scene, SceneManager* sceneManager, EngineState& state) {
        if (!device || !surface || !queue || !pipeline) return;

        int width = 800, height = 600;
        if (display) display->get_window_size(&width, &height);
        if (width == 0 || height == 0) return;

        // --- DYNAMIC RESIZE HANDLING ---
        // Track current dimensions to catch window resizes or inspector toggles
        static int currentWidth = 0;
        static int currentHeight = 0;

        if (width != currentWidth || height != currentHeight) {
            currentWidth = width;
            currentHeight = height;

            // 1. Reconfigure surface for the new window size
            wgpu::SurfaceConfiguration config {};
            config.device = device;
            config.format = wgpu::TextureFormat::RGBA8Unorm; 
            config.usage = wgpu::TextureUsage::RenderAttachment;
            config.width = static_cast<uint32_t>(width);
            config.height = static_cast<uint32_t>(height);
            config.presentMode = wgpu::PresentMode::Fifo;
            config.alphaMode = wgpu::CompositeAlphaMode::Auto;
            surface.Configure(&config);

            // 2. Recreate depth texture to match the new dimensions
            wgpu::TextureDescriptor depthDesc{};
            depthDesc.usage = wgpu::TextureUsage::RenderAttachment;
            depthDesc.dimension = wgpu::TextureDimension::e2D;
            depthDesc.size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
            depthDesc.format = wgpu::TextureFormat::Depth24Plus;
            depthDesc.mipLevelCount = 1; 
            depthDesc.sampleCount = 1;   
            
            depthTexture = device.CreateTexture(&depthDesc);
            depthView = depthTexture.CreateView();
        }

        static bool initialModelLoaded = false;
        if (!initialModelLoaded) {
            Crescendo::AssetLoader::loadModel(this, "assets/testcard.glb", scene);
            initialModelLoaded = true;
        }

        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        

        // 1. Continuous time for background scrolling and shader noise
        static float totalTime = 0.0f;
        totalTime += 0.016f; 

        // 2. Clamped animation time for the card reveal (3 seconds long)
        static float animTime = 0.0f;
        if (animTime < 3.0f) {
            animTime += 0.016f;
        }
        
        // Progress goes from 0.0 to 1.0 and permanently locks
        float progress = animTime / 3.0f; 
        if (progress > 1.0f) progress = 1.0f;

        // 3. Smooth easing (starts fast, slows down gracefully to a stop)
        float ease = progress * progress * (3.0f - 2.0f * progress);

        glm::vec3 cameraPos = glm::vec3(0.0f, 2.0f, 5.0f);
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        
        // 4. One full spin (6.28318 rads), offset by -90 degrees (-1.5708 rads) to face the front
        float currentAngle = (ease * 6.28318f) - 1.5708f;
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), currentAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        
        // Smoothly interpolate the tilt so it has that premium, weighty physical feel
        currentTiltX += (tiltTargetX - currentTiltX) * 0.1f;
        currentTiltY += (tiltTargetY - currentTiltY) * 0.1f;

        // Apply a stronger rotation based on the mouse (Pitch and Yaw)
        glm::mat4 tilt = glm::rotate(glm::mat4(1.0f), currentTiltY * 0.6f, glm::vec3(1.0f, 0.0f, 0.0f));
        tilt = glm::rotate(tilt, currentTiltX * 0.6f, glm::vec3(0.0f, 1.0f, 0.0f));

        // Combine the cinematic spin with the interactive mouse tilt
        model = tilt * model; 
        
        glm::mat4 mvp = projection * view * model;
        // Pack the data exactly how WGSL expects it
        struct UBOData {
            glm::mat4 mvp;
            glm::vec4 eyePos;
            float time;
            float ripProgress;
            float pad1, pad2;
        } uboData;

        uboData.mvp = mvp;
        uboData.eyePos = glm::vec4(cameraPos, 1.0f);
        
        // Background keeps scrolling forever
        uboData.time = totalTime; 
        
        // player ripping progress is controlled by mouse drag, clamped to 0.0 - 1.2 for shader math
        uboData.ripProgress = interactiveRip;

        queue.WriteBuffer(uniformBuffer, 0, &uboData, sizeof(UBOData));

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
            // 1. Draw the Procedural Background FIRST
            pass.SetPipeline(bgPipeline);
            if (bindGroup) pass.SetBindGroup(0, bindGroup); 
            pass.Draw(3); // Draw our 3-vertex full-screen triangle

            // 2. Draw the 3D Cards SECOND
            pass.SetPipeline(pipeline);
            for (const auto& mesh : meshes) {
                if (mesh.vertexBuffer && mesh.indexBuffer) {
                    pass.SetVertexBuffer(0, mesh.vertexBuffer);
                    pass.SetIndexBuffer(mesh.indexBuffer, wgpu::IndexFormat::Uint32);
                    
                    // Quick and dirty loop to draw 3 cards side-by-side
                    for (int i = -1; i <= 1; i++) {
                        // Draw a single, perfectly centered card
                        glm::mat4 translation = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));
                        glm::mat4 multiModel = translation * model; 
                        
                        uboData.mvp = projection * view * multiModel;
                        queue.WriteBuffer(uniformBuffer, 0, &uboData, sizeof(UBOData));
                        
                        pass.DrawIndexed(mesh.indexCount);
                    }
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