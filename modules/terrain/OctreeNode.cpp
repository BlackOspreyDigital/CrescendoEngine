#include "OctreeNode.hpp"
#include "TerrainManager.hpp"

#include "servers/rendering/RenderingServer.hpp"
#include <algorithm> 


namespace Crescendo::Terrain { 
    
    bool OctreeNode::CheckForFinishedMeshes(Crescendo::RenderingServer* renderer, Crescendo::Scene* scene, const glm::vec3& chunkOrigin) {
        if (isGenerating && pendingBakeResult.valid()) {
            if (pendingBakeResult.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                
                Crescendo::ChunkBakeResult result = pendingBakeResult.get();
                
                if (result.hasMesh) {
                    // Hand the fully built GPU mesh to the renderer (Instant!)
                    renderer->meshes.push_back(std::move(result.generatedMesh));
                    meshID = static_cast<int>(renderer->meshes.size() - 1);
                    
                    // Note: We don't call CreateTerrainCollider here anymore! 
                    // The background thread already did it, and the collision is active.
                } else {
                    // --- THE FIX ---
                    meshID = -2; // -2 explicitly means "Empty air, but finished generating!"
                }
                
                isGenerating = false;
                return true;
            }
        }

        if (!isLeaf) {
            for (auto& child : children) {
                if (child && child->CheckForFinishedMeshes(renderer, scene, child->center - glm::vec3(child->size / 2.0f))) {
                    return true;
                }
            }
        }
        return false;
    }

    void OctreeNode::Merge(TerrainManager* manager) {
        if (isLeaf) return;
        
        for (int i = 0; i < 8; i++) {
            if (children[i]) {
                children[i]->Merge(manager);
                
                // Remove from the bake queue so we don't bake a deleted chunk!
                auto& queue = manager->chunkQueue;
                queue.erase(std::remove(queue.begin(), queue.end(), children[i].get()), queue.end());
                
                // THIS FREES THE MEMORY!
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