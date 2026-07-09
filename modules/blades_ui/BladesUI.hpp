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

        // Input hooks to trigger the spring motion
        void MoveLeft();
        void MoveRight();

        // Returns a const reference to avoid deep copying the whole array every frame
        const std::vector<BladeRenderData>& GetFrameData() const;

        int GetActiveIndex() const { return activeIndex; }

    private:
        int32_t activeIndex = 0;
        float scrollVelocity = 0.0f;
        float currentX = 0.0f;
        
        std::vector<BladeRenderData> bladeData;
    };
}