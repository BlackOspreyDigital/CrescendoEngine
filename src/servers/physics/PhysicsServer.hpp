#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h> 
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>

// Vehicle Headers
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <unordered_map>
#include <thread> 
#include <algorithm> 
#include <cstdio>

using namespace JPH;

namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
};

namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS(2);
};

class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING: return inObject2 == Layers::MOVING; 
            case Layers::MOVING:     return true; 
            default:                 return false;
        }
    }
};

class BPLayerInterfaceImpl : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    virtual uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }
    
    #if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char *GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override {
        switch ((BroadPhaseLayer::Type)inLayer) {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
            default: return "INVALID";
        }
    }
    #endif

private:
    BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:     return true; 
            default:                 return false;
        }
    }
};

class PhysicsServer {
public:
    PhysicsSystem* physicsSystem = nullptr;
    TempAllocatorImpl* tempAllocator = nullptr;
    JobSystemThreadPool* jobSystem = nullptr;
    
    BPLayerInterfaceImpl broad_phase_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;
    ObjectLayerPairFilterImpl object_vs_object_layer_filter;

    BodyInterface* bodyInterface = nullptr;
    std::unordered_map<int, BodyID> entityBodyMap;

    void Initialize() {
        std::cout << "[Physics] Initializing Jolt (Z-Up Mode)..." << std::endl;
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();

        tempAllocator = new TempAllocatorImpl(10 * 1024 * 1024);
        jobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

        physicsSystem = new PhysicsSystem();
        physicsSystem->Init(1024, 0, 1024, 1024, broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);

        // --- FIX 1: Set Gravity to Z-Down ---
        physicsSystem->SetGravity(Vec3(0, 0, -9.81f)); 
        // ------------------------------------

        bodyInterface = &physicsSystem->GetBodyInterface();
        std::cout << "[Physics] Initialization Complete." << std::endl;
    }

    void Cleanup() {
        if (physicsSystem) delete physicsSystem;
        if (jobSystem) delete jobSystem;
        if (tempAllocator) delete tempAllocator;
        if (Factory::sInstance) {
            delete Factory::sInstance;
            Factory::sInstance = nullptr;
        }
    }

    void CreateBox(int entityID, glm::vec3 position, glm::vec3 scale, bool isDynamic) {
        if (!bodyInterface) { std::cerr << "!! CRITICAL: BodyInterface is NULL" << std::endl; return; }

        BoxShapeSettings* boxShape = new BoxShapeSettings(ToJolt(scale / 2.0f)); 

        BodyCreationSettings settings(
            boxShape, 
            ToJolt(position), 
            Quat::sIdentity(), 
            isDynamic ? EMotionType::Dynamic : EMotionType::Static, 
            isDynamic ? Layers::MOVING : Layers::NON_MOVING
        );

        Body* body = bodyInterface->CreateBody(settings);
        if (!body) {
            std::cerr << "!! CRITICAL: Failed to create Box Body for ID " << entityID << std::endl;
            return;
        }
        
        bodyInterface->AddBody(body->GetID(), EActivation::Activate);
        entityBodyMap[entityID] = body->GetID();
    }

