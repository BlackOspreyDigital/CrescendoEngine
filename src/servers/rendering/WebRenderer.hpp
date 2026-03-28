#pragma once
#include "servers/rendering/IRenderer.hpp"

namespace Crescendo {
    class WebRenderer : public IRenderer {
    public:
        WebRenderer() = default;
        ~WebRenderer() override = default;

        bool initialize(DisplayServer* display) override;
        void shutdown() override;
        void render(Scene* scene, SceneManager* sceneManager, EngineState& engineState) override;
        ChunkBakeResult buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) override;
    };
}