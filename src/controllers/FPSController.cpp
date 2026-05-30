#include "FPSController.hpp"
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include "servers/interface/EditorUI.hpp"
#include <iostream>
#include <algorithm>

namespace Crescendo {

    FPSController::~FPSController() {
        Cleanup();
    }

    void FPSController::Initialize(PhysicsServer* physicsServer, glm::vec3 spawnPos) {
        m_spawnPos = spawnPos; // Remember where we started!
        m_fallTimer = 0.0f;

        // --- REGISTER CONVARS FOR LIVE-TUNING ---
        Crescendo::floatConVars["sv_gravity"] = &m_gravity;
        Crescendo::floatConVars["sv_friction"] = &m_friction;
        Crescendo::floatConVars["surf_airaccelerate"] = &m_airAcceleration;
        Crescendo::floatConVars["surf_maxairspeed"] = &m_maxAirSpeed;
        Crescendo::floatConVars["surf_groundspeed"] = &m_maxGroundSpeed;
        // ----------------------------------------

        JPH::Ref<JPH::CapsuleShape> capsule = new JPH::CapsuleShape(0.9f, 0.4f);
        
        JPH::CharacterVirtualSettings settings;
        settings.mShape = capsule;
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisZ(), -0.9f); 
        settings.mUp = JPH::Vec3(0, 0, 1);
        
        // Anything steeper than 35 degrees forces sliding
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(35.0f); 
        
        m_character = new JPH::CharacterVirtual(&settings, JPH::Vec3(spawnPos.x, spawnPos.y, spawnPos.z), JPH::Quat::sIdentity(), physicsServer->physicsSystem);
    }

    void Accelerate(JPH::Vec3& vel, JPH::Vec3 wishDir, float wishSpeedCap, float accel, float dt, float baseSpeed) {
        float currentSpeed = vel.Dot(wishDir);
        float addSpeed = wishSpeedCap - currentSpeed;
        if (addSpeed <= 0) return; 
        
        float accelSpeed = accel * dt * baseSpeed;
        if (accelSpeed > addSpeed) accelSpeed = addSpeed;
        
        vel += wishDir * accelSpeed;
    }

    void FPSController::Update(float deltaTime, PhysicsServer* physicsServer, AudioServer* audioServer, glm::vec3 inputDir, bool jump, glm::vec3 upDir) {
        if (!m_character) return;

        // 1. Tell Jolt which way is UP right now!
        JPH::Vec3 joltUp(upDir.x, upDir.y, upDir.z);
        m_character->SetUp(joltUp);

        JPH::Vec3 wishDir = JPH::Vec3(inputDir.x, inputDir.y, inputDir.z);
        if (wishDir.LengthSq() > 0.0f) wishDir = wishDir.Normalized();

        JPH::CharacterVirtual::EGroundState groundState = m_character->GetGroundState();
        bool onGround = (groundState == JPH::CharacterVirtual::EGroundState::OnGround);
        bool isSurfing = (groundState == JPH::CharacterVirtual::EGroundState::OnSteepGround);

        // --- FALL TIMER LOGIC ---
        if (!onGround && !isSurfing && m_currentVelocity.Length() > 0.1f) {
            m_fallTimer += deltaTime;
        } else if (onGround || isSurfing) {
            m_fallTimer = 0.0f;
        }

        if (m_fallTimer >= 60.0f) {
            std::cout << "[Player] Killed by the Guardians (Orbital Fall Timer)." << std::endl;
            m_character->SetPosition(JPH::Vec3(m_spawnPos.x, m_spawnPos.y, m_spawnPos.z));
            m_currentVelocity = JPH::Vec3::sZero();
            m_character->SetLinearVelocity(JPH::Vec3::sZero());
            m_fallTimer = 0.0f; 
        }
       
        // --- FOOTSTEP AUDIO ---
        static float footstepTimer = 0.0f;
        float currentSpeed = m_currentVelocity.Length();

        if (onGround && currentSpeed > 1.0f) { 
            float stepInterval = 2.6f / currentSpeed; 
            stepInterval = std::clamp(stepInterval, 0.25f, 0.6f);

            footstepTimer += deltaTime;
            if (footstepTimer >= stepInterval) {
                if (audioServer) audioServer->PlayOneShot("assets/audio/step.mp3", 0.6f);
                footstepTimer = 0.0f;
            }
        } else {
            footstepTimer = 0.6f; 
        }
        
        if (onGround) {
            float speed = m_currentVelocity.Length();
            if (speed > 0.1f) {
                float drop = speed * m_friction * deltaTime;
                float newSpeed = std::max(speed - drop, 0.0f);
                newSpeed /= speed;
                m_currentVelocity *= newSpeed;
            } else {
                m_currentVelocity = JPH::Vec3::sZero();
            }
        }

        if (onGround) {
            // THE FIX: Remove velocity pushing INTO the floor instead of hardcoding SetZ(0)
            float upVelocity = m_currentVelocity.Dot(joltUp);
            m_currentVelocity -= joltUp * upVelocity; 

            Accelerate(m_currentVelocity, wishDir, m_maxGroundSpeed, m_groundAcceleration, deltaTime, m_maxGroundSpeed);
            
            if (jump) {
                // THE FIX: Jump pushes you along the dynamic UP vector!
                m_currentVelocity += joltUp * m_jumpForce;
            }
        } else {
            Accelerate(m_currentVelocity, wishDir, m_maxAirSpeed, m_airAcceleration, deltaTime, m_maxGroundSpeed);
            
            // THE FIX: Multiply gravity by the UP vector (m_gravity is negative, so this pulls you DOWN)
            m_currentVelocity += (joltUp * m_gravity) * deltaTime;

            if (isSurfing) {
                JPH::Vec3 surfNormal = m_character->GetGroundNormal();
                float backoff = m_currentVelocity.Dot(surfNormal);
                if (backoff < 0.0f) {
                    m_currentVelocity -= surfNormal * backoff;
                }
            }
        }

        m_character->SetLinearVelocity(m_currentVelocity);
        
        // THE FIX: Pass the directional gravity into ExtendedUpdate so the raycaster knows which way to look for the ground!
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        m_character->ExtendedUpdate(
            deltaTime,
            joltUp * m_gravity, 
            updateSettings,
            physicsServer->physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
            physicsServer->physicsSystem->GetDefaultLayerFilter(Layers::MOVING),
            { }, { }, 
            *physicsServer->tempAllocator
        );

        m_currentVelocity = m_character->GetLinearVelocity();
    }

    glm::vec3 FPSController::GetPosition() const {
        if (!m_character) return glm::vec3(0.0f);
        JPH::Vec3 p = m_character->GetPosition();
        return glm::vec3(p.GetX(), p.GetY(), p.GetZ() + 1.1f); 
    }

    void FPSController::Cleanup() {
        m_character = nullptr; 
    }
}