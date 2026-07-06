#include "modules/voxel/VoxelTerrainModule.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include "servers/physics/PhysicsServer.hpp" 
#include "servers/rendering/RenderTypes.hpp" 
#include "scene/Scene.hpp"
#include "scene/BaseEntity.hpp" 
#include "scene/Component.hpp" 
#include "servers/camera/Camera.hpp"
#include <algorithm>

namespace Crescendo {

    bool VoxelTerrainModule::Initialize(VkDevice device, VmaAllocator allocator) {
        m_device = device;
        m_allocator = allocator;
        return CreateComputePipelines();
    }

    void VoxelTerrainModule::Shutdown() {
        // Clean up compute pipelines and VMA buffers here
    }

    void VoxelTerrainModule::Update(Scene* scene, IRenderer* renderer, const Camera& camera) {
        if (!scene || !renderer) return;

        glm::vec3 camPos = glm::vec3(camera.Position);
        auto* vkRenderer = static_cast<RenderingServer*>(renderer);

        for (auto* ent : scene->entities) {
            if (!ent || !ent->HasComponent<ProceduralPlanetComponent>()) continue;
            
            auto planet = ent->GetComponent<ProceduralPlanetComponent>();
            if (!planet->rootNode) continue;

            planet->rootNode->Update(camPos - glm::vec3(ent->origin), planet->lodSplitThreshold, planet->chunkManager.get(), scene->physics);

            auto& queue = planet->chunkManager->chunkQueue;
            std::sort(queue.begin(), queue.end(), [&](Crescendo::Terrain::OctreeNode* a, Crescendo::Terrain::OctreeNode* b) {
                float distA = glm::length((camPos - glm::vec3(ent->origin)) - a->center);
                float distB = glm::length((camPos - glm::vec3(ent->origin)) - b->center);
                return distA > distB; 
            });

            int chunksLaunched = 0;
            while (!queue.empty() && chunksLaunched < 1) { 
                auto* node = queue.back();
                queue.pop_back();

                node->isGenerating = true; 

                TerrainComputePush pushData{}; 
                pushData.chunkOrigin = node->center - glm::vec3(node->size / 2.0f);
                pushData.chunkSize = node->size;
                pushData.planetCenter = glm::vec3(0.0f);
                pushData.planetRadius = planet->settings.radius;
                pushData.amplitude = planet->settings.amplitude;
                pushData.frequency = planet->settings.frequency;
                pushData.octaves = planet->settings.octaves;
                pushData.resolution = 32;
                pushData.lod = node->lod;

                bool needsCollision = (node->lod <= 1);
                auto* physicsServer = scene->physics;
                glm::vec3 planetOrigin = ent->origin; 

                node->pendingBakeResult = std::async(std::launch::async, [vkRenderer, pushData, needsCollision, physicsServer, planetOrigin]() -> Crescendo::ChunkBakeResult {
                    
                    ChunkBakeResult result = vkRenderer->buildChunkMesh(pushData, needsCollision);
                    
                    if (needsCollision && result.hasMesh && physicsServer) {
                        int stride = sizeof(Vertex) / sizeof(float);
                        result.physicsBodyID = physicsServer->CreateTerrainCollider(result.collisionVerts, result.collisionIndices, pushData.chunkOrigin, planetOrigin, stride);
                        
                        result.collisionVerts.clear();
                        result.collisionIndices.clear();
                    }
                    
                    // <-- FIXED: std::move() prevents the deleted constructor error! -->
                    return std::move(result); 
                });
                
                chunksLaunched++;
            }

            planet->rootNode->CheckForFinishedMeshes(vkRenderer, scene, planet->rootNode->center - glm::vec3(planet->rootNode->size / 2.0f));
        }
    }

    void VoxelTerrainModule::GatherOpaquePackets(Scene* scene, IRenderer* renderer,
                                             const std::map<CBaseEntity*, uint32_t>& entityMap, 
                                             std::vector<VoxelDrawPacket>& outPackets) {
        if (!scene || !renderer) return;
        auto* vkRenderer = static_cast<RenderingServer*>(renderer); // <-- Grab the renderer!

        for (auto* ent : scene->entities) {
            if (!ent || !ent->HasComponent<ProceduralPlanetComponent>()) continue;
            
            auto planet = ent->GetComponent<ProceduralPlanetComponent>();
            if (!planet->rootNode) continue;

            auto it = entityMap.find(ent);
            if (it == entityMap.end()) continue;
            uint32_t gpuIndex = it->second;

            auto gatherOctree = [&](auto& self, Crescendo::Terrain::OctreeNode* node) -> void {
                if (!node || !node->isVisible) return; 

                bool childrenReady = false;
                if (!node->isLeaf) {
                    childrenReady = true;
                    for (auto& child : node->children) {
                        if (child && child->meshID == -1) { childrenReady = false; break; }
                    }
                }

                if (node->isLeaf || !childrenReady) {
                    if (node->meshID >= 0) { 
                        // <-- FIXED: Fetch the exact Vulkan handles from the Server! -->
                        MeshResource& mesh = vkRenderer->meshes[node->meshID];
                        if (mesh.vertexBuffer.handle != VK_NULL_HANDLE) {
                            outPackets.push_back({ mesh.vertexBuffer.handle, mesh.indexBuffer.handle, mesh.indexCount, gpuIndex });
                        }
                    } else if (!node->isLeaf) {
                        for (auto& child : node->children) self(self, child.get());
                    }
                } else if (childrenReady && !node->isLeaf) {
                    for (auto& child : node->children) self(self, child.get());
                }
            };

            gatherOctree(gatherOctree, planet->rootNode.get());
        }
    }
    
    // <-- IMPLEMENTED: GatherShadowPackets -->
    void VoxelTerrainModule::GatherShadowPackets(Scene* scene, IRenderer* renderer,
                                             const std::map<CBaseEntity*, uint32_t>& entityMap, 
                                             std::vector<VoxelDrawPacket>& outPackets) {
        if (!scene || !renderer) return;
        auto* vkRenderer = static_cast<RenderingServer*>(renderer);

        for (auto* ent : scene->entities) {
            if (!ent || !ent->HasComponent<ProceduralPlanetComponent>()) continue;
            
            auto planet = ent->GetComponent<ProceduralPlanetComponent>();
            if (!planet->rootNode) continue;

            auto it = entityMap.find(ent);
            if (it == entityMap.end()) continue;
            uint32_t gpuIndex = it->second;

            auto gatherShadowOctree = [&](auto& self, Crescendo::Terrain::OctreeNode* node) -> void {
                if (!node) return; // We do NOT check isVisible here, so off-screen mountains cast shadows!

                bool childrenReady = false;
                if (!node->isLeaf) {
                    childrenReady = true;
                    for (auto& child : node->children) {
                        if (child && child->meshID == -1) { childrenReady = false; break; }
                    }
                }

                if (node->isLeaf || !childrenReady) {
                    if (node->meshID >= 0) { 
                        MeshResource& mesh = vkRenderer->meshes[node->meshID];
                        if (mesh.vertexBuffer.handle != VK_NULL_HANDLE) {
                            outPackets.push_back({ mesh.vertexBuffer.handle, mesh.indexBuffer.handle, mesh.indexCount, gpuIndex });
                        }
                    }
                }
                
                if (childrenReady && !node->isLeaf) {
                    for (auto& child : node->children) self(self, child.get());
                }
            };

            gatherShadowOctree(gatherShadowOctree, planet->rootNode.get());
        }
    }
}