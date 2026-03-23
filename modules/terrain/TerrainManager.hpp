#pragma once
#include <vector>
#include <algorithm> // Added for std::find

namespace Crescendo::Terrain {
    class OctreeNode; // Forward declaration

    class TerrainManager {
    public:
        // The waiting line for chunks before they get sent to the GPU
        std::vector<OctreeNode*> chunkQueue;

        // Safely adds a chunk to the queue, ensuring no duplicates!
        void EnqueueChunk(OctreeNode* node) {
            if (!node) return;
            
            if (std::find(chunkQueue.begin(), chunkQueue.end(), node) == chunkQueue.end()) {
                chunkQueue.push_back(node);
            }
        }
    };
}