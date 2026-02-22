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
        std::cout << "[Physics] Building Jolt Vehicle Constraint..." << std::endl;
        
        JPH::VehicleConstraintSettings vehicleSettings;
        
        // 1. Z-Up Engine Configuration
        vehicleSettings.mUp = JPH::Vec3(0, 0, 1);
        vehicleSettings.mForward = JPH::Vec3(0, 1, 0);
        vehicleSettings.mMaxPitchRollAngle = JPH::DegreesToRadians(60.0f);

        // 2. Wheel Dimensions and Suspension
        float wheelRadius = 0.35f; 
        float wheelWidth = 0.2f;
        float halfTrackWidth = 0.9f; 
        float halfWheelBase = 1.5f;  
        float suspensionLength = 0.5f;

        JPH::WheelSettingsWV wheelSettingsFront, wheelSettingsRear;
        wheelSettingsFront.mRadius = wheelRadius;
        wheelSettingsFront.mWidth = wheelWidth;
        wheelSettingsFront.mSuspensionMinLength = 0.3f;
        wheelSettingsFront.mSuspensionMaxLength = 0.5f;
        wheelSettingsFront.mMaxSteerAngle = JPH::DegreesToRadians(35.0f);

        wheelSettingsRear = wheelSettingsFront;
        wheelSettingsRear.mMaxSteerAngle = 0.0f; // Rear wheels don't steer

        // 3. Attach Wheels to the Chassis
        JPH::Vec3 wheelPositions[4] = {
            JPH::Vec3(-halfTrackWidth,  halfWheelBase, -suspensionLength), // Front Left
            JPH::Vec3( halfTrackWidth,  halfWheelBase, -suspensionLength), // Front Right
            JPH::Vec3(-halfTrackWidth, -halfWheelBase, -suspensionLength), // Rear Left
            JPH::Vec3( halfTrackWidth, -halfWheelBase, -suspensionLength)  // Rear Right
        };

        for (int i = 0; i < 4; i++) {
            JPH::WheelSettingsWV* ws = (i < 2) ? new JPH::WheelSettingsWV(wheelSettingsFront) : new JPH::WheelSettingsWV(wheelSettingsRear);
            ws->mPosition = wheelPositions[i];
            
            ws->mSteeringAxis = JPH::Vec3(0, 0, 1); 
            ws->mSuspensionForcePoint = wheelPositions[i];
            ws->mSuspensionDirection = JPH::Vec3(0, 0, -1);
            
            vehicleSettings.mWheels.push_back(ws);
        }

        // 4. Engine and Transmission Setup
        JPH::WheeledVehicleControllerSettings* controllerSettings = new JPH::WheeledVehicleControllerSettings();
        controllerSettings->mEngine.mMaxTorque = 500.0f;
        controllerSettings->mEngine.mMinRPM = 1000.0f;
        controllerSettings->mEngine.mMaxRPM = 6000.0f;
        
        controllerSettings->mTransmission.mMode = JPH::ETransmissionMode::Auto;
        controllerSettings->mTransmission.mGearRatios = { 2.66f, 1.78f, 1.30f, 1.0f, 0.74f, 0.50f }; 
        
        controllerSettings->mDifferentials.clear();
        JPH::VehicleDifferentialSettings diff;
        diff.mLeftWheel = 2; // Rear Left
        diff.mRightWheel = 3; // Rear Right
        controllerSettings->mDifferentials.push_back(diff);

        vehicleSettings.mController = controllerSettings;

        // 5. Finalize Constraint
        vehicleConstraint = new JPH::VehicleConstraint(*chassisBody, vehicleSettings);
        
        JPH::Ref<JPH::VehicleCollisionTester> tester = new JPH::VehicleCollisionTesterRay(
            Layers::MOVING, 
            JPH::Vec3(0, 0, 1) 
        );
        vehicleConstraint->SetVehicleCollisionTester(tester);

        physicsSystem->AddConstraint(vehicleConstraint);
        physicsSystem->AddStepListener(vehicleConstraint);
        
        vehicleController = static_cast<JPH::WheeledVehicleController*>(vehicleConstraint->GetController());
        std::cout << "[Physics] Vehicle Constraint Active!" << std::endl;
    }

    void VehicleController::SetInputs(float forward, float right, float brake, float handbrake) {
        if (!vehicleController) return;
        
        currentSteering = currentSteering + (right - currentSteering) * 0.1f;
        
        vehicleController->SetDriverInput(forward, currentSteering, brake, handbrake);
    }

    void VehicleController::UpdateWheelTransforms(CBaseEntity* frontLeft, CBaseEntity* frontRight, CBaseEntity* rearLeft, CBaseEntity* rearRight) {
        if (!vehicleConstraint) return;
        
        CBaseEntity* wheels[4] = { frontLeft, frontRight, rearLeft, rearRight };

        for (int i = 0; i < 4; i++) {
            if (!wheels[i]) continue;

            JPH::RMat44 wheelMat = vehicleConstraint->GetWheelWorldTransform(
                i, 
                JPH::Vec3(0, 1, 0), // Wheel Forward (Y)
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