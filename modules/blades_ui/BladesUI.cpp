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

    void BladesUI::ToggleVisibility() {
        isVisible = !isVisible;
    }

    void BladesUI::Update(float dt) {
        // 1. Spring Physics (Applied directly to the Index value)
        const float k = 120.0f; // Stiffness
        const float d = 14.0f;  // Damping
        
        // --- X-Axis Scroll Physics ---
        float targetIndex = static_cast<float>(activeIndex);
        float force = -k * (currentX - targetIndex) - d * scrollVelocity;
        scrollVelocity += force * dt;
        currentX += scrollVelocity * dt;

        // --- Y-Axis Visibility Swoop Physics ---
        float targetOffsetY = isVisible ? 0.0f : -1000.0f; // Drop 1000 units down when hidden
        float offsetForce = -k * (currentOffsetY - targetOffsetY) - d * offsetVelocity;
        offsetVelocity += offsetForce * dt;
        currentOffsetY += offsetVelocity * dt;

        // 2. The 360 Tab Stacking Math
        for (size_t i = 0; i < bladeData.size(); ++i) {
            auto& blade = bladeData[i];

            float diff = static_cast<float>(i) - currentX;
            float absDiff = std::abs(diff);

            float selected = std::max(0.0f, 1.0f - absDiff);
            blade.selectedFactor = selected * selected * (3.0f - 2.0f * selected); 

            const float stackBaseX = 550.0f;   
            const float stackSpacing = 35.0f;  
            const float stackBaseZ = 35.0f;    
            const float stackZSpacing = 12.0f; 

            float targetX = 0.0f;
            float targetZ = 50.0f; 

            if (diff < -1.0f) {
                float stackIndex = std::abs(diff) - 1.0f;
                targetX = -stackBaseX - (stackIndex * stackSpacing);
                targetZ = stackBaseZ - (stackIndex * stackZSpacing);
            } else if (diff > 1.0f) {
                float stackIndex = diff - 1.0f;
                targetX = stackBaseX + (stackIndex * stackSpacing);
                targetZ = stackBaseZ - (stackIndex * stackZSpacing);
            } else {
                targetX = diff * stackBaseX;
                targetZ = 50.0f - (absDiff * (50.0f - stackBaseZ));
            }

            // Apply Transforms: Inject currentOffsetY here!
            blade.position = glm::vec3(targetX, currentOffsetY, targetZ);
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