#pragma once
#include "scene/Scene.hpp"
#include <string>

namespace Crescendo {

    class Scene;
    class RenderingServer;

    class SceneSerializer {
    public:
        SceneSerializer(Scene* scene, RenderingServer* renderer);
    
        bool Serialize(const std::string& filepath);
        bool Deserialize(const std::string& filepath);

    private:
        Scene* m_Scene;
        RenderingServer* m_Renderer;
    };
}