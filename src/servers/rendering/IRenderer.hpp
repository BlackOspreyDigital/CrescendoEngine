#pragma once
#include "RenderTypes.hpp"
#include <memory>

namespace Crescendo {
    // Forward declare to keep compile times fast
    class DisplayServer;
    class Scene;
    class SceneManager;
    struct EngineState;

    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        // Signatures must exactly match your Vulkan implementation!
        virtual bool initialize(DisplayServer* display) = 0;
        virtual void shutdown() = 0;
        virtual void render(Scene* scene, SceneManager* sceneManager, EngineState& engineState) = 0;
        virtual ChunkBakeResult buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) = 0;
    };
}