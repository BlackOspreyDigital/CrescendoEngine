#pragma once 

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "scene/Scene.hpp"
#include "servers/rendering/Vertex.hpp" 

namespace tinygltf { class Model; class Node; }

namespace Crescendo {
    class IRenderer; // <-- 1. Forward declare the base interface

    // Alias to keep the function signature clean
    using RawMeshMap = std::unordered_map<std::string, std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

    class AssetLoader {
    public:
        // <-- 2. Swap RenderingServer* for IRenderer*
        static void loadModel(IRenderer* renderer, const std::string& filePath, Scene* scene);

    private:
        // <-- 3. Swap RenderingServer* for IRenderer*
        static void loadGLTF(IRenderer* renderer, const std::string& filePath, Scene* scene);
        
        // <-- 4. Swap RenderingServer* for IRenderer*
        static void processGLTFNode(IRenderer* renderer, tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, const std::string& filePath, Scene* scene, glm::mat4 parentMatrix, RawMeshMap& rawMeshes);
    };
}