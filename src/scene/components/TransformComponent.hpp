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
            
            // Create a 32-bit temporary float for ImGui to use
            glm::vec3 tempOrigin = glm::vec3(owner->origin);
            if (ImGui::DragFloat3("Position", glm::value_ptr(tempOrigin), 0.1f)) {
                // If the slider is moved, save it back to the 64-bit origin
                owner->origin = glm::dvec3(tempOrigin);
            }
            ImGui::DragFloat3("Rotation", glm::value_ptr(owner->angles), 1.0f);
            ImGui::DragFloat3("Scale",    glm::value_ptr(owner->scale), 0.1f);
        }
    };
}