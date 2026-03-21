#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/compatibility.hpp> // For glm::lerp
#include <SDL2/SDL.h> 


namespace Crescendo {
    class Camera {
    public:
        // Camera Attributes
        glm::vec3 Position;
        glm::vec3 Front;
        glm::vec3 Up;
        glm::vec3 Right;
        glm::vec3 WorldUp;

        // Euler Angles
        float Yaw;
        float Pitch;

        // Camera Options
        float MovementSpeed;
        float MouseSensitivity;
        float Zoom;
        
        // Perspective State
        float fov = 80.0f; 
        float nearClip = 0.3f;
        // THE FIX: Crank this up so the planet (and the rest of the universe) doesn't vanish.
        float farClip = 50000.0f;

        // Constructor
        Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f), float yaw = -90.0f, float pitch = 0.0f) 
            : Front(glm::vec3(0.0f, 1.0f, 0.0f)), MovementSpeed(10.0f), MouseSensitivity(0.1f), Zoom(45.0f) {
            
            Position = position;
            WorldUp = up;
            Yaw = yaw;
            Pitch = pitch;
            UpdateCameraVectors();
        }
        
        void SetPosition(const glm::vec3& pos) {
            Position = pos;
        }

        void Move(const glm::vec3& direction, float amount) {
            Position += direction * amount;
        }

        glm::vec3 GetPosition() {
            return Position;
        }

        void SetRotation(const glm::vec3& rot) {
            // Assuming rot is (Pitch, Yaw, Roll)
            Pitch = rot.x;
            Yaw = rot.y;
            UpdateCameraVectors();
        }

        // Returns the view matrix calculated using Euler Angles and the LookAt Matrix
        glm::mat4 GetViewMatrix() {
            return glm::lookAt(Position, Position + Front, Up);
        }

        glm::mat4 GetProjectionMatrix(float aspectRatio) {
            // Fixes Vulkan's inverted Y coordinate automatically
            glm::mat4 proj = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);
            proj[1][1] *= -1; 
            return proj;
        }

        struct ViewFrustum {
            glm::vec4 planes[6];
        };

        ViewFrustum GetFrustum(float aspectRatio) {
            ViewFrustum frustum;
            glm::mat4 proj = GetProjectionMatrix(aspectRatio);
            glm::mat4 view = GetViewMatrix();
            glm::mat4 vp = proj* view;

            // Extract 6 planes of the camera view
            frustum.planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);   // Left
            frustum.planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);   // Right
            frustum.planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],vp[2][3] + vp[2][1], vp[3][3] + vp[3][2]);   // Bottom
            frustum.planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],vp[2][3] + vp[2][2], vp[3][3] - vp[3][1]);   // Top
            frustum.planes[4] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] + vp[1][2],vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);   // Near
            frustum.planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);   // Far

            // Normalize the planes
            for (int i = 0; i < 6; i++) {
                float length = glm::length(glm::vec3(frustum.planes[i]));
                frustum.planes[i] /= length;
            }
            return frustum;
        }

        bool IsSphereInFrustum(const ViewFrustum& f, const glm::vec3& center, float radius) const {
            for (int i = 0; i < 6; i++) {
                // If sphere is completely behind any of the 6 planes, it is invisible.
                if (glm::dot(glm::vec3(f.planes[i]), center) + f.planes[i].w < -radius) {
                    return false;
                }
            }
            return true;
        }

        
        // Process Keyboard (WASD + QE)
        void Update(float deltaTime) {
            // DELETED: Raw SDL polling was bypassing ImGui and the Console!
            // Editor Camera movement is now handled exclusively inside EditorUI::Prepare()
        }
        

        void LookAt(glm::vec3 target) {
            glm::vec3 direction = glm::normalize(target - Position);

            // Calculate Pitch (Z-Up)
            Pitch = glm::degrees(asin(direction.z));
            
            // Calculate Yaw (Z-Up)
            Yaw = glm::degrees(atan2(direction.y, direction.x));

            if (Pitch > 89.0f) Pitch = 89.0f;
            if (Pitch < -89.0f) Pitch = -89.0f;

            UpdateCameraVectors();
        }

        // Process Mouse Movement
        void Rotate(float xoffset, float yoffset, bool constrainPitch = true) {
            xoffset *= MouseSensitivity;
            yoffset *= MouseSensitivity;

            Yaw   += xoffset;
            Pitch += yoffset;

            if (constrainPitch) {
                if (Pitch > 89.0f) Pitch = 89.0f;
                if (Pitch < -89.0f) Pitch = -89.0f;
            }

            UpdateCameraVectors();
        }

    private:
        void UpdateCameraVectors() {
            // Calculate the new Front vector
            glm::vec3 front;
            front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
            front.y = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
            front.z = sin(glm::radians(Pitch));
            Front = glm::normalize(front);
            
            // Also re-calculate the Right and Up vector
            Right = glm::normalize(glm::cross(Front, WorldUp));  
            Up    = glm::normalize(glm::cross(Right, Front));
        }

    };
}