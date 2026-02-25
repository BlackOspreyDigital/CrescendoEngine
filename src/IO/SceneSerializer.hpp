#pragma once
#include "scene/Scene.hpp"
#include <string>

namespace Crescendo {

    class SceneSerializer {
    public:
        SceneSerializer(Scene* scene);
    

        // Serialize the current scene out of a file
        bool Serialize(const std::string& filepath);

        // Clear the current scene and load from a file 
        bool Deserialize(const std::string& filepath);

    private:
        Scene* m_Scene;
    };
}