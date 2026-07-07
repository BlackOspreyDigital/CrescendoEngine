#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace Crescendo::Modules {

    // Aligned to std430/std140 (48 bytes total stride) & matched to Material.hpp conventions
    struct alignas(16) BladeRenderData {
        glm::vec3 position;       // Offset 0  (12 bytes)
        float rotationY;          // Offset 12 (4 bytes)  -> Fills Chunk 1 (16 bytes)
        
        glm::vec4 colorTint;      // Offset 16 (16 bytes) -> Fills Chunk 2 (16 bytes)
        
        float scale;              // Offset 32 (4 bytes)
        float selectedFactor;     // Offset 36 (4 bytes)
        int32_t baseMaterialID;   // Offset 40 (4 bytes)  -> Replaces uint32_t (Default: -1)
        int32_t labelAtlasID;     // Offset 44 (4 bytes)  -> Replaces uint32_t (Default: -1)
                                  // Fills Chunk 3 (16 bytes total, 0 wasted!)
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

    private:
        int32_t activeIndex = 0;
        float scrollVelocity = 0.0f;
        float currentX = 0.0f;
        
        std::vector<BladeRenderData> bladeData;
    };
}