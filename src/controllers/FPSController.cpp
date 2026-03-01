#include "FPSController.hpp"
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include "servers/interface/EditorUI.hpp"
#include <iostream>


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
        
        // THE SURF FIX: Anything steeper than 35 degrees forces sliding
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(35.0f); 
        
        m_character = new JPH::CharacterVirtual(&settings, JPH::Vec3(spawnPos.x, spawnPos.y, spawnPos.z), JPH::Quat::sIdentity(), physicsServer->physicsSystem);
    }

    // THE MATH FIX: Added 'baseSpeed' parameter
    void Accelerate(JPH::Vec3& vel, JPH::Vec3 wishDir, float wishSpeedCap, float accel, float dt, float baseSpeed) {
        float currentSpeed = vel.Dot(wishDir);
        float addSpeed = wishSpeedCap - currentSpeed;
        if (addSpeed <= 0) return; 
        
        float accelSpeed = accel * dt * baseSpeed;
        if (accelSpeed > addSpeed) accelSpeed = addSpeed;
        
        vel += wishDir * accelSpeed;
    }

    void FPSController::Update(float deltaTime, PhysicsServer* physicsServer, glm::vec3 inputDir, bool jump) {
        if (!m_character) return;

        JPH::Vec3 wishDir = JPH::Vec3(inputDir.x, inputDir.y, inputDir.z);
        if (wishDir.LengthSq() > 0.0f) wishDir = wishDir.Normalized();

        // 1. Check our exact ground state
        JPH::CharacterVirtual::EGroundState groundState = m_character->GetGroundState();
        bool onGround = (groundState == JPH::CharacterVirtual::EGroundState::OnGround);
        
        // NEW: Check if we are sliding on a surf ramp!
        bool isSurfing = (groundState == JPH::CharacterVirtual::EGroundState::OnSteepGround);

        // --- FALL TIMER & DEATH LOGIC ---
        // If we are NOT on flat ground AND NOT surfing a ramp, we are truly falling!
        if (!onGround && !isSurfing && m_character->GetLinearVelocity().GetZ() < -0.1f) {
            m_fallTimer += deltaTime;
        } else if (onGround || isSurfing) {
            // We landed safely, OR we are touching a surf ramp! Reset the timer.
            m_fallTimer = 0.0f;
        }

        // If we fall for more than 5 seconds (or hit the absolute bottom of the world)
        if (m_fallTimer >= 5.0f || m_character->GetPosition().GetZ() < -500.0f) {
            std::cout << "[Player] Killed by the Guardians (Fall Timer)." << std::endl;
            m_character->SetPosition(JPH::Vec3(m_spawnPos.x, m_spawnPos.y, m_spawnPos.z));
            m_currentVelocity = JPH::Vec3::sZero();
            m_character->SetLinearVelocity(JPH::Vec3::sZero());
            m_fallTimer = 0.0f; // Reset timer after respawn
        }
        // ---------------------------------------

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
            m_currentVelocity.SetZ(0.0f); 
            Accelerate(m_currentVelocity, wishDir, m_maxGroundSpeed, m_groundAcceleration, deltaTime, m_maxGroundSpeed);
            
            if (jump) {
                m_currentVelocity.SetZ(m_jumpForce);
            }
        } else {
            // 1. Air Acceleration (Builds the sideways force)
            Accelerate(m_currentVelocity, wishDir, m_maxAirSpeed, m_airAcceleration, deltaTime, m_maxGroundSpeed);
            
            // 2. Apply Gravity
            m_currentVelocity += JPH::Vec3(0, 0, m_gravity) * deltaTime;

            // --- 3. SOURCE ENGINE SURF MATH (Clip Velocity) ---
            if (isSurfing) {
                // Get the exact angle of the ramp face
                JPH::Vec3 surfNormal = m_character->GetGroundNormal();
                
                // Check how much of our velocity is pointing directly INTO the ramp
                float backoff = m_currentVelocity.Dot(surfNormal);
                
                // If we are pushing into the wall, perfectly redirect that force ALONG the wall
                if (backoff < 0.0f) {
                    m_currentVelocity -= surfNormal * backoff;
                }
            }
            // --------------------------------------------------
        }

        // Send the perfectly calculated surf velocity to Jolt
        m_character->SetLinearVelocity(m_currentVelocity);
        
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        m_character->ExtendedUpdate(
            deltaTime,
            JPH::Vec3(0, 0, m_gravity),
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