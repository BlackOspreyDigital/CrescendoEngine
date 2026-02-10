#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL2/SDL.h> 
#include <algorithm>

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
        float nearClip = 0.1f;
        float farClip = 2000.0f;

        // Constructor
        Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f), float yaw = -90.0f, float pitch = 0.0f) 
            : Front(glm::vec3(0.0f, 1.0f, 0.0f)), MovementSpeed(10.0f), MouseSensitivity(0.1f), Zoom(45.0f) {
            
            Position = position;
            WorldUp = up;
            Yaw = yaw;
            Pitch = pitch;
            UpdateCameraVectors();
        }

        // --- INTERFACE MATCHING YOUR ENGINE ---
        
        void SetPosition(const glm::vec3& pos) {
            Position = pos;
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

        // Process Keyboard (WASD + QE)
        void Update(float deltaTime) {
            float velocity = MovementSpeed * deltaTime;
            
            const Uint8* state = SDL_GetKeyboardState(nullptr);
            
            // Forward/Back
            if (state[SDL_SCANCODE_W]) Position += Front * velocity;
            if (state[SDL_SCANCODE_S]) Position -= Front * velocity;
            if (state[SDL_SCANCODE_A]) Position -= Right * velocity;
            if (state[SDL_SCANCODE_D]) Position += Right * velocity;
            if (state[SDL_SCANCODE_Q]) Position += WorldUp * velocity;
            if (state[SDL_SCANCODE_E]) Position -= WorldUp * velocity;
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