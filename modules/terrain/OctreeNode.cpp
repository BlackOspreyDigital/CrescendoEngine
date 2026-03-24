#include "OctreeNode.hpp"
#include "TerrainManager.hpp"
#include "modules/terrain/VoxelGenerator.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include <algorithm> // Required for std::remove

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

    // --- THE NEW, SAFE MERGE FUNCTION ---
    void OctreeNode::Merge(TerrainManager* manager) {
        if (isLeaf) return;
        
        for (int i = 0; i < 8; ++i) {
            if (children[i]) {
                children[i]->Merge(manager); 
                
                // Yank this child out of the bake queue before it is destroyed
                auto& queue = manager->chunkQueue;
                queue.erase(std::remove(queue.begin(), queue.end(), children[i].get()), queue.end());
                
                children[i].reset();
            }
        }
        isLeaf = true;
    }

    void OctreeNode::Update(const glm::vec3& localCameraPos, float splitThreshold, TerrainManager* manager) {
        float distance = glm::distance(localCameraPos, center);
        
        isVisible = true;
        
        if (glm::length(center) > 1.0f) {
            glm::vec3 camDir = glm::normalize(localCameraPos);
            glm::vec3 chunkDir = glm::normalize(center);
            if (glm::dot(camDir, chunkDir) < -0.15f) isVisible = false;
        }

        if (!isVisible && lod < 4) {
            if (!isLeaf && !IsGeneratingTree()) Merge(manager); 
            
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
            if (!isLeaf && !IsGeneratingTree()) Merge(manager); 
            
            if (isLeaf && meshID == -1 && !isGenerating) {
                manager->EnqueueChunk(this);
            }
        }
    }
}