#include "OctreeNode.hpp"
#include "TerrainManager.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/physics/PhysicsServer.hpp"
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
                    // --- SAVE THE PHYSICS ID SO WE CAN DELETE IT LATER ---
                    physicsBodyID = result.physicsBodyID; 
                } else {
                    meshID = -2; 
                }
                
                isGenerating = false;
                integratedAny = true;
            }
        }

        if (!isLeaf) {
            for (auto& child : children) {
                if (child && child->CheckForFinishedMeshes(renderer, scene, child->center - glm::vec3(child->size / 2.0f))) {
                    integratedAny = true;
                }
            }
        }
        return integratedAny;
    }

    void OctreeNode::Merge(TerrainManager* manager, Crescendo::PhysicsServer* physicsServer) {
        if (isLeaf) return;
        
        for (int i = 0; i < 8; i++) {
            if (children[i]) {
                children[i]->Merge(manager, physicsServer);
                
                auto& queue = manager->chunkQueue;
                queue.erase(std::remove(queue.begin(), queue.end(), children[i].get()), queue.end());
                
                // --- DESTROY THE JOLT PHYSICS BODY TO PREVENT RAM LEAKS ---
                if (children[i]->physicsBodyID != 0 && physicsServer) {
                    physicsServer->DestroyCollider(children[i]->physicsBodyID);
                    children[i]->physicsBodyID = 0;
                }
                
                children[i].reset();
            }
        }
        isLeaf = true;
    }

    void OctreeNode::Update(const glm::vec3& localCameraPos, float splitThreshold, TerrainManager* manager, Crescendo::PhysicsServer* physicsServer) {
        float distanceToCenter = glm::distance(localCameraPos, center);
        float distanceToSurface = std::max(1.0f, distanceToCenter - (size * 0.866f)); 
        
        isVisible = true;

        if (!isVisible && lod < 4) {
            if (!isLeaf && !IsGeneratingTree()) Merge(manager, physicsServer); 
            if (isLeaf && meshID == -1 && !isGenerating) manager->EnqueueChunk(this);
            return; 
        }

        bool shouldSplit = (size / distanceToSurface) > splitThreshold;

        if (shouldSplit && lod > 0) {
            if (isLeaf) Subdivide();
            for (auto& child : children) {
                if (child) child->Update(localCameraPos, splitThreshold, manager, physicsServer);
            }
        } else {
            if (!isLeaf && !IsGeneratingTree()) Merge(manager, physicsServer); 
            if (isLeaf && meshID == -1 && !isGenerating) manager->EnqueueChunk(this);
        }
    } 
}