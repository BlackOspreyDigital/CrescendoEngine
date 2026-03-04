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
            
            ImGui::DragFloat3("Position", glm::value_ptr(owner->origin), 0.1f);
            ImGui::DragFloat3("Rotation", glm::value_ptr(owner->angles), 1.0f);
            ImGui::DragFloat3("Scale",    glm::value_ptr(owner->scale), 0.1f);
        }
    };
}