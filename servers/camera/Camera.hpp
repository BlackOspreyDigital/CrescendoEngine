#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm> // For std::clamp if needed

namespace Crescendo {
    class Camera {
    public:
        // Orbital State
        float distance = 5.0f;
        float yaw = 45.0f;
        float pitch = 30.0f;
        glm::vec3 target = glm::vec3(0.0f);
        
        // Perspective State
        float fov = 45.0f; // Degrees
        float nearClip = 0.1f;
        float farClip = 2000.0f;

        // Getters
        glm::vec3 GetPosition() {
            glm::vec3 pos;
            float radYaw = glm::radians(yaw);
            float radPitch = glm::radians(pitch);

            pos.x = distance * cos(radPitch) * cos(radYaw);
            pos.y = distance * cos(radPitch) * sin(radYaw);
            pos.z = distance * sin(radPitch);
            return target + pos;
        }

        glm::mat4 GetViewMatrix() {
            return glm::lookAt(GetPosition(), target, glm::vec3(0.0f, 0.0f, 1.0f));
        }

        glm::mat4 GetProjectionMatrix(float aspectRatio) {
            // Fixes Vulkan's inverted Y coordinate automatically
            glm::mat4 proj = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);
            proj[1][1] *= -1; 
            return proj;
        }

        void Zoom(float offset) {
            distance -= offset;
            if (distance < 1.0f) distance = 1.0f;
        }

        void Rotate(float dx, float dy) {
            yaw -= dx;
            pitch += dy;
            // Clamp to avoid gimbal locking the orbit
            if (pitch > 89.0f) pitch = 89.0f;
            if (pitch < -89.0f) pitch = -89.0f;
        }
    };
}