    VehicleConstraint* CreateRaycastCar(int entityID, glm::vec3 position) {
        std::cout << "[Physics] Creating Car Chassis (Z-Up)..." << std::endl;
        if (!bodyInterface) return nullptr;

        // 1. Create Chassis Body
        // Z-Up: Car is usually Length=X, Width=Y, Height=Z
        Ref<BoxShape> chassisShape = new BoxShape(Vec3(2.0f, 1.0f, 0.5f)); 
        // Offset center of mass DOWN on Z
        Ref<OffsetCenterOfMassShape> offsetShape = new OffsetCenterOfMassShape(chassisShape, Vec3(0, 0, -0.5f));

        BodyCreationSettings carBodySettings(offsetShape, ToJolt(position), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
        carBodySettings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        carBodySettings.mMassPropertiesOverride.mMass = 1500.0f;
        
        Body* carBody = bodyInterface->CreateBody(carBodySettings);
        if (!carBody) return nullptr;
        
        bodyInterface->AddBody(carBody->GetID(), EActivation::Activate);
        entityBodyMap[entityID] = carBody->GetID();

        // 2. Define Wheels
        VehicleConstraintSettings settings;
        settings.mDrawConstraintSize = 0.1f;
        settings.mMaxPitchRollAngle = JPH::DegreesToRadians(60.0f);

        // FIX 2: Z-Up Wheel Offsets (X=Forward, Y=Side, Z=Up)
        Vec3 wheelOffsets[] = {
            Vec3( 1.6f,  1.0f, -0.2f), // Front Left
            Vec3( 1.6f, -1.0f, -0.2f), // Front Right
            Vec3(-1.6f,  1.0f, -0.2f), // Rear Left
            Vec3(-1.6f, -1.0f, -0.2f)  // Rear Right
        };

        for (int i = 0; i < 4; i++) {
            WheelSettingsWV* w = new WheelSettingsWV();
            w->mPosition = wheelOffsets[i];
            
            // --- FIX: Use Updated Jolt Variable Names ---
            w->mSuspensionDirection = Vec3(0, 0, -1); // Down (Z)
            w->mSteeringAxis = Vec3(0, 0, 1);         // Up (Z) - Axis used for steering rotation
            // Note: Jolt usually derives the axle from Cross(Suspension, Steering) or forward.
            // If mSteeringAxis doesn't exist, try just setting mSuspensionDirection.
            // --------------------------------------------

            w->mRadius = 0.4f; w->mWidth = 0.3f;
            w->mSuspensionMinLength = 0.3f; w->mSuspensionMaxLength = 0.7f;
            w->mMaxSteerAngle = (i < 2) ? JPH::DegreesToRadians(40.0f) : 0.0f;
            w->mSuspensionSpring.mFrequency = 2.0f; w->mSuspensionSpring.mDamping = 0.5f;
            settings.mWheels.push_back(w);
        }

        WheeledVehicleControllerSettings* controllerSettings = new WheeledVehicleControllerSettings();
        controllerSettings->mEngine.mMaxTorque = 500.0f;
        controllerSettings->mDifferentials.resize(1);
        controllerSettings->mDifferentials[0].mLeftWheel = 0; // Front Left
        controllerSettings->mDifferentials[0].mRightWheel = 1; // Front Right
        settings.mController = controllerSettings;

        VehicleConstraint* vehicle = new VehicleConstraint(*carBody, settings);
        vehicle->SetVehicleCollisionTester(new VehicleCollisionTesterRay(Layers::MOVING));
        
        physicsSystem->AddConstraint(vehicle);
        physicsSystem->AddStepListener(vehicle);

        return vehicle;
    }

    void Update(float deltaTime, std::vector<class CBaseEntity*>& entityList) {
        if (!physicsSystem) return;
        physicsSystem->Update(deltaTime, 1, tempAllocator, jobSystem);

        for (auto& pair : entityBodyMap) {
            int entID = pair.first;
            BodyID bodyID = pair.second;
            
            if (entID < (int)entityList.size() && entityList[entID]) {
                
                // --- FIX 4: The "Anti-Gravity" Static Lock ---
                if (bodyInterface->GetMotionType(bodyID) == EMotionType::Static) {
                    continue; // NEVER move static objects
                }
                // -------------------------------------------

                if (bodyInterface->IsActive(bodyID)) {
                    Vec3 pos = bodyInterface->GetPosition(bodyID);
                    entityList[entID]->origin = ToGlm(pos);
                    
                    Quat rot = bodyInterface->GetRotation(bodyID);
                    glm::quat glmRot(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
                    entityList[entID]->angles = glm::degrees(glm::eulerAngles(glmRot));
                }
            }
        }
    }

    static inline JPH::Vec3 ToJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
    static inline glm::vec3 ToGlm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
};