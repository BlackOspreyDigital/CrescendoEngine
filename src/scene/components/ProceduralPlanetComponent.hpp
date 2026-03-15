#pragma once
#include "scene/Component.hpp"
#include "modules/terrain/VoxelGenerator.hpp"
#include "modules/terrain/OctreeNode.hpp" 
#include <glm/gtc/type_ptr.hpp>
#include <memory>                         

namespace Crescendo {
    
    // --- THE FIX: Forward declare the Manager to break the circular include! ---
    namespace Terrain { class TerrainManager; }

    class ProceduralPlanetComponent : public Component {
    public:
        Terrain::VoxelSettings settings;
        int resolution = 32;
        float chunkSize = 30.0f; 
        
        std::unique_ptr<Terrain::OctreeNode> rootNode;
        float lodSplitThreshold = 1.25f; 
        
        // --- THE FIX: Store it as a pointer! ---
        std::unique_ptr<Terrain::TerrainManager> chunkManager; 
        
        // Atmosphere variables
        float atmosphereScale = 1.5f; 
        float atmosphereIntensity = 15.0f;
        glm::vec3 rayleigh = glm::vec3(0.015f, 0.035f, 0.075f);
        float mie = 0.003f;

        std::string GetName() const override { return "Procedural Planet"; }
        void DrawInspectorUI() override { /* Left blank! */ }
    };
}