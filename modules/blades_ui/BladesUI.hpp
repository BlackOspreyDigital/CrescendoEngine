#pragma once
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace Crescendo::Modules {

    // Aligned to std430/std140 (48 bytes total stride) & matched to Material.hpp conventions
    struct BladeRenderData {
        glm::vec3 position;        // Offset 0
        float rotationY;           // Offset 12
        glm::vec4 colorTint;       // Offset 16
        float scale;               // Offset 32
        float selectedFactor;      // Offset 36
        int baseMaterialID;        // Offset 40
        float padding;             // Offset 44
        glm::vec4 iconUVBounds;    // Offset 48 (u0, v0, u1, v1)
    };

    class BladesUI {
    public:
        BladesUI(); 

        // Updates spring-damper physics and calculates 2.5D transforms
        void Update(float dt);

        // Input hooks
        void MoveLeft();
        void MoveRight();
        void ToggleVisibility();

        const std::vector<BladeRenderData>& GetFrameData() const;
        int GetActiveIndex() const { return activeIndex; }
        bool IsVisible() const { return isVisible; }

    private:
        bool isVisible = false;            // Start hidden
        float currentOffsetY = -1000.0f;   // Start off-screen (below the camera)
        float offsetVelocity = 0.0f;       // Physics velocity for the swoop animation

        int32_t activeIndex = 0;
        float scrollVelocity = 0.0f;
        float currentX = 0.0f;
        
        std::vector<BladeRenderData> bladeData;
    };
}