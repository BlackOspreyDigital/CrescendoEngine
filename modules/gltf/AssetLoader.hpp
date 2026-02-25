#pragma once 

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "scene/Scene.hpp"
#include "servers/rendering/Vertex.hpp" 

namespace tinygltf { class Model; class Node; }

namespace Crescendo {
    class RenderingServer;

    // Alias to keep the function signature clean
    using RawMeshMap = std::unordered_map<std::string, std::pair<std::vector<Vertex>, std::vector<uint32_t>>>;

    class AssetLoader {
    public:
        static void loadModel(RenderingServer* renderer, const std::string& filePath, Scene* scene);

    private:
        static void loadGLTF(RenderingServer* renderer, const std::string& filePath, Scene* scene);
        
        // <-- UPDATE THIS SIGNATURE: Add the 'rawMeshes' map at the end
        static void processGLTFNode(RenderingServer* renderer, tinygltf::Model& model, tinygltf::Node& node, CBaseEntity* parent, const std::string& baseDir, Scene* scene, glm::mat4 parentMatrix, RawMeshMap& rawMeshes);
    };
}