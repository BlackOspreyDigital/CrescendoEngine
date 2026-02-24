#include "controllers/VehicleController.hpp"
#include "servers/physics/PhysicsServer.hpp" // Required for Layers::MOVING
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <iostream>

namespace Crescendo {

    VehicleController::~VehicleController() {
        // Jolt owns this memory, so we just null the pointer
        vehicleController = nullptr;
    }

    void VehicleController::Cleanup(JPH::PhysicsSystem* physicsSystem) {
        if (vehicleConstraint && physicsSystem) {
            physicsSystem->RemoveStepListener(vehicleConstraint);
            physicsSystem->RemoveConstraint(vehicleConstraint);
            vehicleConstraint = nullptr;
        }
    }

    void VehicleController::Initialize(JPH::PhysicsSystem* physicsSystem, JPH::Body* chassisBody) {
        std::cout << "[Physics] Building Jolt Vehicle Constraint...\n";
        
        // Store references so we can wake the body up later
        this->physicsSystem = physicsSystem;
        this->chassisId = chassisBody->GetID();
        
        JPH::VehicleConstraintSettings vehicleSettings;
        
        // 1. Z-Up Engine Configuration
        vehicleSettings.mUp = JPH::Vec3(0, 0, 1);
        vehicleSettings.mForward = JPH::Vec3(0, -1, 0); // Forward is -Y
        vehicleSettings.mMaxPitchRollAngle = JPH::DegreesToRadians(60.0f);

        // 2. Wheel Dimensions and Suspension (Go-Kart Tuning)
        float wheelRadius = 0.45f; // Increased for visual mesh clipping
        float wheelWidth = 0.2f;
        float halfTrackWidth = 0.9f; 
        float halfWheelBase = 1.5f;  

        // 3. Attach Wheels to the Chassis
        // Z set to 0.0f so suspension attaches to the belly, fixing the floating chassis
        JPH::Vec3 wheelPositions[4] = {
            JPH::Vec3(-halfTrackWidth, -halfWheelBase, 0.0f), // Front Left (-Y)
            JPH::Vec3( halfTrackWidth, -halfWheelBase, 0.0f), // Front Right (-Y)
            JPH::Vec3(-halfTrackWidth,  halfWheelBase, 0.0f), // Rear Left (+Y)
            JPH::Vec3( halfTrackWidth,  halfWheelBase, 0.0f)  // Rear Right (+Y)
        };

        for (int i = 0; i < 4; i++) {
            // Create a brand new settings object for each wheel to satisfy Jolt's memory rules
            JPH::WheelSettingsWV* ws = new JPH::WheelSettingsWV();
            
            ws->mRadius = wheelRadius;
            ws->mWidth = wheelWidth;
            ws->mSuspensionMinLength = 0.3f;
            ws->mSuspensionMaxLength = 0.5f;
            
            if (i < 2) {
                ws->mMaxSteerAngle = JPH::DegreesToRadians(35.0f);
            } else {
                ws->mMaxSteerAngle = 0.0f; // Rear wheels don't steer
            }

            ws->mPosition = wheelPositions[i];
            ws->mSteeringAxis = JPH::Vec3(0, 0, 1); 
            ws->mSuspensionForcePoint = wheelPositions[i];
            ws->mSuspensionDirection = JPH::Vec3(0, 0, -1);
            
            vehicleSettings.mWheels.push_back(ws);
        }

        // 4. Engine and Transmission Setup
        JPH::WheeledVehicleControllerSettings* controllerSettings = new JPH::WheeledVehicleControllerSettings();
        
        // High torque combined with low mass equals instant acceleration
        controllerSettings->mEngine.mMaxTorque = 4000.0f; 
        controllerSettings->mEngine.mMinRPM = 1000.0f;
        controllerSettings->mEngine.mMaxRPM = 8500.0f; 
        
        controllerSettings->mTransmission.mMode = JPH::ETransmissionMode::Auto;
        controllerSettings->mTransmission.mSwitchTime = 0.05f; 
        controllerSettings->mTransmission.mGearRatios = { 3.0f, 2.0f, 1.5f, 1.1f, 0.8f }; 
        
        controllerSettings->mDifferentials.clear();
        JPH::VehicleDifferentialSettings diff;
        diff.mLeftWheel = 2; // Rear Left
        diff.mRightWheel = 3; // Rear Right
        diff.mDifferentialRatio = 4.5f; 
        
        controllerSettings->mDifferentials.push_back(diff);

        vehicleSettings.mController = controllerSettings;

        // 5. Finalize Constraint
        vehicleConstraint = new JPH::VehicleConstraint(*chassisBody, vehicleSettings);
        
        JPH::Ref<JPH::VehicleCollisionTester> tester = new JPH::VehicleCollisionTesterRay(
            Layers::MOVING, 
            JPH::Vec3(0, 0, 1) // Up
        );
        vehicleConstraint->SetVehicleCollisionTester(tester);

        physicsSystem->AddConstraint(vehicleConstraint);
        physicsSystem->AddStepListener(vehicleConstraint);
        
        vehicleController = static_cast<JPH::WheeledVehicleController*>(vehicleConstraint->GetController());
        std::cout << "[Physics] Vehicle Constraint Active!\n";
    }

    void VehicleController::SetInputs(float forward, float right, float brake, float handbrake) {
        if (!vehicleController) return;
        
        currentSteering = currentSteering + (right - currentSteering) * 0.1f;
        
        vehicleController->SetDriverInput(forward, currentSteering, brake, handbrake);

        // Wake up the physics body if the driver is touching the controls!
        if ((forward != 0.0f || right != 0.0f || brake != 0.0f || handbrake != 0.0f) && physicsSystem) {
            physicsSystem->GetBodyInterface().ActivateBody(chassisId);
        }
    }

    void VehicleController::UpdateWheelTransforms(CBaseEntity* frontLeft, CBaseEntity* frontRight, CBaseEntity* rearLeft, CBaseEntity* rearRight) {
        if (!vehicleConstraint) return;
        
        CBaseEntity* wheels[4] = { frontLeft, frontRight, rearLeft, rearRight };

        for (int i = 0; i < 4; i++) {
            if (!wheels[i]) continue;

            JPH::RMat44 wheelMat = vehicleConstraint->GetWheelWorldTransform(
                i, 
                JPH::Vec3(1, 0, 0), // Jolt expects Wheel Right (X)
                JPH::Vec3(0, 0, 1)  // Wheel Up (Z)
            );

            JPH::Vec3 jPos = wheelMat.GetTranslation();
            JPH::Quat jRot = wheelMat.GetRotation().GetQuaternion();

            wheels[i]->origin = glm::vec3(jPos.GetX(), jPos.GetY(), jPos.GetZ());
            
            glm::quat q(jRot.GetW(), jRot.GetX(), jRot.GetY(), jRot.GetZ());
            wheels[i]->angles = glm::degrees(glm::eulerAngles(q));
        }
    }
}