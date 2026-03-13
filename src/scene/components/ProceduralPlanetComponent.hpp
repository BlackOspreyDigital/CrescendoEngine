#pragma once

#include "scene/Component.hpp"
#include "modules/terrain/VoxelGenerator.hpp"

namespace Crescendo {

    class ProceduralPlanetComponent : public Component {
    public:
        // 1. The data we want to tweak in the UI
        Terrain::VoxelSettings settings;
        int resolution = 32;
        float chunkSize = 30.0f;

        // 2. Fulfill your engine's dynamic component interface
        std::string GetName() const override { 
            return "Procedural Planet"; 
        }

        void DrawInspectorUI() override {
            // We hardcoded the ImGui sliders directly into EditorUI.cpp
            // to make hooking up the "Regenerate Mesh" button easier.
            // So we leave this completely blank!
        }
    };

}