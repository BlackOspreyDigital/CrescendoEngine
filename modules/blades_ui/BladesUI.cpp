#include "BladesUI.hpp"
#include <algorithm>
#include <cmath>

namespace Crescendo::Modules {

    BladesUI::BladesUI() {
        // Mock up 5 dummy blades for the renderer to chew on
        const int NUM_BLADES = 5;
        for (int i = 0; i < NUM_BLADES; ++i) {
            BladeRenderData data{};
            data.scale = 1.0f;
            data.colorTint = glm::vec4(1.0f);
            data.baseMaterialID = 0; // Replaced textureID
            data.labelAtlasID = 0;   // Replaced labelHash
            bladeData.push_back(data);
        }
    }

    void BladesUI::Update(float dt) {
        // 1. Premium Spring Physics Constants
        const float k = 150.0f; // Stiffness (higher = snappier)
        const float d = 15.0f;  // Damping (higher = heavier/less bounce)
        const float BLADE_SPACING = 400.0f; 
        
        // 2. Global Scroll Physics
        float targetX = -static_cast<float>(activeIndex) * BLADE_SPACING; 
        float force = -k * (currentX - targetX) - d * scrollVelocity;
        scrollVelocity += force * dt;
        currentX += scrollVelocity * dt;

        // 3. Update individual blade transforms for the 2.5D Carousel effect
        for (size_t i = 0; i < bladeData.size(); ++i) {
            auto& blade = bladeData[i];

            // Base X position relative to the center of the screen
            float bladeBaseX = (static_cast<float>(i) * BLADE_SPACING) + currentX;
            
            // Calculate distance from center (1.0 = one full space away)
            float distFromCenter = std::abs(bladeBaseX) / BLADE_SPACING;
            
            // Selected factor: 1.0 when perfectly centered, 0.0 when 1+ space away
            float selected = std::max(0.0f, 1.0f - distFromCenter);
            blade.selectedFactor = selected;
            
            // --- 2.5D MATH ---
            
            // Position: Slide X, stay on Y, pop forward on Z when selected
            blade.position = glm::vec3(bladeBaseX, 0.0f, selected * 50.0f);
            
            // Scale: Grow by 20% when perfectly centered
            blade.scale = 1.0f + (selected * 0.2f); 
            
            // Rotation: Slight curve towards the viewer depending on which side they are on
            // (Clamped to avoid extreme clipping on the edges of ultra-wide monitors)
            float rotationAngle = std::clamp(bladeBaseX / 1500.0f, -0.4f, 0.4f);
            blade.rotationY = rotationAngle;
            
            // Alpha: Fade blades to black/transparent the further they get from the center
            float alpha = std::clamp(1.0f - (distFromCenter * 0.4f), 0.0f, 1.0f);
            blade.colorTint = glm::vec4(1.0f, 1.0f, 1.0f, alpha);
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