#pragma once 

#include <string>
#include <glm/glm.hpp>
#include "scene/Scene.hpp"

namespace tinygltf { class Model; class Node; }

namespace Crescendo {
    class RenderingServer;

    class AssetLoader {
    public:
        static void loadModel(RenderingServer* renderer, const std::string& filePath, Scene* scene);

    private:
        static void loadGLTF(RenderingServer* renderer, const std::string& filePath, Scene* scene);
        static void processGLTFNode(RenderingServer* renderer, tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene, glm::mat4 parentMatrix);
    };
}