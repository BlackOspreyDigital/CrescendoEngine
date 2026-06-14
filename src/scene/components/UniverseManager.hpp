#pragma once
#include "scene/Scene.hpp"
#include <glm/glm.hpp>
#include <limits>

namespace Crescendo {

    class UniverseManager {
    public:
        // Returns the gravity vector for ANY position in the universe
        static glm::vec3 GetGravityVector(glm::dvec3 worldPosition, Scene* scene, float& outDistanceToCore) {
            if (!scene) return glm::vec3(0.0f, 0.0f, -9.81f); // Default flat Z-up gravity

            CBaseEntity* closestPlanet = nullptr;
            double minDistance = std::numeric_limits<double>::max();

            // Using the brand new Fast-Cache! 
            for (CBaseEntity* planet : scene->planets) {
                double dist = glm::length(worldPosition - glm::dvec3(planet->origin));
                if (dist < minDistance) {
                    minDistance = dist;
                    closestPlanet = planet;
                }
            }

            if (closestPlanet) {
                outDistanceToCore = static_cast<float>(minDistance);
                
                // Vector pointing FROM the object TO the planet's core
                glm::dvec3 pullDir = glm::dvec3(closestPlanet->origin) - worldPosition;
                return glm::normalize(glm::vec3(pullDir)); 
            }

            outDistanceToCore = 0.0f;
            return glm::vec3(0.0f, 0.0f, 0.0f); // Zero G in deep space!
        }
    };
}