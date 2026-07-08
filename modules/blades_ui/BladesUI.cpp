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

            // --- X-AXIS STACKING ---
            float stackBaseX = 600.0f;  // Push inactive blades way off to the sides
            float stackSpacing = 70.0f; // The width of the "tab" peeking out
            
            float targetX = 0.0f;
            if (diff <= -1.0f) {
                // Stacked on the Left
                targetX = -stackBaseX + ((diff + 1.0f) * stackSpacing);
            } else if (diff >= 1.0f) {
                // Stacked on the Right
                targetX = stackBaseX + ((diff - 1.0f) * stackSpacing);
            } else {
                // Smoothly sliding across the screen
                targetX = diff * stackBaseX; 
            }

            // --- Z-AXIS DEPTH ---
            // Active blade pops to Z=50. Inactive blades fall back to Z=35, Z=20, etc.
            // This guarantees they overlap correctly!
            float targetZ = 50.0f - (absDiff * 15.0f);

            // Apply Transforms
            blade.position = glm::vec3(targetX, 0.0f, targetZ);
            blade.scale = 1.0f;      // Kept at 1.0 because our Vertex shader handles the widescreen size
            blade.rotationY = 0.0f;  // Perfectly flat facing the camera
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