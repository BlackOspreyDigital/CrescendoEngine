#include "WebRenderer.hpp"
#include <iostream>

// Include the Emscripten and WebGL headers when compiling for Web
#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <GLES3/gl3.h> // WebGL 2.0 uses OpenGL ES 3.0 syntax
#endif

namespace Crescendo {

    bool WebRenderer::initialize(DisplayServer* display) {
        std::cout << "[WebRenderer] Initializing WebGL 2.0 Renderer..." << std::endl;
        
#ifdef __EMSCRIPTEN__
        // SDL automatically handles the context creation for us!
        // We just need to set our default clear color (Let's use a nice Osprey Blue)
        glClearColor(0.2f, 0.6f, 0.8f, 1.0f); 
#endif
        return true;
    }

    void WebRenderer::shutdown() {
        std::cout << "[WebRenderer] Shutting down WebGL context." << std::endl;
    }

    void WebRenderer::render(Scene* scene, SceneManager* sceneManager, EngineState& engineState) {
#ifdef __EMSCRIPTEN__
        // For phase 1, just clear the screen so we know it works!
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
    }

    ChunkBakeResult WebRenderer::buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) {
        // WebGL doesn't have Compute Shaders natively yet! 
        // We will tackle this beast later (Likely using Web Workers or Transform Feedback).
        ChunkBakeResult emptyResult;
        return emptyResult;
    }
}