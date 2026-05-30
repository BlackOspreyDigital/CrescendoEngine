#pragma once
#include "scene/Component.hpp"
#include "modules/terrain/OctreeNode.hpp"
#include "modules/terrain/VoxelGenerator.hpp"

namespace Crescendo {
    class RenderingServer;

    // --- 1. THE FRUSTUM MATH ---
    struct Frustum {
        glm::vec4 planes[6];
        
        Frustum(const glm::mat4& view, const glm::mat4& proj) {
            // Multiply Projection and View to get the camera's total matrix
            glm::mat4 vp = proj * view;
            
            // Extract the 6 planes (Left, Right, Bottom, Top, Near, Far)
            planes[0] = vp[3] + vp[0];
            planes[1] = vp[3] - vp[0];
            planes[2] = vp[3] + vp[1];
            planes[3] = vp[3] - vp[1];
            planes[4] = vp[3] + vp[2];
            planes[5] = vp[3] - vp[2];
            
            // Normalize them so the distance calculations are accurate
            for (int i = 0; i < 6; i++) {
                planes[i] /= glm::length(glm::vec3(planes[i]));
            }
        }
        
        bool IsSphereVisible(const glm::vec3& center, float radius) const {
            for (int i = 0; i < 6; i++) {
                // If the sphere is completely behind any of the 6 camera planes, it is invisible!
                if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius) {
                    return false; 
                }
            }
            return true; 
        }
    };

    class PlanetManagerComponent : public Component {
    public:
        std::unique_ptr<Terrain::OctreeNode> root;
        float splitDistance = 1.5f; 
        
        std::string GetName() const override { return "Planet Manager"; }

        void Initialize(glm::vec3 origin, float totalSize, int maxLOD) {
            root = std::make_unique<Terrain::OctreeNode>(origin, totalSize, maxLOD);
        }

        // --- 2. PASS MATRICES AND ORIGIN TO THE UPDATE FUNCTION ---
        void UpdatePlanet(const glm::vec3& cameraPos, const glm::mat4& view, const glm::mat4& proj, RenderingServer* renderer, Scene* scene, Terrain::TerrainManager* manager, const Terrain::VoxelSettings& settings, const glm::vec3& planetOrigin) {
            if (!root) return;
        
            // 1. LOD FIX: Convert World camera to Local camera!
            // Subtracting the planet's origin tricks the octree into thinking it's back at (0,0,0)
            glm::vec3 localCameraPos = cameraPos - planetOrigin;
            root->Update(localCameraPos, splitDistance, manager); 
        
            root->CheckForFinishedMeshes(renderer, scene, root->center);
        
            Frustum frustum(view, proj);
            RequestLeafMeshes(root.get(), settings, frustum, planetOrigin);
        }

    private:
        // Make sure to add planetOrigin to this signature too!
        void RequestLeafMeshes(Terrain::OctreeNode* node, const Terrain::VoxelSettings& settings, const Frustum& frustum, const glm::vec3& planetOrigin) {
            
            float radius = (node->size / 2.0f) * 1.732f; 
            
            // 2. FRUSTUM FIX: Convert Local chunk center to World Space!
            // Adding the planet's origin pushes the chunk forward into the camera's actual view
            glm::vec3 worldChunkCenter = node->center + planetOrigin;

            // Check against the WORLD center, not the local center!
            if (!frustum.IsSphereVisible(worldChunkCenter, radius)) {
                return; 
            }

            if (node->isLeaf) {
                // Handled asynchronously by Update()
            } else {
                for (auto& child : node->children) {
                    if (child) RequestLeafMeshes(child.get(), settings, frustum, planetOrigin);
                }
            }
        }
    };
}