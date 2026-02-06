#pragma once
#include <glm/glm.hpp>
#include <string>

namespace Crescendo {
    struct GameObject {
        std::string name;
        int meshID = -1; // -1 means no mesh
        int textureID = 0;
        
        // Transform Data
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 rot = glm::vec3(0.0f); // Euler angles in degrees
        glm::vec3 scale = glm::vec3(1.0f);

        // Constructor for convenience
        GameObject(std::string n, int mID, int tID = 0)
            : name(n), meshID(mID), textureID(tID) {}
    };
}