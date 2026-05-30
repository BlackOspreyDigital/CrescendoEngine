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
        // 50,000 unit radius. We drop frequency massively so terrain stretches out, 
        // and boost amplitude so mountains actually have height.
        Terrain::VoxelSettings settings = { 50000.0f, 6, 2500.0f, 0.0005f }; 
        int resolution = 32;
        
        // The Root Chunk size should theoretically be (Radius * 2) to encapsulate the whole planet.
        float chunkSize = 100000.0f;

        // NEW: We need to explicitly track max depth to prevent the engine 
        // from infinitely subdividing back down to microscopic voxels.
        int maxLOD = 6;
        
        std::unique_ptr<Terrain::OctreeNode> rootNode;
        float lodSplitThreshold = 1.25f; 
        
        // Store it as a pointer!
        std::unique_ptr<Terrain::TerrainManager> chunkManager; 
        
        // --- THE TWO NEW VARIABLES ---
        int atmosphereMeshID = -1;
        
        // --- ATMOSPHERE BOUNDS ---
        float atmosphereCeiling = 1.15f; // Multiplier for the outer edge
        float atmosphereFloor = 0.0f;    // Offset from the exact terrain radius
        
        // Atmosphere shading variables
        float atmosphereIntensity = 15.0f;
        glm::vec3 rayleigh = glm::vec3(0.015f, 0.035f, 0.075f);
        float mie = 0.003f;

        std::string GetName() const override { return "Procedural Planet"; }
        void DrawInspectorUI() override { /* Left blank! */ }
    };
}