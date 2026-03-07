#pragma once
#include "scene/Component.hpp"
#include "scene/BaseEntity.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace Crescendo {

    class MeshRendererComponent : public Component {
    public:
        std::string GetName() const override { return "Mesh Renderer"; }

        void DrawInspectorUI() override {
            if (!owner) return;

            ImGui::TextDisabled("Material Properties");
            ImGui::ColorEdit3("Albedo", glm::value_ptr(owner->albedoColor));
            ImGui::SliderFloat("Roughness", &owner->roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &owner->metallic, 0.0f, 1.0f);
            ImGui::SliderFloat("Emission", &owner->emission, 0.0f, 20.0f);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.0f, 1.0f), "PBR +");
            ImGui::SliderFloat("Clearcoat", &owner->clearcoat, 0.0f, 1.0f);
            ImGui::SliderFloat("Coat Roughness", &owner->clearcoatRoughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Sheen", &owner->sheen, 0.0f, 1.0f);
            ImGui::SliderFloat("Specular Weight", &owner->specularWeight, 0.0f, 1.0f);
            ImGui::Spacing();
            
            
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::TextDisabled("Transparency & Volume");
            ImGui::SliderFloat("Transmission (Glass)", &owner->transmission, 0.0f, 1.0f);
            
            // Only show advanced volume settings if the material is actually transparent
            if (owner->transmission > 0.0f) {
                ImGui::Indent();
                ImGui::ColorEdit3("Volume Tint", glm::value_ptr(owner->attenuationColor));
                ImGui::DragFloat("Density (Dist)", &owner->attenuationDistance, 0.01f, 0.001f, 10.0f);
                ImGui::SliderFloat("Refraction (IOR)", &owner->ior, 1.0f, 2.5f); 
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::SliderFloat("Normal Strength", &owner->normalStrength, 0.0f, 5.0f);
        }
    };
}