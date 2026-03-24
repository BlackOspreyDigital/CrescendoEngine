#pragma once

// 1. Standard / Engine Includes FIRST
#include "scene/BaseEntity.hpp"
#include "scene/Scene.hpp"
#include "scene/components/ProceduralPlanetComponent.hpp"
#include "modules/terrain/VoxelGenerator.hpp"
#include "servers/camera/Camera.hpp"

#include "servers/rendering/Vertex.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <unordered_map>


// 2. The Core Jolt Header MUST BE HERE
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>

// 3. All other Jolt Headers go AFTER Jolt.h
#include <Jolt/Math/Float3.h> // <--- Moved down!
#include <Jolt/Geometry/Triangle.h> // <--- Added to fix 'VertexList'
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h> 
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

// Vehicle Headers
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>

using namespace JPH;

namespace Crescendo {

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

    uint32_t CreateTerrainCollider(const std::vector<float>& verts, const std::vector<uint32_t>& inds, const glm::vec3& chunkOrigin, int stride) {
        if (!bodyInterface || verts.empty() || inds.empty()) return 0;

        JPH::VertexList joltVerts;
        joltVerts.reserve(verts.size() / stride);
        
        // Build the vertices using the dynamic stride
        for(size_t i = 0; i < verts.size(); i += stride) {
            joltVerts.push_back(JPH::Float3(verts[i] + chunkOrigin.x, verts[i+1] + chunkOrigin.y, verts[i+2] + chunkOrigin.z));
        }

        JPH::IndexedTriangleList joltInds;
        joltInds.reserve(inds.size() / 3);
        for (size_t i = 0; i < inds.size(); i += 3) {
            joltInds.push_back(JPH::IndexedTriangle(inds[i], inds[i+1], inds[i+2]));
        }

        JPH::MeshShapeSettings meshSettings(joltVerts, joltInds);
        JPH::ShapeSettings::ShapeResult result = meshSettings.Create();
        
        if (result.HasError()) {
            std::cerr << "[Physics] Failed to create Terrain Collider: " << result.GetError().c_str() << std::endl;
            return 0;
        }

        BodyCreationSettings settings(result.Get(), JPH::Vec3::sZero(), JPH::Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
        Body* body = bodyInterface->CreateBody(settings);
        
        if (body) {
            bodyInterface->AddBody(body->GetID(), EActivation::DontActivate);
            return body->GetID().GetIndexAndSequenceNumber();
        }
        return 0;
    }

    BodyInterface* bodyInterface = nullptr;
    std::unordered_map<int, BodyID> entityBodyMap;

    void Initialize() {
        std::cout << "[Physics] Initializing Jolt (Z-Up Mode)..." << std::endl;
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();

        tempAllocator = new TempAllocatorImpl(10 * 1024 * 1024);
        
        // --- THE FIX: Lock Jolt to exactly 4 cores ---
        jobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, 4);

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

    void CreateMeshCollider(int entityID, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, glm::vec3 pos, glm::vec3 scale) {
        if (!bodyInterface) {
            std::cerr << "!! CRITICAL: BodyInterface is NULL" << std::endl; 
            return; 
        }

        // 1. Convert our Vulkan Vertices into Jolt's Float3 format, applying the model's scale
        JPH::VertexList joltVertices;
        joltVertices.reserve(vertices.size());
        for (const auto& v : vertices) {
            joltVertices.push_back(JPH::Float3(v.pos.x * scale.x, v.pos.y * scale.y, v.pos.z * scale.z));
        }

        // 2. Convert our raw index buffer into Jolt's IndexedTriangle format
        JPH::IndexedTriangleList joltIndices;
        joltIndices.reserve(indices.size() / 3);
        for (size_t i = 0; i < indices.size(); i += 3) {
            joltIndices.push_back(JPH::IndexedTriangle(indices[i], indices[i+1], indices[i+2]));
        }

        // 3. Bake the Mesh Shape
        JPH::MeshShapeSettings meshSettings(joltVertices, joltIndices);
        JPH::ShapeSettings::ShapeResult result = meshSettings.Create();
        
        if (result.HasError()) {
            std::cerr << "[Physics] Failed to create Mesh Collider: " << result.GetError().c_str() << std::endl;
            return;
        }

        // 4. Attach it to a Static Body
        BodyCreationSettings settings(
            result.Get(), 
            ToJolt(pos), 
            JPH::Quat::sIdentity(), 
            EMotionType::Static, 
            Layers::NON_MOVING
        );

        // ==========================================
        // --- SURF PHYSICS SAUCE ---
        // ==========================================
        settings.mFriction = 0.0f;     // Absolute zero friction so the capsule glides!
        settings.mRestitution = 0.0f;  // No bouncing off ramps
        // ==========================================

        Body* body = bodyInterface->CreateBody(settings);
        if (!body) {
            std::cerr << "[Physics] Failed to create Mesh Body for ID " << entityID << std::endl;
            return;
        }
        
        bodyInterface->AddBody(body->GetID(), EActivation::DontActivate);
        entityBodyMap[entityID] = body->GetID();
        
        std::cout << "[Physics] Mesh Collider generated for Entity " << entityID << " (" << joltIndices.size() << " triangles)" << std::endl;
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

    // Change return type to JPH::Body*
    JPH::Body* CreateChassisBody(int entityID, glm::vec3 position) {
        std::cout << "[Physics] Creating Car Chassis Body (Z-Up)..." << std::endl;
        if (!bodyInterface) return nullptr;

        // SLIMMED DOWN COLLISION: 
        // 1.2m wide, 2.6m long, 0.2m tall
        Ref<BoxShape> chassisShape = new BoxShape(Vec3(0.6f, 1.3f, 0.1f)); 
        
        // Slightly reduced the center of mass offset to match the thinner box
        Ref<OffsetCenterOfMassShape> offsetShape = new OffsetCenterOfMassShape(chassisShape, Vec3(0, 0, -0.2f));

        BodyCreationSettings carBodySettings(offsetShape, ToJolt(position), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
        carBodySettings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        carBodySettings.mMassPropertiesOverride.mMass = 250.0f; 
        
        Body* carBody = bodyInterface->CreateBody(carBodySettings);
        if (!carBody) return nullptr;
        
        bodyInterface->AddBody(carBody->GetID(), EActivation::Activate);
        entityBodyMap[entityID] = carBody->GetID();
        
        // Return the raw pointer instead of the ID
        return carBody;
    }

    void ResetBody(int entityID, glm::vec3 pos, glm::vec3 rot) {
        if (!bodyInterface) return;
        
        if (entityBodyMap.find(entityID) != entityBodyMap.end()) {
            JPH::BodyID id = entityBodyMap[entityID];
            
            // 1. Stop all movement
            bodyInterface->SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
            
            // 2. Teleport back to spawn
            glm::quat q = glm::quat(glm::radians(rot)); // Convert Euler angles to Quaternion
            bodyInterface->SetPositionAndRotation(id, ToJolt(pos), JPH::Quat(q.x, q.y, q.z, q.w), JPH::EActivation::Activate);
        }
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

    // ---------------------------------------------------------
    // PLANETARY GRAVITY & GROUND SNAPPING
    // ---------------------------------------------------------
    void ApplyPlanetaryGravityToCamera(Crescendo::Camera& cam, Crescendo::CBaseEntity* planet) {
        if (!planet || !planet->HasComponent<Crescendo::ProceduralPlanetComponent>()) return;
        
        auto planetComp = planet->GetComponent<Crescendo::ProceduralPlanetComponent>();

        // 1. Calculate the vector from the Planet's Core to the Camera
        glm::vec3 toCam = cam.Position - planet->origin;
        float distanceToCore = glm::length(toCam);
        if (distanceToCore == 0.0f) return;

        // 2. Determine "Up" (Away from the core)
        glm::vec3 upDir = toCam / distanceToCore;

        // 3. Align the Camera so you can walk completely around the sphere!
        cam.WorldUp = upDir;

        // 4. Sample the Voxel Density at the camera's exact coordinates
        // We check 2.0 units below the camera to act as the player's "legs"
        glm::vec3 feetPos = toCam - (upDir * 2.0f); 
        float density = Crescendo::Terrain::VoxelGenerator::EvaluateDensity(feetPos, planetComp->settings);

        // 5. Ground Snapping
        if (density > 0.0f) {
            // We are UNDERGROUND! Push the camera up out of the dirt.
            cam.Position += upDir * (density + 0.1f);
        } 
        else if (density > -1.5f) {
            // We are IN THE AIR but very close to the ground. Apply gravity!
            cam.Position -= upDir * 0.15f; 
        }
    }

    // ---------------------------------------------------------
    // DYNAMIC CAMERA CONTROLLER
    // ---------------------------------------------------------
    void UpdateCameraPhysics(Crescendo::Camera& cam, Crescendo::Scene* scene) {
        // ... (This is your existing function, it stays exactly the same!)
        if (!scene) return;

        Crescendo::CBaseEntity* closestPlanet = nullptr;
        float minDistance = std::numeric_limits<float>::max();

        // 1. Scan the scene dynamically for planets
        for (auto* ent : scene->entities) {
            if (ent && ent->HasComponent<Crescendo::ProceduralPlanetComponent>()) {
                float dist = glm::length(cam.Position - ent->origin);
                if (dist < minDistance) {
                    minDistance = dist;
                    closestPlanet = ent;
                }
            }
        }

        // 2. If a planet exists, let its gravity take over!
        if (closestPlanet) {
            ApplyPlanetaryGravityToCamera(cam, closestPlanet);
            
            // Dynamically scale speed based on this specific planet's radius!
            auto planetComp = closestPlanet->GetComponent<Crescendo::ProceduralPlanetComponent>();
            float alt = glm::length(cam.Position - closestPlanet->origin) - planetComp->settings.radius;
            
            // Slower near the dirt, screaming fast in orbit
            cam.MovementSpeed = glm::clamp(alt * 0.1f, 10.0f, 1000.0f);
        } else {
            // 3. No planets in the scene? Default to standard free-cam flight.
            cam.MovementSpeed = 50.0f;
            cam.WorldUp = glm::vec3(0.0f, 0.0f, 1.0f); // Default Z-Up
        }
    }

    static inline JPH::Vec3 ToJolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
    static inline glm::vec3 ToGlm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
};
}