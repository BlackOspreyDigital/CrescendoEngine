#pragma once
#include "scene/Component.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace Crescendo {

    class PointLightComponent : public Component {
    public:
        glm::vec3 color = glm::vec3(1.0f, 0.4f, 0.1f);
        float intensity = 25.0f;
        float radius = 15.0f;

        std::string GetName() const override { return "Point Light"; }

        void DrawInspectorUI() override {
            ImGui::ColorEdit3("Color", glm::value_ptr(color));
            ImGui::SliderFloat("Intensity", &intensity, 0.0f, 100.0f);
            ImGui::SliderFloat("Radius", &radius, 1.0f, 100.0f);
        }
    };
}