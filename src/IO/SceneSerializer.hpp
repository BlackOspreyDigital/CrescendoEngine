#pragma once
#include "scene/Scene.hpp"
#include <string>

namespace Crescendo {

    class Scene;
    class IRenderer; // Changed from RenderingServer

    class SceneSerializer {
    public:
        SceneSerializer(Scene* scene, IRenderer* renderer); // Updated
    
        bool Serialize(const std::string& filepath);
        bool Deserialize(const std::string& filepath);

    private:
        Scene* m_Scene;
        IRenderer* m_Renderer; // Updated
    };
}