#include "BladesUI.hpp"
#include "UI_Icons.hpp" 
#include <algorithm>
#include <cmath>

namespace Crescendo::Modules {

    BladesUI::BladesUI() {
        // Let's create 10 dummy blades to see your icons in action
        const int NUM_BLADES = 10;
        for (int i = 0; i < NUM_BLADES; ++i) {
            BladeRenderData data{};
            data.scale = 1.0f;
            data.colorTint = glm::vec4(1.0f);
            data.baseMaterialID = 0; 
            
            // Pull the exact UV bounds from the Python-generated array!
            // (We use modulo so it loops safely if you spawn more blades than icons)
            int iconCount = sizeof(Crescendo::Modules::Icons::ATLAS_UVS) / sizeof(glm::vec4);
            data.iconUVBounds = Crescendo::Modules::Icons::ATLAS_UVS[i % iconCount]; 
            
            bladeData.push_back(data);
        }
    }

    void BladesUI::Update(float dt) {
        // 1. Spring Physics (Applied directly to the Index value)
        const float k = 120.0f; // Stiffness
        const float d = 14.0f;  // Damping
        
        float targetIndex = static_cast<float>(activeIndex);
        float force = -k * (currentX - targetIndex) - d * scrollVelocity;
        scrollVelocity += force * dt;
        currentX += scrollVelocity * dt; // currentX smoothly glides between 0.0, 1.0, 2.0, etc.

        // 2. The 360 Tab Stacking Math
        for (size_t i = 0; i < bladeData.size(); ++i) {
            auto& blade = bladeData[i];

            // diff = How many slots away from the center this blade is right now
            float diff = static_cast<float>(i) - currentX;
            float absDiff = std::abs(diff);

            // Smoothly calculate glow/highlight (1.0 = center, 0.0 = edge)
            float selected = std::max(0.0f, 1.0f - absDiff);
            blade.selectedFactor = selected * selected * (3.0f - 2.0f * selected); 

            // --- SEAMLESS X & Z STACKING ---
            const float stackBaseX = 550.0f;   // Distance where card reaches the edge stack
            const float stackSpacing = 35.0f;  // How much each inactive tab peeks out
            
            const float stackBaseZ = 35.0f;    // Depth when card first enters the edge stack
            const float stackZSpacing = 12.0f; // Depth step between stacked cards to prevent Z-fighting

            float targetX = 0.0f;
            float targetZ = 50.0f; // Active center depth

            if (diff < -1.0f) {
                // Stacked on the Left
                float stackIndex = std::abs(diff) - 1.0f;
                targetX = -stackBaseX - (stackIndex * stackSpacing);
                targetZ = stackBaseZ - (stackIndex * stackZSpacing);
            } else if (diff > 1.0f) {
                // Stacked on the Right
                float stackIndex = diff - 1.0f;
                targetX = stackBaseX + (stackIndex * stackSpacing);
                targetZ = stackBaseZ - (stackIndex * stackZSpacing);
            } else {
                // Smoothly sliding between center (diff=0) and edge (absDiff=1)
                targetX = diff * stackBaseX;
                targetZ = 50.0f - (absDiff * (50.0f - stackBaseZ));
            }

            // Apply Transforms
            blade.position = glm::vec3(targetX, 0.0f, targetZ);
            blade.scale = 1.0f;      
            blade.rotationY = 0.0f;  
            blade.colorTint = glm::vec4(1.0f); 
        }
    }

    const std::vector<BladeRenderData>& BladesUI::GetFrameData() const {
        return bladeData;
    }

    void BladesUI::MoveLeft() {
        if (activeIndex > 0) activeIndex--;
    }

    void BladesUI::MoveRight() {
        if (activeIndex < static_cast<int>(bladeData.size()) - 1) activeIndex++;
    }
}