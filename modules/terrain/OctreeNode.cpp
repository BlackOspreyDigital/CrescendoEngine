#include "OctreeNode.hpp"
#include "TerrainManager.hpp"

#include "servers/rendering/RenderingServer.hpp"
#include <algorithm> 


namespace Crescendo::Terrain { 
    
    bool OctreeNode::CheckForFinishedMeshes(Crescendo::RenderingServer* renderer, Crescendo::Scene* scene, const glm::vec3& chunkOrigin) {
        bool integratedAny = false;

        if (isGenerating && pendingBakeResult.valid()) {
            if (pendingBakeResult.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                Crescendo::ChunkBakeResult result = pendingBakeResult.get();
                
                if (result.hasMesh) {
                    renderer->meshes.push_back(std::move(result.generatedMesh));
                    meshID = static_cast<int>(renderer->meshes.size() - 1);
                } else {
                    meshID = -2; 
                }
                
                isGenerating = false;
                integratedAny = true;
            }
        }

        if (!isLeaf) {
            for (auto& child : children) {
                // Let every single child process! Do not break the loop early.
                if (child && child->CheckForFinishedMeshes(renderer, scene, child->center - glm::vec3(child->size / 2.0f))) {
                    integratedAny = true;
                }
            }
        }
        return integratedAny;
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
    // 1. Calculate approximate distance to the SURFACE of the chunk
    float distanceToCenter = glm::distance(localCameraPos, center);
    float distanceToSurface = std::max(1.0f, distanceToCenter - (size * 0.866f)); 
    
    isVisible = true;

    // --- HORIZON CULLING COMPLETELY REMOVED ---
    // Frustum math in PlanetManager handles culling. This prevents the 
    // engine from murdering chunks directly beneath the player's feet.

    if (!isVisible && lod < 4) {
        if (!isLeaf && !IsGeneratingTree()) Merge(manager); 
        
        if (isLeaf && meshID == -1 && !isGenerating) manager->EnqueueChunk(this);
        return; 
    }

    // 2. Use the surface distance to dictate the split
    bool shouldSplit = (size / distanceToSurface) > splitThreshold;

    if (shouldSplit && lod > 0) {
        if (isLeaf) Subdivide();
        for (auto& child : children) {
            if (child) child->Update(localCameraPos, splitThreshold, manager);
        }

    } else {
        if (!isLeaf && !IsGeneratingTree()) Merge(manager); 
        
        if (isLeaf && meshID == -1 && !isGenerating) manager->EnqueueChunk(this);
    }
} 
} 