#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <glm/glm.hpp>
#include <glm/gtx/compatibility.hpp>
#include "scene/GameWorld.hpp" 

using namespace JPH;

namespace Crescendo {
    class CarController {
    public:
        VehicleConstraint* vehicle = nullptr;
        
        // --- VISUAL LINKS ---
        CBaseEntity* chassisEntity = nullptr;
        CBaseEntity* wheelEntities[4]; // FL, FR, RL, RR

        // --- LUA EXPOSED VARIABLES ---
        // Lua can change these numbers to tune the car!
        float engineTorque = 500.0f;
        float brakeForce = 1500.0f;
        
        void SetVehicle(VehicleConstraint* v) {
            vehicle = v;
        }

        // --- HELPER FOR LUA: Get Speed in KM/H ---
        float GetSpeedKmH() {
            if (!vehicle) return 0.0f;
            // Jolt returns velocity in Meters per Second. 
            // Multiply by 3.6 to get Km/H.
            float speedMs = vehicle->GetVehicleBody()->GetLinearVelocity().Length();
            return speedMs * 3.6f;
        }

        // --- HELPER FOR LUA: Direct Input ---
        // Lua passes us floats (0.0 to 1.0), we pass them to Jolt.
        void SetDriverInput(float forward, float right, float brake, float handbrake) {
            if (vehicle) {
                WheeledVehicleController* controller = static_cast<WheeledVehicleController*>(vehicle->GetController());

                // Jolt rquires seperate calls for each input
                controller->SetForwardInput(forward);
                controller->SetRightInput(right);
                controller->SetBrakeInput(brake);
                controller->SetHandBrakeInput(handbrake);
            }
        }

        // --- VISUAL SYNC ---
        void SyncVisuals() {
            if (!vehicle) return;

            // 1. Sync Chassis
            RVec3 bodyPos = vehicle->GetVehicleBody()->GetPosition();
            Quat bodyRot = vehicle->GetVehicleBody()->GetRotation();
            
            if (chassisEntity) {
                chassisEntity->origin = glm::vec3(bodyPos.GetX(), bodyPos.GetY(), bodyPos.GetZ());
                glm::quat q(bodyRot.GetW(), bodyRot.GetX(), bodyRot.GetY(), bodyRot.GetZ());
                chassisEntity->angles = glm::degrees(glm::eulerAngles(q));
            }

            // 2. Sync Wheels
            for (int i = 0; i < 4; i++) {
                if (wheelEntities[i]) {
                    // Get the wheel transform relative to the world
                    // We use the Right and Up axis to orient it correctly
                    Mat44 wheelTransform = vehicle->GetWheelWorldTransform(i, Vec3::sAxisY(), Vec3::sAxisX());
                    
                    Vec3 wheelPos = wheelTransform.GetTranslation();
                    Quat wheelRot = wheelTransform.GetRotation().GetQuaternion();

                    wheelEntities[i]->origin = glm::vec3(wheelPos.GetX(), wheelPos.GetY(), wheelPos.GetZ());
                    
                    glm::quat q(wheelRot.GetW(), wheelRot.GetX(), wheelRot.GetY(), wheelRot.GetZ());
                    wheelEntities[i]->angles = glm::degrees(glm::eulerAngles(q));
                }
            }
        }
    };
}