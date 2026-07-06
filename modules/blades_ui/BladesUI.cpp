#include "BladesUI.hpp"
#include <algorithm>

void Crescendo::Modules::BladesUI::Update(float dt) {
    // Spring physics constants
    const float k = 25.0f; 
    const float d = 0.65f; 
    
    // Target position for the selected blade
    float targetX = -static_cast<float>(activeIndex) * 400.0f; 
    
    // F = -kx - dv
    float force = -k * (currentX - targetX) - d * scrollVelocity;
    scrollVelocity += force * dt;
    currentX += scrollVelocity * dt;
}