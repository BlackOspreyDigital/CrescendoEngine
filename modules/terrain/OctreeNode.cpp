#include "OctreeNode.hpp"
#include "TerrainManager.hpp"
#include "modules/terrain/VoxelGenerator.hpp"
#include "servers/rendering/RenderingServer.hpp"

namespace Crescendo::Terrain {
    
    bool OctreeNode::CheckForFinishedMeshes(Crescendo::RenderingServer* renderer) {
        if (isGenerating && pendingData.valid()) {
            if (pendingData.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                ChunkData data = pendingData.get();
                if (!data.vertices.empty()) {
                    meshID = renderer->acquireMesh("PROCEDURAL", "Chunk", data.vertices, data.indices);
                }
                isGenerating = false;
                return true;
            }
        }

        if (!isLeaf) {
            for (auto& child : children) {
                if (child && child->CheckForFinishedMeshes(renderer)) {
                    return true;
                }
            }
        }
        return false;
    }

    void OctreeNode::Update(const glm::vec3& localCameraPos, float splitThreshold, TerrainManager* manager) {
        float distance = glm::distance(localCameraPos, center);
        
        // 1. Use the permanent class variable instead of a local bool
        isVisible = true;
        
        if (glm::length(center) > 1.0f) {
            glm::vec3 camDir = glm::normalize(localCameraPos);
            glm::vec3 chunkDir = glm::normalize(center);
            if (glm::dot(camDir, chunkDir) < -0.15f) isVisible = false;
        }

        if (!isVisible && lod < 4) {
            // ONLY merge if no background threads are active!
            if (!isLeaf && !IsGeneratingTree()) Merge();
            
            if (isLeaf && meshID == -1 && !isGenerating) manager->EnqueueChunk(this);
            return; 
        }

        bool shouldSplit = (size / distance) > splitThreshold;

        if (shouldSplit && lod > 0) {
            if (isLeaf) Subdivide();
            for (auto& child : children) {
                if (child) child->Update(localCameraPos, splitThreshold, manager);
            }
        } else {
            // ONLY merge if no background threads are active!
            if (!isLeaf && !IsGeneratingTree()) Merge();
            
            if (isLeaf && meshID == -1 && !isGenerating) {
                manager->EnqueueChunk(this);
            }
        }
    }
}