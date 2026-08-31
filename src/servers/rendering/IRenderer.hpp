#pragma once
#include "RenderTypes.hpp"
#include "core/EngineState.hpp"
#include "Vertex.hpp"
#include <string>

namespace Crescendo {

    class DisplayServer;
    class Scene;
    class SceneManager;
    class Camera;
    
    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        // Signatures must exactly match your Vulkan implementation!
        virtual bool initialize(DisplayServer* display) = 0;
        virtual bool isInitialized() const = 0;
        virtual void shutdown() = 0;
        virtual void render(Scene* scene, SceneManager* sceneManager, EngineState& engineState) = 0;
        virtual ChunkBakeResult buildChunkMesh(const TerrainComputePush& pushData, bool needsCollision) = 0;
        virtual Camera* GetMainCamera() = 0; // Unified camera getter 
        virtual void SetTagEditorConfig(bool isTagEditor, const std::string& projectRoot) {}
        virtual int acquireMesh(const std::string& path, const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) = 0;
        virtual int acquireTexture(const std::string& texturePath) = 0;
    };
}