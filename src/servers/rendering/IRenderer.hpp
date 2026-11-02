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
    class SymbolServer;

    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        // Signatures must exactly match your Vulkan implementation!
        virtual bool initialize(DisplayServer* display) = 0;
        virtual bool isInitialized() const = 0;
        virtual void shutdown() = 0;
        virtual void render(Scene* scene, SceneManager* sceneManager, EngineState& engineState) = 0;
        virtual Camera* GetMainCamera() = 0; // Unified camera getter 
        virtual SymbolServer* GetSymbolServer() { return nullptr; }
        virtual void SetTagEditorConfig(bool isTagEditor, const std::string& projectRoot) {}
        virtual int acquireMesh(const std::string& path, const std::string& name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) = 0;
        virtual int acquireTexture(const std::string& texturePath) = 0;
        virtual void loadSkybox(const std::string& path, Scene* scene) {}
    };
}
