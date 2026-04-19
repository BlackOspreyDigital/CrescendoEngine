#pragma once
#include "scene/Component.hpp"
#include "scene/BaseEntity.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace Crescendo {

    class TransformComponent : public Component {
    public:
        std::string GetName() const override { return "Transform"; }

        void DrawInspectorUI() override {
            if (!owner) return;
            
            // Bridge: We manipulate the owner's legacy variables directly.
            
            // 1. Copy the 64-bit double into a temporary 32-bit float
            glm::vec3 tempOrigin = glm::vec3(owner->origin);
                    
            // 2. Let ImGui edit the temporary float
            if (ImGui::DragFloat3("Position", glm::value_ptr(tempOrigin), 0.1f)) {
                // 3. If the user dragged the slider, save it back to the 64-bit double!
                owner->origin = glm::dvec3(tempOrigin);
            }
            ImGui::DragFloat3("Rotation", glm::value_ptr(owner->angles), 1.0f);
            ImGui::DragFloat3("Scale",    glm::value_ptr(owner->scale), 0.1f);
        }
    };
}