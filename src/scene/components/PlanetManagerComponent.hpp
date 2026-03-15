#pragma once
#include "scene/Component.hpp"
#include "modules/terrain/OctreeNode.hpp"

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

        // --- 2. PASS MATRICES TO THE UPDATE FUNCTION ---
        void UpdatePlanet(const glm::vec3& cameraPos, const glm::mat4& view, const glm::mat4& proj, RenderingServer* renderer, const Terrain::VoxelSettings& settings) {
            if (!root) return;

            root->Update(cameraPos, splitDistance);
            root->CheckForFinishedMeshes(renderer);

            // Construct the frustum for this exact frame
            Frustum frustum(view, proj);

            RequestLeafMeshes(root.get(), settings, frustum);
        }

    private:
        void RequestLeafMeshes(Terrain::OctreeNode* node, const Terrain::VoxelSettings& settings, const Frustum& frustum) {
            
            // --- 3. THE CULLING CHECK ---
            // A perfect sphere radius encompassing our cubic chunk (Size/2 * sqrt(3))
            float radius = (node->size / 2.0f) * 1.732f; 
            
            // If it's outside the camera view, immediately exit before requesting a thread!
            if (!frustum.IsSphereVisible(node->center, radius)) {
                return; 
            }

            if (node->isLeaf) {
                node->RequestMesh(settings);
            } else {
                for (auto& child : node->children) {
                    if (child) RequestLeafMeshes(child.get(), settings, frustum);
                }
            }
        }
    };
}