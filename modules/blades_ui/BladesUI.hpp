#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace Crescendo::Modules {

    struct BladeRenderData {
        glm::vec3 position;
        float rotationY;
        glm::vec4 colorTint;
        float selectedFactor;
        uint32_t textureID;
        uint32_t labelHash;
    };

    class BladesUI {
    public:
        // Updates spring-damper physics and logic
        void Update(float dt);

        // Returns a copy of the render data for the current frame
        // (Fast, copy is cheap for UI size arrays)
        std::vector<BladeRenderData> GetFrameData() const;

    private:
        int32_t activeIndex = 0;
        float scrollVelocity = 0.0f;
        float currentX = 0.0f;
    };
}