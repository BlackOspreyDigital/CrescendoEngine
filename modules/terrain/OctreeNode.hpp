#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <array>
#include <future>
#include <chrono>
#include <atomic> 
#include "VoxelGenerator.hpp"
#include "servers/rendering/RenderingServer.hpp"

namespace Crescendo {
    class RenderingServer; 
}

namespace Crescendo::Terrain {


    class OctreeNode {
    public:

        // Thread Counter for background workers
        // this is static meaning all chunks share this exact same variable
        static inline std::atomic<int> activeThreads{0};

        glm::vec3 center;
        float size;
        int lod; // 0 = highest detail, 5 = lowest
        
        bool isLeaf = true;
        int meshID = -1; // Linked to RenderingServer

        // Child order: 0: -x-y-z, 1: +x-y-z, 2: -x+y-z, 3: +x+y-z...
        std::array<std::unique_ptr<OctreeNode>, 8> children;

        OctreeNode(glm::vec3 c, float s, int l) : center(c), size(s), lod(l) {}

        std::future<ChunkData> pendingData;
        bool isGenerating = false;

        void RequestMesh(const VoxelSettings& settings, int resolution = 32) {
            if (isGenerating || meshID != -1) return;

            // --- CAPPING THE CORES ---
            // If 4 cores are already busy, cancel the request. 
            // The render loop will organically try again next frame!
            if (activeThreads.load() >= 4) return; 

            isGenerating = true;
            activeThreads++; // Reserve our core

            pendingData = std::async(std::launch::async, [this, settings, resolution]() {
                
                glm::vec3 origin = center - glm::vec3(size / 2.0f);
                auto data = VoxelGenerator::GenerateChunk(origin, resolution, size, settings);
                
                activeThreads--; // Free up the core when finished!
                return data;
            });
        }

        // We change this to return a boolean so we know if an upload happened!
        bool CheckForFinishedMeshes(Crescendo::RenderingServer* renderer) {
            if (isGenerating && pendingData.valid()) {
                if (pendingData.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    ChunkData data = pendingData.get();
                    if (!data.vertices.empty()) {
                        meshID = renderer->acquireMesh("PROCEDURAL", "Chunk", data.vertices, data.indices);
                    }
                    isGenerating = false;
                    
                    // THE FIX: We just did a heavy VRAM upload. Return true to stop 
                    // uploading any other chunks this frame!
                    return true; 
                }
            }

            // Recurse down the tree
            if (!isLeaf) {
                for (auto& child : children) {
                    // If a child uploaded a mesh, stop checking the rest of the tree for this frame
                    if (child && child->CheckForFinishedMeshes(renderer)) {
                        return true; 
                    }
                }
            }
            return false;
        }

        // Recursive function to update the tree based on camera distance
        void Update(const glm::vec3& cameraPos, float splitThreshold) {
            float distance = glm::distance(cameraPos, center);

            // --- 1. HORIZON CULLING ---
            bool isVisible = true;
            // We skip the root node (0,0,0) to prevent a divide-by-zero error
            if (glm::length(center) > 1.0f) { 
                glm::vec3 camDir = glm::normalize(cameraPos);
                glm::vec3 chunkDir = glm::normalize(center);
                
                // If the dot product is less than -0.2, it is safely behind the planet's curve!
                // (We use -0.2 instead of 0.0 to give a small buffer for tall mountains peeking over the edge)
                if (glm::dot(camDir, chunkDir) < -0.2f) {
                    isVisible = false;
                }
            }

            // --- 2. CULLING ENFORCEMENT ---
            // If the chunk is on the dark side, force it to stay low-detail (LOD 4 or 5).
            // This prevents the dark side from ever generating millions of grass-level voxels!
            if (!isVisible && lod < 4) {
                if (!isLeaf) Merge();
                return; // Stop subdividing!
            }

            // --- 3. NORMAL DISTANCE LOD ---
            if (distance < size * splitThreshold && lod > 0) {
                if (isLeaf) Subdivide();
                for (auto& child : children) {
                    if (child) child->Update(cameraPos, splitThreshold);
                }
            } else {
                if (!isLeaf) Merge();
            }
        }

        void Subdivide() {
            isLeaf = false;
            float newSize = size / 2.0f;
            float q = size / 4.0f; // Quarter size for center offsets

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
            for (auto& child : children) child.reset(); // Recursive unique_ptr cleanup
            isLeaf = true;
        }

        
    };
}
