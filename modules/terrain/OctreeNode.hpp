#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <array>
#include <future>
#include "VoxelGenerator.hpp"

// Forward declare what we need as pointers! No #includes for these here!
namespace Crescendo { class RenderingServer; }
namespace Crescendo::Terrain { class TerrainManager; }

namespace Crescendo::Terrain {

    class OctreeNode {
    public:
        glm::vec3 center;
        float size;
        int lod; 
        
        bool isLeaf = true;
        int meshID = -1; 

        std::array<std::unique_ptr<OctreeNode>, 8> children;

        OctreeNode(glm::vec3 c, float s, int l) : center(c), size(s), lod(l) {}

        std::future<ChunkData> pendingData;
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

        // ONLY DECLARE THE FUNCTIONS HERE. No {} brackets!
        bool CheckForFinishedMeshes(Crescendo::RenderingServer* renderer);
        void Update(const glm::vec3& localCameraPos, float splitThreshold, TerrainManager* manager);
        
        // Subdivide and Merge don't use external classes, so they can stay inline.
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

        void Merge() {
            for (auto& child : children) child.reset(); 
            isLeaf = true;
        }
    };
}