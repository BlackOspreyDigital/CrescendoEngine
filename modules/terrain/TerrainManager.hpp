#pragma once
#include <vector>
#include <algorithm>
#include <atomic>
#include <glm/glm.hpp>
#include "OctreeNode.hpp"
#include "VoxelGenerator.hpp"

namespace Crescendo::Terrain {

    class TerrainManager {
    public:
        std::vector<OctreeNode*> chunkQueue;
        std::atomic<int> activeThreads{0};
        const int MAX_THREADS = 4; // Safely capped to 4 cores!

        // 1. Add chunks to the waiting list
        void EnqueueChunk(OctreeNode* node) {
            if (!node || node->isGenerating || node->meshID != -1) return;
            
            // Prevent duplicate enqueues
            if (std::find(chunkQueue.begin(), chunkQueue.end(), node) == chunkQueue.end()) {
                chunkQueue.push_back(node);
            }
        }

        // 2. Sort and Dispatch (Call this once per frame)
        void ProcessQueue(const glm::vec3& cameraPos, const VoxelSettings& settings) {
            if (chunkQueue.empty()) return;

            // Sort the queue: Chunks closest to the camera move to the front!
            std::sort(chunkQueue.begin(), chunkQueue.end(), [&](OctreeNode* a, OctreeNode* b) {
                float distA = glm::length(cameraPos - a->center);
                float distB = glm::length(cameraPos - b->center);
                return distA < distB; 
            });

            // Dispatch threads until we hit our core limit
            auto it = chunkQueue.begin();
            while (it != chunkQueue.end() && activeThreads.load() < MAX_THREADS) {
                OctreeNode* node = *it;
                
                node->isGenerating = true;
                activeThreads++;

                // --- FIX 1: COPY BY VALUE ---
                // Extract the variables the thread needs BEFORE launching it.
                // If the OctreeNode gets deleted mid-generation, the thread won't crash!
                glm::vec3 origin = node->center - glm::vec3(node->size / 2.0f);
                float size = node->size;
                int lod = node->lod;

                // Notice we capture [this, origin, size, lod, settings] instead of the 'node' pointer
                node->pendingData = std::async(std::launch::async, [this, origin, size, lod, settings]() {
                    auto data = VoxelGenerator::GenerateChunk(origin, 32, size, settings, lod);
                    activeThreads--; // Free up the core when done
                    return data;
                });
                
                it++; 
            }

            // --- FIX 2: STATELESS QUEUE ---
            // Nuke the rest of the queue. If a chunk wasn't processed this frame, 
            // the rendering loop will organically re-add it next frame IF it still exists!
            // This 100% guarantees we never hold onto a deleted pointer.
            // Need to come up with a better system to hide the workers from the users!
            chunkQueue.clear(); 
        }
    };
}