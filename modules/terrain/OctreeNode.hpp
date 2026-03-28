#pragma once
#include <glm/glm.hpp>

#include <memory>
#include <array>
#include <future>

#include "servers/rendering/RenderingServer.hpp"

namespace Crescendo { class Scene; }

namespace Crescendo::Terrain {

    class TerrainManager;

    class OctreeNode {
    public:
        glm::vec3 center;
        float size;
        int lod; 
        
        bool isLeaf = true;
        int meshID = -1; 

        std::array<std::unique_ptr<OctreeNode>, 8> children;

        OctreeNode(glm::vec3 c, float s, int l) : center(c), size(s), lod(l) {}

        std::future<Crescendo::ChunkBakeResult> pendingBakeResult; 
        bool isGenerating = false;
        bool isVisible = true;

        bool IsGeneratingTree()const {
            if (isGenerating) return true;
            if (!isLeaf) {
                for (const auto& child : children) {
                    if (child && child->IsGeneratingTree()) return true;
                }
            }
            return false;
        }

        bool CheckForFinishedMeshes(Crescendo::RenderingServer* renderer, Crescendo::Scene* scene, const glm::vec3& chunkOrigin);
        void Update(const glm::vec3& localCameraPos, float splitThreshold, TerrainManager* manager);
        void Merge(TerrainManager* manager);
                
        void Subdivide() {
            isLeaf = false;
            float newSize = size / 2.0f;
            float q = size / 4.0f; 

            for (int i = 0; i < 8; i++) {
                glm::vec3 offset = {
                    (i & 1) ? q : -q,
                    (i & 2) ? q : -q,
                    (i & 4) ? q : -q
                };
                children[i] = std::make_unique<OctreeNode>(center + offset, newSize, lod - 1);
            }
        }
    };
}