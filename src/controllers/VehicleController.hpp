#pragma once

#include "scene/Scene.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

namespace Crescendo {

    class VehicleController {
    public:
        VehicleController() = default;
        ~VehicleController();

        // Called by PhysicsServer after the rigid body is created
        void Initialize(JPH::PhysicsSystem* physicsSystem, JPH::BodyID chassisID);
        
        // Updates steering, gas, and brakes before the physics step
        void SetInputs(float forward, float right, float brake, float handbrake);

        // Syncs the Vulkan wheel meshes to the Jolt physics calculations
        void UpdateWheelTransforms(CBaseEntity* frontLeft, CBaseEntity* frontRight, CBaseEntity* rearLeft, CBaseEntity* rearRight);

    private:
        JPH::Ref<JPH::VehicleConstraint> vehicleConstraint;
        JPH::WheeledVehicleController* vehicleController = nullptr;
        
        float currentSteering = 0.0f;
    };

}