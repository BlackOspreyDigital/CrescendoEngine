#pragma once
#include "scene/Component.hpp"
#include "modules/terrain/VoxelGenerator.hpp"
#include "modules/terrain/OctreeNode.hpp" 
#include <glm/gtc/type_ptr.hpp>
#include <memory>                         

namespace Crescendo {
    
    // Forward declare the Manager to break the circular include!
    namespace Terrain { class TerrainManager; }

    class ProceduralPlanetComponent : public Component {
    public:
        Terrain::VoxelSettings settings;
        int resolution = 32;
        float chunkSize = 30.0f; 
        
        std::unique_ptr<Terrain::OctreeNode> rootNode;
        float lodSplitThreshold = 1.25f; 
        
        // Store it as a pointer!
        std::unique_ptr<Terrain::TerrainManager> chunkManager; 
        
        // --- THE TWO NEW VARIABLES ---
        int atmosphereMeshID = -1;
        float atmosphereScale = 1.50f; 
        // -----------------------------
        
        // Atmosphere shading variables
        float atmosphereIntensity = 15.0f;
        glm::vec3 rayleigh = glm::vec3(0.015f, 0.035f, 0.075f);
        float mie = 0.003f;

        std::string GetName() const override { return "Procedural Planet"; }
        void DrawInspectorUI() override { /* Left blank! */ }
    };
}