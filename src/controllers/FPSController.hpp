#pragma once

#include "scene/Scene.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include "servers/physics/PhysicsServer.hpp"
#include <glm/glm.hpp>

namespace Crescendo {

    class FPSController {
    public:
        FPSController() = default;
        ~FPSController();

        // Spawns the player capsule
        void Initialize(PhysicsServer* physicsServer, glm::vec3 spawnPos);

        // Movement math and steps virtual character
        void Update(float deltaTime, PhysicsServer* physicsServer, glm::vec3 inputDir, bool jump);

        // lock main camera to players head
        glm::vec3 GetPosition() const;

        void Cleanup();

    private:
        JPH::Ref<JPH::CharacterVirtual> m_character;

        // Source Engine secret sauce 
        float m_maxGroundSpeed = 6.0f;
        float m_maxAirSpeed = 0.6f;             // Caps standard air movement but allows gaining speed via grav
        float m_groundAcceleration = 10.0f;
        float m_airAcceleration = 150.0f;       // High air accel is the core of surfing
        float m_friction = 4.0f;
        float m_jumpForce = 5.0f;
        float m_gravity = -15.0f;               // Slightly snappier gravity feels better for FPS

        // Track velocity manually to apply our own momentum
        JPH::Vec3 m_currentVelocity = JPH::Vec3::sZero();
    };
}