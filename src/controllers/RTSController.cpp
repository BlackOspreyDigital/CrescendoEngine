#include "RTSController.hpp"
#include "core/Input.hpp"
#include "core/EnginePaths.hpp"
#include "deps/json/json.hpp"
#include "servers/interface/SymbolServer.hpp"
#include <imgui.h>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace Crescendo {

    static inline float GetUnitRadius(UnitType type) {
        switch (type) {
            case UnitType::Tank: return 2.2f;
            case UnitType::Plane: return 2.0f;
            case UnitType::CommandMech: return 3.2f;
            case UnitType::Factory: return 6.0f;
            case UnitType::Turret: return 2.2f;
            case UnitType::LandFactory: return 5.5f;
            case UnitType::AirFactory: return 5.5f;
            case UnitType::Engineer: return 1.8f;
            case UnitType::MassGen: return 3.2f;
            case UnitType::EnergyGen: return 3.2f;
            default: return 2.0f;
        }
    }

    static inline bool IsAir(UnitType type) {
        return (type == UnitType::Plane);
    }

    static inline bool IsStaticBuilding(UnitType type) {
        return (type == UnitType::Factory || type == UnitType::LandFactory || type == UnitType::AirFactory ||
                type == UnitType::Turret || type == UnitType::MassGen || type == UnitType::EnergyGen);
    }


    // 24-vertex flat-shaded cube generator with outward normals and CCW winding
    static void GetCubeData(std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        struct Face {
            glm::vec3 normal;
            glm::vec3 p0, p1, p2, p3;
        };

        Face faces[6] = {
            // Top (+Z)
            { { 0,  0,  1}, {-0.5, -0.5,  0.5}, { 0.5, -0.5,  0.5}, { 0.5,  0.5,  0.5}, {-0.5,  0.5,  0.5} },
            // Bottom (-Z)
            { { 0,  0, -1}, { 0.5, -0.5, -0.5}, {-0.5, -0.5, -0.5}, {-0.5,  0.5, -0.5}, { 0.5,  0.5, -0.5} },
            // Front (+Y)
            { { 0,  1,  0}, { 0.5,  0.5, -0.5}, {-0.5,  0.5, -0.5}, {-0.5,  0.5,  0.5}, { 0.5,  0.5,  0.5} },
            // Back (-Y)
            { { 0, -1,  0}, {-0.5, -0.5, -0.5}, { 0.5, -0.5, -0.5}, { 0.5, -0.5,  0.5}, {-0.5, -0.5,  0.5} },
            // Right (+X)
            { { 1,  0,  0}, { 0.5, -0.5, -0.5}, { 0.5,  0.5, -0.5}, { 0.5,  0.5,  0.5}, { 0.5, -0.5,  0.5} },
            // Left (-X)
            { {-1,  0,  0}, {-0.5,  0.5, -0.5}, {-0.5, -0.5, -0.5}, {-0.5, -0.5,  0.5}, {-0.5,  0.5,  0.5} }
        };

        for (int f = 0; f < 6; f++) {
            uint32_t baseIdx = (uint32_t)verts.size();
            
            Vertex v0{}, v1{}, v2{}, v3{};
            v0.pos = faces[f].p0; v0.normal = faces[f].normal; v0.color = glm::vec3(1.0f); v0.texCoord = glm::vec2(0, 0);
            v1.pos = faces[f].p1; v1.normal = faces[f].normal; v1.color = glm::vec3(1.0f); v1.texCoord = glm::vec2(1, 0);
            v2.pos = faces[f].p2; v2.normal = faces[f].normal; v2.color = glm::vec3(1.0f); v2.texCoord = glm::vec2(1, 1);
            v3.pos = faces[f].p3; v3.normal = faces[f].normal; v3.color = glm::vec3(1.0f); v3.texCoord = glm::vec2(0, 1);

            verts.push_back(v0);
            verts.push_back(v1);
            verts.push_back(v2);
            verts.push_back(v3);

            inds.push_back(baseIdx + 0);
            inds.push_back(baseIdx + 1);
            inds.push_back(baseIdx + 2);

            inds.push_back(baseIdx + 0);
            inds.push_back(baseIdx + 2);
            inds.push_back(baseIdx + 3);
        }
    }

    static void GetCylinderData(std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        int segments = 16;
        float radius = 0.5f;
        float halfHeight = 0.5f;

        // Side wall
        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / (float)segments * 2.0f * 3.14159265358979323846f;
            float x = cos(theta) * radius;
            float y = sin(theta) * radius;

            Vertex v1{}, v2{};
            v1.pos = glm::vec3(x, y, -halfHeight);
            v1.normal = glm::vec3(cos(theta), sin(theta), 0.0f);
            v1.texCoord = glm::vec2((float)i / segments, 0.0f);
            v1.color = glm::vec3(1.0f);

            v2.pos = glm::vec3(x, y, halfHeight);
            v2.normal = glm::vec3(cos(theta), sin(theta), 0.0f);
            v2.texCoord = glm::vec2((float)i / segments, 1.0f);
            v2.color = glm::vec3(1.0f);

            verts.push_back(v1);
            verts.push_back(v2);
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t base = i * 2;
            inds.push_back(base + 0); inds.push_back(base + 2); inds.push_back(base + 1);
            inds.push_back(base + 1); inds.push_back(base + 2); inds.push_back(base + 3);
        }

        // Top cap (+Z)
        uint32_t topCenterIdx = (uint32_t)verts.size();
        Vertex vTopCenter{};
        vTopCenter.pos = glm::vec3(0.0f, 0.0f, halfHeight);
        vTopCenter.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        vTopCenter.texCoord = glm::vec2(0.5f, 0.5f);
        vTopCenter.color = glm::vec3(1.0f);
        verts.push_back(vTopCenter);

        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / (float)segments * 2.0f * 3.14159265358979323846f;
            Vertex v{};
            v.pos = glm::vec3(cos(theta) * radius, sin(theta) * radius, halfHeight);
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            v.texCoord = glm::vec2(0.5f + cos(theta) * 0.5f, 0.5f + sin(theta) * 0.5f);
            v.color = glm::vec3(1.0f);
            verts.push_back(v);
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t curr = topCenterIdx + 1 + i;
            uint32_t next = topCenterIdx + 1 + (i + 1);
            inds.push_back(topCenterIdx);
            inds.push_back(curr);
            inds.push_back(next);
        }

        // Bottom cap (-Z)
        uint32_t botCenterIdx = (uint32_t)verts.size();
        Vertex vBotCenter{};
        vBotCenter.pos = glm::vec3(0.0f, 0.0f, -halfHeight);
        vBotCenter.normal = glm::vec3(0.0f, 0.0f, -1.0f);
        vBotCenter.texCoord = glm::vec2(0.5f, 0.5f);
        vBotCenter.color = glm::vec3(1.0f);
        verts.push_back(vBotCenter);

        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / (float)segments * 2.0f * 3.14159265358979323846f;
            Vertex v{};
            v.pos = glm::vec3(cos(theta) * radius, sin(theta) * radius, -halfHeight);
            v.normal = glm::vec3(0.0f, 0.0f, -1.0f);
            v.texCoord = glm::vec2(0.5f + cos(theta) * 0.5f, 0.5f + sin(theta) * 0.5f);
            v.color = glm::vec3(1.0f);
            verts.push_back(v);
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t curr = botCenterIdx + 1 + i;
            uint32_t next = botCenterIdx + 1 + (i + 1);
            inds.push_back(botCenterIdx);
            inds.push_back(next);
            inds.push_back(curr);
        }
    }

    static void GetSphereData(std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        int stacks = 10, slices = 10;
        for (int i = 0; i <= stacks; ++i) {
            float v = (float)i / (float)stacks;
            float phi = v * 3.14159265358979323846f;
            for (int j = 0; j <= slices; ++j) {
                float u = (float)j / (float)slices;
                float theta = u * 2.0f * 3.14159265358979323846f;
                float x = cos(theta) * sin(phi);
                float y = sin(theta) * sin(phi);
                float z = cos(phi);
                Vertex vx{};
                vx.pos = glm::vec3(x, y, z) * 0.5f;
                vx.color = glm::vec3(1.0f);
                vx.normal = glm::vec3(x, y, z);
                vx.texCoord = glm::vec2(u, v);
                verts.push_back(vx);
            }
        }
        for (int i = 0; i < stacks; ++i) {
            for (int j = 0; j < slices; ++j) {
                int first = (i * (slices + 1)) + j;
                int second = first + slices + 1;
                inds.push_back(first); inds.push_back(second); inds.push_back(first + 1);
                inds.push_back(second); inds.push_back(second + 1); inds.push_back(first + 1);
            }
        }
    }

    static void GetRingData(std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        int segments = 40;
        float rInner = 0.82f;
        float rOuter = 1.0f;
        float halfHeight = 0.04f;

        // Top surface (+Z)
        uint32_t topBase = (uint32_t)verts.size();
        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / (float)segments * 2.0f * 3.14159265358979323846f;
            float ct = cos(theta);
            float st = sin(theta);

            Vertex vIn{}, vOut{};
            vIn.pos = glm::vec3(ct * rInner, st * rInner, halfHeight);
            vIn.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            vIn.color = glm::vec3(0.0f, 1.0f, 0.6f);
            vIn.texCoord = glm::vec2(ct * 0.5f + 0.5f, st * 0.5f + 0.5f);

            vOut.pos = glm::vec3(ct * rOuter, st * rOuter, halfHeight);
            vOut.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            vOut.color = glm::vec3(0.0f, 1.0f, 0.6f);
            vOut.texCoord = glm::vec2(ct * 0.5f + 0.5f, st * 0.5f + 0.5f);

            verts.push_back(vIn);
            verts.push_back(vOut);
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t b = topBase + i * 2;
            inds.push_back(b + 0); inds.push_back(b + 1); inds.push_back(b + 3);
            inds.push_back(b + 0); inds.push_back(b + 3); inds.push_back(b + 2);
        }

        // Bottom surface (-Z)
        uint32_t botBase = (uint32_t)verts.size();
        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / (float)segments * 2.0f * 3.14159265358979323846f;
            float ct = cos(theta);
            float st = sin(theta);

            Vertex vIn{}, vOut{};
            vIn.pos = glm::vec3(ct * rInner, st * rInner, -halfHeight);
            vIn.normal = glm::vec3(0.0f, 0.0f, -1.0f);
            vIn.color = glm::vec3(0.0f, 1.0f, 0.6f);
            vIn.texCoord = glm::vec2(ct * 0.5f + 0.5f, st * 0.5f + 0.5f);

            vOut.pos = glm::vec3(ct * rOuter, st * rOuter, -halfHeight);
            vOut.normal = glm::vec3(0.0f, 0.0f, -1.0f);
            vOut.color = glm::vec3(0.0f, 1.0f, 0.6f);
            vOut.texCoord = glm::vec2(ct * 0.5f + 0.5f, st * 0.5f + 0.5f);

            verts.push_back(vIn);
            verts.push_back(vOut);
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t b = botBase + i * 2;
            inds.push_back(b + 0); inds.push_back(b + 3); inds.push_back(b + 1);
            inds.push_back(b + 0); inds.push_back(b + 2); inds.push_back(b + 3);
        }
    }

    RTSController::~RTSController() {
        Cleanup();
    }

    void RTSController::Initialize(Scene* scene, PhysicsServer* physicsServer, IRenderer* renderer, glm::vec3 spawnPos) {
        m_scene = scene;
        m_physicsServer = physicsServer;
        m_renderer = renderer;

        if (renderer) {
            std::vector<Vertex> cubeVerts; std::vector<uint32_t> cubeInds;
            GetCubeData(cubeVerts, cubeInds);
            m_cubeMeshIndex = renderer->acquireMesh("PROCEDURAL", "RTS_UnitCube", cubeVerts, cubeInds);

            std::vector<Vertex> sphereVerts; std::vector<uint32_t> sphereInds;
            GetSphereData(sphereVerts, sphereInds);
            m_sphereMeshIndex = renderer->acquireMesh("PROCEDURAL", "RTS_UnitSphere", sphereVerts, sphereInds);

            std::vector<Vertex> cylVerts; std::vector<uint32_t> cylInds;
            GetCylinderData(cylVerts, cylInds);
            m_cylinderMeshIndex = renderer->acquireMesh("PROCEDURAL", "RTS_UnitCylinder", cylVerts, cylInds);

            std::vector<Vertex> ringVerts; std::vector<uint32_t> ringInds;
            GetRingData(ringVerts, ringInds);
            m_ringMeshIndex = renderer->acquireMesh("PROCEDURAL", "RTS_SelectionRing", ringVerts, ringInds);

            // Load Cubemap Sky and activate Image-Based Lighting
            renderer->loadSkybox("assets/skies/rts_sky.hdr", scene);
            if (scene) {
                scene->environment.skyType = SkyType::HDRMap;
                scene->environment.ambientIntensity = 1.35f;
            }
        }

        // Spawn Ground Floor
        CBaseEntity* floor = scene->CreateEntity("prop_dynamic");
        floor->targetName = "BaseFloor";
        floor->modelIndex = m_cubeMeshIndex;
        floor->origin = glm::dvec3(0.0, 0.0, -1.0);
        floor->scale = glm::vec3(300.0f, 300.0f, 1.0f);
        floor->albedoColor = glm::vec3(0.15f, 0.35f, 0.15f);
        floor->roughness = 1.0f;
        floor->metallic = 0.0f;

        // Create Skinny Emissive Red and Blue Cylinder Rally Point Markers
        m_playerRallyPos = glm::vec3(-50.0f, 0.0f, 0.0f);
        m_enemyRallyPos = glm::vec3(50.0f, 0.0f, 0.0f);

        m_playerRallyEntity = scene->CreateEntity("prop_dynamic");
        m_playerRallyEntity->targetName = "PlayerRallyBeacon";
        m_playerRallyEntity->origin = glm::dvec3(m_playerRallyPos.x, m_playerRallyPos.y, 4.0);
        m_playerRallyEntity->scale = glm::vec3(0.35f, 0.35f, 8.0f);
        m_playerRallyEntity->modelIndex = m_cylinderMeshIndex;
        m_playerRallyEntity->albedoColor = glm::vec3(0.1f, 0.5f, 1.0f);
        m_playerRallyEntity->emission = 6.0f;
        m_playerRallyEntity->roughness = 0.2f;

        m_enemyRallyEntity = scene->CreateEntity("prop_dynamic");
        m_enemyRallyEntity->targetName = "EnemyRallyBeacon";
        m_enemyRallyEntity->origin = glm::dvec3(m_enemyRallyPos.x, m_enemyRallyPos.y, 4.0);
        m_enemyRallyEntity->scale = glm::vec3(0.35f, 0.35f, 8.0f);
        m_enemyRallyEntity->modelIndex = m_cylinderMeshIndex;
        m_enemyRallyEntity->albedoColor = glm::vec3(1.0f, 0.15f, 0.1f);
        m_enemyRallyEntity->emission = 6.0f;
        m_enemyRallyEntity->roughness = 0.2f;

        // Create pool of 3D Selection Ring Entities
        m_selectionRingEntities.clear();
        for (int i = 0; i < 32; i++) {
            CBaseEntity* ringEnt = scene->CreateEntity("prop_dynamic");
            ringEnt->targetName = "SelectionRing";
            ringEnt->modelIndex = m_ringMeshIndex;
            ringEnt->origin = glm::dvec3(0.0, 0.0, -999.0);
            ringEnt->scale = glm::vec3(0.0f);
            ringEnt->albedoColor = glm::vec3(0.0f, 1.0f, 0.6f);
            ringEnt->emission = 6.0f;
            ringEnt->roughness = 0.2f;
            m_selectionRingEntities.push_back(ringEnt);
        }

        // Create pool of Projectile Entities (Tank shells & Airplane rockets)
        m_projectiles.clear();
        for (int i = 0; i < 32; i++) {
            RTSProjectile p;
            p.active = false;
            p.entity = scene->CreateEntity("prop_dynamic");
            p.entity->targetName = "RTS_Projectile";
            p.entity->modelIndex = m_cubeMeshIndex;
            p.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
            p.entity->scale = glm::vec3(0.0f);
            p.entity->albedoColor = glm::vec3(1.0f, 0.95f, 0.1f);
            p.entity->emission = 14.0f;
            p.entity->roughness = 0.2f;
            m_projectiles.push_back(p);
        }

        // Create pool of Emissive Explosion Sphere Cluster Entities
        m_explosionSpheres.clear();
        for (int i = 0; i < 64; i++) {
            RTSExplosionSphere s;
            s.active = false;
            s.entity = scene->CreateEntity("prop_dynamic");
            s.entity->targetName = "RTS_Explosion";
            s.entity->modelIndex = m_sphereMeshIndex;
            s.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
            s.entity->scale = glm::vec3(0.0f);
            s.entity->albedoColor = glm::vec3(1.0f, 0.9f, 0.1f);
            s.entity->emission = 14.0f;
            s.entity->roughness = 0.2f;
            m_explosionSpheres.push_back(s);
        }

        // Create Building Placement Ghost Entity
        m_buildGhostEntity = scene->CreateEntity("prop_dynamic");
        m_buildGhostEntity->targetName = "BuildGhost";
        m_buildGhostEntity->modelIndex = m_cubeMeshIndex;
        m_buildGhostEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
        m_buildGhostEntity->scale = glm::vec3(0.0f);
        m_buildGhostEntity->albedoColor = glm::vec3(0.2f, 0.95f, 0.4f);
        m_buildGhostEntity->emission = 4.0f;
        m_buildGhostEntity->roughness = 0.2f;

        // Create Construction Beam Entity
        m_buildBeamEntity = scene->CreateEntity("prop_dynamic");
        m_buildBeamEntity->targetName = "BuildBeam";
        m_buildBeamEntity->modelIndex = m_cylinderMeshIndex;
        m_buildBeamEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
        m_buildBeamEntity->scale = glm::vec3(0.0f);
        m_buildBeamEntity->albedoColor = glm::vec3(0.2f, 0.95f, 1.0f);
        m_buildBeamEntity->emission = 12.0f;
        m_buildBeamEntity->roughness = 0.1f;

        // Create Green Resource Deposit Nodes (embedded in the ground)
        m_resourceNodes.clear();
        glm::vec3 depositPositions[] = {
            glm::vec3(-95.0f,  18.0f, -0.45f),
            glm::vec3(-95.0f, -18.0f, -0.45f),
            glm::vec3(-60.0f,  35.0f, -0.45f),
            glm::vec3(-60.0f, -35.0f, -0.45f),
            glm::vec3(  0.0f,  50.0f, -0.45f),
            glm::vec3(  0.0f, -50.0f, -0.45f),
            glm::vec3(  0.0f,   0.0f, -0.45f),
            glm::vec3( 95.0f,  18.0f, -0.45f),
            glm::vec3( 95.0f, -18.0f, -0.45f),
            glm::vec3( 60.0f,  35.0f, -0.45f),
            glm::vec3( 60.0f, -35.0f, -0.45f)
        };

        for (const auto& dPos : depositPositions) {
            ResourceDepositNode node;
            node.position = dPos;
            node.occupied = false;
            node.entity = scene->CreateEntity("prop_dynamic");
            node.entity->targetName = "ResourceDepositCube";
            node.entity->modelIndex = m_cubeMeshIndex;
            node.entity->origin = glm::dvec3(dPos.x, dPos.y, dPos.z);
            node.entity->scale = glm::vec3(3.8f, 3.8f, 0.2f);
            node.entity->albedoColor = glm::vec3(0.1f, 0.95f, 0.3f);
            node.entity->emission = 3.5f;
            node.entity->roughness = 0.2f;
            m_resourceNodes.push_back(node);
        }

        // Start the Skirmish match!
        StartNewSkirmish();
    }

    void RTSController::StartNewSkirmish() {
        if (!m_scene) return;

        // 1. Recycle existing unit entities
        for (auto& u : m_units) {
            if (u.entity) {
                u.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                u.entity->scale = glm::vec3(0.0f);
                m_deadUnitEntities.push_back(u.entity);
            }
        }
        m_units.clear();

        // 2. Hide projectiles and explosions
        for (auto& p : m_projectiles) {
            p.active = false;
            if (p.entity) {
                p.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                p.entity->scale = glm::vec3(0.0f);
            }
        }
        for (auto& s : m_explosionSpheres) {
            s.active = false;
            if (s.entity) {
                s.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                s.entity->scale = glm::vec3(0.0f);
            }
        }
        for (auto* ring : m_selectionRingEntities) {
            if (ring) {
                ring->origin = glm::dvec3(0.0, 0.0, -999.0);
                ring->scale = glm::vec3(0.0f);
            }
        }
        if (m_buildBeamEntity) {
            m_buildBeamEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
            m_buildBeamEntity->scale = glm::vec3(0.0f);
        }

        // 3. Economy & Resources from SkirmishSettings
        playerMass = (float)m_skirmishSettings.startingMass;
        playerEnergy = (float)m_skirmishSettings.startingEnergy;
        enemyMass = (float)m_skirmishSettings.startingMass;
        enemyEnergy = (float)m_skirmishSettings.startingEnergy;
        m_boostTimer = 0.0f;

        // 4. Rally Points
        m_playerRallyPos = glm::vec3(-50.0f, 0.0f, 0.0f);
        m_enemyRallyPos = glm::vec3(50.0f, 0.0f, 0.0f);
        if (m_playerRallyEntity) m_playerRallyEntity->origin = glm::dvec3(m_playerRallyPos.x, m_playerRallyPos.y, 4.0);
        if (m_enemyRallyEntity) m_enemyRallyEntity->origin = glm::dvec3(m_enemyRallyPos.x, m_enemyRallyPos.y, 4.0);

        // 5. Spawn Bases & Economy Structures (Mass Gens exactly on green resource nodes at X = -95 and 95)
        // Player Base & Resource Generators
        SpawnUnit(m_scene, m_physicsServer, UnitType::Factory, Team::Player, glm::vec3(-80.0f, 0.0f, 2.0f), glm::vec3(8.0f, 8.0f, 4.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::MassGen, Team::Player, glm::vec3(-95.0f, 18.0f, 1.5f), glm::vec3(4.0f, 4.0f, 3.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::EnergyGen, Team::Player, glm::vec3(-80.0f, -18.0f, 2.0f), glm::vec3(4.0f, 4.0f, 4.0f));

        // Enemy Base & Resource Generators
        SpawnUnit(m_scene, m_physicsServer, UnitType::Factory, Team::Enemy, glm::vec3(80.0f, 0.0f, 2.0f), glm::vec3(8.0f, 8.0f, 4.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::MassGen, Team::Enemy, glm::vec3(95.0f, 18.0f, 1.5f), glm::vec3(4.0f, 4.0f, 3.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::EnergyGen, Team::Enemy, glm::vec3(80.0f, -18.0f, 2.0f), glm::vec3(4.0f, 4.0f, 4.0f));

        // 6. Spawn Starting Forces based on settings
        int tankCount = std::clamp(m_skirmishSettings.initialTanks, 1, 8);
        for (int i = 0; i < tankCount; i++) {
            float yOffset = ((float)i - (float)(tankCount - 1) * 0.5f) * 6.0f;
            SpawnUnit(m_scene, m_physicsServer, UnitType::Tank, Team::Player, glm::vec3(-65.0f, yOffset, 0.0f));
            SpawnUnit(m_scene, m_physicsServer, UnitType::Tank, Team::Enemy, glm::vec3(65.0f, yOffset, 0.0f));
        }

        int planeCount = std::clamp(m_skirmishSettings.initialPlanes, 0, 4);
        for (int i = 0; i < planeCount; i++) {
            float yOffset = ((float)i - (float)(planeCount - 1) * 0.5f) * 8.0f;
            SpawnUnit(m_scene, m_physicsServer, UnitType::Plane, Team::Player, glm::vec3(-70.0f, yOffset, 12.0f));
            SpawnUnit(m_scene, m_physicsServer, UnitType::Plane, Team::Enemy, glm::vec3(70.0f, yOffset, 12.0f));
        }

        // Spawn starting Engineers
        SpawnUnit(m_scene, m_physicsServer, UnitType::Engineer, Team::Player, glm::vec3(-68.0f, -12.0f, 0.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::Engineer, Team::Enemy, glm::vec3(68.0f, -12.0f, 0.0f));

        // Spawn Armored Command Mechs (SupCom FAF ACU)
        SpawnUnit(m_scene, m_physicsServer, UnitType::CommandMech, Team::Player, glm::vec3(-55.0f, 0.0f, 2.6f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::CommandMech, Team::Enemy, glm::vec3(55.0f, 0.0f, 2.6f));

        // Reset resource node occupation states
        for (auto& node : m_resourceNodes) {
            node.occupied = false;
            if (glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(-95.0f, 18.0f)) < 3.0f ||
                glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(95.0f, 18.0f)) < 3.0f) {
                node.occupied = true;
            }
        }

        // 7. Reset Camera & Match State
        m_cameraTarget = glm::vec3(-50.0f, 0.0f, 0.0f);
        m_cameraZoom = 70.0f;
        m_aiTimer = 0.0f;
        m_resourceTimer = 0.0f;
        m_matchTime = 0.0f;
        gameOver = false;
        m_playerWon = false;
        m_showSkirmishMenu = false;
        gameMessage = (m_skirmishSettings.gameMode == 0) ? "Annihilation Mode: Destroy all enemy forces and structures!" :
                      ((m_skirmishSettings.gameMode == 1) ? "Assassination Mode: Destroy the enemy Command Center!" :
                       "Commander Mode (SupCom FAF): Protect your Command Mech & eliminate the enemy Commander!");
    }

    void RTSController::SpawnExplosionCluster(const glm::vec3& pos, float scaleMultiplier, int sphereCount) {
        int spawned = 0;
        for (auto& s : m_explosionSpheres) {
            if (!s.active && s.entity) {
                s.active = true;
                s.center = pos;
                s.age = 0.0f;
                s.maxLife = 0.32f + (float)(rand() % 15) * 0.01f;

                if (spawned == 0) {
                    // Central core burst
                    s.offset = glm::vec3(0.0f, 0.0f, 0.4f * scaleMultiplier);
                    s.velocity = glm::vec3(0.0f, 0.0f, 1.5f);
                    s.peakScale = 2.4f * scaleMultiplier;
                    s.color = glm::vec3(1.0f, 0.95f, 0.25f);
                } else {
                    // Satellite spheres bursting outwards in cluster
                    float theta = (float)(rand() % 360) * 0.0174533f;
                    float speed = 3.0f + (float)(rand() % 40) * 0.1f;
                    s.offset = glm::vec3(cos(theta) * 0.4f, sin(theta) * 0.4f, (float)(rand() % 10) * 0.08f);
                    s.velocity = glm::vec3(cos(theta) * speed, sin(theta) * speed, 1.2f + (float)(rand() % 25) * 0.1f);
                    s.peakScale = (1.1f + (float)(rand() % 8) * 0.1f) * scaleMultiplier;
                    s.color = glm::vec3(1.0f, 0.75f + (float)(rand() % 20) * 0.01f, 0.1f);
                }

                s.entity->origin = glm::dvec3(pos.x, pos.y, pos.z);
                s.entity->scale = glm::vec3(0.1f);
                s.entity->albedoColor = s.color;
                s.entity->emission = 14.0f;

                spawned++;
                if (spawned >= sphereCount) break;
            }
        }
    }

    void RTSController::FireProjectile(ProjectileType type, Team team, const glm::vec3& startPos, const glm::vec3& targetPos, float damage) {
        for (auto& p : m_projectiles) {
            if (!p.active && p.entity) {
                p.active = true;
                p.type = type;
                p.team = team;
                p.position = startPos;
                p.targetPos = targetPos;
                p.damage = damage;
                p.life = 0.0f;
                p.maxLife = 3.5f;

                glm::vec3 diff = targetPos - startPos;
                float dist = glm::length(diff);
                glm::vec3 dir = (dist > 0.001f) ? (diff / dist) : glm::vec3(1, 0, 0);

                float speed = (type == ProjectileType::Rocket) ? 45.0f : 65.0f;
                p.velocity = dir * speed;

                // Orient projectile toward target
                float yaw = glm::degrees(atan2(dir.y, dir.x));
                float pitch = glm::degrees(asin(std::clamp(dir.z, -1.0f, 1.0f)));
                p.entity->angles = glm::vec3(pitch, 0.0f, yaw - 90.0f);

                if (type == ProjectileType::Rocket) {
                    p.entity->scale = glm::vec3(0.35f, 1.6f, 0.35f);
                    p.entity->albedoColor = glm::vec3(1.0f, 0.95f, 0.15f);
                    p.entity->emission = 12.0f;
                } else {
                    p.entity->scale = glm::vec3(0.4f, 0.8f, 0.4f);
                    p.entity->albedoColor = glm::vec3(1.0f, 0.9f, 0.2f);
                    p.entity->emission = 14.0f;
                }

                p.entity->origin = glm::dvec3(startPos.x, startPos.y, startPos.z);
                break;
            }
        }
    }

    void RTSController::SpawnUnit(Scene* scene, PhysicsServer* physicsServer, UnitType type, Team team, glm::vec3 pos, glm::vec3 scale) {
        if (!scene) return;

        CBaseEntity* ent = nullptr;
        if (!m_deadUnitEntities.empty()) {
            ent = m_deadUnitEntities.back();
            m_deadUnitEntities.pop_back();
        } else {
            ent = scene->CreateEntity("prop_dynamic");
        }
        ent->origin = glm::dvec3(pos.x, pos.y, pos.z);
        ent->angles = glm::vec3(0.0f);
        ent->modelIndex = (type == UnitType::Plane) ? m_sphereMeshIndex : ((type == UnitType::EnergyGen) ? m_cylinderMeshIndex : m_cubeMeshIndex);
        ent->roughness = 1.0f;
        ent->emission = 0.0f;

        RTSUnit unit;
        unit.entity = ent;
        unit.entityIndex = ent->index;
        unit.type = type;
        unit.team = team;
        unit.position = pos;
        unit.targetPosition = (team == Team::Player) ? m_playerRallyPos : m_enemyRallyPos;
        unit.hasTarget = true;
        unit.selected = false;

        if (type == UnitType::Tank) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(2.5f, 2.0f, 1.2f) : scale;
            unit.maxHealth = 120.0f;
            unit.health = 120.0f;
            unit.speed = 8.0f;
            ent->albedoColor = (team == Team::Player) ? glm::vec3(0.1f, 0.4f, 0.9f) : glm::vec3(0.9f, 0.2f, 0.1f);
        } else if (type == UnitType::Plane) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.5f, 1.5f, 1.5f) : scale;
            unit.maxHealth = 70.0f;
            unit.health = 70.0f;
            unit.speed = 14.0f;
            ent->albedoColor = (team == Team::Player) ? glm::vec3(0.3f, 0.8f, 1.0f) : glm::vec3(1.0f, 0.5f, 0.1f);
        } else if (type == UnitType::Factory) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(6.0f, 6.0f, 4.0f) : scale;
            unit.maxHealth = 500.0f;
            unit.health = 500.0f;
            unit.speed = 0.0f;
            ent->albedoColor = glm::vec3(0.2f, 0.7f, 0.3f);
        } else if (type == UnitType::MassGen) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.0f, 3.0f, 3.0f) : scale;
            unit.maxHealth = 200.0f;
            unit.health = 200.0f;
            unit.speed = 0.0f;
            ent->albedoColor = glm::vec3(0.7f, 0.2f, 0.9f);
            ent->emission = 2.5f;
        } else if (type == UnitType::EnergyGen) {
            ent->modelIndex = m_cylinderMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.0f, 3.0f, 4.0f) : scale;
            unit.maxHealth = 200.0f;
            unit.health = 200.0f;
            unit.speed = 0.0f;
            ent->albedoColor = glm::vec3(0.9f, 0.9f, 0.1f);
            ent->emission = 2.5f;
        } else if (type == UnitType::Engineer) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.8f, 1.8f, 1.2f) : scale;
            unit.maxHealth = 90.0f;
            unit.health = 90.0f;
            unit.speed = 7.0f;
            ent->albedoColor = glm::vec3(0.95f, 0.95f, 0.95f);
            ent->emission = 0.5f;
        } else if (type == UnitType::LandFactory) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(7.0f, 6.0f, 3.5f) : scale;
            unit.maxHealth = 450.0f;
            unit.health = 450.0f;
            unit.speed = 0.0f;
            ent->albedoColor = (team == Team::Player) ? glm::vec3(0.25f, 0.55f, 0.85f) : glm::vec3(0.85f, 0.35f, 0.25f);
            ent->emission = 1.0f;
        } else if (type == UnitType::AirFactory) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(6.0f, 7.0f, 4.0f) : scale;
            unit.maxHealth = 400.0f;
            unit.health = 400.0f;
            unit.speed = 0.0f;
            ent->albedoColor = (team == Team::Player) ? glm::vec3(0.2f, 0.75f, 0.85f) : glm::vec3(0.85f, 0.45f, 0.2f);
            ent->emission = 1.5f;
        } else if (type == UnitType::Turret) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.5f, 1.5f, 6.0f) : scale;
            unit.maxHealth = 400.0f;
            unit.health = 400.0f;
            unit.speed = 0.0f;
            ent->albedoColor = (team == Team::Player) ? glm::vec3(0.18f, 0.55f, 0.85f) : glm::vec3(0.85f, 0.22f, 0.18f);
            ent->emission = 1.8f;
        } else if (type == UnitType::CommandMech) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.2f, 3.2f, 5.2f) : scale;
            unit.maxHealth = 2200.0f;
            unit.health = 2200.0f;
            unit.speed = 4.5f;
            ent->albedoColor = (team == Team::Player) ? glm::vec3(0.18f, 0.58f, 0.95f) : glm::vec3(0.95f, 0.18f, 0.22f);
            ent->emission = 3.5f;
        }
        m_units.push_back(unit);
    }

    void RTSController::Update(float deltaTime, Scene* scene, PhysicsServer* physicsServer, AudioServer* audioServer, Camera* cam, IRenderer* renderer) {
        if (!scene || !cam) return;

        // Toggle Skirmish Menu with Escape
        if (Input::GetKeyDown(SDL_SCANCODE_ESCAPE) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_showSkirmishMenu = !m_showSkirmishMenu;
        }

        // --- 1. RTS CAMERA PAN & ZOOM ---
        float panSpeed = 50.0f * deltaTime;
        float radYaw = glm::radians(m_cameraYaw);
        glm::vec2 camForward = glm::vec2(-sin(radYaw), cos(radYaw));
        glm::vec2 camRight = glm::vec2(cos(radYaw), sin(radYaw));

        if (Input::IsKeyDown(SDL_SCANCODE_W) || Input::IsKeyDown(SDL_SCANCODE_UP)) m_cameraTarget += glm::vec3(camForward * panSpeed, 0.0f);
        if (Input::IsKeyDown(SDL_SCANCODE_S) || Input::IsKeyDown(SDL_SCANCODE_DOWN)) m_cameraTarget -= glm::vec3(camForward * panSpeed, 0.0f);
        if (Input::IsKeyDown(SDL_SCANCODE_A) || Input::IsKeyDown(SDL_SCANCODE_LEFT)) m_cameraTarget -= glm::vec3(camRight * panSpeed, 0.0f);
        if (Input::IsKeyDown(SDL_SCANCODE_D) || Input::IsKeyDown(SDL_SCANCODE_RIGHT)) m_cameraTarget += glm::vec3(camRight * panSpeed, 0.0f);

        m_cameraZoom -= (float)Input::scrollY * 3.0f;
        m_cameraZoom = std::clamp(m_cameraZoom, 15.0f, 220.0f);

        m_cameraTarget.x = std::clamp(m_cameraTarget.x, -125.0f, 125.0f);
        m_cameraTarget.y = std::clamp(m_cameraTarget.y, -125.0f, 125.0f);

        // --- LEFT ALT PAN ANGLE / ORBIT (PITCH + GIMBAL YAW ROTATION) ---
        bool isAltHeld = Input::IsKeyDown(SDL_SCANCODE_LALT) || Input::IsKeyDown(SDL_SCANCODE_RALT);
        bool isMouseRotating = isAltHeld && ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) || (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_MIDDLE)));
        if (isMouseRotating) {
            m_cameraPitch -= (float)Input::mouseRelY * 0.35f;
            m_cameraPitch = std::clamp(m_cameraPitch, 15.0f, 85.0f);
            m_cameraYaw += (float)Input::mouseRelX * 0.35f;
            if (m_cameraYaw > 360.0f) m_cameraYaw -= 360.0f;
            if (m_cameraYaw < -360.0f) m_cameraYaw += 360.0f;
        }

        float radPitch = glm::radians(m_cameraPitch);
        radYaw = glm::radians(m_cameraYaw);
        glm::vec3 camOffset = glm::vec3(
            sin(radYaw) * cos(radPitch) * m_cameraZoom,
            -cos(radYaw) * cos(radPitch) * m_cameraZoom,
            sin(radPitch) * m_cameraZoom
        );
        glm::vec3 camPos = m_cameraTarget + camOffset;
        cam->SetPosition(camPos);
        cam->LookAt(m_cameraTarget);

        // --- 2. ECONOMY TICK ---
        m_resourceTimer += deltaTime;
        // Countdown 10-Minute Mass Overdrive timer
        if (m_boostTimer > 0.0f) {
            m_boostTimer -= deltaTime;
            if (m_boostTimer < 0.0f) m_boostTimer = 0.0f;
        }

        // Auto-boost threshold: if player reaches 1000 mass and no boost is active, activate boost!
        if (playerMass >= 1000.0f && m_boostTimer <= 0.0f) {
            ActivateBoost();
        }

        m_resourceTimer += deltaTime;
        if (m_resourceTimer >= 1.0f) {
            m_resourceTimer = 0.0f;

            int playerMassGenCount = 0;
            int playerEnergyGenCount = 0;
            int enemyMassGenCount = 0;
            int enemyEnergyGenCount = 0;

            for (const auto& u : m_units) {
                if (u.health > 0.0f) {
                    if (u.team == Team::Player) {
                        if (u.type == UnitType::MassGen) playerMassGenCount++;
                        if (u.type == UnitType::EnergyGen) playerEnergyGenCount++;
                    } else {
                        if (u.type == UnitType::MassGen) enemyMassGenCount++;
                        if (u.type == UnitType::EnergyGen) enemyEnergyGenCount++;
                    }
                }
            }

            float massPerGen = (m_boostTimer > 0.0f) ? 30.0f : 15.0f; // 2x income during 10-min boost!
            if (playerMassGenCount > 0) playerMass += (float)playerMassGenCount * massPerGen;
            if (playerEnergyGenCount > 0) playerEnergy += (float)playerEnergyGenCount * 15.0f;

            if (enemyMassGenCount > 0) enemyMass += (float)enemyMassGenCount * 15.0f;
            if (enemyEnergyGenCount > 0) enemyEnergy += (float)enemyEnergyGenCount * 15.0f;
        }

        // --- 3. ENEMY AI TICK ---
        m_aiTimer += deltaTime;
        float aiInterval = (m_skirmishSettings.aiDifficulty == 0) ? 24.0f : ((m_skirmishSettings.aiDifficulty == 2) ? 9.0f : 16.0f);
        if (m_aiTimer >= aiInterval && !gameOver) {
            m_aiTimer = 0.0f;
            if (enemyMass >= 100.0f && enemyEnergy >= 100.0f) {
                enemyMass -= 100.0f;
                enemyEnergy -= 100.0f;
                m_pendingSpawns.push_back({(rand() % 2 == 0) ? UnitType::Tank : UnitType::Plane, Team::Enemy, glm::vec3(70.0f, (float)(rand() % 24 - 12), (rand() % 2 == 0 ? 0.0f : 12.0f))});
                gameMessage = "Enemy AI deployed reinforcements!";
            }
        }

        // --- ENEMY ENGINEER SCOUTING & EXPANSION AI ---
        m_enemyEngineerTimer += deltaTime;
        if (m_enemyEngineerTimer >= 4.0f && !gameOver) {
            m_enemyEngineerTimer = 0.0f;
            for (auto& u : m_units) {
                if (u.team == Team::Enemy && u.type == UnitType::Engineer && u.health > 0.0f) {
                    if (!u.hasTarget) {
                        // Find nearest unoccupied resource node on enemy/neutral territory
                        ResourceDepositNode* targetNode = nullptr;
                        float bestNodeDist = 9999.0f;
                        for (auto& node : m_resourceNodes) {
                            if (!node.occupied) {
                                float d = glm::distance(glm::vec2(u.position.x, u.position.y), glm::vec2(node.position.x, node.position.y));
                                if (d < bestNodeDist) {
                                    bestNodeDist = d;
                                    targetNode = &node;
                                }
                            }
                        }

                        if (targetNode) {
                            if (bestNodeDist <= 7.0f) {
                                // Arrived at node: build Mass Generator!
                                m_pendingSpawns.push_back({UnitType::MassGen, Team::Enemy, glm::vec3(targetNode->position.x, targetNode->position.y, 1.5f)});
                                SpawnExplosionCluster(targetNode->position, 0.8f, 5);
                                targetNode->occupied = true;
                                gameMessage = "Enemy Engineer established a Mass Generator outpost!";
                            } else {
                                // Move toward node to build
                                u.targetPosition = glm::vec3(targetNode->position.x, targetNode->position.y, 0.0f);
                                u.hasTarget = true;
                            }
                        } else {
                            // All nodes claimed: scout the map between strategic waypoints
                            float scoutX = (float)(rand() % 160 - 40);
                            float scoutY = (float)(rand() % 160 - 80);
                            u.targetPosition = glm::vec3(scoutX, scoutY, 0.0f);
                            u.hasTarget = true;
                        }
                    }
                }
            }
        }

        // --- DEFENSIVE PATROL & RETALIATION: Mobilize nearby enemy combat units if buildings or engineers have threats close by ---
        for (auto& defender : m_units) {
            if (defender.team == Team::Enemy && defender.health > 0.0f && !IsStaticBuilding(defender.type) && defender.type != UnitType::Engineer) {
                for (const auto& structure : m_units) {
                    if (structure.team == Team::Enemy && structure.health > 0.0f && (IsStaticBuilding(structure.type) || structure.type == UnitType::Engineer)) {
                        float distToStructure = glm::distance(defender.position, structure.position);
                        if (distToStructure <= 55.0f) {
                            for (const auto& threat : m_units) {
                                if (threat.team == Team::Player && threat.health > 0.0f) {
                                    float threatDist = glm::distance(threat.position, structure.position);
                                    if (threatDist <= 42.0f) {
                                        defender.targetPosition = glm::vec3(threat.position.x, threat.position.y, (defender.type == UnitType::Plane) ? 12.0f : 0.0f);
                                        defender.hasTarget = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- 4. MOUSE SELECTION & ORDERS ---
        m_activeCam = cam;
        ImVec2 mousePos = ImGui::GetMousePos();

        bool isOverViewport = (mousePos.x >= m_viewportPos.x && mousePos.x <= m_viewportPos.x + m_viewportSize.x &&
                               mousePos.y >= m_viewportPos.y && mousePos.y <= m_viewportPos.y + m_viewportSize.y);

        ImVec2 hudPos(m_viewportPos.x + 15, m_viewportPos.y + 15);
        ImVec2 hudSize(370, 100);
        bool isOverHud = (mousePos.x >= hudPos.x && mousePos.x <= hudPos.x + hudSize.x &&
                          mousePos.y >= hudPos.y && mousePos.y <= hudPos.y + hudSize.y);

        ImVec2 buildPos(m_viewportPos.x + (m_viewportSize.x * 0.5f) - 190.0f, m_viewportPos.y + m_viewportSize.y - 100.0f);
        ImVec2 buildSize(380, 90);
        bool isOverBuild = (mousePos.x >= buildPos.x && mousePos.x <= buildPos.x + buildSize.x &&
                            mousePos.y >= buildPos.y && mousePos.y <= buildPos.y + buildSize.y);

        bool canInteract3D = isOverViewport && !isOverHud && !isOverBuild && !isAltHeld;
        bool isLeftDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        bool isRightDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;


        // --- 4. BUILDING PLACEMENT PREVIEW & CONFIRMATION ---
        if (m_isPlacingBuilding && m_buildGhostEntity) {
            glm::vec3 groundPos = ScreenToGround(cam, glm::vec2(mousePos.x, mousePos.y));

            // If placing MassGen, snap to closest unoccupied green resource deposit node
            if (m_placingBuildingType == UnitType::MassGen) {
                float minSnapDist = 9999.0f;
                for (const auto& node : m_resourceNodes) {
                    if (!node.occupied) {
                        float d = glm::distance(glm::vec2(groundPos.x, groundPos.y), glm::vec2(node.position.x, node.position.y));
                        if (d < minSnapDist) {
                            minSnapDist = d;
                            groundPos = glm::vec3(node.position.x, node.position.y, 1.5f);
                        }
                    }
                }
            }

            float ghostZ = (m_placingBuildingType == UnitType::MassGen) ? 1.5f : ((m_placingBuildingType == UnitType::AirFactory) ? 2.0f : ((m_placingBuildingType == UnitType::Turret) ? 3.0f : 1.75f));
            m_buildGhostEntity->origin = glm::dvec3(groundPos.x, groundPos.y, ghostZ);
            if (m_placingBuildingType == UnitType::LandFactory) {
                m_buildGhostEntity->scale = glm::vec3(7.0f, 6.0f, 3.5f);
                m_buildGhostEntity->albedoColor = glm::vec3(0.25f, 0.6f, 0.9f);
            } else if (m_placingBuildingType == UnitType::AirFactory) {
                m_buildGhostEntity->scale = glm::vec3(6.0f, 7.0f, 4.0f);
                m_buildGhostEntity->albedoColor = glm::vec3(0.2f, 0.85f, 0.9f);
            } else if (m_placingBuildingType == UnitType::MassGen) {
                m_buildGhostEntity->scale = glm::vec3(4.0f, 4.0f, 3.0f);
                m_buildGhostEntity->albedoColor = glm::vec3(0.8f, 0.3f, 1.0f);
            } else if (m_placingBuildingType == UnitType::Turret) {
                m_buildGhostEntity->scale = glm::vec3(1.5f, 1.5f, 6.0f);
                m_buildGhostEntity->albedoColor = glm::vec3(0.25f, 0.7f, 1.0f);
            }
            m_buildGhostEntity->emission = 4.5f;

            bool isLeftClick = canInteract3D && isLeftDown && !m_isDraggingBox;
            bool isRightClick = canInteract3D && isRightDown && !m_wasRightMouseDown;

            if (isLeftClick) {
                int massCost = (m_placingBuildingType == UnitType::AirFactory) ? 220 : ((m_placingBuildingType == UnitType::LandFactory) ? 180 : ((m_placingBuildingType == UnitType::Turret) ? 140 : 100));
                int energyCost = (m_placingBuildingType == UnitType::AirFactory) ? 160 : ((m_placingBuildingType == UnitType::LandFactory) ? 120 : ((m_placingBuildingType == UnitType::Turret) ? 100 : 80));

                if (playerMass >= (float)massCost && playerEnergy >= (float)energyCost) {
                    playerMass -= (float)massCost;
                    playerEnergy -= (float)energyCost;

                    RTSUnit* selectedEng = nullptr;
                    for (auto& u : m_units) {
                        if (u.selected && u.team == Team::Player && (u.type == UnitType::Engineer || u.type == UnitType::CommandMech) && u.health > 0.0f) {
                            selectedEng = &u;
                            break;
                        }
                    }

                    float buildTime = (m_placingBuildingType == UnitType::MassGen) ? 60.0f : ((m_placingBuildingType == UnitType::AirFactory) ? 30.0f : ((m_placingBuildingType == UnitType::Turret) ? 20.0f : 25.0f));
                    if (selectedEng) {
                        selectedEng->targetPosition = groundPos;
                        selectedEng->hasTarget = true;
                        m_engineerTask.active = true;
                        m_engineerTask.constructing = false;
                        m_engineerTask.buildType = m_placingBuildingType;
                        m_engineerTask.buildPos = groundPos;
                        m_engineerTask.engineerEntityIndex = selectedEng->entityIndex;
                        m_engineerTask.progress = 0.0f;
                        m_engineerTask.totalTime = buildTime;
                        m_engineerTask.sparkTimer = 0.0f;
                        gameMessage = (selectedEng->type == UnitType::CommandMech) ? "Command Mech assigned to construct building!" : "Engineer assigned to construct building!";
                    } else {
                        m_pendingSpawns.push_back({m_placingBuildingType, Team::Player, glm::vec3(groundPos.x, groundPos.y, ghostZ)});
                        SpawnExplosionCluster(groundPos, 0.8f, 5);
                        gameMessage = "Structure placed!";
                    }

                    m_isPlacingBuilding = false;
                    m_buildGhostEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                    m_buildGhostEntity->scale = glm::vec3(0.0f);
                } else {
                    gameMessage = "Insufficient Mass or Energy for construction!";
                }
            } else if (isRightClick) {
                m_isPlacingBuilding = false;
                m_buildGhostEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                m_buildGhostEntity->scale = glm::vec3(0.0f);
                gameMessage = "Construction canceled.";
            }
        } else {
            // Left Click / Drag Box Selection
            if (canInteract3D && isLeftDown) {
                if (!m_isDraggingBox) {
                    m_isDraggingBox = true;
                    m_dragStartScreen = glm::vec2(mousePos.x, mousePos.y);
                }
                m_dragEndScreen = glm::vec2(mousePos.x, mousePos.y);
            } else if (!isLeftDown && m_isDraggingBox) {
                m_dragEndScreen = glm::vec2(mousePos.x, mousePos.y);
                glm::vec2 diff = m_dragEndScreen - m_dragStartScreen;
                if (glm::length(diff) < 14.0f) {
                    SelectSingleUnit(scene, cam, m_dragEndScreen);
                } else {
                    SelectUnitsInBox(scene, cam, glm::min(m_dragStartScreen, m_dragEndScreen), glm::max(m_dragStartScreen, m_dragEndScreen));
                }
                m_isDraggingBox = false;
            }

            if (isAltHeld) {
                m_isDraggingBox = false;
            }

            // Right-Click Hold / Drag Line Formation
            if (canInteract3D && isRightDown) {
                if (!m_wasRightMouseDown) {
                    m_isDraggingFormation = true;
                    m_formationStartScreen = glm::vec2(mousePos.x, mousePos.y);
                    m_formationStartWorld = ScreenToGround(cam, m_formationStartScreen);
                }
                m_formationEndScreen = glm::vec2(mousePos.x, mousePos.y);
                m_formationEndWorld = ScreenToGround(cam, m_formationEndScreen);
            } else if (!isRightDown && m_wasRightMouseDown && m_isDraggingFormation) {
                float dragScreenDist = glm::distance(m_formationStartScreen, m_formationEndScreen);
                
                std::vector<RTSUnit*> selectedUnits;
                for (auto& u : m_units) {
                    if (u.selected && u.team == Team::Player && !IsStaticBuilding(u.type) && u.health > 0.0f) {
                        selectedUnits.push_back(&u);
                    }
                }

                if (dragScreenDist > 18.0f && selectedUnits.size() > 1) {
                    // Line Formation along the dragged vector
                    int N = (int)selectedUnits.size();
                    glm::vec3 lineVec = m_formationEndWorld - m_formationStartWorld;
                    float lineLen = glm::length(lineVec);
                    glm::vec3 lineDir = (lineLen > 0.001f) ? (lineVec / lineLen) : glm::vec3(1, 0, 0);

                    // Sort units along line direction to prevent crossing paths
                    std::vector<std::pair<float, RTSUnit*>> projected;
                    for (auto* u : selectedUnits) {
                        float proj = glm::dot(u->position - m_formationStartWorld, lineDir);
                        projected.push_back({proj, u});
                    }
                    std::sort(projected.begin(), projected.end(), [](const auto& a, const auto& b) {
                        return a.first < b.first;
                    });

                    for (int i = 0; i < N; i++) {
                        float t = (N > 1) ? (float)i / (float)(N - 1) : 0.5f;
                        glm::vec3 targetSlot = m_formationStartWorld + lineVec * t;
                        targetSlot.x = std::clamp(targetSlot.x, -140.0f, 140.0f);
                        targetSlot.y = std::clamp(targetSlot.y, -140.0f, 140.0f);
                        projected[i].second->targetPosition = targetSlot;
                        projected[i].second->hasTarget = true;
                    }
                    gameMessage = "Line formation deployed for " + std::to_string(N) + " units!";
                } else {
                    // Regular right-click move order or factory rally point
                    glm::vec3 groundTarget = ScreenToGround(cam, glm::vec2(mousePos.x, mousePos.y));

                    int selectedMovableCount = 0;
                    bool isFactorySelected = false;
                    for (const auto& u : m_units) {
                        if (u.selected && u.team == Team::Player) {
                            if (u.type == UnitType::Factory || u.type == UnitType::LandFactory || u.type == UnitType::AirFactory) {
                                isFactorySelected = true;
                            } else if (!IsStaticBuilding(u.type)) {
                                selectedMovableCount++;
                            }
                        }
                    }

                    if (selectedMovableCount > 0) {
                        IssueMoveOrder(scene, groundTarget);
                    } else if (isFactorySelected) {
                        m_playerRallyPos = groundTarget;
                        if (m_playerRallyEntity) {
                            m_playerRallyEntity->origin = glm::dvec3(m_playerRallyPos.x, m_playerRallyPos.y, 4.0);
                        }
                        gameMessage = "Factory rally point updated!";
                    }
                }
                m_isDraggingFormation = false;
            }
        }
        m_wasRightMouseDown = isRightDown;

        // Update 3D In-Engine Selection Rings around selected units
        size_t ringIdx = 0;
        for (const auto& u : m_units) {
            if (u.selected && ringIdx < m_selectionRingEntities.size()) {
                CBaseEntity* ringEnt = m_selectionRingEntities[ringIdx++];
                float radius = (u.type == UnitType::Plane) ? 2.8f : ((u.type == UnitType::Factory) ? 6.0f : ((u.type == UnitType::CommandMech) ? 3.6f : 2.5f));
                float ringZ = (u.type == UnitType::Plane) ? 11.5f : 0.05f;
                ringEnt->origin = glm::dvec3(u.position.x, u.position.y, ringZ);
                ringEnt->scale = glm::vec3(radius, radius, 1.0f);
                ringEnt->albedoColor = glm::vec3(0.0f, 1.0f, 0.6f);
                ringEnt->emission = 6.0f;
            }
        }
        for (; ringIdx < m_selectionRingEntities.size(); ++ringIdx) {
            m_selectionRingEntities[ringIdx]->origin = glm::dvec3(0.0, 0.0, -999.0);
            m_selectionRingEntities[ringIdx]->scale = glm::vec3(0.0f);
        }

        // --- 5. SIMULATE PROJECTILES (Tank Shells & Airplane Rockets) ---
        for (auto& p : m_projectiles) {
            if (!p.active) continue;

            p.life += deltaTime;
            p.position += p.velocity * deltaTime;
            p.entity->origin = glm::dvec3(p.position.x, p.position.y, p.position.z);

            float distToTarget = glm::distance(p.position, p.targetPos);
            bool impacted = (distToTarget < 2.2f) || (p.life >= p.maxLife);

            // Proximity check against enemy units
            if (!impacted) {
                for (auto& targetUnit : m_units) {
                    if (targetUnit.team != p.team && targetUnit.health > 0.0f) {
                        float hitDist = (targetUnit.type == UnitType::Plane) ? 3.5f : 2.8f;
                        if (glm::distance(p.position, targetUnit.position) < hitDist) {
                            impacted = true;
                            break;
                        }
                    }
                }
            }

            if (impacted) {
                p.active = false;
                p.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                p.entity->scale = glm::vec3(0.0f);

                // Spawn cluster of emissive yellow spheres expanding as an explosion
                float explScale = (p.type == ProjectileType::Rocket) ? 1.5f : 1.0f;
                int sphereCount = (p.type == ProjectileType::Rocket) ? 7 : 5;
                SpawnExplosionCluster(p.position, explScale, sphereCount);

                if (audioServer) {
                    audioServer->PlayOneShot("assets/audio/step.mp3", 1.0f);
                }

                // Area damage
                float splashRadius = (p.type == ProjectileType::Rocket) ? 6.5f : 4.0f;
                for (auto& u : m_units) {
                    if (u.team != p.team && u.health > 0.0f) {
                        float d = glm::distance(p.position, u.position);
                        if (d < splashRadius) {
                            float falloff = 1.0f - (d / splashRadius);
                            u.health -= p.damage * std::max(0.4f, falloff);

                            // Alert nearby friendly defenders if a building or engineer is attacked
                            if (IsStaticBuilding(u.type) || u.type == UnitType::Engineer) {
                                float defenseAlertRadius = 60.0f;
                                for (auto& defender : m_units) {
                                    if (defender.team == u.team && defender.health > 0.0f &&
                                        (defender.type == UnitType::Tank || defender.type == UnitType::Plane || defender.type == UnitType::CommandMech)) {
                                        float distToVictim = glm::distance(defender.position, u.position);
                                        if (distToVictim <= defenseAlertRadius) {
                                            defender.targetPosition = glm::vec3(p.position.x, p.position.y, (defender.type == UnitType::Plane) ? 12.0f : 0.0f);
                                            defender.hasTarget = true;
                                        }
                                    }
                                }
                            }
                            if (u.health <= 0.0f) {
                                float deathScale = (u.type == UnitType::CommandMech) ? 4.5f : ((u.type == UnitType::Factory) ? 3.5f : 1.8f);
                                int deathSpheres = (u.type == UnitType::CommandMech) ? 25 : 10;
                                SpawnExplosionCluster(u.position, deathScale, deathSpheres);
                                if (u.type == UnitType::CommandMech) {
                                    gameMessage = (u.team == Team::Player) ? "CRITICAL: Player Command Mech destroyed in nuclear overload!" : "Enemy Command Mech eliminated!";
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- 6. SIMULATE EXPANDING EMISSIVE EXPLOSION SPHERE CLUSTERS ---
        for (auto& s : m_explosionSpheres) {
            if (!s.active) continue;

            s.age += deltaTime;
            if (s.age >= s.maxLife) {
                s.active = false;
                s.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                s.entity->scale = glm::vec3(0.0f);
            } else {
                float t = s.age / s.maxLife; // 0 to 1
                float growth = (t < 0.3f) ? sin(t / 0.3f * 1.5707963f) : cos((t - 0.3f) / 0.7f * 1.5707963f);
                growth = std::max(0.0f, growth);
                float curScale = s.peakScale * growth;

                glm::vec3 curPos = s.center + s.offset + s.velocity * s.age;
                s.entity->origin = glm::dvec3(curPos.x, curPos.y, curPos.z);
                s.entity->scale = glm::vec3(curScale);
                s.entity->albedoColor = s.color;
                s.entity->emission = 14.0f * growth;
            }
        }

        // --- 7. UNIT MOVEMENT & COMBAT TARGETING ---
        for (size_t uIdx = 0; uIdx < m_units.size();) {
            auto& unit = m_units[uIdx];
            CBaseEntity* ent = unit.entity;

            if (!ent || unit.health <= 0.0f) {
                if (ent) {
                    ent->origin = glm::dvec3(0.0, 0.0, -999.0);
                    ent->scale = glm::vec3(0.0f);
                    m_deadUnitEntities.push_back(ent);
                }
                m_units.erase(m_units.begin() + uIdx);
                continue;
            }

            // Movement logic
            if (unit.hasTarget && !IsStaticBuilding(unit.type)) {
                glm::vec3 currPos(ent->origin.x, ent->origin.y, ent->origin.z);
                glm::vec3 dir = unit.targetPosition - currPos;
                if (unit.type != UnitType::Plane) {
                    dir.z = 0.0f;
                }
                float dist = glm::length(dir);
                if (dist > 1.0f) {
                    dir = glm::normalize(dir);
                    float moveStep = unit.speed * deltaTime;
                    if (moveStep > dist) moveStep = dist;
                    currPos += dir * moveStep;
                    
                    currPos.x = std::clamp(currPos.x, -140.0f, 140.0f);
                    currPos.y = std::clamp(currPos.y, -140.0f, 140.0f);

                    if (unit.type == UnitType::Plane) {
                        currPos.z = 12.0f;
                    } else {
                        currPos.z = 0.0f;
                    }
                    ent->origin = glm::dvec3(currPos.x, currPos.y, currPos.z);
                    unit.position = currPos;

                    // Orient unit along movement direction
                    float moveYaw = glm::degrees(atan2(dir.y, dir.x));
                    ent->angles = glm::vec3(0.0f, 0.0f, moveYaw);
                } else {
                    unit.hasTarget = false;
                }
            }

            // Engineer constructing a building (1-min build time for Mass Gen, accelerated by boost)
            if ((unit.type == UnitType::Engineer || unit.type == UnitType::CommandMech) && m_engineerTask.active && m_engineerTask.engineerEntityIndex == unit.entityIndex) {
                float distToSite = glm::distance(glm::vec2(unit.position.x, unit.position.y), glm::vec2(m_engineerTask.buildPos.x, m_engineerTask.buildPos.y));
                if (distToSite <= 7.5f) {
                    if (!m_engineerTask.constructing) {
                        m_engineerTask.constructing = true;
                        if (audioServer) audioServer->PlayOneShot("assets/audio/laser.wav", 0.65f);
                    }
                    unit.hasTarget = false;
                    // Update Emissive Construction Beam from Engineer to Build Site
                    if (m_buildBeamEntity) {
                        glm::vec3 p1 = unit.position + glm::vec3(0.0f, 0.0f, (unit.type == UnitType::CommandMech) ? 2.5f : 0.8f);
                        glm::vec3 p2 = m_engineerTask.buildPos + glm::vec3(0.0f, 0.0f, 1.2f);
                        glm::vec3 diff = p2 - p1;
                        float len = glm::length(diff);
                        if (len > 0.1f) {
                            glm::vec3 dir = diff / len;
                            float pitchRad = acos(std::clamp(dir.z, -1.0f, 1.0f));
                            float yawRad = atan2(dir.x, -dir.y);
                            m_buildBeamEntity->angles = glm::vec3(glm::degrees(pitchRad), 0.0f, glm::degrees(yawRad));
                            m_buildBeamEntity->origin = glm::dvec3((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f, (p1.z + p2.z) * 0.5f);
                            m_buildBeamEntity->scale = glm::vec3(0.22f, 0.22f, len);
                            m_buildBeamEntity->albedoColor = glm::vec3(0.2f, 0.95f, 1.0f);
                            m_buildBeamEntity->emission = 12.0f;
                        }
                    }

                    float speedMult = (m_boostTimer > 0.0f) ? 2.0f : 1.0f;
                    m_engineerTask.progress += deltaTime * speedMult;

                    // Visual sparks only (NO SOUND during building as requested)
                    m_engineerTask.sparkTimer += deltaTime;
                    if (m_engineerTask.sparkTimer >= 0.7f) {
                        m_engineerTask.sparkTimer = 0.0f;
                        SpawnExplosionCluster(m_engineerTask.buildPos + glm::vec3(0.0f, 0.0f, 0.4f), 0.35f, 2);
                    }

                    int pct = (int)((m_engineerTask.progress / m_engineerTask.totalTime) * 100.0f);
                    int remSec = (int)std::max(0.0f, (m_engineerTask.totalTime - m_engineerTask.progress) / speedMult);
                    const char* bName = (m_engineerTask.buildType == UnitType::MassGen) ? "Mass Gen (1-Min Build)" : ((m_engineerTask.buildType == UnitType::AirFactory) ? "Air Factory" : ((m_engineerTask.buildType == UnitType::Turret) ? "Point Defense Tower" : "Land Factory"));
                    gameMessage = std::string("Constructing ") + bName + ": " + std::to_string(pct) + "% (" + std::to_string(remSec) + "s remaining)";

                    if (m_engineerTask.progress >= m_engineerTask.totalTime) {
                        float bZ = (m_engineerTask.buildType == UnitType::MassGen) ? 1.5f : ((m_engineerTask.buildType == UnitType::AirFactory) ? 2.0f : ((m_engineerTask.buildType == UnitType::Turret) ? 3.0f : 1.75f));
                        m_pendingSpawns.push_back({m_engineerTask.buildType, Team::Player, glm::vec3(m_engineerTask.buildPos.x, m_engineerTask.buildPos.y, bZ)});
                        SpawnExplosionCluster(m_engineerTask.buildPos, 1.0f, 6);

                        if (m_engineerTask.buildType == UnitType::MassGen) {
                            for (auto& node : m_resourceNodes) {
                                if (glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(m_engineerTask.buildPos.x, m_engineerTask.buildPos.y)) < 3.0f) {
                                    node.occupied = true;
                                    break;
                                }
                            }
                        }

                        // Hide beam upon completion
                        if (m_buildBeamEntity) {
                            m_buildBeamEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                            m_buildBeamEntity->scale = glm::vec3(0.0f);
                        }

                        m_engineerTask.active = false;
                        m_engineerTask.constructing = false;
                        gameMessage = std::string(bName) + " complete and operational!";
                    }
                }
            }

            // Combat Engagement & Target Acquisition
            unit.attackCooldown -= deltaTime;
            if (unit.attackCooldown <= 0.0f && (unit.type == UnitType::Tank || unit.type == UnitType::Plane || unit.type == UnitType::Turret || unit.type == UnitType::CommandMech)) {
                float engageRadius = (unit.type == UnitType::Plane) ? 42.0f : ((unit.type == UnitType::CommandMech) ? 36.0f : ((unit.type == UnitType::Turret) ? 35.0f : 30.0f));

                RTSUnit* bestTarget = nullptr;
                float bestDist = engageRadius;
                // Priority 0: Threats attacking or within close range of friendly buildings/engineers
                for (const auto& friendly : m_units) {
                    if (friendly.team == unit.team && friendly.health > 0.0f && (IsStaticBuilding(friendly.type) || friendly.type == UnitType::Engineer)) {
                        float distToFriendly = glm::distance(unit.position, friendly.position);
                        if (distToFriendly <= 55.0f) {
                            for (auto& threat : m_units) {
                                if (threat.team != unit.team && threat.health > 0.0f) {
                                    float threatToFriendly = glm::distance(threat.position, friendly.position);
                                    if (threatToFriendly <= 40.0f) {
                                        float d = glm::distance(unit.position, threat.position);
                                        if (d < bestDist) {
                                            bestDist = d;
                                            bestTarget = &threat;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }


                // Priority 1: enemy combat units
                for (auto& enemy : m_units) {
                    if (enemy.team != unit.team && enemy.health > 0.0f) {
                        if (enemy.type == UnitType::Tank || enemy.type == UnitType::Plane || enemy.type == UnitType::Engineer || enemy.type == UnitType::CommandMech) {
                            float d = glm::distance(unit.position, enemy.position);
                            if (d < bestDist) {
                                bestDist = d;
                                bestTarget = &enemy;
                            }
                        }
                    }
                }

                // Priority 2: enemy structures (factory / generators / turrets)
                if (!bestTarget) {
                    for (auto& enemy : m_units) {
                        if (enemy.team != unit.team && enemy.health > 0.0f) {
                            float d = glm::distance(unit.position, enemy.position);
                            if (d < bestDist) {
                                bestDist = d;
                                bestTarget = &enemy;
                            }
                        }
                    }
                }

                if (bestTarget) {
                    if (unit.type == UnitType::Tank) {
                        glm::vec3 muzzle = unit.position + glm::vec3(0.0f, 0.0f, 0.6f);
                        glm::vec3 targetAim = bestTarget->position + glm::vec3(0.0f, 0.0f, 0.6f);
                        FireProjectile(ProjectileType::TankShell, unit.team, muzzle, targetAim, 26.0f);
                        SpawnExplosionCluster(muzzle, 0.35f, 2);
                        if (audioServer) audioServer->PlayOneShot("assets/audio/cannon.wav", 0.9f);
                        unit.attackCooldown = 1.8f;
                    } else if (unit.type == UnitType::Plane) {
                        glm::vec3 pLaunch = unit.position - glm::vec3(0.0f, 0.0f, 1.0f);
                        glm::vec3 targetAim = bestTarget->position + glm::vec3(0.0f, 0.0f, 0.5f);
                        FireProjectile(ProjectileType::Rocket, unit.team, pLaunch, targetAim, 39.0f);
                        SpawnExplosionCluster(pLaunch, 0.45f, 2);
                        if (audioServer) audioServer->PlayOneShot("assets/audio/rocket.wav", 0.9f);
                        unit.attackCooldown = 2.2f;
                    } else if (unit.type == UnitType::Turret) {
                        glm::vec3 muzzle = unit.position + glm::vec3(0.0f, 0.0f, 2.8f);
                        glm::vec3 targetAim = bestTarget->position + glm::vec3(0.0f, 0.0f, 0.6f);
                        FireProjectile(ProjectileType::TankShell, unit.team, muzzle, targetAim, 18.0f);
                        SpawnExplosionCluster(muzzle, 0.35f, 2);
                        if (audioServer) audioServer->PlayOneShot("assets/audio/turret.wav", 0.8f);
                        unit.attackCooldown = 1.1f;
                    } else if (unit.type == UnitType::CommandMech) {
                        glm::vec3 muzzle = unit.position + glm::vec3(0.0f, 0.0f, 2.5f);
                        glm::vec3 targetAim = bestTarget->position + glm::vec3(0.0f, 0.0f, 0.6f);
                        FireProjectile(ProjectileType::TankShell, unit.team, muzzle, targetAim, 60.0f);
                        SpawnExplosionCluster(muzzle, 0.6f, 6);
                        if (audioServer) audioServer->PlayOneShot("assets/audio/cannon.wav", 1.0f);
                        unit.attackCooldown = 2.5f;
                    }
                }
            }

            ++uIdx;
        }

        // Hide beam if not actively constructing
        if (m_buildBeamEntity && (!m_engineerTask.active || !m_engineerTask.constructing)) {
            m_buildBeamEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
            m_buildBeamEntity->scale = glm::vec3(0.0f);
        }

        // --- 8. UNIT-TO-UNIT & UNIT-TO-STRUCTURE COLLISION PASS (2-PASS RELAXATION) ---
        size_t unitCount = m_units.size();
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < unitCount; ++i) {
                auto& uA = m_units[i];
                if (uA.health <= 0.0f) continue;
                bool airA = IsAir(uA.type);
                bool staticA = IsStaticBuilding(uA.type);

                for (size_t j = i + 1; j < unitCount; ++j) {
                    auto& uB = m_units[j];
                    if (uB.health <= 0.0f) continue;
                    bool airB = IsAir(uB.type);
                    bool staticB = IsStaticBuilding(uB.type);

                    // Air units only collide with air units; ground units only collide with ground units/structures
                    if (airA != airB) continue;
                    if (staticA && staticB) continue;

                    float rA = GetUnitRadius(uA.type);
                    float rB = GetUnitRadius(uB.type);
                    float minSep = rA + rB;

                    float dx = uA.position.x - uB.position.x;
                    float dy = uA.position.y - uB.position.y;
                    float distSq = dx * dx + dy * dy;

                    if (distSq < minSep * minSep) {
                        float dist = sqrt(distSq);
                        float overlap = minSep - dist;
                        glm::vec2 normal;
                        if (dist > 0.001f) {
                            normal = glm::vec2(dx / dist, dy / dist);
                        } else {
                            float angle = (float)((uA.entityIndex * 37 + uB.entityIndex * 73) % 360) * 0.0174533f;
                            normal = glm::vec2(cos(angle), sin(angle));
                        }

                        if (staticA && !staticB) {
                            // Structure A is static: bump movable unit B away
                            uB.position.x -= normal.x * overlap;
                            uB.position.y -= normal.y * overlap;
                            uB.position.x = std::clamp(uB.position.x, -140.0f, 140.0f);
                            uB.position.y = std::clamp(uB.position.y, -140.0f, 140.0f);
                            if (uB.entity) {
                                uB.entity->origin.x = uB.position.x;
                                uB.entity->origin.y = uB.position.y;
                            }
                        } else if (!staticA && staticB) {
                            // Structure B is static: bump movable unit A away
                            uA.position.x += normal.x * overlap;
                            uA.position.y += normal.y * overlap;
                            uA.position.x = std::clamp(uA.position.x, -140.0f, 140.0f);
                            uA.position.y = std::clamp(uA.position.y, -140.0f, 140.0f);
                            if (uA.entity) {
                                uA.entity->origin.x = uA.position.x;
                                uA.entity->origin.y = uA.position.y;
                            }
                        } else if (!staticA && !staticB) {
                            // Both are movable units: push apart smoothly
                            float halfOverlap = overlap * 0.5f;
                            uA.position.x += normal.x * halfOverlap;
                            uA.position.y += normal.y * halfOverlap;
                            uB.position.x -= normal.x * halfOverlap;
                            uB.position.y -= normal.y * halfOverlap;

                            uA.position.x = std::clamp(uA.position.x, -140.0f, 140.0f);
                            uA.position.y = std::clamp(uA.position.y, -140.0f, 140.0f);
                            uB.position.x = std::clamp(uB.position.x, -140.0f, 140.0f);
                            uB.position.y = std::clamp(uB.position.y, -140.0f, 140.0f);

                            if (uA.entity) {
                                uA.entity->origin.x = uA.position.x;
                                uA.entity->origin.y = uA.position.y;
                            }
                            if (uB.entity) {
                                uB.entity->origin.x = uB.position.x;
                                uB.entity->origin.y = uB.position.y;
                            }

                            // Arrival chaining when crowding near destination
                            float goalDistA = glm::distance(glm::vec2(uA.targetPosition.x, uA.targetPosition.y), glm::vec2(uA.position.x, uA.position.y));
                            float goalDistB = glm::distance(glm::vec2(uB.targetPosition.x, uB.targetPosition.y), glm::vec2(uB.position.x, uB.position.y));
                            if (!uA.hasTarget && uB.hasTarget && goalDistB < minSep * 2.2f) {
                                uB.hasTarget = false;
                            } else if (uA.hasTarget && !uB.hasTarget && goalDistA < minSep * 2.2f) {
                                uA.hasTarget = false;
                            }
                        }
                    }
                }
            }
        }

        // --- 9. ANNIHILATION / ASSASSINATION WIN & LOSS CONDITIONS ---
        int enemyStructures = 0;
        int enemyCombat = 0;
        int playerStructures = 0;
        int playerCombat = 0;

        for (const auto& u : m_units) {
            if (u.health <= 0.0f) continue;
            if (u.team == Team::Enemy) {
                if (u.type == UnitType::Factory || u.type == UnitType::MassGen || u.type == UnitType::EnergyGen) {
                    enemyStructures++;
                } else {
                    enemyCombat++;
                }
            } else if (u.team == Team::Player) {
                if (u.type == UnitType::Factory || u.type == UnitType::MassGen || u.type == UnitType::EnergyGen) {
                    playerStructures++;
                } else {
                    playerCombat++;
                }
            }
        }

        int totalEnemyAssets = enemyStructures + enemyCombat;
        int totalPlayerAssets = playerStructures + playerCombat;

        if (!gameOver) {
            m_matchTime += deltaTime;

            if (m_skirmishSettings.gameMode == 0) {
                // --- ANNIHILATION MODE ---
                // Win when ALL enemy units AND buildings are eliminated!
                if (totalEnemyAssets == 0) {
                    gameOver = true;
                    m_playerWon = true;
                    gameMessage = "ANNIHILATION VICTORY! All enemy forces and bases eliminated!";
                    SpawnExplosionCluster(glm::vec3(m_cameraTarget.x, m_cameraTarget.y, 8.0f), 3.0f, 12);
                } else if (totalPlayerAssets == 0) {
                    gameOver = true;
                    m_playerWon = false;
                    gameMessage = "ANNIHILATION DEFEAT! All your forces and structures were destroyed!";
                }
            } else if (m_skirmishSettings.gameMode == 1) {
                // --- HQ ASSASSINATION MODE (Command Center Only) ---
                bool enemyBaseAlive = false;
                bool playerBaseAlive = false;
                for (const auto& u : m_units) {
                    if (u.type == UnitType::Factory && u.health > 0.0f) {
                        if (u.team == Team::Enemy) enemyBaseAlive = true;
                        if (u.team == Team::Player) playerBaseAlive = true;
                    }
                }
                if (!enemyBaseAlive) {
                    gameOver = true;
                    m_playerWon = true;
                    gameMessage = "VICTORY! Enemy Command Center destroyed!";
                    SpawnExplosionCluster(glm::vec3(m_cameraTarget.x, m_cameraTarget.y, 8.0f), 3.0f, 12);
                } else if (!playerBaseAlive) {
                    gameOver = true;
                    m_playerWon = false;
                    gameMessage = "DEFEAT! Your Command Center was destroyed!";
                }
            } else {
                // --- COMMANDER ASSASSINATION (SupCom FAF ACU Mode) ---
                bool enemyMechAlive = false;
                bool playerMechAlive = false;
                for (const auto& u : m_units) {
                    if (u.type == UnitType::CommandMech && u.health > 0.0f) {
                        if (u.team == Team::Enemy) enemyMechAlive = true;
                        if (u.team == Team::Player) playerMechAlive = true;
                    }
                }
                if (!enemyMechAlive) {
                    gameOver = true;
                    m_playerWon = true;
                    gameMessage = "VICTORY! Enemy Armored Command Mech eliminated!";
                    SpawnExplosionCluster(glm::vec3(m_cameraTarget.x, m_cameraTarget.y, 8.0f), 3.5f, 15);
                } else if (!playerMechAlive) {
                    gameOver = true;
                    m_playerWon = false;
                    gameMessage = "DEFEAT! Your Armored Command Mech was destroyed in combat!";
                }
            }
        }

        // --- 10. DEFERRED SPAWN QUEUE EXECUTION ---
        if (!m_pendingSpawns.empty()) {
            for (const auto& sp : m_pendingSpawns) {
                SpawnUnit(scene, physicsServer, sp.type, sp.team, sp.pos, sp.scale);
            }
            m_pendingSpawns.clear();
        }
    }

    void RTSController::ActivateBoost() {
        if (playerMass >= 1000.0f) {
            playerMass -= 1000.0f;
            m_boostTimer += 600.0f; // 10 minutes (600s)
            gameMessage = "10-Minute Mass Overdrive activated! 2x generator income & 2x build speed!";
        } else {
            gameMessage = "Need 1000 Mass to activate 10-Minute Overdrive Boost!";
        }
    }

    glm::vec3 RTSController::ScreenToGround(Camera* cam, glm::vec2 screenPos) {
        if (!cam) return m_cameraTarget;

        float width = (m_viewportSize.x > 0.0f) ? m_viewportSize.x : 1280.0f;
        float height = (m_viewportSize.y > 0.0f) ? m_viewportSize.y : 720.0f;
        float aspect = width / height;

        float px = screenPos.x - m_viewportPos.x;
        float py = screenPos.y - m_viewportPos.y;

        float tanFov = tan(glm::radians(cam->fov * 0.5f));
        float u = ((px / width) * 2.0f - 1.0f) * tanFov * aspect;
        float v = (1.0f - (py / height) * 2.0f) * tanFov;

        glm::vec3 rayDir = glm::normalize(cam->Front + cam->Right * u + cam->Up * v);
        glm::vec3 camPos = cam->GetPosition();

        if (std::abs(rayDir.z) > 1e-5f) {
            float t = -camPos.z / rayDir.z;
            if (t > 0.0f) {
                glm::vec3 hit = camPos + rayDir * t;
                hit.x = std::clamp(hit.x, -140.0f, 140.0f);
                hit.y = std::clamp(hit.y, -140.0f, 140.0f);
                hit.z = 0.0f;
                return hit;
            }
        }
        return m_cameraTarget;
    }

    bool RTSController::WorldToScreen(Camera* cam, const glm::vec3& worldPos, glm::vec2& outScreenPos) {
        if (!cam) return false;

        glm::vec3 toPos = worldPos - cam->GetPosition();
        float dFront = glm::dot(toPos, cam->Front);
        if (dFront <= 0.1f) return false;

        float dRight = glm::dot(toPos, cam->Right);
        float dUp = glm::dot(toPos, cam->Up);

        float width = (m_viewportSize.x > 0.0f) ? m_viewportSize.x : 1280.0f;
        float height = (m_viewportSize.y > 0.0f) ? m_viewportSize.y : 720.0f;
        float aspect = width / height;

        float tanFov = tan(glm::radians(cam->fov * 0.5f));
        float normX = (dRight / dFront) / (tanFov * aspect);
        float normY = (dUp / dFront) / tanFov;

        outScreenPos.x = m_viewportPos.x + (normX + 1.0f) * 0.5f * width;
        outScreenPos.y = m_viewportPos.y + (1.0f - normY) * 0.5f * height;
        return true;
    }

    void RTSController::SelectSingleUnit(Scene* scene, Camera* cam, glm::vec2 screenPos) {
        ClearSelection();
        if (!cam) return;

        RTSUnit* closestUnit = nullptr;
        float minDist = 50.0f; // Screen-space click tolerance

        // 1. Check screen-space distance to each player unit
        for (auto& unit : m_units) {
            if (unit.team != Team::Player) continue;
            glm::vec2 unitScreenPos;
            float zOffset = (unit.type == UnitType::Plane) ? 0.0f : 0.6f;
            if (WorldToScreen(cam, unit.position + glm::vec3(0.0f, 0.0f, zOffset), unitScreenPos)) {
                float dist = glm::distance(screenPos, unitScreenPos);
                if (dist < minDist) {
                    minDist = dist;
                    closestUnit = &unit;
                }
            }
        }

        // 2. Fallback: check ground ray hit against units
        if (!closestUnit) {
            glm::vec3 groundClick = ScreenToGround(cam, screenPos);
            float minWorldDist = 5.0f;
            for (auto& unit : m_units) {
                if (unit.team != Team::Player) continue;
                float d = glm::distance(glm::vec2(groundClick.x, groundClick.y), glm::vec2(unit.position.x, unit.position.y));
                float maxR = (unit.type == UnitType::Factory) ? 7.0f : ((unit.type == UnitType::Plane) ? 5.0f : 4.0f);
                if (d < maxR && d < minWorldDist) {
                    minWorldDist = d;
                    closestUnit = &unit;
                }
            }
        }

        if (closestUnit) {
            closestUnit->selected = true;
            if (closestUnit->type == UnitType::Factory) {
                gameMessage = "Headquarters selected. Right-click map to set rally point.";
            } else if (closestUnit->type == UnitType::LandFactory) {
                gameMessage = "Land Factory selected. Produce Tanks & Engineers.";
            } else if (closestUnit->type == UnitType::AirFactory) {
                gameMessage = "Air Factory selected. Produce Airplanes.";
            } else if (closestUnit->type == UnitType::Engineer) {
                gameMessage = "Engineer selected. Select structures to construct from Build Menu.";
            } else {
                gameMessage = "Unit selected. Right-click map to move.";
            }
        }
    }

    void RTSController::SelectUnitsInBox(Scene* scene, Camera* cam, glm::vec2 minScreen, glm::vec2 maxScreen) {
        ClearSelection();
        if (!cam) return;

        int count = 0;
        for (auto& unit : m_units) {
            if (unit.team != Team::Player || unit.type == UnitType::Factory || unit.type == UnitType::MassGen || unit.type == UnitType::EnergyGen) continue;
            glm::vec2 unitScreenPos;
            if (WorldToScreen(cam, unit.position, unitScreenPos)) {
                if (unitScreenPos.x >= minScreen.x && unitScreenPos.x <= maxScreen.x &&
                    unitScreenPos.y >= minScreen.y && unitScreenPos.y <= maxScreen.y) {
                    unit.selected = true;
                    count++;
                }
            }
        }
        if (count == 0) {
            SelectSingleUnit(scene, cam, minScreen);
        } else {
            gameMessage = std::to_string(count) + " units selected. Right-click map to move.";
        }
    }

    void RTSController::ClearSelection() {
        for (auto& unit : m_units) {
            unit.selected = false;
        }
    }

    void RTSController::IssueMoveOrder(Scene* scene, glm::vec3 targetPos) {
        std::vector<RTSUnit*> selectedUnits;
        for (auto& unit : m_units) {
            if (unit.selected && unit.team == Team::Player && unit.type != UnitType::Factory && unit.type != UnitType::MassGen && unit.type != UnitType::EnergyGen) {
                selectedUnits.push_back(&unit);
            }
        }
        if (selectedUnits.empty()) return;

        int num = (int)selectedUnits.size();
        for (int i = 0; i < num; i++) {
            glm::vec3 offset(0.0f);
            if (num > 1) {
                float angle = (float)i / (float)num * 2.0f * 3.14159265f;
                float radius = 2.0f * sqrt((float)num);
                offset = glm::vec3(cos(angle) * radius, sin(angle) * radius, 0.0f);
            }

            selectedUnits[i]->targetPosition = targetPos + offset;
            selectedUnits[i]->hasTarget = true;
        }
        gameMessage = "Move order issued to " + std::to_string(num) + " units.";
    }

    void RTSController::DrawHUD(ImVec2 viewportPos, ImVec2 viewportSize) {
        m_viewportPos = viewportPos;
        m_viewportSize = viewportSize;

        if (m_isDraggingBox) {
            ImGui::GetBackgroundDrawList()->AddRect(
                ImVec2(m_dragStartScreen.x, m_dragStartScreen.y),
                ImVec2(m_dragEndScreen.x, m_dragEndScreen.y),
                IM_COL32(0, 255, 100, 200), 0.0f, 0, 2.0f
            );
        }

        // Draw real-time prediction line formation preview
        if (m_isDraggingFormation && m_activeCam) {
            float dragScreenDist = glm::distance(m_formationStartScreen, m_formationEndScreen);
            std::vector<const RTSUnit*> selectedUnits;
            for (const auto& u : m_units) {
                if (u.selected && u.team == Team::Player && !IsStaticBuilding(u.type) && u.health > 0.0f) {
                    selectedUnits.push_back(&u);
                }
            }

            if (dragScreenDist > 18.0f && selectedUnits.size() > 1) {
                ImGui::GetBackgroundDrawList()->AddLine(
                    ImVec2(m_formationStartScreen.x, m_formationStartScreen.y),
                    ImVec2(m_formationEndScreen.x, m_formationEndScreen.y),
                    IM_COL32(0, 255, 180, 220), 2.5f
                );

                int N = (int)selectedUnits.size();
                glm::vec3 lineVec = m_formationEndWorld - m_formationStartWorld;
                for (int i = 0; i < N; i++) {
                    float t = (float)i / (float)(N - 1);
                    glm::vec3 slotWorld = m_formationStartWorld + lineVec * t;
                    glm::vec2 slotScreen;
                    if (WorldToScreen(m_activeCam, slotWorld, slotScreen)) {
                        ImGui::GetBackgroundDrawList()->AddCircle(
                            ImVec2(slotScreen.x, slotScreen.y),
                            14.0f,
                            IM_COL32(0, 255, 200, 220),
                            16,
                            2.0f
                        );
                        ImGui::GetBackgroundDrawList()->AddCircleFilled(
                            ImVec2(slotScreen.x, slotScreen.y),
                            4.0f,
                            IM_COL32(0, 255, 240, 255)
                        );
                    }
                }
            }
        }

        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 15, viewportPos.y + 15), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(410, 115), ImGuiCond_Always);
        ImGui::Begin("Rude RTS HUD", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("Mass: %.0f | Energy: %.0f", playerMass, playerEnergy);
        ImGui::SameLine(335);
        if (ImGui::Button("Menu", ImVec2(60, 22))) {
            m_showSkirmishMenu = !m_showSkirmishMenu;
        }
        ImGui::Text("Status: %s", gameMessage.c_str());
        
        int selectedCount = 0;
        int totalPlayerUnits = 0;
        bool isFactorySelected = false;
        const RTSUnit* singleSelectedUnit = nullptr;
        int playerStructures = 0, playerCombat = 0, enemyStructures = 0, enemyCombat = 0;
        for (const auto& u : m_units) {
            if (u.health <= 0.0f) continue;
            if (u.team == Team::Player) {
                if (u.type == UnitType::Factory || u.type == UnitType::MassGen || u.type == UnitType::EnergyGen) {
                    playerStructures++;
                    if (u.selected && u.type == UnitType::Factory) { isFactorySelected = true; singleSelectedUnit = &u; }
                } else {
                    playerCombat++;
                    totalPlayerUnits++;
                    if (u.selected) { selectedCount++; singleSelectedUnit = &u; }
                }
            } else {
                if (u.type == UnitType::Factory || u.type == UnitType::MassGen || u.type == UnitType::EnergyGen) {
                    enemyStructures++;
                } else {
                    enemyCombat++;
                }
            }
        }

        if (m_boostTimer > 0.0f) {
            int mins = (int)(m_boostTimer / 60.0f);
            int secs = (int)(m_boostTimer) % 60;
            char bBuf[64];
            snprintf(bBuf, sizeof(bBuf), "%02d:%02d", mins, secs);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.15f, 1.0f));
            ImGui::Text("[OVERDRIVE: %s - 2x MASS & 2x BUILD SPEED]", bBuf);
            ImGui::PopStyleColor();
        } else {
            ImGui::Text("Overdrive: Inactive ($1000M -> 10-Min Boost)");
        }

        const char* modeName = (m_skirmishSettings.gameMode == 0) ? "Annihilation" :
                               ((m_skirmishSettings.gameMode == 1) ? "Assassination (HQ)" : "Commander (SupCom FAF)");
        ImGui::Text("Mode: %s | Enemy: %d | Player: %d",
                    modeName,
                    enemyStructures + enemyCombat, playerStructures + playerCombat);

        if (isFactorySelected && singleSelectedUnit) {
            ImGui::Text("Selection: [Player Factory] (Health: %.0f/%.0f | Rally: %.0f, %.0f)", singleSelectedUnit->health, singleSelectedUnit->maxHealth, m_playerRallyPos.x, m_playerRallyPos.y);
        } else if (selectedCount == 1 && singleSelectedUnit) {
            const char* typeName = "Unit";
            if (singleSelectedUnit->type == UnitType::Plane) typeName = "Airplane (Rockets)";
            else if (singleSelectedUnit->type == UnitType::Tank) typeName = "Tank (Cannon)";
            else if (singleSelectedUnit->type == UnitType::Engineer) typeName = "Engineer (Builder)";
            else if (singleSelectedUnit->type == UnitType::CommandMech) typeName = "Armored Command Mech (ACU)";
            else if (singleSelectedUnit->type == UnitType::LandFactory) typeName = "Land Factory";
            else if (singleSelectedUnit->type == UnitType::AirFactory) typeName = "Air Factory";
            else if (singleSelectedUnit->type == UnitType::MassGen) typeName = "Mass Generator (+15M/s)";
            else if (singleSelectedUnit->type == UnitType::EnergyGen) typeName = "Energy Generator (+15E/s)";
            else if (singleSelectedUnit->type == UnitType::Turret) typeName = "Point Defense Tower";
            ImGui::Text("Selected: %s | Health: %.0f / %.0f", typeName, singleSelectedUnit->health, singleSelectedUnit->maxHealth);
        } else {
            ImGui::Text("Player Units: %d (Selected: %d)", totalPlayerUnits, selectedCount);
        }
        ImGui::End();

        bool isEngineerOrMechSelected = false;
        bool isLandFactorySelected = false;
        bool isAirFactorySelected = false;
        for (const auto& u : m_units) {
            if (u.selected && u.team == Team::Player) {
                if (u.type == UnitType::Engineer || u.type == UnitType::CommandMech) isEngineerOrMechSelected = true;
                else if (u.type == UnitType::LandFactory) isLandFactorySelected = true;
                else if (u.type == UnitType::AirFactory) isAirFactorySelected = true;
            }
        }

        if (isEngineerOrMechSelected) {
            ImGui::SetNextWindowPos(ImVec2(viewportPos.x + (viewportSize.x * 0.5f) - 310.0f, viewportPos.y + viewportSize.y - 105.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(620, 95), ImGuiCond_Always);
            ImGui::Begin("Construction Menu (Commander / Engineer)", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

            if (ImGui::Button("Land Factory ($180M/$120E)", ImVec2(145, 30))) {
                m_isPlacingBuilding = true;
                m_placingBuildingType = UnitType::LandFactory;
                gameMessage = "Click ground to place Land Factory. Right-click cancels.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Air Factory ($220M/$160E)", ImVec2(145, 30))) {
                m_isPlacingBuilding = true;
                m_placingBuildingType = UnitType::AirFactory;
                gameMessage = "Click ground to place Air Factory. Right-click cancels.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Mass Gen ($100M/$80E)", ImVec2(145, 30))) {
                m_isPlacingBuilding = true;
                m_placingBuildingType = UnitType::MassGen;
                gameMessage = "Click green deposit node to place Mass Gen. Right-click cancels.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Point Defense ($140M/$100E)", ImVec2(145, 30))) {
                m_isPlacingBuilding = true;
                m_placingBuildingType = UnitType::Turret;
                gameMessage = "Click ground to place Point Defense Tower. Right-click cancels.";
            }
            ImGui::Text("Click a structure button to place. Mass Gen snaps to green deposit nodes.");
            ImGui::End();
        } else if (isLandFactorySelected) {
            ImGui::SetNextWindowPos(ImVec2(viewportPos.x + (viewportSize.x * 0.5f) - 190.0f, viewportPos.y + viewportSize.y - 100.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(380, 90), ImGuiCond_Always);
            ImGui::Begin("Land Factory Production", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

            if (ImGui::Button("Build Tank ($100M/$100E)", ImVec2(175, 30))) {
                if (playerMass >= 100.0f && playerEnergy >= 100.0f) {
                    playerMass -= 100.0f; playerEnergy -= 100.0f;
                    m_pendingSpawns.push_back({UnitType::Tank, Team::Player, glm::vec3(-70.0f, (float)(rand() % 16 - 8), 0.0f)});
                    gameMessage = "Tank ordered!";
                } else gameMessage = "Not enough Mass or Energy!";
            }
            ImGui::SameLine();
            if (ImGui::Button("Build Engineer ($75M/$50E)", ImVec2(175, 30))) {
                if (playerMass >= 75.0f && playerEnergy >= 50.0f) {
                    playerMass -= 75.0f; playerEnergy -= 50.0f;
                    m_pendingSpawns.push_back({UnitType::Engineer, Team::Player, glm::vec3(-70.0f, (float)(rand() % 16 - 8), 0.0f)});
                    gameMessage = "Engineer ordered!";
                } else gameMessage = "Not enough Mass or Energy!";
            }
            ImGui::Text("Land Production | LeftClick: Select | RightClick: Move/Rally");
            ImGui::End();
        } else if (isAirFactorySelected) {
            ImGui::SetNextWindowPos(ImVec2(viewportPos.x + (viewportSize.x * 0.5f) - 140.0f, viewportPos.y + viewportSize.y - 100.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(280, 90), ImGuiCond_Always);
            ImGui::Begin("Air Factory Production", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

            if (ImGui::Button("Build Airplane ($150M/$150E)", ImVec2(250, 30))) {
                if (playerMass >= 150.0f && playerEnergy >= 150.0f) {
                    playerMass -= 150.0f; playerEnergy -= 150.0f;
                    m_pendingSpawns.push_back({UnitType::Plane, Team::Player, glm::vec3(-70.0f, (float)(rand() % 16 - 8), 12.0f)});
                    gameMessage = "Airplane ordered!";
                } else gameMessage = "Not enough Mass or Energy!";
            }
            ImGui::Text("Air Production | LeftClick: Select | RightClick: Move");
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(viewportPos.x + (viewportSize.x * 0.5f) - 270.0f, viewportPos.y + viewportSize.y - 100.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(540, 90), ImGuiCond_Always);
            ImGui::Begin("Headquarters Production", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

            if (ImGui::Button("Build Tank ($100M/$100E)", ImVec2(165, 30))) {
                if (playerMass >= 100.0f && playerEnergy >= 100.0f) {
                    playerMass -= 100.0f; playerEnergy -= 100.0f;
                    m_pendingSpawns.push_back({UnitType::Tank, Team::Player, glm::vec3(-70.0f, (float)(rand() % 16 - 8), 0.0f)});
                    gameMessage = "Tank ordered!";
                } else gameMessage = "Not enough Mass or Energy!";
            }
            ImGui::SameLine();
            if (ImGui::Button("Build Plane ($150M/$150E)", ImVec2(165, 30))) {
                if (playerMass >= 150.0f && playerEnergy >= 150.0f) {
                    playerMass -= 150.0f; playerEnergy -= 150.0f;
                    m_pendingSpawns.push_back({UnitType::Plane, Team::Player, glm::vec3(-70.0f, (float)(rand() % 16 - 8), 12.0f)});
                    gameMessage = "Airplane ordered!";
                } else gameMessage = "Not enough Mass or Energy!";
            }
            ImGui::SameLine();
            if (ImGui::Button("Build Engineer ($75M/$50E)", ImVec2(165, 30))) {
                if (playerMass >= 75.0f && playerEnergy >= 50.0f) {
                    playerMass -= 75.0f; playerEnergy -= 50.0f;
                    m_pendingSpawns.push_back({UnitType::Engineer, Team::Player, glm::vec3(-70.0f, (float)(rand() % 16 - 8), 0.0f)});
                    gameMessage = "Engineer ordered!";
                } else gameMessage = "Not enough Mass or Energy!";
            }
            ImGui::Text("LeftClick: Select | RightClick: Move / Rally | Alt+Drag: Orbit");
            ImGui::End();
        }
        if (gameOver) {
            DrawGameOver(viewportPos, viewportSize);
        }

        if (m_showSkirmishMenu) {
            DrawSkirmishMenu(viewportPos, viewportSize);
        }
    }

    void RTSController::DrawGameOver(ImVec2 viewportPos, ImVec2 viewportSize) {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + (viewportSize.x * 0.5f) - 220.0f, viewportPos.y + (viewportSize.y * 0.5f) - 160.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(440, 320), ImGuiCond_Always);
        ImGui::Begin("Game Over", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (m_playerWon) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
            ImGui::SetWindowFontScale(1.4f);
            ImGui::Text("  === VICTORY ===");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            ImGui::Separator();
            const char* gModeName = (m_skirmishSettings.gameMode == 0) ? "Annihilation" :
                                    ((m_skirmishSettings.gameMode == 1) ? "Command Center (HQ)" : "Commander Assassination (SupCom FAF)");
            ImGui::Text("Mode: %s", gModeName);
            ImGui::Text("%s", gameMessage.c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
            ImGui::SetWindowFontScale(1.4f);
            ImGui::Text("  === DEFEAT ===");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            ImGui::Separator();
            const char* gModeName = (m_skirmishSettings.gameMode == 0) ? "Annihilation" :
                                    ((m_skirmishSettings.gameMode == 1) ? "Command Center (HQ)" : "Commander Assassination (SupCom FAF)");
            ImGui::Text("Mode: %s", gModeName);
            ImGui::Text("%s", gameMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Text("Match Duration: %.1f seconds", m_matchTime);
        ImGui::Text("Final Mass: %.0f | Final Energy: %.0f", playerMass, playerEnergy);
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Restart Skirmish", ImVec2(420, 36))) {
            StartNewSkirmish();
        }

        if (ImGui::Button("Skirmish Settings & Menu", ImVec2(420, 36))) {
            m_showSkirmishMenu = true;
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Exit Game", ImVec2(420, 36))) {
            SDL_Event quitEv{};
            quitEv.type = SDL_QUIT;
            SDL_PushEvent(&quitEv);
        }
        ImGui::PopStyleColor();

        ImGui::End();
    }

    void RTSController::DrawSkirmishMenu(ImVec2 viewportPos, ImVec2 viewportSize) {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + (viewportSize.x * 0.5f) - 250.0f, viewportPos.y + (viewportSize.y * 0.5f) - 230.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(500, 460), ImGuiCond_Always);
        ImGui::Begin("Skirmish Setup & Main Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        ImGui::SetWindowFontScale(1.3f);
        ImGui::Text("  === RUDE RTS: SKIRMISH SETUP ===");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();

        // 1. Game Mode
        ImGui::Text("Game Mode:");
        ImGui::RadioButton("Commander Assassination (SupCom FAF - ACU Death)", &m_skirmishSettings.gameMode, 2);
        ImGui::RadioButton("Annihilation (Eliminate All Units & Buildings)", &m_skirmishSettings.gameMode, 0);
        ImGui::RadioButton("Command Center (HQ Assassination)", &m_skirmishSettings.gameMode, 1);
        ImGui::Separator();

        // 2. AI Difficulty
        ImGui::Text("Enemy AI Aggression:");
        ImGui::RadioButton("Easy (24s reinforcements)", &m_skirmishSettings.aiDifficulty, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Normal (16s)", &m_skirmishSettings.aiDifficulty, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Brutal (9s)", &m_skirmishSettings.aiDifficulty, 2);
        ImGui::Separator();

        // 3. Starting Resources
        ImGui::Text("Starting Resources (Mass & Energy):");
        if (ImGui::RadioButton("$200 (Low)", m_skirmishSettings.startingMass == 200)) {
            m_skirmishSettings.startingMass = 200; m_skirmishSettings.startingEnergy = 200;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("$500 (Standard)", m_skirmishSettings.startingMass == 500)) {
            m_skirmishSettings.startingMass = 500; m_skirmishSettings.startingEnergy = 500;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("$1000 (High)", m_skirmishSettings.startingMass == 1000)) {
            m_skirmishSettings.startingMass = 1000; m_skirmishSettings.startingEnergy = 1000;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("$5000 (Rich)", m_skirmishSettings.startingMass == 5000)) {
            m_skirmishSettings.startingMass = 5000; m_skirmishSettings.startingEnergy = 5000;
        }
        ImGui::Separator();

        // 4. Initial Forces
        ImGui::Text("Starting Units Per Side:");
        ImGui::SliderInt("Tanks", &m_skirmishSettings.initialTanks, 1, 6);
        ImGui::SliderInt("Planes", &m_skirmishSettings.initialPlanes, 0, 4);
        ImGui::Separator();
        ImGui::Spacing();

        // Action Buttons
        if (!gameOver) {
            if (ImGui::Button("Resume Match", ImVec2(235, 36))) {
                m_showSkirmishMenu = false;
            }
            ImGui::SameLine();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.25f, 1.0f));
        float startBtnWidth = (!gameOver) ? 235.0f : 480.0f;
        if (ImGui::Button("Start Skirmish", ImVec2(startBtnWidth, 36))) {
            StartNewSkirmish();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Exit Game", ImVec2(480, 36))) {
            SDL_Event quitEv{};
            quitEv.type = SDL_QUIT;
            SDL_PushEvent(&quitEv);
        }
        ImGui::PopStyleColor();

        ImGui::End();
    }

    void RTSController::Cleanup() {
        m_units.clear();
        m_selectionRingEntities.clear();
        m_projectiles.clear();
        m_deadUnitEntities.clear();
        m_explosionSpheres.clear();
        m_resourceNodes.clear();
        m_buildGhostEntity = nullptr;
        m_buildBeamEntity = nullptr;
        m_playerRallyEntity = nullptr;
        m_enemyRallyEntity = nullptr;
    }
}
