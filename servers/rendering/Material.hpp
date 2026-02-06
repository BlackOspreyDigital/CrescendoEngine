#pragma once
#include <glm/glm.hpp>
#include <string>

struct Material {
    std::string name;

    // tex
    int textureID = -1; 
    int normalMap = -1;

    // pbr extensions
    glm::vec3 albedoColor = {1.0f, 1.0f, 1.0};
    float roughness = 0.5f;
    float metallic = 0.0f;
};