#include "FPSController.hpp"
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

namespace Crescendo {

    FPSController::~FPSController() {
        Cleanup();
    }

    void FPSController::Initialize(PhysicsServer* physicsServer, glm::vec3 spawnPos) {
        // 1. Create the Player Collider (0.4m radius, 0.9m half-height = ~1.8m tall player)
        JPH::Ref<JPH::CapsuleShape> capsule = new JPH::CapsuleShape(0.9f, 0.4f);
        
        // 2. Character Virtual Settings
        JPH::CharacterVirtualSettings settings;
        settings.mShape = capsule;
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisZ(), -0.9f); // Stand on feet
        settings.mUp = JPH::Vec3(0, 0, 1);
        
        // 3. Instantiate the Phantom Body
        m_character = new JPH::CharacterVirtual(&settings, JPH::Vec3(spawnPos.x, spawnPos.y, spawnPos.z), JPH::Quat::sIdentity(), physicsServer->physicsSystem);
    }

    
    void Accelerate(JPH::Vec3& vel, JPH::Vec3 wishDir, float wishSpeed, float accel, float dt) {
        // How much of our current velocity is aligned with our desired input direction?
        float currentSpeed = vel.Dot(wishDir);
        
        // How much speed are we allowed to add before hitting the cap?
        float addSpeed = wishSpeed - currentSpeed;
        if (addSpeed <= 0) return; // We are maxed out in this direction!
        
        // Calculate the raw acceleration burst
        float accelSpeed = accel * dt * wishSpeed;
        
        // Cap it if it overshoots
        if (accelSpeed > addSpeed) accelSpeed = addSpeed;
        
        // Apply the newly calculated burst
        vel += wishDir * accelSpeed;
    }

    void FPSController::Update(float deltaTime, PhysicsServer* physicsServer, glm::vec3 inputDir, bool jump) {
        if (!m_character) return;

        // Normalize input
        JPH::Vec3 wishDir = JPH::Vec3(inputDir.x, inputDir.y, inputDir.z);
        if (wishDir.LengthSq() > 0.0f) wishDir = wishDir.Normalized();

        // Check if grounded
        JPH::CharacterVirtual::EGroundState groundState = m_character->GetGroundState();
        bool onGround = (groundState == JPH::CharacterVirtual::EGroundState::OnGround);

        // 1. Apply Friction (Only on ground)
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

        // 2. Apply Acceleration & Jumping
        if (onGround) {
            m_currentVelocity.SetZ(0.0f); // Stick to the floor
            Accelerate(m_currentVelocity, wishDir, m_maxGroundSpeed, m_groundAcceleration, deltaTime);
            
            if (jump) {
                m_currentVelocity.SetZ(m_jumpForce);
            }
        } else {
            // THE SURF MATH: High acceleration, but incredibly low max air speed!
            Accelerate(m_currentVelocity, wishDir, m_maxAirSpeed, m_airAcceleration, deltaTime);
            
            // Apply Gravity
            m_currentVelocity += JPH::Vec3(0, 0, m_gravity) * deltaTime;
        }

        // 3. Tell Jolt our intended velocity
        m_character->SetLinearVelocity(m_currentVelocity);
        
        // 4. Let Jolt resolve collisions and slide along ramps
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        m_character->ExtendedUpdate(
            deltaTime,
            JPH::Vec3(0, 0, m_gravity),
            updateSettings,
            physicsServer->physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
            physicsServer->physicsSystem->GetDefaultLayerFilter(Layers::MOVING),
            { }, { }, // Body and Shape filters
            *physicsServer->tempAllocator
        );

        // 5. Read back the actual velocity after Jolt slid us across geometry
        m_currentVelocity = m_character->GetLinearVelocity();
    }

    glm::vec3 FPSController::GetPosition() const {
        if (!m_character) return glm::vec3(0.0f);
        JPH::Vec3 p = m_character->GetPosition();
        // Add an offset so the camera sits at "eye level" (near the top of the 1.8m capsule)
        return glm::vec3(p.GetX(), p.GetY(), p.GetZ() + 0.7f); 
    }

    void FPSController::Cleanup() {
        m_character = nullptr; 
    }
}