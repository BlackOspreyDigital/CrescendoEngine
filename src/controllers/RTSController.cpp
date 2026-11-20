#include "RTSController.hpp"
#include "core/Input.hpp"
#include "core/EnginePaths.hpp"
#include "deps/json/json.hpp"
#include "servers/interface/SymbolServer.hpp"
 #include "modules/gltf/AssetLoader.hpp"
#include <imgui.h>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <limits>
namespace Crescendo {

    static inline float GetUnitRadius(UnitType type) {
        switch (type) {
            case UnitType::Tank: return 2.2f;
            case UnitType::Plane: return 3.6f;
            case UnitType::SupportMech: return 3.0f;
            case UnitType::CommandMech: return 3.2f;
            case UnitType::Factory: return 6.0f;
            case UnitType::Turret: return 2.2f;
            case UnitType::AATurret: return 2.2f;
            case UnitType::LandFactory: return 5.5f;
            case UnitType::AirFactory: return 5.5f;
            case UnitType::Engineer: return 1.8f;
            case UnitType::MassGen: return 3.2f;
            case UnitType::EnergyGen: return 3.2f;
            case UnitType::ShieldGen: return 2.6f;
            case UnitType::MassStorage: return 3.0f;
            case UnitType::EnergyStorage: return 3.0f;
            case UnitType::NukeSilo: return 5.0f;
            default: return 2.0f;
        }
    }

    static inline bool IsAir(UnitType type) {
        return (type == UnitType::Plane);
    }

    static inline bool IsStaticBuilding(UnitType type) {
        return (type == UnitType::Factory || type == UnitType::LandFactory || type == UnitType::AirFactory ||
                type == UnitType::Turret || type == UnitType::AATurret || type == UnitType::MassGen || type == UnitType::EnergyGen ||
                type == UnitType::ShieldGen || type == UnitType::MassStorage || type == UnitType::EnergyStorage || type == UnitType::NukeSilo);
    }

    FactionId RTSController::GetTeamFaction(Team team) const {
        return (team == Team::Player) ? m_skirmishSettings.playerFaction : m_skirmishSettings.enemyFaction;
    }

    const FactionDef& RTSController::GetTeamFactionDef(Team team) const {
        return GetFactionDef(GetTeamFaction(team));
    }

    glm::vec3 RTSController::GetTeamDisplayColor(Team team, bool accent) const {
        const auto& fac = GetTeamFactionDef(team);
        glm::vec3 col = accent ? fac.accentColor : fac.primaryColor;
        bool isMirrorEnemy = (team == Team::Enemy) && (m_skirmishSettings.playerFaction == m_skirmishSettings.enemyFaction);
        return ApplyMirrorContrast(col, isMirrorEnemy);
    }

    glm::vec3 RTSController::GetTeamShieldColor(Team team) const {
        const auto& fac = GetTeamFactionDef(team);
        bool isMirrorEnemy = (team == Team::Enemy) && (m_skirmishSettings.playerFaction == m_skirmishSettings.enemyFaction);
        return ApplyMirrorContrast(fac.shieldColor, isMirrorEnemy);
    }

    void RTSController::GetUnitBuildCost(UnitType type, FactionId faction, int& outMass, int& outEnergy, float& outBuildTime) const {
        const auto& fac = GetFactionDef(faction);
        int baseM = 80, baseE = 60;
        float baseT = 10.0f;
        bool isMobileUnit = false;

        switch (type) {
            case UnitType::Tank:          baseM = 80;  baseE = 60;  baseT = 24.0f; isMobileUnit = true; break;
            case UnitType::Plane:         baseM = 120; baseE = 120; baseT = 38.0f; isMobileUnit = true; break;
            case UnitType::Engineer:      baseM = 60;  baseE = 40;  baseT = 25.0f; isMobileUnit = true; break;
            case UnitType::SupportMech:   baseM = 240; baseE = 180; baseT = 75.0f; isMobileUnit = true; break;
            case UnitType::LandFactory:   baseM = 160; baseE = 100; baseT = 90.0f; break;
            case UnitType::AirFactory:    baseM = 200; baseE = 140; baseT = 90.0f; break;
            case UnitType::Turret:        baseM = 120; baseE = 80;  baseT = 45.0f; break;
            case UnitType::AATurret:      baseM = 110; baseE = 80;  baseT = 45.0f; break;
            case UnitType::ShieldGen:     baseM = 140; baseE = 160; baseT = 80.0f; break;
            case UnitType::MassGen:       baseM = 90;  baseE = 60;  baseT = 55.0f; break;
            case UnitType::EnergyGen:     baseM = 60;  baseE = 90;  baseT = 55.0f; break;
            case UnitType::MassStorage:   baseM = 120; baseE = 100; baseT = 40.0f; break;
            case UnitType::EnergyStorage: baseM = 80;  baseE = 160; baseT = 40.0f; break;
            case UnitType::NukeSilo:      baseM = 500; baseE = 600; baseT = 240.0f; break;
            default:                      baseM = 100; baseE = 100; baseT = 20.0f; break;
        }

        float costMult = isMobileUnit ? fac.mods.unitCostMult : 1.0f;
        outMass = std::max(10, static_cast<int>(std::round(baseM * costMult)));
        outEnergy = std::max(10, static_cast<int>(std::round(baseE * costMult)));
        outBuildTime = baseT;
    }

    std::string RTSController::GetMatchupLabel() const {
        const auto& pFac = GetTeamFactionDef(Team::Player);
        const auto& eFac = GetTeamFactionDef(Team::Enemy);
        return std::string("[") + pFac.shortCode + "] " + pFac.name + " vs [" + eFac.shortCode + "] " + eFac.name;
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

    static void GetStylizedTreeData(int variant, bool isLod1, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
        glm::vec3 trunkColor(0.30f, 0.17f, 0.10f);
        glm::vec3 foliageTipColor(0.92f, 0.38f, 0.09f);
        glm::vec3 foliageMidColor(0.84f, 0.28f, 0.07f);
        glm::vec3 foliageBaseColor(0.72f, 0.19f, 0.05f);

        if (isLod1) {
            // Ultra-lightweight crossed-card + trunk silhouette for zoomed-out LOD1
            float hTrunk = 0.8f;
            float wTrunk = 0.16f;
            float rCanopy = (variant == 1) ? 1.45f : ((variant == 2) ? 1.30f : 1.15f);
            float zMin = 0.45f;
            float zMax = (variant == 0) ? 2.85f : ((variant == 1) ? 2.45f : 2.65f);

            auto pushQuad = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                                glm::vec3 n, glm::vec3 cBot, glm::vec3 cTop, float wBot, float wTop) {
                uint32_t b = static_cast<uint32_t>(verts.size());
                Vertex v0{}; v0.pos = p0; v0.normal = n; v0.color = cBot; v0.texCoord = {0, 0}; v0.texCoord1 = {wBot, 0};
                Vertex v1{}; v1.pos = p1; v1.normal = n; v1.color = cBot; v1.texCoord = {1, 0}; v1.texCoord1 = {wBot, 0};
                Vertex v2{}; v2.pos = p2; v2.normal = n; v2.color = cTop; v2.texCoord = {1, 1}; v2.texCoord1 = {wTop, 0};
                Vertex v3{}; v3.pos = p3; v3.normal = n; v3.color = cTop; v3.texCoord = {0, 1}; v3.texCoord1 = {wTop, 0};
                verts.push_back(v0); verts.push_back(v1); verts.push_back(v2); verts.push_back(v3);
                inds.push_back(b + 0); inds.push_back(b + 1); inds.push_back(b + 2);
                inds.push_back(b + 0); inds.push_back(b + 2); inds.push_back(b + 3);
            };

            // Crossed trunk planes
            pushQuad({-wTrunk, 0, 0}, {wTrunk, 0, 0}, {wTrunk, 0, hTrunk}, {-wTrunk, 0, hTrunk},
                     {0, 1, 0}, trunkColor, trunkColor, 0.0f, 0.25f);
            pushQuad({0, -wTrunk, 0}, {0, wTrunk, 0}, {0, wTrunk, hTrunk}, {0, -wTrunk, hTrunk},
                     {1, 0, 0}, trunkColor, trunkColor, 0.0f, 0.25f);

            // Crossed diamond canopy silhouette (two perpendicular triangles + horizontal cross)
            auto pushTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 ca, glm::vec3 cb, glm::vec3 cc,
                               glm::vec3 n, float wa, float wb, float wc) {
                uint32_t base = static_cast<uint32_t>(verts.size());
                Vertex va{}; va.pos = a; va.normal = n; va.color = ca; va.texCoord = {0, 0}; va.texCoord1 = {wa, 0};
                Vertex vb{}; vb.pos = b; vb.normal = n; vb.color = cb; vb.texCoord = {1, 0}; vb.texCoord1 = {wb, 0};
                Vertex vc{}; vc.pos = c; vc.normal = n; vc.color = cc; vc.texCoord = {0.5f, 1}; vc.texCoord1 = {wc, 0};
                verts.push_back(va); verts.push_back(vb); verts.push_back(vc);
                inds.push_back(base + 0); inds.push_back(base + 1); inds.push_back(base + 2);
            };
            pushTri({-rCanopy, 0, zMin}, {rCanopy, 0, zMin}, {0, 0, zMax}, foliageBaseColor, foliageBaseColor, foliageTipColor, {0, 1, 0.35f}, 0.3f, 0.3f, 1.0f);
            pushTri({0, -rCanopy, zMin}, {0, rCanopy, zMin}, {0, 0, zMax}, foliageBaseColor, foliageBaseColor, foliageTipColor, {1, 0, 0.35f}, 0.3f, 0.3f, 1.0f);
            return;
        }

        // LOD0: Tapered trunk + multi-tier stylized canopy with wind weights in texCoord1.x
        int segments = 6;
        float radiusBot = 0.22f;
        float radiusTop = 0.14f;
        float zBot = 0.0f;
        float zTop = (variant == 1) ? 0.85f : 0.72f;

        for (int i = 0; i <= segments; ++i) {
            float theta = (float)i / (float)segments * 2.0f * 3.14159265358979323846f;
            float cosT = cos(theta);
            float sinT = sin(theta);

            Vertex v1{};
            v1.pos = glm::vec3(cosT * radiusBot, sinT * radiusBot, zBot);
            v1.normal = glm::vec3(cosT, sinT, 0.0f);
            v1.texCoord = glm::vec2((float)i / segments, 0.0f);
            v1.texCoord1 = glm::vec2(0.0f, 0.0f); // Anchored at ground
            v1.color = trunkColor;

            Vertex v2{};
            v2.pos = glm::vec3(cosT * radiusTop, sinT * radiusTop, zTop);
            v2.normal = glm::vec3(cosT, sinT, 0.0f);
            v2.texCoord = glm::vec2((float)i / segments, 1.0f);
            v2.texCoord1 = glm::vec2(0.22f, 0.0f);
            v2.color = trunkColor;

            verts.push_back(v1);
            verts.push_back(v2);
        }
        for (int i = 0; i < segments; ++i) {
            uint32_t base = i * 2;
            inds.push_back(base + 0); inds.push_back(base + 2); inds.push_back(base + 1);
            inds.push_back(base + 1); inds.push_back(base + 2); inds.push_back(base + 3);
        }

        auto addTier = [&](float zBase, float rBase, float zApex, float yawOff, glm::vec3 cBase, glm::vec3 cApex, float wBase, float wApex) {
            uint32_t apexIdx = static_cast<uint32_t>(verts.size());
            Vertex vApex{};
            vApex.pos = glm::vec3(0.0f, 0.0f, zApex);
            vApex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            vApex.texCoord = glm::vec2(0.5f, 1.0f);
            vApex.texCoord1 = glm::vec2(wApex, 0.0f);
            vApex.color = cApex;
            verts.push_back(vApex);

            uint32_t baseStart = static_cast<uint32_t>(verts.size());
            for (int i = 0; i <= segments; ++i) {
                float theta = (float)i / (float)segments * 2.0f * 3.14159265358979323846f + yawOff;
                float cosT = cos(theta);
                float sinT = sin(theta);
                Vertex v{};
                v.pos = glm::vec3(cosT * rBase, sinT * rBase, zBase);
                v.normal = glm::normalize(glm::vec3(cosT * (zApex - zBase), sinT * (zApex - zBase), rBase * 0.85f));
                v.texCoord = glm::vec2((float)i / segments, 0.0f);
                v.texCoord1 = glm::vec2(wBase, 0.0f);
                v.color = cBase;
                verts.push_back(v);
            }
            for (int i = 0; i < segments; ++i) {
                uint32_t curr = baseStart + i;
                uint32_t nxt = baseStart + i + 1;
                inds.push_back(curr);
                inds.push_back(nxt);
                inds.push_back(apexIdx);
            }

            uint32_t botCenterIdx = static_cast<uint32_t>(verts.size());
            Vertex vBot{};
            vBot.pos = glm::vec3(0.0f, 0.0f, zBase);
            vBot.normal = glm::vec3(0.0f, 0.0f, -1.0f);
            vBot.texCoord = glm::vec2(0.5f, 0.5f);
            vBot.texCoord1 = glm::vec2(wBase * 0.8f, 0.0f);
            vBot.color = cBase * 0.65f;
            verts.push_back(vBot);
            for (int i = 0; i < segments; ++i) {
                uint32_t curr = baseStart + i;
                uint32_t nxt = baseStart + i + 1;
                inds.push_back(botCenterIdx);
                inds.push_back(nxt);
                inds.push_back(curr);
            }
        };

        if (variant == 0) {
            // Tall 2-tier alien conifer
            addTier(0.55f, 1.25f, 1.85f, 0.00f, foliageBaseColor, foliageMidColor, 0.35f, 0.72f);
            addTier(1.30f, 0.88f, 2.85f, 0.52f, foliageMidColor, foliageTipColor, 0.65f, 1.00f);
        } else if (variant == 1) {
            // Broad windswept mesa canopy
            addTier(0.68f, 1.48f, 1.75f, 0.25f, foliageBaseColor, foliageMidColor, 0.38f, 0.78f);
            addTier(1.25f, 1.05f, 2.40f, 0.78f, foliageMidColor, foliageTipColor, 0.68f, 1.00f);
        } else {
            // 3-tier slender spire
            addTier(0.50f, 1.15f, 1.50f, 0.00f, foliageBaseColor, foliageMidColor, 0.32f, 0.60f);
            addTier(1.10f, 0.90f, 2.15f, 0.45f, foliageMidColor, foliageTipColor, 0.58f, 0.84f);
            addTier(1.65f, 0.60f, 2.75f, 0.90f, foliageMidColor, foliageTipColor, 0.80f, 1.00f);
        }
    }

    static void GetWaterPlaneData(std::vector<Vertex>& verts, std::vector<uint32_t>& inds, int gridRes = 64, float mapSize = 1000.0f) {
        int stride = gridRes + 1;
        float cellSize = mapSize / (float)gridRes;
        float halfSize = mapSize * 0.5f;

        for (int j = 0; j <= gridRes; ++j) {
            float y = -halfSize + (float)j * cellSize;
            float v = (float)j / (float)gridRes;
            for (int i = 0; i <= gridRes; ++i) {
                float x = -halfSize + (float)i * cellSize;
                float u = (float)i / (float)gridRes;

                Vertex vx{};
                vx.pos = glm::vec3(x, y, 0.0f);
                vx.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                vx.texCoord = glm::vec2(u, v) * 25.0f; // Wave texture tiling
                vx.color = glm::vec3(0.08f, 0.38f, 0.58f);
                verts.push_back(vx);
            }
        }

        for (int j = 0; j < gridRes; ++j) {
            for (int i = 0; i < gridRes; ++i) {
                uint32_t i0 = j * stride + i;
                uint32_t i1 = j * stride + (i + 1);
                uint32_t i2 = (j + 1) * stride + (i + 1);
                uint32_t i3 = (j + 1) * stride + i;

                inds.push_back(i0); inds.push_back(i1); inds.push_back(i2);
                inds.push_back(i0); inds.push_back(i2); inds.push_back(i3);
            }
        }
    }

    RTSController::~RTSController() {
        Cleanup();
    }

    float RTSController::GetTerrainHeight(float worldX, float worldY) const {
        if (m_heightmap.empty()) return 0.0f;
        float nx = (worldX + 500.0f) / 1000.0f;
        float ny = (worldY + 500.0f) / 1000.0f;
        if (nx < 0.0f || nx > 1.0f || ny < 0.0f || ny > 1.0f) return 0.0f;

        float gx = nx * (float)TERRAIN_GRID_RES;
        float gy = ny * (float)TERRAIN_GRID_RES;
        int x0 = std::clamp((int)gx, 0, TERRAIN_GRID_RES - 1);
        int y0 = std::clamp((int)gy, 0, TERRAIN_GRID_RES - 1);
        int x1 = x0 + 1;
        int y1 = y0 + 1;

        float tx = gx - (float)x0;
        float ty = gy - (float)y0;

        int stride = TERRAIN_GRID_RES + 1;
        float h00 = m_heightmap[y0 * stride + x0];
        float h10 = m_heightmap[y0 * stride + x1];
        float h01 = m_heightmap[y1 * stride + x0];
        float h11 = m_heightmap[y1 * stride + x1];

        float top = (1.0f - tx) * h00 + tx * h10;
        float bottom = (1.0f - tx) * h01 + tx * h11;
        return (1.0f - ty) * top + ty * bottom;
    }

    glm::vec3 RTSController::GetTerrainNormal(float worldX, float worldY) const {
        float eps = 4.0f;
        float hL = GetTerrainHeight(worldX - eps, worldY);
        float hR = GetTerrainHeight(worldX + eps, worldY);
        float hD = GetTerrainHeight(worldX, worldY - eps);
        float hU = GetTerrainHeight(worldX, worldY + eps);
        return glm::normalize(glm::vec3(-(hR - hL) / (2.0f * eps), -(hU - hD) / (2.0f * eps), 1.0f));
    }

    void RTSController::RecomputeTerrainNormals(int gx0, int gx1, int gy0, int gy1) {
        int N = TERRAIN_GRID_RES;
        int stride = N + 1;
        float cellSize = TERRAIN_MAP_SIZE / (float)N;

        gx0 = std::clamp(gx0, 0, N);
        gx1 = std::clamp(gx1, 0, N);
        gy0 = std::clamp(gy0, 0, N);
        gy1 = std::clamp(gy1, 0, N);

        for (int j = gy0; j <= gy1; ++j) {
            for (int i = gx0; i <= gx1; ++i) {
                int leftI = std::max(0, i - 1);
                int rightI = std::min(N, i + 1);
                int downJ = std::max(0, j - 1);
                int upJ = std::min(N, j + 1);

                float hL = m_heightmap[j * stride + leftI];
                float hR = m_heightmap[j * stride + rightI];
                float hD = m_heightmap[downJ * stride + i];
                float hU = m_heightmap[upJ * stride + i];

                float dx = (rightI - leftI) * cellSize;
                float dy = (upJ - downJ) * cellSize;

                float dh_dx = (hR - hL) / (dx > 0.001f ? dx : 1.0f);
                float dh_dy = (hU - hD) / (dy > 0.001f ? dy : 1.0f);

                glm::vec3 normal = glm::normalize(glm::vec3(-dh_dx, -dh_dy, 1.0f));
                int idx = j * stride + i;
                m_terrainVertices[idx].normal = normal;

                float slope = 1.0f - normal.z;
                float alt = m_heightmap[idx];
                if (slope > 0.30f || alt > 55.0f) {
                    m_terrainVertices[idx].color = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.55f, 0.50f, 0.45f), std::clamp((slope - 0.2f) * 2.5f, 0.0f, 0.85f));
                } else {
                    m_terrainVertices[idx].color = glm::vec3(1.0f, 1.0f, 1.0f);
                }
            }
        }
    }

    void RTSController::GenerateProceduralTerrain(int preset) {
        int N = TERRAIN_GRID_RES;
        int stride = N + 1;
        float mapSize = TERRAIN_MAP_SIZE;
        float cellSize = mapSize / (float)N;
        m_heightmap.assign(stride * stride, 0.0f);

        if (preset == 1) {
            // Strategic Mountain Pass (Classic RTS dividing ridge & valleys)
            for (int j = 0; j <= N; ++j) {
                float wy = -500.0f + (float)j * cellSize;
                for (int i = 0; i <= N; ++i) {
                    float wx = -500.0f + (float)i * cellSize;

                    // Base clearings: flat zones around Player base (-260, 0) and Enemy base (260, 0)
                    float distPlayer = glm::distance(glm::vec2(wx, wy), glm::vec2(-260.0f, 0.0f));
                    float distEnemy  = glm::distance(glm::vec2(wx, wy), glm::vec2(260.0f, 0.0f));
                    if (distPlayer < 85.0f || distEnemy < 85.0f) {
                        m_heightmap[j * stride + i] = 0.0f;
                        continue;
                    }

                    // North & South perimeter alpine mountain ranges
                    float northDist = std::max(0.0f, (std::abs(wy) - 150.0f) / 180.0f);
                    float perimeterMtn = pow(northDist, 1.4f) * 65.0f;

                    // Central spine mountain ridge around X = 0
                    float centerDist = std::abs(wx);
                    float centerRidge = std::max(0.0f, 1.0f - (centerDist / 95.0f));

                    // 2 Strategic drivable choke-point passes at Y = +110m and Y = -110m
                    float passNorth = exp(-pow((wy - 110.0f) / 42.0f, 2.0f));
                    float passSouth = exp(-pow((wy - (-110.0f)) / 42.0f, 2.0f));
                    float passClearance = 1.0f - std::max(passNorth, passSouth) * 0.95f;

                    float spineHeight = centerRidge * passClearance * 45.0f;
                    m_heightmap[j * stride + i] = std::clamp(perimeterMtn + spineHeight, 0.0f, 120.0f);
                }
            }
        } else if (preset == 2) {
            // Rolling Hills
            for (int j = 0; j <= N; ++j) {
                float wy = -500.0f + (float)j * cellSize;
                for (int i = 0; i <= N; ++i) {
                    float wx = -500.0f + (float)i * cellSize;
                    float distPlayer = glm::distance(glm::vec2(wx, wy), glm::vec2(-260.0f, 0.0f));
                    float distEnemy  = glm::distance(glm::vec2(wx, wy), glm::vec2(260.0f, 0.0f));
                    if (distPlayer < 80.0f || distEnemy < 80.0f) {
                        m_heightmap[j * stride + i] = 0.0f;
                        continue;
                    }
                    float h1 = sin(wx * 0.015f) * cos(wy * 0.015f) * 18.0f;
                    float h2 = sin(wx * 0.035f + 1.2f) * cos(wy * 0.032f + 0.8f) * 8.0f;
                    m_heightmap[j * stride + i] = std::max(0.0f, h1 + h2);
                }
            }
        }
    }

    void RTSController::BuildProceduralTerrain(IRenderer* renderer, Scene* scene) {
        if (!renderer || !scene) return;

        int N = TERRAIN_GRID_RES;
        int stride = N + 1;
        float mapSize = TERRAIN_MAP_SIZE;
        float cellSize = mapSize / (float)N;

        if (m_heightmap.size() != static_cast<size_t>(stride * stride)) {
            GenerateProceduralTerrain(0); // Default to clean flat plain (Z = 0.0) as requested
        }

        m_terrainVertices.clear();
        m_terrainVertices.reserve(stride * stride);

        for (int j = 0; j <= N; ++j) {
            float wy = -500.0f + (float)j * cellSize;
            for (int i = 0; i <= N; ++i) {
                float wx = -500.0f + (float)i * cellSize;
                float h = m_heightmap[j * stride + i];

                Vertex v{};
                v.pos = glm::vec3(wx, wy, h);
                v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                v.texCoord = glm::vec2((float)i / (float)N, (float)j / (float)N) * 24.0f;
                v.color = glm::vec3(1.0f, 1.0f, 1.0f);
                m_terrainVertices.push_back(v);
            }
        }

        m_terrainIndices.clear();
        m_terrainIndices.reserve(N * N * 6);
        for (int j = 0; j < N; ++j) {
            for (int i = 0; i < N; ++i) {
                uint32_t v00 = static_cast<uint32_t>(j * stride + i);
                uint32_t v10 = static_cast<uint32_t>(j * stride + (i + 1));
                uint32_t v01 = static_cast<uint32_t>((j + 1) * stride + i);
                uint32_t v11 = static_cast<uint32_t>((j + 1) * stride + (i + 1));

                m_terrainIndices.push_back(v00);
                m_terrainIndices.push_back(v10);
                m_terrainIndices.push_back(v01);

                m_terrainIndices.push_back(v10);
                m_terrainIndices.push_back(v11);
                m_terrainIndices.push_back(v01);
            }
        }

        RecomputeTerrainNormals(0, N, 0, N);

        m_terrainMeshIndex = renderer->acquireMesh("PROCEDURAL", "RTS_TerrainMesh", m_terrainVertices, m_terrainIndices);

        m_terrainEntity = scene->CreateEntity("prop_dynamic");
        m_terrainEntity->targetName = "BaseFloor";
        m_terrainEntity->modelIndex = m_terrainMeshIndex;
        m_terrainEntity->origin = glm::dvec3(0.0, 0.0, 0.0);
        m_terrainEntity->scale = glm::vec3(1.0f, 1.0f, 1.0f);
        int earthTexID = renderer->acquireTexture("assets/textures/meridian_earth_generic.png");
        m_terrainEntity->textureID = earthTexID;
        m_terrainEntity->albedoColor = glm::vec3(1.0f, 1.0f, 1.0f);
        m_terrainEntity->roughness = 0.95f;
        m_terrainEntity->metallic = 0.0f;

        // Subdivided Animated Water Plane
        std::vector<Vertex> waterVerts;
        std::vector<uint32_t> waterInds;
        GetWaterPlaneData(waterVerts, waterInds, 64, TERRAIN_MAP_SIZE);
        m_waterMeshIndex = renderer->acquireMesh("PROCEDURAL", "RTS_WaterPlane", waterVerts, waterInds);

        m_waterEntity = scene->CreateEntity("prop_water");
        m_waterEntity->targetName = "BattlefieldWater";
        m_waterEntity->modelIndex = m_waterMeshIndex;
        m_waterEntity->origin = glm::dvec3(0.0, 0.0, m_enableWater ? m_waterHeight : -999.0);
        m_waterEntity->scale = m_enableWater ? glm::vec3(1.0f, 1.0f, 1.0f) : glm::vec3(0.0f);
        m_waterEntity->albedoColor = glm::vec3(0.08f, 0.38f, 0.58f);
        m_waterEntity->roughness = 0.05f;
        m_waterEntity->metallic = 0.1f;
    }

    void RTSController::PushSculptUndoState() {
        if (m_heightmap.empty()) return;
        m_sculptUndoStack.push_back(m_heightmap);
        if (m_sculptUndoStack.size() > 25) {
            m_sculptUndoStack.erase(m_sculptUndoStack.begin());
        }
        m_sculptRedoStack.clear();
    }

    void RTSController::SculptUndo() {
        if (m_sculptUndoStack.empty()) {
            gameMessage = "Nothing to undo.";
            return;
        }
        m_sculptRedoStack.push_back(m_heightmap);
        m_heightmap = m_sculptUndoStack.back();
        m_sculptUndoStack.pop_back();

        int stride = TERRAIN_GRID_RES + 1;
        for (int i = 0; i <= TERRAIN_GRID_RES; ++i) {
            for (int j = 0; j <= TERRAIN_GRID_RES; ++j) {
                int idx = i * stride + j;
                m_terrainVertices[idx].pos.z = m_heightmap[idx];
            }
        }
        RecomputeTerrainNormals(0, TERRAIN_GRID_RES, 0, TERRAIN_GRID_RES);
        if (m_renderer && m_terrainMeshIndex >= 0) {
            m_renderer->updateMesh(m_terrainMeshIndex, m_terrainVertices);
        }
        UpdateResourceNodesZ();
        UpdateTreesZ();
        gameMessage = "Map Sculpt Undo (" + std::to_string(m_sculptUndoStack.size()) + " undos remaining).";
    }

    void RTSController::SculptRedo() {
        if (m_sculptRedoStack.empty()) {
            gameMessage = "Nothing to redo.";
            return;
        }
        m_sculptUndoStack.push_back(m_heightmap);
        m_heightmap = m_sculptRedoStack.back();
        m_sculptRedoStack.pop_back();

        int stride = TERRAIN_GRID_RES + 1;
        for (int i = 0; i <= TERRAIN_GRID_RES; ++i) {
            for (int j = 0; j <= TERRAIN_GRID_RES; ++j) {
                int idx = i * stride + j;
                m_terrainVertices[idx].pos.z = m_heightmap[idx];
            }
        }
        RecomputeTerrainNormals(0, TERRAIN_GRID_RES, 0, TERRAIN_GRID_RES);
        if (m_renderer && m_terrainMeshIndex >= 0) {
            m_renderer->updateMesh(m_terrainMeshIndex, m_terrainVertices);
        }
        UpdateResourceNodesZ();
        UpdateTreesZ();
        gameMessage = "Map Sculpt Redo.";
    }

    void RTSController::ApplyTerrainBrush(glm::vec3 worldPos, float radius, float strength, int brushType, float targetHeight) {
        if (m_heightmap.empty() || m_terrainVertices.empty()) return;

        int N = TERRAIN_GRID_RES;
        int stride = N + 1;
        float cellSize = TERRAIN_MAP_SIZE / (float)N;

        float minX = worldPos.x - radius;
        float maxX = worldPos.x + radius;
        float minY = worldPos.y - radius;
        float maxY = worldPos.y + radius;

        int gx0 = std::clamp((int)((minX + 500.0f) / cellSize), 0, N);
        int gx1 = std::clamp((int)((maxX + 500.0f) / cellSize) + 1, 0, N);
        int gy0 = std::clamp((int)((minY + 500.0f) / cellSize), 0, N);
        int gy1 = std::clamp((int)((maxY + 500.0f) / cellSize) + 1, 0, N);

        float avgHeight = 0.0f;
        int sampleCount = 0;
        if (brushType == 2) {
            for (int gy = gy0; gy <= gy1; ++gy) {
                for (int gx = gx0; gx <= gx1; ++gx) {
                    float wx = -500.0f + gx * cellSize;
                    float wy = -500.0f + gy * cellSize;
                    float d = glm::distance(glm::vec2(wx, wy), glm::vec2(worldPos.x, worldPos.y));
                    if (d <= radius) {
                        avgHeight += m_heightmap[gy * stride + gx];
                        sampleCount++;
                    }
                }
            }
            if (sampleCount > 0) avgHeight /= (float)sampleCount;
        }

        for (int gy = gy0; gy <= gy1; ++gy) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                float wx = -500.0f + gx * cellSize;
                float wy = -500.0f + gy * cellSize;
                float dist = glm::distance(glm::vec2(wx, wy), glm::vec2(worldPos.x, worldPos.y));
                if (dist <= radius) {
                    float falloff = 0.5f + 0.5f * cos((dist / radius) * 3.14159265f);
                    float delta = strength * falloff;
                    int idx = gy * stride + gx;

                    switch (brushType) {
                        case 0: // Raise
                            m_heightmap[idx] = std::clamp(m_heightmap[idx] + delta, 0.0f, 150.0f);
                            break;
                        case 1: // Lower
                            m_heightmap[idx] = std::clamp(m_heightmap[idx] - delta, -20.0f, 150.0f);
                            break;
                        case 2: // Smooth
                            m_heightmap[idx] = glm::mix(m_heightmap[idx], avgHeight, std::clamp(delta * 0.35f, 0.0f, 1.0f));
                            break;
                        case 3: // Flatten
                            m_heightmap[idx] = glm::mix(m_heightmap[idx], targetHeight, std::clamp(delta * 0.45f, 0.0f, 1.0f));
                            break;
                        case 4: { // Plateau / Terrace
                            float step = std::max(2.0f, m_forgePlateauStep);
                            float snapped = std::round(m_heightmap[idx] / step) * step;
                            m_heightmap[idx] = glm::mix(m_heightmap[idx], snapped, std::clamp(delta * 0.45f, 0.0f, 1.0f));
                            break;
                        }
                        case 5: { // Ramp / Incline
                            m_heightmap[idx] = glm::mix(m_heightmap[idx], worldPos.z, std::clamp(delta * 0.35f, 0.0f, 1.0f));
                            break;
                        }
                        case 6: { // Noise / Roughen
                            float nVal = (sin((float)gx * 0.7f) * cos((float)gy * 0.7f) + sin((float)(gx + gy) * 0.35f)) * 0.5f;
                            m_heightmap[idx] = std::clamp(m_heightmap[idx] + nVal * delta * 0.5f, -20.0f, 150.0f);
                            break;
                        }
                    }
                    m_terrainVertices[idx].pos.z = m_heightmap[idx];
                }
            }
        }

        RecomputeTerrainNormals(gx0, gx1, gy0, gy1);

        if (m_renderer && m_terrainMeshIndex >= 0) {
            m_renderer->updateMesh(m_terrainMeshIndex, m_terrainVertices);
        }
        UpdateResourceNodesZ();
    }

    void RTSController::UpdateResourceNodesZ() {
        for (auto& node : m_resourceNodes) {
            float groundZ = GetTerrainHeight(node.position.x, node.position.y);
            node.position.z = groundZ + 0.15f;
            if (node.entity) {
                node.entity->origin.x = node.position.x;
                node.entity->origin.y = node.position.y;
                node.entity->origin.z = node.position.z;
                glm::vec3 normal = GetTerrainNormal(node.position.x, node.position.y);
                float pitchRad = asin(std::clamp(-normal.y, -1.0f, 1.0f));
                float rollRad  = atan2(normal.x, normal.z);
                node.entity->angles = glm::degrees(glm::vec3(pitchRad, 0.0f, rollRad));
            }
        }
    }

    void RTSController::SpawnResourceNode(glm::vec3 worldPos, ResourceNodeType type) {
        if (!m_scene) return;

        for (const auto& existing : m_resourceNodes) {
            float d = glm::distance(glm::vec2(worldPos.x, worldPos.y), glm::vec2(existing.position.x, existing.position.y));
            if (d < 10.0f) {
                gameMessage = "Cannot place resource deposit too close to another node!";
                return;
            }
        }

        float groundZ = GetTerrainHeight(worldPos.x, worldPos.y);
        ResourceDepositNode node;
        node.position = glm::vec3(worldPos.x, worldPos.y, groundZ + 0.15f);
        node.type = type;
        node.occupied = false;
        node.entity = m_scene->CreateEntity("prop_dynamic");
        node.entity->targetName = (type == ResourceNodeType::Mass) ? "MassDepositNode" : "PowerDepositNode";
        node.entity->modelIndex = (type == ResourceNodeType::Energy) ? m_cylinderMeshIndex : m_cubeMeshIndex;
        node.entity->origin = glm::dvec3(node.position.x, node.position.y, node.position.z);
        node.entity->scale = glm::vec3(3.8f, 3.8f, 0.2f);
        node.entity->albedoColor = (type == ResourceNodeType::Mass) ? glm::vec3(0.1f, 0.95f, 0.3f) : glm::vec3(0.95f, 0.85f, 0.15f);
        node.entity->emission = 3.5f;
        node.entity->roughness = 0.2f;

        glm::vec3 normal = GetTerrainNormal(node.position.x, node.position.y);
        float pitchRad = asin(std::clamp(-normal.y, -1.0f, 1.0f));
        float rollRad  = atan2(normal.x, normal.z);
        node.entity->angles = glm::degrees(glm::vec3(pitchRad, 0.0f, rollRad));

        m_resourceNodes.push_back(node);
        SpawnExplosionCluster(node.position, 0.8f, 4);
        gameMessage = (type == ResourceNodeType::Mass) ? "Placed Mass Deposit Node (Green)" : "Placed Energy Deposit Node (Yellow)";
    }

    void RTSController::ResetDefaultResourceNodes() {
        if (!m_scene) return;
        for (auto& n : m_resourceNodes) {
            if (n.entity) {
                n.entity->scale = glm::vec3(0.0f);
                n.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
            }
        }
        m_resourceNodes.clear();

        struct DepositDef {
            glm::vec3 pos;
            ResourceNodeType type;
        };

        DepositDef depositPositions[] = {
            { glm::vec3(-295.0f,  35.0f, 0.0f), ResourceNodeType::Mass },
            { glm::vec3(-295.0f, -35.0f, 0.0f), ResourceNodeType::Energy },
            { glm::vec3(-180.0f,  80.0f, 0.0f), ResourceNodeType::Mass },
            { glm::vec3(-180.0f, -80.0f, 0.0f), ResourceNodeType::Energy },
            { glm::vec3(-120.0f,   0.0f, 0.0f), ResourceNodeType::Mass },

            { glm::vec3(   0.0f, 150.0f, 0.0f), ResourceNodeType::Energy },
            { glm::vec3(   0.0f,-150.0f, 0.0f), ResourceNodeType::Energy },
            { glm::vec3(   0.0f,   0.0f, 0.0f), ResourceNodeType::Mass },
            { glm::vec3( -60.0f, 220.0f, 0.0f), ResourceNodeType::Mass },
            { glm::vec3(  60.0f,-220.0f, 0.0f), ResourceNodeType::Mass },

            { glm::vec3( 120.0f,   0.0f, 0.0f), ResourceNodeType::Mass },
            { glm::vec3( 180.0f,  80.0f, 0.0f), ResourceNodeType::Mass },
            { glm::vec3( 180.0f, -80.0f, 0.0f), ResourceNodeType::Energy },
            { glm::vec3( 295.0f,  35.0f, 0.0f), ResourceNodeType::Mass },
            { glm::vec3( 295.0f, -35.0f, 0.0f), ResourceNodeType::Energy }
        };

        for (const auto& dDef : depositPositions) {
            SpawnResourceNode(dDef.pos, dDef.type);
        }
        gameMessage = "Reset default resource nodes layout on terrain.";
    }

    void RTSController::SyncFoliageBatches() {
        if (!m_scene || m_treeBatchIndices.empty()) {
            m_foliageDirty = false;
            return;
        }
        for (size_t bIdx : m_treeBatchIndices) {
            if (bIdx < m_scene->foliageBatches.size()) {
                m_scene->foliageBatches[bIdx].instances.clear();
            }
        }
        for (const auto& t : m_trees) {
            size_t vIdx = static_cast<size_t>(std::clamp(t.variant, 0, static_cast<int>(m_treeBatchIndices.size()) - 1));
            size_t bIdx = m_treeBatchIndices[vIdx];
            if (bIdx < m_scene->foliageBatches.size()) {
                FoliageInstance inst{};
                inst.position = t.position;
                inst.yawRadians = glm::radians(t.rotation);
                inst.scale = t.scale;
                inst.windPhase = t.windPhase;
                inst.tint = glm::vec4(t.tint, 1.0f);
                m_scene->foliageBatches[bIdx].instances.push_back(inst);
            }
        }
        m_foliageDirty = false;
    }

    void RTSController::SpawnTree(const glm::vec3& pos, float scale, float rot, int variant) {
        if (!m_scene) return;
        RTSTree tree;
        tree.position = pos;
        tree.position.z = GetTerrainHeight(pos.x, pos.y);
        float sVar = 0.88f + (float)(rand() % 24) * 0.01f;
        tree.scale = glm::vec3(scale * sVar, scale * sVar, scale * sVar);
        tree.rotation = (rot != 0.0f) ? rot : (float)(rand() % 360);
        int numVariants = std::max(1, static_cast<int>(m_treeBatchIndices.size()));
        tree.variant = (variant >= 0) ? (variant % numVariants) : (rand() % numVariants);
        tree.windPhase = (float)(rand() % 628) * 0.01f;

        float rVar = 0.94f + (float)(rand() % 14) * 0.01f;
        float gVar = rVar * (0.82f + (float)(rand() % 18) * 0.01f);
        tree.tint = glm::vec3(rVar, gVar, 0.72f);

        m_trees.push_back(tree);
        m_foliageDirty = true;
    }

    void RTSController::ClearAllTrees() {
        m_trees.clear();
        m_foliageDirty = true;
        SyncFoliageBatches();
    }

    void RTSController::UpdateTreesZ() {
        for (auto& t : m_trees) {
            t.position.z = GetTerrainHeight(t.position.x, t.position.y);
        }
        m_foliageDirty = true;
        SyncFoliageBatches();
    }

    void RTSController::UpdateWaterPlane() {
        if (!m_waterEntity && m_scene && m_waterMeshIndex >= 0) {
            m_waterEntity = m_scene->CreateEntity("prop_water");
            m_waterEntity->targetName = "BattlefieldWater";
            m_waterEntity->modelIndex = m_waterMeshIndex;
            m_waterEntity->albedoColor = glm::vec3(0.08f, 0.38f, 0.58f);
            m_waterEntity->roughness = 0.05f;
            m_waterEntity->metallic = 0.1f;
        }
        if (!m_waterEntity) return;
        if (m_enableWater) {
            m_waterEntity->origin = glm::dvec3(0.0, 0.0, m_waterHeight);
            m_waterEntity->scale = glm::vec3(1.0f, 1.0f, 1.0f);
        } else {
            m_waterEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
            m_waterEntity->scale = glm::vec3(0.0f);
        }
    }
    void RTSController::ScatterProceduralForest(int density) {
        if (!m_scene) return;
        ClearAllTrees();
        int targetCount = std::clamp(density, 20, 4000);
        int numClusters = std::max(4, targetCount / 18);

        std::vector<glm::vec2> clusterCenters;
        for (int c = 0; c < numClusters; ++c) {
            float cx = (float)(rand() % 720 - 360);
            float cy = (float)(rand() % 720 - 360);
            if (glm::distance(glm::vec2(cx, cy), glm::vec2(-260.0f, 0.0f)) > 80.0f &&
                glm::distance(glm::vec2(cx, cy), glm::vec2( 260.0f, 0.0f)) > 80.0f) {
                clusterCenters.push_back(glm::vec2(cx, cy));
            }
        }

        if (clusterCenters.empty()) clusterCenters.push_back(glm::vec2(0.0f, 0.0f));

        int treesPerCluster = targetCount / (int)clusterCenters.size();
        for (const auto& center : clusterCenters) {
            float clusterRadius = 25.0f + (float)(rand() % 25);
            for (int i = 0; i < treesPerCluster; ++i) {
                float ang = (float)(rand() % 360) * 0.0174533f;
                float r = (float)(rand() % 100) * 0.01f * clusterRadius;
                glm::vec3 pos(center.x + cos(ang) * r, center.y + sin(ang) * r, 0.0f);

                bool nearNode = false;
                for (const auto& node : m_resourceNodes) {
                    if (glm::distance(glm::vec2(pos.x, pos.y), glm::vec2(node.position.x, node.position.y)) < 8.0f) {
                        nearNode = true;
                        break;
                    }
                }
                if (nearNode) continue;

                float scale = 0.95f + (float)(rand() % 6) * 0.05f;
                SpawnTree(pos, scale);
            }
        }
        SyncFoliageBatches();
        gameMessage = "Scattered procedural forest (" + std::to_string(m_trees.size()) + " trees).";
    }

    void RTSController::RefreshAvailableCustomMaps() {
        m_availableCustomMaps.clear();
        std::error_code ec;
        std::filesystem::create_directories("scenarios/custom_maps", ec);
        if (std::filesystem::exists("scenarios/custom_maps", ec)) {
            for (const auto& entry : std::filesystem::directory_iterator("scenarios/custom_maps", ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
                    m_availableCustomMaps.push_back(entry.path().stem().string());
                }
            }
        }
        std::sort(m_availableCustomMaps.begin(), m_availableCustomMaps.end());
    }

    bool RTSController::SaveCustomMap(const std::string& mapName) {
        if (mapName.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories("scenarios/custom_maps", ec);
        std::string safeName = mapName;
        if (safeName.size() < 5 || safeName.substr(safeName.size() - 5) != ".json") {
            safeName += ".json";
        }
        std::string filepath = "scenarios/custom_maps/" + safeName;

        nlohmann::json root;
        root["map_name"] = mapName;
        root["grid_res"] = TERRAIN_GRID_RES;
        root["map_size"] = TERRAIN_MAP_SIZE;
        root["enable_water"] = m_enableWater;
        root["water_height"] = m_waterHeight;

        nlohmann::json hArr = nlohmann::json::array();
        for (float h : m_heightmap) {
            hArr.push_back(std::round(h * 100.0f) / 100.0f);
        }
        root["heightmap"] = hArr;

        nlohmann::json rArr = nlohmann::json::array();
        for (const auto& node : m_resourceNodes) {
            nlohmann::json n;
            n["type"] = (node.type == ResourceNodeType::Mass) ? "mass" : "energy";
            n["position"] = { node.position.x, node.position.y, node.position.z };
            rArr.push_back(n);
        }
        root["resource_nodes"] = rArr;

        nlohmann::json tArr = nlohmann::json::array();
        for (const auto& t : m_trees) {
            nlohmann::json tr;
            tr["position"] = { t.position.x, t.position.y, t.position.z };
            tr["scale"] = { t.scale.x, t.scale.y, t.scale.z };
            tr["rotation"] = t.rotation;
            tr["variant"] = t.variant;
            tArr.push_back(tr);
        }
        root["trees"] = tArr;

        std::ofstream file(filepath);
        if (!file.is_open()) {
            gameMessage = "Error: Failed to save map to " + filepath;
            return false;
        }
        file << root.dump(2);
        file.close();
        gameMessage = "Map saved successfully: " + safeName;
        RefreshAvailableCustomMaps();
        return true;
    }

    bool RTSController::LoadCustomMap(const std::string& mapName) {
        if (mapName.empty()) return false;
        std::string filepath = mapName;
        if (filepath.find('/') == std::string::npos && filepath.find('\\') == std::string::npos) {
            filepath = "scenarios/custom_maps/" + mapName;
        }
        if (filepath.size() < 5 || filepath.substr(filepath.size() - 5) != ".json") {
            filepath += ".json";
        }

        std::ifstream file(filepath);
        if (!file.is_open()) {
            gameMessage = "Error: Could not open map " + filepath;
            return false;
        }

        nlohmann::json root;
        try {
            file >> root;
        } catch (...) {
            gameMessage = "Error: Corrupted JSON in map file!";
            return false;
        }
        file.close();

        // 1. Heightmap
        if (root.contains("heightmap") && root["heightmap"].is_array()) {
            auto& hArr = root["heightmap"];
            int stride = TERRAIN_GRID_RES + 1;
            if (hArr.size() == (size_t)(stride * stride)) {
                m_heightmap.resize(stride * stride);
                for (size_t i = 0; i < m_heightmap.size(); ++i) {
                    m_heightmap[i] = hArr[i].get<float>();
                }
                if (m_terrainVertices.size() == static_cast<size_t>(stride * stride)) {
                    for (int i = 0; i <= TERRAIN_GRID_RES; ++i) {
                        for (int j = 0; j <= TERRAIN_GRID_RES; ++j) {
                            int vIdx = i * stride + j;
                            m_terrainVertices[vIdx].pos.z = m_heightmap[vIdx];
                        }
                    }
                    RecomputeTerrainNormals(0, TERRAIN_GRID_RES, 0, TERRAIN_GRID_RES);
                    if (m_renderer && m_terrainMeshIndex >= 0) {
                        m_renderer->updateMesh(m_terrainMeshIndex, m_terrainVertices);
                    }
                }
            }
        }

        // 2. Resource Nodes
        if (root.contains("resource_nodes") && root["resource_nodes"].is_array()) {
            for (auto& n : m_resourceNodes) {
                if (n.entity) {
                    n.entity->scale = glm::vec3(0.0f);
                    n.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                }
            }
            m_resourceNodes.clear();

            for (const auto& nJson : root["resource_nodes"]) {
                std::string tStr = nJson.value("type", "mass");
                ResourceNodeType rType = (tStr == "energy") ? ResourceNodeType::Energy : ResourceNodeType::Mass;
                auto posArr = nJson["position"];
                glm::vec3 pos(posArr[0].get<float>(), posArr[1].get<float>(), posArr[2].get<float>());
                SpawnResourceNode(pos, rType);
            }
        }

        // 3. Trees
        ClearAllTrees();
        if (root.contains("trees") && root["trees"].is_array()) {
            for (const auto& tJson : root["trees"]) {
                auto posArr = tJson["position"];
                glm::vec3 pos(posArr[0].get<float>(), posArr[1].get<float>(), posArr[2].get<float>());
                float scale = 2.0f;
                if (tJson.contains("scale") && tJson["scale"].is_array() && tJson["scale"].size() > 0) {
                    scale = tJson["scale"][0].get<float>();
                }
                float rot = tJson.value("rotation", 0.0f);
                int variant = tJson.value("variant", -1);
                SpawnTree(pos, scale, rot, variant);
            }
        }
        SyncFoliageBatches();
        m_enableWater = root.value("enable_water", false);
        m_waterHeight = root.value("water_height", 0.0f);
        UpdateWaterPlane();

        UpdateResourceNodesZ();
        UpdateTreesZ();

        gameMessage = "Loaded custom map: " + filepath;
        return true;
    }

    void RTSController::DrawForgeUI(ImVec2 viewportPos, ImVec2 viewportSize) {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 20.0f, viewportPos.y + 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 690.0f), ImGuiCond_Always);
        ImGui::Begin("Map Sculptor / Forge Mode", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        ImGui::SetWindowFontScale(1.2f);
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "=== FORGE: MAP SCULPTOR ===");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();

        ImGui::Text("Active Mode:");
        ImGui::RadioButton("Sculpt Mountains", &m_forgeToolMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Delete Node/Tree", &m_forgeToolMode, 3);
        ImGui::RadioButton("Place Mass Node", &m_forgeToolMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Place Power Node", &m_forgeToolMode, 2);
        ImGui::RadioButton("Plant / Scatter Trees", &m_forgeToolMode, 4);
        ImGui::Separator();

        if (m_forgeToolMode == 0) {
            if (ImGui::Button("Undo (Ctrl+Z)", ImVec2(155, 26))) {
                SculptUndo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Redo (Ctrl+Y)", ImVec2(155, 26))) {
                SculptRedo();
            }
            ImGui::Separator();

            ImGui::Text("Sculpting Brush Tool:");
            ImGui::RadioButton("Raise", &m_forgeBrushType, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Lower", &m_forgeBrushType, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Smooth", &m_forgeBrushType, 2);
            ImGui::SameLine();
            ImGui::RadioButton("Flatten", &m_forgeBrushType, 3);

            ImGui::RadioButton("Plateau", &m_forgeBrushType, 4);
            ImGui::SameLine();
            ImGui::RadioButton("Ramp", &m_forgeBrushType, 5);
            ImGui::SameLine();
            ImGui::RadioButton("Noise", &m_forgeBrushType, 6);
            ImGui::Separator();

            ImGui::TextDisabled("Keys: Hold SHIFT to Smooth | CTRL to Invert");

            ImGui::SliderFloat("Brush Radius", &m_forgeBrushRadius, 15.0f, 150.0f, "%.0fm");
            ImGui::SliderFloat("Brush Intensity", &m_forgeBrushStrength, 0.2f, 4.0f, "%.2fx");
            if (m_forgeBrushType == 3) {
                ImGui::SliderFloat("Target Height", &m_forgeFlattenHeight, 0.0f, 100.0f, "%.1fm");
            } else if (m_forgeBrushType == 4) {
                ImGui::SliderFloat("Plateau Step", &m_forgePlateauStep, 2.0f, 25.0f, "%.1fm");
            }
            ImGui::Separator();

            ImGui::Text("Procedural Presets:");
            if (ImGui::Button("Generate Mountain Pass (Chokepoints)", ImVec2(320, 26))) {
                PushSculptUndoState();
                GenerateProceduralTerrain(1);
                RecomputeTerrainNormals(0, TERRAIN_GRID_RES, 0, TERRAIN_GRID_RES);
                if (m_renderer && m_terrainMeshIndex >= 0) m_renderer->updateMesh(m_terrainMeshIndex, m_terrainVertices);
                UpdateResourceNodesZ();
                UpdateTreesZ();
                gameMessage = "Mountain Pass terrain generated!";
            }
            if (ImGui::Button("Generate Rolling Hills", ImVec2(320, 26))) {
                PushSculptUndoState();
                GenerateProceduralTerrain(2);
                RecomputeTerrainNormals(0, TERRAIN_GRID_RES, 0, TERRAIN_GRID_RES);
                if (m_renderer && m_terrainMeshIndex >= 0) m_renderer->updateMesh(m_terrainMeshIndex, m_terrainVertices);
                UpdateResourceNodesZ();
                UpdateTreesZ();
                gameMessage = "Rolling Hills terrain generated!";
            }
            if (ImGui::Button("Reset to Flat Plain (0.0m)", ImVec2(320, 26))) {
                PushSculptUndoState();
                GenerateProceduralTerrain(0);
                RecomputeTerrainNormals(0, TERRAIN_GRID_RES, 0, TERRAIN_GRID_RES);
                if (m_renderer && m_terrainMeshIndex >= 0) m_renderer->updateMesh(m_terrainMeshIndex, m_terrainVertices);
                UpdateResourceNodesZ();
                UpdateTreesZ();
                gameMessage = "Terrain reset to flat plain.";
            }
        } else if (m_forgeToolMode == 4) {
            ImGui::TextColored(ImVec4(1.0f, 0.50f, 0.22f, 1.0f), "Trees & Natural Forest Tool:");
            ImGui::Text("Left-Click terrain to plant trees.");
            ImGui::Text("Right-Click existing tree to delete.");
            ImGui::Text("Active on Map: %d Trees", (int)m_trees.size());
            ImGui::Separator();
            if (ImGui::Button("Scatter Natural Forests (150 Trees)", ImVec2(320, 26))) {
                ScatterProceduralForest(150);
            }
            if (ImGui::Button("Dense Forest Blanket (300 Trees)", ImVec2(320, 26))) {
                ScatterProceduralForest(300);
            }
            if (ImGui::Button("Instanced Forest Stress (1500 Trees)", ImVec2(320, 26))) {
                ScatterProceduralForest(1500);
            }
            if (ImGui::Button("Clear All Trees", ImVec2(320, 26))) {
                ClearAllTrees();
                gameMessage = "Cleared all trees from battlefield.";
            }
        } else {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Resource Node Placement:");
            ImGui::Text("Click anywhere on terrain to place deposit.");
            ImGui::Text("Right-click an existing node to delete it.");

            int massCount = 0, powerCount = 0;
            for (const auto& n : m_resourceNodes) {
                if (n.type == ResourceNodeType::Mass) massCount++;
                else powerCount++;
            }
            ImGui::Text("Active on Map: %d Mass, %d Power", massCount, powerCount);
            ImGui::Separator();

            if (ImGui::Button("Reset Default Resource Layout", ImVec2(320, 28))) {
                ResetDefaultResourceNodes();
            }
            if (ImGui::Button("Clear All Resource Nodes", ImVec2(320, 28))) {
                for (auto& n : m_resourceNodes) {
                    if (n.entity) {
                        n.entity->scale = glm::vec3(0.0f);
                        n.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                    }
                }
                m_resourceNodes.clear();
                gameMessage = "Cleared all resource nodes.";
            }
        }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "Valley Water Simulation:");
        if (ImGui::Checkbox("Enable Valley Water", &m_enableWater)) {
            UpdateWaterPlane();
        }
        if (m_enableWater) {
            if (ImGui::SliderFloat("Water Level", &m_waterHeight, -10.0f, 60.0f, "%.1fm")) {
                UpdateWaterPlane();
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Custom Map File Storage:");
        ImGui::InputText("Map File", m_customMapName, sizeof(m_customMapName));
        if (ImGui::Button("Save Map (.json)", ImVec2(155, 28))) {
            SaveCustomMap(m_customMapName);
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Maps", ImVec2(155, 28))) {
            RefreshAvailableCustomMaps();
        }
        if (!m_availableCustomMaps.empty()) {
            std::vector<const char*> mapNames;
            for (const auto& s : m_availableCustomMaps) mapNames.push_back(s.c_str());
            m_selectedCustomMapIndex = std::clamp(m_selectedCustomMapIndex, 0, (int)mapNames.size() - 1);
            ImGui::Combo("Saved Maps", &m_selectedCustomMapIndex, mapNames.data(), (int)mapNames.size());
            if (ImGui::Button("Load Selected Map", ImVec2(320, 28))) {
                LoadCustomMap(m_availableCustomMaps[m_selectedCustomMapIndex]);
            }
        } else {
            ImGui::TextDisabled("No maps found in scenarios/custom_maps/");
        }

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.65f, 0.25f, 1.0f));
        if (ImGui::Button("Launch Skirmish on this Map", ImVec2(320, 36))) {
            m_inForgeMode = false;
            m_inMatch = true;
            m_showSkirmishMenu = false;
            StartNewSkirmish();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        if (ImGui::Button("Return to Main Menu", ImVec2(320, 30))) {
            m_inForgeMode = false;
            m_showSkirmishMenu = true;
        }

        ImGui::End();
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

            m_treeBatchIndices.clear();
            scene->foliageBatches.clear();

            // Load optional artist GLB tree variants first (assets/models/trees/tree_0..3.glb or assets/models/tree_0..3.glb)
            for (int v = 0; v < 4; ++v) {
                std::string glbCandidates[2] = {
                    "assets/models/trees/tree_" + std::to_string(v) + ".glb",
                    "assets/models/tree_" + std::to_string(v) + ".glb"
                };
                size_t beforeCount = scene->entities.size();
                std::string loadedPath;
                for (const auto& candidate : glbCandidates) {
                    AssetLoader::loadModel(renderer, candidate, scene);
                    if (scene->entities.size() > beforeCount) {
                        loadedPath = candidate;
                        break;
                    }
                }
                if (scene->entities.size() > beforeCount) {
                    FoliageBatch batch{};
                    batch.name = "GLTF_Tree_" + std::to_string(v);
                    batch.lodSwitchDist = 175.0f;
                    batch.maxDrawDist = 950.0f;
                    batch.boundingRadius = 3.5f;
                    batch.windStrength = 0.16f;
                    batch.windFrequency = 1.6f;

                    for (size_t idx = beforeCount; idx < scene->entities.size(); ++idx) {
                        CBaseEntity* ent = scene->entities[idx];
                        if (!ent) continue;
                        if (ent->modelIndex >= 0) {
                            FoliageMeshPart part{};
                            part.lod0MeshID = ent->modelIndex;
                            part.textureID = ent->textureID;
                            part.normalTextureID = ent->normalTextureID;
                            part.roughness = (ent->roughness > 0.05f) ? ent->roughness : 0.88f;
                            part.subsurface = (ent->textureID > 0) ? 0.42f : 0.25f;
                            part.doubleSided = true;
                            part.castShadows = true;
                            batch.parts.push_back(part);
                        }
                        ent->origin = glm::dvec3(0.0, 0.0, -999.0);
                        ent->scale = glm::vec3(0.0f);
                    }
                    if (!batch.parts.empty()) {
                        m_treeBatchIndices.push_back(scene->foliageBatches.size());
                        scene->foliageBatches.push_back(std::move(batch));
                    }
                    (void)loadedPath;
                }
            }

            // Fallback: 3 stylized instanced procedural variants (LOD0 + LOD1 crossed-card silhouette)
            if (m_treeBatchIndices.empty()) {
                for (int v = 0; v < 3; ++v) {
                    std::vector<Vertex> lod0Verts; std::vector<uint32_t> lod0Inds;
                    std::vector<Vertex> lod1Verts; std::vector<uint32_t> lod1Inds;
                    GetStylizedTreeData(v, false, lod0Verts, lod0Inds);
                    GetStylizedTreeData(v, true, lod1Verts, lod1Inds);

                    int lod0ID = renderer->acquireMesh("PROCEDURAL", "RTS_TreeLOD0_V" + std::to_string(v), lod0Verts, lod0Inds);
                    int lod1ID = renderer->acquireMesh("PROCEDURAL", "RTS_TreeLOD1_V" + std::to_string(v), lod1Verts, lod1Inds);

                    FoliageBatch batch{};
                    batch.name = "Procedural_Tree_V" + std::to_string(v);
                    batch.lodSwitchDist = 175.0f;
                    batch.maxDrawDist = 950.0f;
                    batch.boundingRadius = 3.2f;
                    batch.windStrength = 0.16f;
                    batch.windFrequency = 1.6f;

                    FoliageMeshPart part{};
                    part.lod0MeshID = lod0ID;
                    part.lod1MeshID = lod1ID;
                    part.textureID = 0;
                    part.roughness = 0.88f;
                    part.subsurface = 0.42f;
                    part.doubleSided = true;
                    part.castShadows = true;
                    batch.parts.push_back(part);

                    m_treeBatchIndices.push_back(scene->foliageBatches.size());
                    scene->foliageBatches.push_back(std::move(batch));
                }
            }
            RefreshAvailableCustomMaps();
            ClearAllTrees();
            size_t priorEntCount = scene->entities.size();
            std::string acuPaths[] = {
                "Meridian_Crescendo_Edition/data/acu.glb",
                "assets/models/acu.glb"
            };
            for (const auto& p : acuPaths) {
                AssetLoader::loadModel(renderer, p, scene);
                if (scene->entities.size() > priorEntCount) break;
            }
            if (scene->entities.size() > priorEntCount) {
                CBaseEntity* tempAcu = scene->entities[priorEntCount];
                if (tempAcu) {
                    m_acuMeshIndex = tempAcu->modelIndex;
                }
                for (size_t idx = priorEntCount; idx < scene->entities.size(); ++idx) {
                    if (scene->entities[idx]) {
                        scene->entities[idx]->origin = glm::dvec3(0.0, 0.0, -999.0);
                        scene->entities[idx]->scale = glm::vec3(0.0f);
                    }
                }
            }

            // Load T1 Battle Tank 3D Model
            priorEntCount = scene->entities.size();
            std::string tankPaths[] = { "Meridian_Crescendo_Edition/data/t1_tank.glb", "assets/models/t1_tank.glb" };
            for (const auto& p : tankPaths) {
                AssetLoader::loadModel(renderer, p, scene);
                if (scene->entities.size() > priorEntCount) break;
            }
            if (scene->entities.size() > priorEntCount) {
                CBaseEntity* tempTank = scene->entities[priorEntCount];
                if (tempTank) {
                    m_tankMeshIndex = tempTank->modelIndex;
                }
                for (size_t idx = priorEntCount; idx < scene->entities.size(); ++idx) {
                    if (scene->entities[idx]) {
                        scene->entities[idx]->origin = glm::dvec3(0.0, 0.0, -999.0);
                        scene->entities[idx]->scale = glm::vec3(0.0f);
                    }
                }
            }

            // Load T1 Gunship / Combat Aircraft 3D Model
            priorEntCount = scene->entities.size();
            std::string gunshipPaths[] = { "Meridian_Crescendo_Edition/data/t1_gunship.glb", "assets/models/t1_gunship.glb" };
            for (const auto& p : gunshipPaths) {
                AssetLoader::loadModel(renderer, p, scene);
                if (scene->entities.size() > priorEntCount) break;
            }
            if (scene->entities.size() > priorEntCount) {
                CBaseEntity* tempGunship = scene->entities[priorEntCount];
                if (tempGunship) {
                    m_gunshipMeshIndex = tempGunship->modelIndex;
                }
                for (size_t idx = priorEntCount; idx < scene->entities.size(); ++idx) {
                    if (scene->entities[idx]) {
                        scene->entities[idx]->origin = glm::dvec3(0.0, 0.0, -999.0);
                        scene->entities[idx]->scale = glm::vec3(0.0f);
                    }
                }
            }

            // Load T1 Land Facility Model (Hull + Front Bay Door)
            priorEntCount = scene->entities.size();
            std::string landFactPaths[] = { "Meridian_Crescendo_Edition/data/t1_land_facility.glb", "assets/models/t1_land_facility.glb" };
            for (const auto& p : landFactPaths) {
                AssetLoader::loadModel(renderer, p, scene);
                if (scene->entities.size() > priorEntCount) break;
            }
            if (scene->entities.size() > priorEntCount) {
                for (size_t idx = priorEntCount; idx < scene->entities.size(); ++idx) {
                    CBaseEntity* temp = scene->entities[idx];
                    if (!temp) continue;
                    if (temp->targetName.find("Door") != std::string::npos || temp->targetName.find("door") != std::string::npos) {
                        m_landFactoryDoorMeshIndex = temp->modelIndex;
                    } else if (temp->targetName.find("Hull") != std::string::npos || temp->targetName.find("hull") != std::string::npos) {
                        m_landFactoryMeshIndex = temp->modelIndex;
                    }
                    temp->origin = glm::dvec3(0.0, 0.0, -999.0);
                    temp->scale = glm::vec3(0.0f);
                }
                if (m_landFactoryMeshIndex < 0) {
                    CBaseEntity* lastTemp = scene->entities.back();
                    if (lastTemp) m_landFactoryMeshIndex = lastTemp->modelIndex;
                }
            }

            // Load T1 Air Facility Model
            priorEntCount = scene->entities.size();
            std::string airFactPaths[] = { "Meridian_Crescendo_Edition/data/t1_air_facility.glb", "assets/models/t1_air_facility.glb" };
            for (const auto& p : airFactPaths) {
                AssetLoader::loadModel(renderer, p, scene);
                if (scene->entities.size() > priorEntCount) break;
            }
            if (scene->entities.size() > priorEntCount) {
                for (size_t idx = priorEntCount; idx < scene->entities.size(); ++idx) {
                    CBaseEntity* temp = scene->entities[idx];
                    if (!temp) continue;
                    if (m_airFactoryMeshIndex < 0) {
                        m_airFactoryMeshIndex = temp->modelIndex;
                    }
                    temp->origin = glm::dvec3(0.0, 0.0, -999.0);
                    temp->scale = glm::vec3(0.0f);
                }
            }

            // Load Cubemap Sky and activate Image-Based Lighting
            renderer->loadSkybox("assets/skies/rts_sky.hdr", scene);
            if (scene) {
                scene->environment.skyType = SkyType::HDRMap;
                scene->environment.ambientIntensity = 1.35f;
                scene->environment.enableFog = true;
                scene->environment.fogColor = glm::vec4(0.07f, 0.10f, 0.15f, 0.0075f);
                scene->environment.fogParams = glm::vec4(0.015f, 0.92f, 35.0f, 0.0f);
                scene->environment.fogOfWarCenter = glm::vec2(-260.0f, 0.0f);
                scene->environment.fogOfWarRadius = 110.0f;
            }
        }

        // Spawn Procedural Subdivided Terrain
        BuildProceduralTerrain(renderer, scene);

        // Create Skinny Emissive Red and Blue Cylinder Rally Point Markers
        m_playerRallyPos = glm::vec3(-180.0f, 0.0f, 0.0f);
        m_enemyRallyPos = glm::vec3(180.0f, 0.0f, 0.0f);

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
        for (int i = 0; i < 128; i++) {
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
        // Create pool of 3D Translucent Force-Field Shield Orb Entities
        m_shieldOrbEntities.clear();
        for (int i = 0; i < 16; i++) {
            CBaseEntity* orb = scene->CreateEntity("prop_dynamic");
            orb->targetName = "RTS_ShieldOrb";
            orb->modelIndex = m_sphereMeshIndex;
            orb->origin = glm::dvec3(0.0, 0.0, -999.0);
            orb->scale = glm::vec3(0.0f);
            orb->transmission = 1.0f; // 100% transmission (non-solid glass/energy)
            orb->thickness = 0.12f;   // Thin force-field membrane
            orb->ior = 1.08f;         // Subtle force-field refraction
            orb->roughness = 0.02f;   // Clean, non-blurry refraction
            orb->attenuationDistance = 35.0f;
            orb->albedoColor = glm::vec3(0.01f, 0.08f, 0.15f);
            orb->attenuationColor = glm::vec3(0.45f, 0.88f, 1.0f);
            orb->emission = 0.5f;
            orb->clearcoat = 1.0f;
            orb->clearcoatRoughness = 0.02f;
            m_shieldOrbEntities.push_back(orb);
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

        // Create Resource Deposit Nodes (Mass & Power/Energy snapped flush to terrain)
        ResetDefaultResourceNodes();
        // Initialize into Main Menu state (wait for player to configure and launch skirmish)
        m_inMatch = false;
        m_showSkirmishMenu = true;
        m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        m_cameraZoom = 240.0f;
        m_cameraPitch = 45.0f;
        m_cameraYaw = 25.0f;
        gameMessage = "Crescendo RTS - Configure rules and click Launch Skirmish to deploy!";
    }
    void RTSController::StartNewSkirmish() {
        if (!m_scene) return;

        // 0. Reload custom map or default layout to reset match to pristine map state
        if (!m_availableCustomMaps.empty() && m_selectedCustomMapIndex >= 0 && m_selectedCustomMapIndex < (int)m_availableCustomMaps.size()) {
            LoadCustomMap(m_availableCustomMaps[m_selectedCustomMapIndex]);
        } else {
            ResetDefaultResourceNodes();
            UpdateResourceNodesZ();
            UpdateTreesZ();
        }
        UpdateWaterPlane();

        // 1. Recycle existing unit entities (including shield orbs)
        for (auto& u : m_units) {
            if (u.entity) {
                u.entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                u.entity->scale = glm::vec3(0.0f);
                m_deadUnitEntities.push_back(u.entity);
            }
            if (u.shieldOrbEntity) {
                u.shieldOrbEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                u.shieldOrbEntity->scale = glm::vec3(0.0f);
            }
            if (u.doorEntity) {
                u.doorEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                u.doorEntity->scale = glm::vec3(0.0f);
            }
        }
        m_units.clear();

        m_constructionTasks.clear();
        m_pendingSpawns.clear();
        m_isPlacingBuilding = false;
        if (m_buildGhostEntity) {
            m_buildGhostEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
            m_buildGhostEntity->scale = glm::vec3(0.0f);
        }

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

        // 3. Economy & Resources from SkirmishSettings + Faction Bonuses
        const auto& playerFac = GetTeamFactionDef(Team::Player);
        const auto& enemyFac = GetTeamFactionDef(Team::Enemy);
        playerMass = (float)(m_skirmishSettings.startingMass + playerFac.mods.bonusStartingMass);
        playerEnergy = (float)(m_skirmishSettings.startingEnergy + playerFac.mods.bonusStartingEnergy);
        enemyMass = (float)(m_skirmishSettings.startingMass + enemyFac.mods.bonusStartingMass);
        enemyEnergy = (float)(m_skirmishSettings.startingEnergy + enemyFac.mods.bonusStartingEnergy);
        m_boostTimer = 0.0f;

        // 4. Rally Points (tinted by active faction colors)
        m_playerRallyPos = glm::vec3(-180.0f, 0.0f, 0.0f);
        m_enemyRallyPos = glm::vec3(180.0f, 0.0f, 0.0f);
        if (m_playerRallyEntity) {
            m_playerRallyEntity->origin = glm::dvec3(m_playerRallyPos.x, m_playerRallyPos.y, 4.0);
            m_playerRallyEntity->albedoColor = GetTeamDisplayColor(Team::Player, false);
        }
        if (m_enemyRallyEntity) {
            m_enemyRallyEntity->origin = glm::dvec3(m_enemyRallyPos.x, m_enemyRallyPos.y, 4.0);
            m_enemyRallyEntity->albedoColor = GetTeamDisplayColor(Team::Enemy, false);
        }

        // 5. Spawn Bases & Economy Structures on the 20x20 Battlefield
        SpawnUnit(m_scene, m_physicsServer, UnitType::Factory, Team::Player, glm::vec3(-260.0f, 0.0f, 2.0f), glm::vec3(10.0f, 10.0f, 5.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::MassGen, Team::Player, glm::vec3(-295.0f, 35.0f, 1.5f), glm::vec3(4.0f, 4.0f, 3.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::EnergyGen, Team::Player, glm::vec3(-295.0f, -35.0f, 2.0f), glm::vec3(4.0f, 4.0f, 4.0f));
        for (int t = 0; t < playerFac.mods.bonusStartingTurrets; ++t) {
            float yOff = (t == 0) ? 0.0f : ((t % 2 == 1 ? 1.0f : -1.0f) * 35.0f * (float)((t + 1) / 2));
            SpawnUnit(m_scene, m_physicsServer, UnitType::Turret, Team::Player, glm::vec3(-235.0f, yOff, 3.0f));
        }

        SpawnUnit(m_scene, m_physicsServer, UnitType::Factory, Team::Enemy, glm::vec3(260.0f, 0.0f, 2.0f), glm::vec3(10.0f, 10.0f, 5.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::MassGen, Team::Enemy, glm::vec3(295.0f, 35.0f, 1.5f), glm::vec3(4.0f, 4.0f, 3.0f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::EnergyGen, Team::Enemy, glm::vec3(295.0f, -35.0f, 2.0f), glm::vec3(4.0f, 4.0f, 4.0f));
        for (int t = 0; t < enemyFac.mods.bonusStartingTurrets; ++t) {
            float yOff = (t == 0) ? 0.0f : ((t % 2 == 1 ? 1.0f : -1.0f) * 20.0f * (float)((t + 1) / 2));
            SpawnUnit(m_scene, m_physicsServer, UnitType::Turret, Team::Enemy, glm::vec3(235.0f, yOff, 3.0f));
        }
        // 6. Spawn Starting Forces based on settings + faction starting force bonuses
        int playerTankCount = std::clamp(m_skirmishSettings.initialTanks + playerFac.mods.bonusStartingTanks, 1, 10);
        for (int i = 0; i < playerTankCount; i++) {
            float yOffset = ((float)i - (float)(playerTankCount - 1) * 0.5f) * 8.0f;
            SpawnUnit(m_scene, m_physicsServer, UnitType::Tank, Team::Player, glm::vec3(-240.0f, yOffset, 0.0f));
        }
        int enemyTankCount = std::clamp(m_skirmishSettings.initialTanks + enemyFac.mods.bonusStartingTanks, 1, 10);
        for (int i = 0; i < enemyTankCount; i++) {
            float yOffset = ((float)i - (float)(enemyTankCount - 1) * 0.5f) * 8.0f;
            SpawnUnit(m_scene, m_physicsServer, UnitType::Tank, Team::Enemy, glm::vec3(240.0f, yOffset, 0.0f));
        }

        int playerPlaneCount = std::clamp(m_skirmishSettings.initialPlanes + playerFac.mods.bonusStartingPlanes, 0, 8);
        for (int i = 0; i < playerPlaneCount; i++) {
            float yOffset = ((float)i - (float)(playerPlaneCount - 1) * 0.5f) * 12.0f;
            SpawnUnit(m_scene, m_physicsServer, UnitType::Plane, Team::Player, glm::vec3(-250.0f, yOffset, 12.0f));
        }
        int enemyPlaneCount = std::clamp(m_skirmishSettings.initialPlanes + enemyFac.mods.bonusStartingPlanes, 0, 8);
        for (int i = 0; i < enemyPlaneCount; i++) {
            float yOffset = ((float)i - (float)(enemyPlaneCount - 1) * 0.5f) * 12.0f;
            SpawnUnit(m_scene, m_physicsServer, UnitType::Plane, Team::Enemy, glm::vec3(250.0f, yOffset, 12.0f));
        }

        // Spawn starting Engineers + faction engineer bonuses
        int playerEngCount = 1 + playerFac.mods.bonusStartingEngineers;
        for (int i = 0; i < playerEngCount; ++i) {
            SpawnUnit(m_scene, m_physicsServer, UnitType::Engineer, Team::Player, glm::vec3(-235.0f, -25.0f + (float)i * 50.0f, 0.0f));
        }
        int enemyEngCount = 1 + enemyFac.mods.bonusStartingEngineers;
        for (int i = 0; i < enemyEngCount; ++i) {
            SpawnUnit(m_scene, m_physicsServer, UnitType::Engineer, Team::Enemy, glm::vec3(235.0f, -25.0f + (float)i * 50.0f, 0.0f));
        }

        // Spawn Armored Command Mechs (SupCom FAF ACU)
        SpawnUnit(m_scene, m_physicsServer, UnitType::CommandMech, Team::Player, glm::vec3(-220.0f, 0.0f, 2.6f));
        SpawnUnit(m_scene, m_physicsServer, UnitType::CommandMech, Team::Enemy, glm::vec3(220.0f, 0.0f, 2.6f));
        for (auto& node : m_resourceNodes) {
            node.reservedBy = -1;
            node.occupied = false;
            if (glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(-295.0f, 35.0f)) < 4.0f ||
                glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(-295.0f, -35.0f)) < 4.0f ||
                glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(295.0f, 35.0f)) < 4.0f ||
                glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(295.0f, -35.0f)) < 4.0f) {
                node.occupied = true;
            }
        }

        // 7. Reset Camera & Match State
        m_scene->environment.enableFog = m_skirmishSettings.enableFogOfWar;
        m_cameraTarget = glm::vec3(-220.0f, 0.0f, 0.0f);
        m_cameraZoom = 120.0f;
        m_aiTimer = 0.0f;
        ResetAI();
        m_resourceTimer = 0.0f;
        m_matchTime = 0.0f;
        m_noRushTimer = (float)m_skirmishSettings.noRushMinutes * 60.0f;
        playerMass = (float)(m_skirmishSettings.startingMass + playerFac.mods.bonusStartingMass);
        playerEnergy = (float)(m_skirmishSettings.startingEnergy + playerFac.mods.bonusStartingEnergy);
        enemyMass = (float)(m_skirmishSettings.startingMass + enemyFac.mods.bonusStartingMass);
        enemyEnergy = (float)(m_skirmishSettings.startingEnergy + enemyFac.mods.bonusStartingEnergy);
        for (int k = 0; k < 10; ++k) m_controlGroups[k].clear();
        m_factoryQueues.clear();
        gameOver = false;
        m_playerWon = false;
        m_showSkirmishMenu = false;
        std::string matchupStr = GetMatchupLabel();
        if (m_noRushTimer > 0.0f) {
            gameMessage = matchupStr + " | No Rush (" + std::to_string(m_skirmishSettings.noRushMinutes) + "m): Fortify base!";
        } else {
            gameMessage = matchupStr + " | " +
                          ((m_skirmishSettings.gameMode == 0) ? "Annihilation Mode!" :
                          ((m_skirmishSettings.gameMode == 1) ? "Assassination (HQ) Mode!" :
                           "Commander Mode (SupCom FAF)!"));
        }
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
                p.faction = GetTeamFaction(team);
                p.position = startPos;
                p.targetPos = targetPos;
                p.damage = damage;
                p.life = 0.0f;
                p.maxLife = 3.5f;

                glm::vec3 diff = targetPos - startPos;
                float dist = glm::length(diff);
                glm::vec3 dir = (dist > 0.001f) ? (diff / dist) : glm::vec3(1, 0, 0);

                float speed = 65.0f;
                if (type == ProjectileType::Rocket) speed = 45.0f;
                else if (type == ProjectileType::AAFlak) speed = 90.0f;
                else if (type == ProjectileType::StrategicNuke) speed = 32.0f;
                else if (type == ProjectileType::EMPPulse) speed = 42.0f;
                p.velocity = dir * speed;

                // Orient projectile toward target
                float yaw = glm::degrees(atan2(dir.y, dir.x));
                float pitch = glm::degrees(asin(std::clamp(dir.z, -1.0f, 1.0f)));
                p.entity->angles = glm::vec3(pitch, 0.0f, yaw - 90.0f);

                glm::vec3 facProjColor = GetFactionDef(p.faction).projectileColor;
                if (p.faction == m_skirmishSettings.playerFaction && team == Team::Enemy) {
                    facProjColor = ApplyMirrorContrast(facProjColor, true);
                }
                if (type == ProjectileType::Rocket) {
                    p.entity->scale = glm::vec3(0.35f, 1.6f, 0.35f);
                    p.entity->albedoColor = glm::mix(glm::vec3(1.0f, 0.95f, 0.15f), facProjColor, 0.45f);
                    p.entity->emission = 12.0f;
                } else if (type == ProjectileType::AAFlak) {
                    p.entity->scale = glm::vec3(0.25f, 0.8f, 0.25f);
                    p.entity->albedoColor = glm::mix(glm::vec3(0.2f, 0.95f, 1.0f), facProjColor, 0.50f);
                    p.entity->emission = 16.0f;
                } else if (type == ProjectileType::StrategicNuke) {
                    p.entity->scale = glm::vec3(1.2f, 4.0f, 1.2f);
                    p.entity->albedoColor = glm::vec3(1.0f, 0.2f, 0.1f);
                    p.entity->emission = 22.0f;
                    p.maxLife = 12.0f;
                } else if (type == ProjectileType::EMPPulse) {
                    p.entity->scale = glm::vec3(0.8f, 2.2f, 0.8f);
                    p.entity->albedoColor = glm::vec3(0.1f, 0.85f, 1.0f);
                    p.entity->emission = 18.0f;
                    p.maxLife = 8.0f;
                } else {
                    p.entity->scale = glm::vec3(0.4f, 0.8f, 0.4f);
                    p.entity->albedoColor = glm::mix(glm::vec3(1.0f, 0.9f, 0.2f), facProjColor, 0.40f);
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
        float groundZ = GetTerrainHeight(pos.x, pos.y);
        pos.z = (type == UnitType::Plane) ? (groundZ + 14.0f) : (groundZ + pos.z);
        ent->origin = glm::dvec3(pos.x, pos.y, pos.z);
        ent->angles = glm::vec3(0.0f);
        ent->modelIndex = (type == UnitType::Plane) ? m_sphereMeshIndex : ((type == UnitType::EnergyGen) ? m_cylinderMeshIndex : ((type == UnitType::CommandMech && m_acuMeshIndex >= 0) ? m_acuMeshIndex : m_cubeMeshIndex));
        ent->roughness = 1.0f;
        ent->emission = 0.0f;

        FactionId facId = GetTeamFaction(team);
        const auto& fac = GetFactionDef(facId);
        glm::vec3 facPrimary = GetTeamDisplayColor(team, false);
        glm::vec3 facAccent = GetTeamDisplayColor(team, true);
        glm::vec3 facShield = GetTeamShieldColor(team);

        RTSUnit unit;
        unit.entity = ent;
        unit.entityIndex = ent->index;
        unit.type = type;
        unit.team = team;
        unit.faction = facId;
        unit.position = pos;
        unit.targetPosition = (team == Team::Player) ? m_playerRallyPos : m_enemyRallyPos;
        unit.hasTarget = true;
        unit.selected = false;

        if (type == UnitType::Tank) {
            if (m_tankMeshIndex >= 0) {
                ent->modelIndex = m_tankMeshIndex;
                ent->scale = (scale == glm::vec3(1.0f) || scale == glm::vec3(2.5f, 2.0f, 1.2f)) ? glm::vec3(2.4f, 2.4f, 2.4f) : scale;
            } else {
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(2.5f, 2.0f, 1.2f) : scale;
            }
            unit.maxHealth = 120.0f;
            unit.speed = 8.0f;
            ent->albedoColor = facPrimary;
        } else if (type == UnitType::Plane) {
            if (m_gunshipMeshIndex >= 0) {
                ent->modelIndex = m_gunshipMeshIndex;
                ent->scale = (scale == glm::vec3(1.0f) || scale == glm::vec3(1.5f, 1.5f, 1.5f)) ? glm::vec3(2.2f, 2.2f, 2.2f) : scale;
            } else {
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.5f, 1.5f, 1.5f) : scale;
            }
            unit.maxHealth = 70.0f;
            unit.speed = 14.0f;
            ent->albedoColor = facAccent;
        } else if (type == UnitType::Factory) {
            if (m_landFactoryMeshIndex >= 0) {
                ent->modelIndex = m_landFactoryMeshIndex;
                ent->scale = (scale == glm::vec3(1.0f) || scale == glm::vec3(10.0f, 10.0f, 5.0f)) ? glm::vec3(4.2f, 4.2f, 4.2f) : scale;
                ent->origin.z = groundZ;
                unit.position.z = groundZ;
                ent->angles = glm::vec3(0.0f, 0.0f, (team == Team::Enemy) ? 180.0f : 0.0f);
            } else {
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(6.0f, 6.0f, 4.0f) : scale;
            }
            unit.maxHealth = 800.0f;
            unit.speed = 0.0f;
            ent->albedoColor = facPrimary;
            ent->emission = 1.2f;
            if (m_landFactoryDoorMeshIndex >= 0 && scene) {
                CBaseEntity* doorEnt = scene->CreateEntity("prop_dynamic");
                doorEnt->targetName = std::string(fac.shortCode) + ((team == Team::Player) ? "_Player_HQDoor" : "_Enemy_HQDoor");
                doorEnt->modelIndex = m_landFactoryDoorMeshIndex;
                doorEnt->origin = glm::dvec3(unit.position.x, unit.position.y, unit.position.z - 0.15f);
                doorEnt->angles = ent->angles;
                doorEnt->scale = ent->scale;
                doorEnt->roughness = 0.85f;
                doorEnt->albedoColor = ent->albedoColor * 0.82f;
                doorEnt->emission = ent->emission * 0.75f;
                unit.doorEntity = doorEnt;
                unit.doorCloseProgress = 0.0f;
                unit.doorReleaseTimer = 0.0f;
            }
        } else if (type == UnitType::MassGen) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.0f, 3.0f, 3.0f) : scale;
            unit.maxHealth = 200.0f;
            unit.speed = 0.0f;
            ent->albedoColor = glm::mix(glm::vec3(0.7f, 0.2f, 0.9f), facPrimary, 0.35f);
            ent->emission = 2.5f;
        } else if (type == UnitType::EnergyGen) {
            ent->modelIndex = m_cylinderMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.0f, 3.0f, 4.0f) : scale;
            unit.maxHealth = 200.0f;
            unit.speed = 0.0f;
            ent->albedoColor = glm::mix(glm::vec3(0.9f, 0.9f, 0.1f), facPrimary, 0.25f);
            ent->emission = 2.5f;
        } else if (type == UnitType::Engineer) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.8f, 1.8f, 1.2f) : scale;
            unit.maxHealth = 90.0f;
            unit.speed = 7.0f;
            ent->albedoColor = glm::mix(glm::vec3(0.95f, 0.95f, 0.95f), facPrimary, 0.35f);
            ent->emission = 0.5f;
        } else if (type == UnitType::LandFactory) {
            if (m_landFactoryMeshIndex >= 0) {
                ent->modelIndex = m_landFactoryMeshIndex;
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.2f, 3.2f, 3.2f) : scale;
                ent->origin.z = groundZ;
                unit.position.z = groundZ;
                ent->angles = glm::vec3(0.0f, 0.0f, (team == Team::Enemy) ? 180.0f : 0.0f);
            } else {
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(7.0f, 6.0f, 3.5f) : scale;
            }
            unit.maxHealth = 600.0f;
            unit.speed = 0.0f;
            ent->albedoColor = facPrimary;
            ent->emission = 1.0f;
            if (m_landFactoryDoorMeshIndex >= 0 && scene) {
                CBaseEntity* doorEnt = scene->CreateEntity("prop_dynamic");
                doorEnt->targetName = std::string(fac.shortCode) + ((team == Team::Player) ? "_Player_LandFactoryDoor" : "_Enemy_LandFactoryDoor");
                doorEnt->modelIndex = m_landFactoryDoorMeshIndex;
                doorEnt->origin = glm::dvec3(unit.position.x, unit.position.y, unit.position.z - 0.15f);
                doorEnt->angles = ent->angles;
                doorEnt->scale = ent->scale;
                doorEnt->roughness = 0.85f;
                doorEnt->albedoColor = ent->albedoColor * 0.82f;
                doorEnt->emission = ent->emission * 0.75f;
                unit.doorEntity = doorEnt;
                unit.doorCloseProgress = 0.0f;
                unit.doorReleaseTimer = 0.0f;
            }
        } else if (type == UnitType::AirFactory) {
            if (m_airFactoryMeshIndex >= 0) {
                ent->modelIndex = m_airFactoryMeshIndex;
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.2f, 3.2f, 3.2f) : scale;
                ent->origin.z = groundZ;
                unit.position.z = groundZ;
                ent->angles = glm::vec3(0.0f, 0.0f, (team == Team::Enemy) ? 180.0f : 0.0f);
            } else {
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(6.0f, 7.0f, 4.0f) : scale;
            }
            unit.maxHealth = 550.0f;
            unit.speed = 0.0f;
            ent->albedoColor = facAccent;
            ent->emission = 1.5f;
        } else if (type == UnitType::Turret) {
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.5f, 1.5f, 6.0f) : scale;
            unit.maxHealth = 400.0f;
            unit.speed = 0.0f;
            ent->albedoColor = facPrimary;
            ent->emission = 1.8f;
        } else if (type == UnitType::AATurret) {
            ent->modelIndex = m_cylinderMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.8f, 1.8f, 4.5f) : scale;
            unit.maxHealth = 380.0f;
            unit.speed = 0.0f;
            ent->albedoColor = facAccent;
            ent->emission = 2.2f;
        } else if (type == UnitType::CommandMech) {
            if (m_acuMeshIndex >= 0) {
                ent->modelIndex = m_acuMeshIndex;
                ent->scale = (scale == glm::vec3(1.0f) || scale == glm::vec3(3.2f, 3.2f, 5.2f)) ? glm::vec3(1.6f, 1.6f, 1.6f) : scale;
                ent->origin.z = groundZ;
                unit.position.z = groundZ;
            } else {
                ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.2f, 3.2f, 5.2f) : scale;
            }
            unit.maxHealth = 2200.0f;
            unit.speed = 4.5f;
            ent->albedoColor = facPrimary;
            ent->emission = 3.5f;
        } else if (type == UnitType::ShieldGen) {
            ent->modelIndex = m_cylinderMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(4.0f, 4.0f, 2.5f) : scale;
            unit.maxHealth = 600.0f;
            unit.maxShieldHealth = 1500.0f;
            unit.shieldRadius = 38.0f; // Reduced 2 sizes down (tight tactical base dome!)
            unit.isShieldActive = true;
            unit.shieldRechargeTimer = 0.0f;
            unit.speed = 0.0f;
            ent->albedoColor = facAccent;
            ent->emission = 2.5f;
            if (scene) {
                CBaseEntity* orb = scene->CreateEntity("prop_dynamic");
                orb->targetName = std::string(fac.shortCode) + ((team == Team::Player) ? "_Player_ShieldOrb" : "_Enemy_ShieldOrb");
                orb->modelIndex = m_sphereMeshIndex;
                orb->origin = glm::dvec3(0.0, 0.0, -999.0);
                orb->scale = glm::vec3(0.0f);
                orb->transmission = 0.0f; // Pure alpha blend: zero refraction blit stalls!
                orb->alpha = 0.48f;
                orb->thickness = 0.0f;
                orb->roughness = 0.95f; // Soft, blurred matte force-field look
                orb->albedoColor = facShield;
                orb->emission = 2.2f;
                orb->clearcoat = 0.0f;
                orb->clearcoatRoughness = 1.0f;
                unit.shieldOrbEntity = orb;
            }
        } else if (type == UnitType::MassStorage) {
            ent->modelIndex = m_cubeMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.6f, 3.6f, 3.0f) : scale;
            unit.maxHealth = 500.0f;
            unit.speed = 0.0f;
            ent->albedoColor = glm::mix(glm::vec3(0.65f, 0.25f, 0.9f), facPrimary, 0.35f);
            ent->emission = 2.0f;
        } else if (type == UnitType::EnergyStorage) {
            ent->modelIndex = m_cylinderMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(3.5f, 3.5f, 3.8f) : scale;
            unit.maxHealth = 450.0f;
            unit.speed = 0.0f;
            ent->albedoColor = glm::mix(glm::vec3(0.95f, 0.85f, 0.15f), facPrimary, 0.25f);
            ent->emission = 2.8f;
        } else if (type == UnitType::NukeSilo) {
            ent->modelIndex = m_cylinderMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(5.5f, 5.5f, 3.2f) : scale;
            unit.maxHealth = 1200.0f;
            unit.speed = 0.0f;
            ent->albedoColor = facPrimary;
            ent->emission = 3.0f;
        } else if (type == UnitType::SupportMech) {
            ent->modelIndex = (m_acuMeshIndex >= 0) ? m_acuMeshIndex : m_cubeMeshIndex;
            ent->scale = (scale == glm::vec3(1.0f)) ? glm::vec3(1.3f, 1.3f, 1.3f) : scale;
            unit.maxHealth = 1100.0f;
            unit.speed = 6.2f;
            ent->albedoColor = facAccent;
            ent->emission = 2.5f;
        }

        // Apply faction multipliers to final health, shield, and movement speed
        if (IsStaticBuilding(type)) {
            unit.maxHealth *= fac.mods.structureHealthMult;
        } else {
            unit.maxHealth *= fac.mods.unitHealthMult;
            unit.speed *= fac.mods.unitSpeedMult;
        }
        unit.health = unit.maxHealth;
        if (unit.maxShieldHealth > 0.0f) {
            unit.maxShieldHealth *= fac.mods.shieldHealthMult;
            unit.shieldHealth = unit.maxShieldHealth;
        }
        unit.baseScale = ent->scale;
        if (team == Team::Enemy && m_skirmishSettings.enableFogOfWar) {
            ent->scale = glm::vec3(0.0f);
            ent->alpha = 0.0f;
            if (unit.doorEntity) {
                unit.doorEntity->scale = glm::vec3(0.0f);
                unit.doorEntity->alpha = 0.0f;
            }
        }
        m_units.push_back(unit);
    }

    void RTSController::Update(float deltaTime, Scene* scene, PhysicsServer* physicsServer, AudioServer* audioServer, Camera* cam, IRenderer* renderer) {
        if (!scene || !cam) return;
        if (m_foliageDirty) {
            SyncFoliageBatches();
        }

        // Toggle Skirmish Menu with Escape
        if (Input::GetKeyDown(SDL_SCANCODE_ESCAPE) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_showSkirmishMenu = !m_showSkirmishMenu;
        }
        // Toggle Debug Telemetry with F3 or Backquote (tilde)
        if (Input::GetKeyDown(SDL_SCANCODE_F3) || ImGui::IsKeyPressed(ImGuiKey_F3, false) ||
            Input::GetKeyDown(SDL_SCANCODE_GRAVE) || ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
            m_showDebugTelemetry = !m_showDebugTelemetry;
        }
        // If in Main Menu (before match launched), perform cinematic camera orbit and return
        // --- FORGE MODE: MAP SCULPTOR ACTIVE ---
        if (m_inForgeMode) {
            float panSpeed = (m_cameraZoom * 0.75f) * deltaTime;
            float radYaw = glm::radians(m_cameraYaw);
            glm::vec2 camForward = glm::vec2(-sin(radYaw), cos(radYaw));
            glm::vec2 camRight = glm::vec2(cos(radYaw), sin(radYaw));

            if (Input::IsKeyDown(SDL_SCANCODE_W)) m_cameraTarget += glm::vec3(camForward * panSpeed, 0.0f);
            if (Input::IsKeyDown(SDL_SCANCODE_S)) m_cameraTarget -= glm::vec3(camForward * panSpeed, 0.0f);
            if (Input::IsKeyDown(SDL_SCANCODE_A)) m_cameraTarget -= glm::vec3(camRight * panSpeed, 0.0f);
            if (Input::IsKeyDown(SDL_SCANCODE_D)) m_cameraTarget += glm::vec3(camRight * panSpeed, 0.0f);

            float wheelDelta = (float)Input::scrollY;
            if (wheelDelta == 0.0f && ImGui::GetCurrentContext() != nullptr) {
                wheelDelta = ImGui::GetIO().MouseWheel;
            }
            if (std::abs(wheelDelta) > 0.01f) {
                float zoomSpeed = 2.5f + (m_cameraZoom * 0.06f);
                m_cameraZoom = std::clamp(m_cameraZoom - wheelDelta * zoomSpeed, 15.0f, 450.0f);
            }
            m_cameraTarget.x = std::clamp(m_cameraTarget.x, -460.0f, 460.0f);
            m_cameraTarget.y = std::clamp(m_cameraTarget.y, -460.0f, 460.0f);
            m_cameraTarget.z = GetTerrainHeight(m_cameraTarget.x, m_cameraTarget.y);

            bool isAltHeld = Input::IsKeyDown(SDL_SCANCODE_LALT);
            bool isMouseRotating = isAltHeld && ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) || (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_MIDDLE)));
            if (isMouseRotating) {
                m_cameraPitch -= (float)Input::mouseRelY * 0.35f;
                m_cameraPitch = std::clamp(m_cameraPitch, 15.0f, 85.0f);
                m_cameraYaw += (float)Input::mouseRelX * 0.35f;
                if (m_cameraYaw > 360.0f) m_cameraYaw -= 360.0f;
                if (m_cameraYaw < -360.0f) m_cameraYaw += 360.0f;
            }

            float radPitch = glm::radians(m_cameraPitch);
            glm::vec3 camOffset = glm::vec3(
                sin(radYaw) * cos(radPitch) * m_cameraZoom,
                -cos(radYaw) * cos(radPitch) * m_cameraZoom,
                sin(radPitch) * m_cameraZoom
            );
            cam->SetPosition(m_cameraTarget + camOffset);
            cam->LookAt(m_cameraTarget);

            // Real-time terrain sculpting or deposit node stamping
            ImVec2 mPos = ImGui::GetMousePos();
            bool isLeftDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
            bool isRightDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
            bool isOverViewport = (mPos.x >= m_viewportPos.x && mPos.x <= m_viewportPos.x + m_viewportSize.x &&
                                   mPos.y >= m_viewportPos.y && mPos.y <= m_viewportPos.y + m_viewportSize.y);
            bool isOverForgePanel = (mPos.x >= m_viewportPos.x + 15.0f && mPos.x <= m_viewportPos.x + 390.0f &&
                                     mPos.y >= m_viewportPos.y + 15.0f && mPos.y <= m_viewportPos.y + 650.0f);

            bool isShiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
            bool isCtrlHeld = (SDL_GetModState() & KMOD_CTRL) != 0;

            // Undo / Redo Hotkeys in Forge Mode:
            if (isCtrlHeld) {
                if (Input::GetKeyDown(SDL_SCANCODE_Z) || ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                    if (isShiftHeld) SculptRedo();
                    else SculptUndo();
                } else if (Input::GetKeyDown(SDL_SCANCODE_Y) || ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                    SculptRedo();
                }
            }

            if (isOverViewport && !isOverForgePanel && !isAltHeld) {
                glm::vec3 brushGround = ScreenToGround(cam, glm::vec2(mPos.x, mPos.y));

                if (m_forgeToolMode == 0) {
                    // Sculpt Terrain
                    if (isLeftDown) {
                        if (!m_isSculptStrokeActive) {
                            PushSculptUndoState();
                            m_isSculptStrokeActive = true;
                        }
                        int activeBrush = m_forgeBrushType;
                        if (isShiftHeld) {
                            activeBrush = 2; // Shift temporarily switches to Smooth brush!
                        } else if (isCtrlHeld) {
                            if (activeBrush == 0) activeBrush = 1;
                            else if (activeBrush == 1) activeBrush = 0;
                        }
                        ApplyTerrainBrush(brushGround, m_forgeBrushRadius, m_forgeBrushStrength * deltaTime * 18.0f, activeBrush, m_forgeFlattenHeight);
                    } else {
                        m_isSculptStrokeActive = false;
                    }
                } else if (m_forgeToolMode == 1 || m_forgeToolMode == 2) {
                    if (isLeftDown && !m_wasLeftMouseDown) {
                        SpawnResourceNode(brushGround, (m_forgeToolMode == 1) ? ResourceNodeType::Mass : ResourceNodeType::Energy);
                    }
                } else if (m_forgeToolMode == 3) {
                    // Delete Node or Tree
                    if (isLeftDown && !m_wasLeftMouseDown) {
                        bool deleted = false;
                        for (auto it = m_resourceNodes.begin(); it != m_resourceNodes.end(); ++it) {
                            if (glm::distance(glm::vec2(brushGround.x, brushGround.y), glm::vec2(it->position.x, it->position.y)) < 12.0f) {
                                if (it->entity) {
                                    it->entity->scale = glm::vec3(0.0f);
                                    it->entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                                }
                                m_resourceNodes.erase(it);
                                gameMessage = "Resource node deleted.";
                                deleted = true;
                                break;
                            }
                        }
                        if (!deleted) {
                            for (auto it = m_trees.begin(); it != m_trees.end(); ++it) {
                                if (glm::distance(glm::vec2(brushGround.x, brushGround.y), glm::vec2(it->position.x, it->position.y)) < 6.0f) {
                                    m_trees.erase(it);
                                    m_foliageDirty = true;
                                    gameMessage = "Tree deleted.";
                                    break;
                                }
                            }
                        }
                    }
                } else if (m_forgeToolMode == 4) {
                    // Plant Tree
                    if (isLeftDown) {
                        bool tooClose = false;
                        for (const auto& existing : m_trees) {
                            if (glm::distance(glm::vec2(brushGround.x, brushGround.y), glm::vec2(existing.position.x, existing.position.y)) < 4.0f) {
                                tooClose = true;
                                break;
                            }
                        }
                        if (!tooClose) {
                            float scale = 1.0f + (float)(rand() % 6) * 0.05f;
                            SpawnTree(brushGround, scale);
                            gameMessage = "Planted tree on terrain.";
                        }
                    }
                }

                // Right click always deletes clicked node or tree if in Forge Mode
                if (isRightDown && !m_wasRightMouseDown) {
                    bool deleted = false;
                    for (auto it = m_resourceNodes.begin(); it != m_resourceNodes.end(); ++it) {
                        if (glm::distance(glm::vec2(brushGround.x, brushGround.y), glm::vec2(it->position.x, it->position.y)) < 12.0f) {
                            if (it->entity) {
                                it->entity->scale = glm::vec3(0.0f);
                                it->entity->origin = glm::dvec3(0.0, 0.0, -999.0);
                            }
                            m_resourceNodes.erase(it);
                            gameMessage = "Resource node deleted.";
                            deleted = true;
                            break;
                        }
                    }
                    if (!deleted) {
                        for (auto it = m_trees.begin(); it != m_trees.end(); ++it) {
                            if (glm::distance(glm::vec2(brushGround.x, brushGround.y), glm::vec2(it->position.x, it->position.y)) < 6.0f) {
                                m_trees.erase(it);
                                m_foliageDirty = true;
                                gameMessage = "Tree deleted.";
                                break;
                            }
                        }
                    }
                }
            }
            m_wasLeftMouseDown = isLeftDown;
            m_wasRightMouseDown = isRightDown;
            return;
        }

        // If in Main Menu (before match launched), perform cinematic camera orbit and return
        if (!m_inMatch) {
            m_cameraYaw += 3.5f * deltaTime;
            if (m_cameraYaw > 360.0f) m_cameraYaw -= 360.0f;
            float radPitch = glm::radians(45.0f);
            float radYaw = glm::radians(m_cameraYaw);
            glm::vec3 camOffset = glm::vec3(
                sin(radYaw) * cos(radPitch) * 240.0f,
                -cos(radYaw) * cos(radPitch) * 240.0f,
                sin(radPitch) * 240.0f
            );
            cam->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f) + camOffset);
            cam->LookAt(glm::vec3(0.0f, 0.0f, 0.0f));
            return;
        }

        // --- FAF FOG OF WAR: UPDATE VISION CLEAR SOURCES & SHROUD ENEMY INTEL ---
        scene->environment.enableFog = m_skirmishSettings.enableFogOfWar;
        scene->environment.fogClearSources.clear();
        float playerVisMult = GetTeamFactionDef(Team::Player).mods.visionRangeMult;
        glm::vec2 basePos(-260.0f, 0.0f);
        float baseRadius = 110.0f * playerVisMult;
        scene->environment.fogOfWarCenter = basePos;
        scene->environment.fogOfWarRadius = baseRadius;

        // 1. Collect and categorize all living player units that can provide vision
        std::vector<const RTSUnit*> mobileExplorers;
        std::vector<const RTSUnit*> outpostStructures;
        std::vector<const RTSUnit*> baseDefenders;

        for (const auto& pu : m_units) {
            if (pu.team == Team::Player && pu.health > 0.0f) {
                float distToBase = glm::distance(glm::vec2(pu.position.x, pu.position.y), basePos);
                if (!IsStaticBuilding(pu.type)) {
                    if (distToBase > 60.0f) {
                        mobileExplorers.push_back(&pu);
                    } else {
                        baseDefenders.push_back(&pu);
                    }
                } else {
                    if (distToBase > 85.0f) {
                        outpostStructures.push_back(&pu);
                    }
                }
            }
        }

        // Prioritize mobile scouts and forward units (Planes > Mechs > Tanks > Engineers)
        std::sort(mobileExplorers.begin(), mobileExplorers.end(), [](const RTSUnit* a, const RTSUnit* b) {
            auto getPrio = [](UnitType t) {
                if (t == UnitType::Plane) return 0;
                if (t == UnitType::CommandMech) return 1;
                if (t == UnitType::Tank) return 2;
                if (t == UnitType::Engineer) return 3;
                return 4;
            };
            return getPrio(a->type) < getPrio(b->type);
        });

        // Add mobile explorer units to vision sources (with spatial de-duplication)
        for (const auto* pu : mobileExplorers) {
            if (scene->environment.fogClearSources.size() >= 64) break;
            float visionRadius = ((pu->type == UnitType::Plane) ? 95.0f :
                                ((pu->type == UnitType::CommandMech) ? 70.0f :
                                ((pu->type == UnitType::Tank) ? 60.0f : 50.0f))) * playerVisMult;

            bool redundant = false;
            for (const auto& src : scene->environment.fogClearSources) {
                if (glm::distance(glm::vec2(pu->position.x, pu->position.y), glm::vec2(src.position.x, src.position.y)) < 22.0f && src.radius >= visionRadius * 0.9f) {
                    redundant = true;
                    break;
                }
            }
            if (!redundant || scene->environment.fogClearSources.size() < 32) {
                scene->environment.fogClearSources.push_back({ pu->position, visionRadius });
            }
        }

        // Add forward outpost structures (Point Defense, forward factories, forward generators)
        for (const auto* pu : outpostStructures) {
            if (scene->environment.fogClearSources.size() >= 64) break;
            float visionRadius = ((pu->type == UnitType::Turret) ? 70.0f : 55.0f) * playerVisMult;
            scene->environment.fogClearSources.push_back({ pu->position, visionRadius });
        }

        // Fill remaining slots with perimeter base units
        for (const auto* pu : baseDefenders) {
            if (scene->environment.fogClearSources.size() >= 64) break;
            float visionRadius = ((pu->type == UnitType::Plane) ? 95.0f : 55.0f) * playerVisMult;
            scene->environment.fogClearSources.push_back({ pu->position, visionRadius });
        }

        // 3. Shroud Enemy Units and Structures outside Player Line-of-Sight (FAF Fog of War)
        if (!m_skirmishSettings.enableFogOfWar) {
            for (auto& u : m_units) {
                if (u.team == Team::Enemy && u.entity && u.health > 0.0f) {
                    u.scoutedByPlayer = true;
                    u.entity->origin = glm::dvec3(u.position.x, u.position.y, u.position.z);
                    u.entity->scale = u.baseScale;
                }
            }
        } else {
            for (auto& u : m_units) {
                if (u.team == Team::Enemy && u.entity && u.health > 0.0f) {
                    bool inPlayerVision = false;

                    // Check distance to base
                    if (glm::distance(glm::vec2(u.position.x, u.position.y), basePos) <= baseRadius) {
                        inPlayerVision = true;
                    }

                    // Check distance to each living player unit
                    if (!inPlayerVision) {
                        for (const auto& pu : m_units) {
                            if (pu.team == Team::Player && pu.health > 0.0f) {
                                float puVision = ((pu.type == UnitType::Plane) ? 95.0f :
                                                ((pu.type == UnitType::Turret) ? 70.0f :
                                                ((pu.type == UnitType::CommandMech) ? 65.0f :
                                                ((pu.type == UnitType::Tank) ? 55.0f :
                                                ((pu.type == UnitType::Engineer) ? 45.0f : 65.0f))))) * playerVisMult;
                                if (glm::distance(glm::vec2(u.position.x, u.position.y), glm::vec2(pu.position.x, pu.position.y)) <= puVision) {
                                    inPlayerVision = true;
                                    break;
                                }
                            }
                        }
                    }

                    float targetAlpha = inPlayerVision ? 1.0f : 0.0f;
                    float fadeRate = 4.0f * deltaTime;
                    if (u.entity->alpha < targetAlpha) u.entity->alpha = std::min(targetAlpha, u.entity->alpha + fadeRate);
                    else if (u.entity->alpha > targetAlpha) u.entity->alpha = std::max(targetAlpha, u.entity->alpha - fadeRate);

                    // Entity origin MUST ALWAYS track true unit simulation position: NEVER reset to (0, 0, -999)!
                    u.entity->origin = glm::dvec3(u.position.x, u.position.y, u.position.z);

                    if (inPlayerVision) {
                        u.scoutedByPlayer = true;
                        u.entity->scale = u.baseScale;
                        if (u.type == UnitType::CommandMech) u.entity->emission = 3.5f;
                        else if (u.type == UnitType::MassGen || u.type == UnitType::EnergyGen) u.entity->emission = 2.5f;
                        else if (u.type == UnitType::Turret) u.entity->emission = 1.8f;
                        else if (u.type == UnitType::AirFactory) u.entity->emission = 1.5f;
                        else if (u.type == UnitType::LandFactory) u.entity->emission = 1.0f;
                        else if (u.type == UnitType::Engineer) u.entity->emission = 0.5f;
                        else u.entity->emission = 0.0f;
                    } else {
                        if (u.entity->alpha > 0.01f) {
                            // Smoothly dissolving out into volumetric fog: maintain scale and zero emission
                            u.entity->scale = u.baseScale;
                            u.entity->emission = 0.0f;
                        } else {
                            // Fully dissolved and outside line-of-sight
                            if (!IsStaticBuilding(u.type)) {
                                u.entity->scale = glm::vec3(0.0f);
                                u.entity->emission = 0.0f;
                                u.selected = false;
                            } else {
                                if (!u.scoutedByPlayer) {
                                    u.entity->scale = glm::vec3(0.0f);
                                    u.entity->emission = 0.0f;
                                    u.selected = false;
                                } else {
                                    // Scouted building: visible as a ghost intel marker
                                    u.entity->scale = u.baseScale;
                                    u.entity->emission = 0.0f;
                                    u.entity->alpha = 0.65f;
                                }
                            }
                        }
                    }
                }
            }
        }

        // 4. Shroud unoccupied resource nodes if outside player vision
        for (auto& node : m_resourceNodes) {
            if (node.entity) {
                bool nodeInVision = (glm::distance(glm::vec2(node.position.x, node.position.y), basePos) <= baseRadius);
                if (!nodeInVision) {
                    for (const auto& pu : m_units) {
                        if (pu.team == Team::Player && pu.health > 0.0f) {
                            float puVision = ((pu.type == UnitType::Plane) ? 95.0f : ((pu.type == UnitType::Turret || pu.type == UnitType::CommandMech) ? 70.0f : 55.0f)) * playerVisMult;
                            if (glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(pu.position.x, pu.position.y)) <= puVision) {
                                nodeInVision = true;
                                break;
                            }
                        }
                    }
                }
                node.entity->emission = nodeInVision ? 3.5f : 0.0f;
            }
        }


        // --- 1. RTS CAMERA PAN & ZOOM ---
        float panSpeed = (90.0f + m_cameraZoom * 0.75f) * deltaTime;
        float radYaw = glm::radians(m_cameraYaw);
        glm::vec2 camForward = glm::vec2(-sin(radYaw), cos(radYaw));
        glm::vec2 camRight = glm::vec2(cos(radYaw), sin(radYaw));

        if (Input::IsKeyDown(SDL_SCANCODE_W) || Input::IsKeyDown(SDL_SCANCODE_UP)) m_cameraTarget += glm::vec3(camForward * panSpeed, 0.0f);
        if (Input::IsKeyDown(SDL_SCANCODE_S) || Input::IsKeyDown(SDL_SCANCODE_DOWN)) m_cameraTarget -= glm::vec3(camForward * panSpeed, 0.0f);
        if (Input::IsKeyDown(SDL_SCANCODE_A) || Input::IsKeyDown(SDL_SCANCODE_LEFT)) m_cameraTarget -= glm::vec3(camRight * panSpeed, 0.0f);
        if (Input::IsKeyDown(SDL_SCANCODE_D) || Input::IsKeyDown(SDL_SCANCODE_RIGHT)) m_cameraTarget += glm::vec3(camRight * panSpeed, 0.0f);

        float wheelDelta = (float)Input::scrollY;
        if (wheelDelta == 0.0f && ImGui::GetCurrentContext() != nullptr) {
            wheelDelta = ImGui::GetIO().MouseWheel;
        }
        if (Input::IsKeyDown(SDL_SCANCODE_PAGEUP) || Input::IsKeyDown(SDL_SCANCODE_EQUALS)) wheelDelta += 8.0f * deltaTime;
        if (Input::IsKeyDown(SDL_SCANCODE_PAGEDOWN) || Input::IsKeyDown(SDL_SCANCODE_MINUS)) wheelDelta -= 8.0f * deltaTime;

        float zoomSpeed = 2.5f + (m_cameraZoom * 0.06f);
        m_cameraZoom -= wheelDelta * zoomSpeed;
        m_cameraZoom = std::clamp(m_cameraZoom, 8.0f, 420.0f);
        m_cameraTarget.x = std::clamp(m_cameraTarget.x, -380.0f, 380.0f);
        m_cameraTarget.y = std::clamp(m_cameraTarget.y, -380.0f, 380.0f);

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
        // --- NO RUSH TIMER TICK ---
        if (m_noRushTimer > 0.0f) {
            m_noRushTimer -= deltaTime;
            if (m_noRushTimer <= 0.0f) {
                m_noRushTimer = 0.0f;
                gameMessage = "NO RUSH PERIOD HAS ENDED! Weapons Free!";
                if (audioServer) audioServer->PlayOneShot("assets/audio/cannon.wav", 1.0f);
            }
        }

        // --- UNIT CONTROL GROUPS (Ctrl + 1..9 to assign, 1..9 to recall) ---
        bool isCtrlHeld = Input::IsKeyDown(SDL_SCANCODE_LCTRL) || Input::IsKeyDown(SDL_SCANCODE_RCTRL);
        SDL_Scancode numKeys[9] = {
            SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
            SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9
        };

        for (int k = 0; k < 9; ++k) {
            if (Input::GetKeyDown(numKeys[k])) {
                int groupNum = k + 1;
                if (isCtrlHeld) {
                    m_controlGroups[groupNum].clear();
                    for (const auto& u : m_units) {
                        if (u.selected && u.team == Team::Player && u.health > 0.0f) {
                            m_controlGroups[groupNum].push_back(u.entityIndex);
                        }
                    }
                    gameMessage = "Control Group " + std::to_string(groupNum) + " assigned (" + std::to_string(m_controlGroups[groupNum].size()) + " units).";
                    if (audioServer) audioServer->PlayOneShot("assets/audio/ui_select.wav", 0.6f);
                } else {
                    if (!m_controlGroups[groupNum].empty()) {
                        ClearSelection();
                        int recalled = 0;
                        for (auto& u : m_units) {
                            if (u.team == Team::Player && u.health > 0.0f) {
                                for (int idx : m_controlGroups[groupNum]) {
                                    if (u.entityIndex == idx) {
                                        u.selected = true;
                                        recalled++;
                                        break;
                                    }
                                }
                            }
                        }
                        if (recalled > 0) {
                            gameMessage = "Group " + std::to_string(groupNum) + " recalled (" + std::to_string(recalled) + " units).";
                            if (audioServer) audioServer->PlayOneShot("assets/audio/ui_select.wav", 0.6f);
                        }
                    }
                }
            }
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
        if (!gameOver) m_resourceTimer += deltaTime;
        // Countdown explicit Mass Overdrive timer.
        if (m_boostTimer > 0.0f) {
            m_boostTimer -= deltaTime;
            if (m_boostTimer < 0.0f) m_boostTimer = 0.0f;
        }

        while (m_resourceTimer >= 1.0f && !gameOver) {
            m_resourceTimer -= 1.0f;

            int playerMassGenCount = 0;
            int playerEnergyGenCount = 0;
            int playerShieldCount = 0;
            int playerMassStorageCount = 0;
            int playerEnergyStorageCount = 0;
            int enemyMassGenCount = 0;
            int enemyEnergyGenCount = 0;
            int enemyShieldCount = 0;
            int enemyMassStorageCount = 0;
            int enemyEnergyStorageCount = 0;

            for (const auto& u : m_units) {
                if (u.health > 0.0f) {
                    if (u.team == Team::Player) {
                        if (u.type == UnitType::MassGen) playerMassGenCount++;
                        else if (u.type == UnitType::EnergyGen) playerEnergyGenCount++;
                        else if (u.type == UnitType::ShieldGen && u.isShieldActive) playerShieldCount++;
                        else if (u.type == UnitType::MassStorage) playerMassStorageCount++;
                        else if (u.type == UnitType::EnergyStorage) playerEnergyStorageCount++;
                    } else {
                        if (u.type == UnitType::MassGen) enemyMassGenCount++;
                        else if (u.type == UnitType::EnergyGen) enemyEnergyGenCount++;
                        else if (u.type == UnitType::ShieldGen && u.isShieldActive) enemyShieldCount++;
                        else if (u.type == UnitType::MassStorage) enemyMassStorageCount++;
                        else if (u.type == UnitType::EnergyStorage) enemyEnergyStorageCount++;
                    }
                }
            }

            const auto& pFac = GetTeamFactionDef(Team::Player);
            const auto& eFac = GetTeamFactionDef(Team::Enemy);
            float massPerGen = (m_boostTimer > 0.0f) ? 8.0f : 4.0f; // Strategic income pacing
            float energyPerGen = 10.0f;
            float baseMassTrickle = 2.0f;
            float baseEnergyTrickle = 4.0f;
            float shieldEnergyUpkeep = 20.0f; // High active power consumption per shield generator

            float playerMassCap = 1000.0f + (float)playerMassStorageCount * 5000.0f;
            float playerEnergyCap = 5000.0f + (float)playerEnergyStorageCount * 15000.0f;
            float enemyMassCap = 1000.0f + (float)enemyMassStorageCount * 5000.0f;
            float enemyEnergyCap = 5000.0f + (float)enemyEnergyStorageCount * 15000.0f;

            float pMassGain = (baseMassTrickle + (float)playerMassGenCount * massPerGen) * pFac.mods.massIncomeMult;
            float pEnergyGain = (baseEnergyTrickle + (float)playerEnergyGenCount * energyPerGen) * pFac.mods.energyIncomeMult;
            playerMass = std::min(playerMassCap, playerMass + pMassGain);
            playerEnergy = std::min(playerEnergyCap, playerEnergy + pEnergyGain - (float)playerShieldCount * shieldEnergyUpkeep);
            if (playerEnergy < 0.0f) {
                playerEnergy = 0.0f;
                // Power brownout: shields collapse!
                for (auto& u : m_units) {
                    if (u.team == Team::Player && u.type == UnitType::ShieldGen && u.isShieldActive) {
                        u.isShieldActive = false;
                        u.shieldHealth = 0.0f;
                        u.shieldRechargeTimer = 15.0f;
                    }
                }
                gameMessage = "CRITICAL: Power brownout! Force-field shields collapsed due to energy deficit!";
            }

            float eMassGain = (baseMassTrickle + (float)enemyMassGenCount * 4.0f) * eFac.mods.massIncomeMult;
            float eEnergyGain = (baseEnergyTrickle + (float)enemyEnergyGenCount * energyPerGen) * eFac.mods.energyIncomeMult;
            enemyMass = std::min(enemyMassCap, enemyMass + eMassGain);
            enemyEnergy = std::min(enemyEnergyCap, enemyEnergy + eEnergyGain - (float)enemyShieldCount * shieldEnergyUpkeep);
            if (enemyEnergy < 0.0f) {
                enemyEnergy = 0.0f;
                for (auto& u : m_units) {
                    if (u.team == Team::Enemy && u.type == UnitType::ShieldGen && u.isShieldActive) {
                        u.isShieldActive = false;
                        u.shieldHealth = 0.0f;
                        u.shieldRechargeTimer = 15.0f;
                    }
                }
            }
        }
        UpdateAI(deltaTime);

        // --- 4. MOUSE SELECTION & ORDERS ---
        m_activeCam = cam;
        ImVec2 mousePos = ImGui::GetMousePos();

        bool isOverViewport = (mousePos.x >= m_viewportPos.x && mousePos.x <= m_viewportPos.x + m_viewportSize.x &&
                               mousePos.y >= m_viewportPos.y && mousePos.y <= m_viewportPos.y + m_viewportSize.y);

        ImVec2 hudPos(m_viewportPos.x + 15, m_viewportPos.y + 15);
        ImVec2 hudSize(445, 140);
        bool isOverHud = (mousePos.x >= hudPos.x && mousePos.x <= hudPos.x + hudSize.x &&
                          mousePos.y >= hudPos.y && mousePos.y <= hudPos.y + hudSize.y);

        float buildW = 325.0f;
        float buildH = 515.0f;
        ImVec2 buildPos(m_viewportPos.x + 16.0f, m_viewportPos.y + m_viewportSize.y - buildH - 16.0f);
        bool isOverBuild = (mousePos.x >= buildPos.x && mousePos.x <= buildPos.x + buildW &&
                            mousePos.y >= buildPos.y && mousePos.y <= buildPos.y + buildH);

        float minimapW = 340.0f;
        float minimapH = 340.0f;
        ImVec2 minimapPos(m_viewportPos.x + m_viewportSize.x - minimapW - 16.0f, m_viewportPos.y + 16.0f);
        bool isOverMinimap = m_showMinimap && (mousePos.x >= minimapPos.x && mousePos.x <= minimapPos.x + minimapW &&
                                              mousePos.y >= minimapPos.y && mousePos.y <= minimapPos.y + minimapH);

        float barW = 460.0f;
        float barH = 92.0f;
        ImVec2 barPos(m_viewportPos.x + (m_viewportSize.x * 0.5f) - barW * 0.5f, m_viewportPos.y + m_viewportSize.y - barH - 12.0f);
        bool isOverSelectedBar = (mousePos.x >= barPos.x && mousePos.x <= barPos.x + barW &&
                                 mousePos.y >= barPos.y && mousePos.y <= barPos.y + barH);

        bool canInteract3D = isOverViewport && !isOverHud && !isOverBuild && !isOverMinimap && !isOverSelectedBar && !isAltHeld && !m_showSkirmishMenu && !m_showSettingsMenu;

        if (m_minimapPingTimer > 0.0f) {
            m_minimapPingTimer -= deltaTime;
            if (m_minimapPingTimer < 0.0f) m_minimapPingTimer = 0.0f;
        }
        bool isLeftDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        bool isRightDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;


        // --- 4. BUILDING PLACEMENT PREVIEW & CONFIRMATION ---
        if (m_isPlacingBuilding && m_buildGhostEntity) {
            glm::vec3 groundPos = ScreenToGround(cam, glm::vec2(mousePos.x, mousePos.y));

            // Snap MassGen to closest unoccupied Mass node
            if (m_placingBuildingType == UnitType::MassGen) {
                float minSnapDist = 18.0f;
                for (const auto& node : m_resourceNodes) {
                    if (!node.occupied && node.type == ResourceNodeType::Mass) {
                        float d = glm::distance(glm::vec2(groundPos.x, groundPos.y), glm::vec2(node.position.x, node.position.y));
                        if (d < minSnapDist) {
                            minSnapDist = d;
                            groundPos = glm::vec3(node.position.x, node.position.y, 1.5f);
                        }
                    }
                }
            } else if (m_placingBuildingType == UnitType::EnergyGen) {
                // Snap EnergyGen to closest unoccupied Power/Energy node
                float minSnapDist = 18.0f;
                for (const auto& node : m_resourceNodes) {
                    if (!node.occupied && node.type == ResourceNodeType::Energy) {
                        float d = glm::distance(glm::vec2(groundPos.x, groundPos.y), glm::vec2(node.position.x, node.position.y));
                        if (d < minSnapDist) {
                            minSnapDist = d;
                            groundPos = glm::vec3(node.position.x, node.position.y, 2.0f);
                        }
                    }
                }
            }

            float ghostOffsetZ = (m_placingBuildingType == UnitType::MassGen) ? 1.5f :
                                 ((m_placingBuildingType == UnitType::AirFactory) ? ((m_airFactoryMeshIndex >= 0) ? 0.0f : 2.0f) :
                                 ((m_placingBuildingType == UnitType::LandFactory) ? ((m_landFactoryMeshIndex >= 0) ? 0.0f : 1.75f) :
                                 ((m_placingBuildingType == UnitType::Turret) ? 3.0f :
                                 ((m_placingBuildingType == UnitType::AATurret) ? 2.25f :
                                 ((m_placingBuildingType == UnitType::EnergyGen) ? 2.0f :
                                 ((m_placingBuildingType == UnitType::EnergyStorage) ? 1.9f : 1.75f))))));
            glm::vec3 ghostPrim = GetTeamDisplayColor(Team::Player, false);
            glm::vec3 ghostAcc = GetTeamDisplayColor(Team::Player, true);
            m_buildGhostEntity->origin = glm::dvec3(groundPos.x, groundPos.y, groundPos.z + ghostOffsetZ);
            if (m_placingBuildingType == UnitType::LandFactory) {
                m_buildGhostEntity->modelIndex = (m_landFactoryMeshIndex >= 0) ? m_landFactoryMeshIndex : m_cubeMeshIndex;
                m_buildGhostEntity->scale = (m_landFactoryMeshIndex >= 0) ? glm::vec3(3.2f, 3.2f, 3.2f) : glm::vec3(7.0f, 6.0f, 3.5f);
                m_buildGhostEntity->albedoColor = ghostPrim;
            } else if (m_placingBuildingType == UnitType::AirFactory) {
                m_buildGhostEntity->modelIndex = (m_airFactoryMeshIndex >= 0) ? m_airFactoryMeshIndex : m_cubeMeshIndex;
                m_buildGhostEntity->scale = (m_airFactoryMeshIndex >= 0) ? glm::vec3(3.2f, 3.2f, 3.2f) : glm::vec3(6.0f, 7.0f, 4.0f);
                m_buildGhostEntity->albedoColor = ghostAcc;
            } else if (m_placingBuildingType == UnitType::MassGen) {
                m_buildGhostEntity->modelIndex = m_cubeMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(4.0f, 4.0f, 3.0f);
                m_buildGhostEntity->albedoColor = glm::mix(glm::vec3(0.8f, 0.3f, 1.0f), ghostPrim, 0.35f);
            } else if (m_placingBuildingType == UnitType::EnergyGen) {
                m_buildGhostEntity->modelIndex = m_cylinderMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(3.0f, 3.0f, 4.0f);
                m_buildGhostEntity->albedoColor = glm::mix(glm::vec3(0.9f, 0.9f, 0.1f), ghostPrim, 0.25f);
            } else if (m_placingBuildingType == UnitType::Turret) {
                m_buildGhostEntity->modelIndex = m_cubeMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(1.5f, 1.5f, 6.0f);
                m_buildGhostEntity->albedoColor = ghostPrim;
            } else if (m_placingBuildingType == UnitType::AATurret) {
                m_buildGhostEntity->modelIndex = m_cylinderMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(1.8f, 1.8f, 4.5f);
                m_buildGhostEntity->albedoColor = ghostAcc;
            } else if (m_placingBuildingType == UnitType::ShieldGen) {
                m_buildGhostEntity->modelIndex = m_cylinderMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(5.0f, 5.0f, 3.5f);
                m_buildGhostEntity->albedoColor = ghostAcc;
            } else if (m_placingBuildingType == UnitType::MassStorage) {
                m_buildGhostEntity->modelIndex = m_cubeMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(3.6f, 3.6f, 3.0f);
                m_buildGhostEntity->albedoColor = glm::mix(glm::vec3(0.65f, 0.25f, 0.9f), ghostPrim, 0.35f);
            } else if (m_placingBuildingType == UnitType::EnergyStorage) {
                m_buildGhostEntity->modelIndex = m_cylinderMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(3.5f, 3.5f, 3.8f);
                m_buildGhostEntity->albedoColor = glm::mix(glm::vec3(0.95f, 0.85f, 0.15f), ghostPrim, 0.25f);
            } else if (m_placingBuildingType == UnitType::NukeSilo) {
                m_buildGhostEntity->modelIndex = m_cylinderMeshIndex;
                m_buildGhostEntity->scale = glm::vec3(5.5f, 5.5f, 3.2f);
                m_buildGhostEntity->albedoColor = ghostPrim;
            }
            m_buildGhostEntity->emission = 4.5f;

            bool isLeftClick = canInteract3D && isLeftDown && !m_isDraggingBox;
            bool isRightClick = canInteract3D && isRightDown && !m_wasRightMouseDown;

            if (isLeftClick) {
                RTSUnit* selectedEng = nullptr;
                for (auto& u : m_units) {
                    if (u.selected && u.team == Team::Player && (u.type == UnitType::Engineer || u.type == UnitType::CommandMech) &&
                        u.health > 0.0f && !HasConstructionTask(u.entityIndex)) {
                        selectedEng = &u;
                        break;
                    }
                }

                if (!selectedEng) {
                    gameMessage = "Select an idle Engineer or Command Mech. Move a builder to cancel its job.";
                    m_isPlacingBuilding = false;
                    m_buildGhostEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                    m_buildGhostEntity->scale = glm::vec3(0.0f);
                } else if (TryStartConstruction(m_placingBuildingType, Team::Player, selectedEng->entityIndex, groundPos)) {
                    gameMessage = "Construction ordered. Move the builder to cancel and refund unbuilt work.";
                    m_isPlacingBuilding = false;
                    m_buildGhostEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                    m_buildGhostEntity->scale = glm::vec3(0.0f);
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
                        targetSlot.x = std::clamp(targetSlot.x, -460.0f, 460.0f);
                        targetSlot.y = std::clamp(targetSlot.y, -460.0f, 460.0f);
                        if (projected[i].second->type == UnitType::Engineer || projected[i].second->type == UnitType::CommandMech)
                            CancelConstruction(projected[i].second->entityIndex);
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
        // If single unit selected, show individual ring. If multiselected, hide individual rings (group hull encompasses them).
        int totalSelectedUnits = 0;
        for (const auto& u : m_units) {
            if (u.selected && u.team == Team::Player && u.health > 0.0f) {
                totalSelectedUnits++;
            }
        }

        size_t ringIdx = 0;
        if (totalSelectedUnits == 1) {
            for (const auto& u : m_units) {
                if (u.selected && ringIdx < m_selectionRingEntities.size()) {
                    CBaseEntity* ringEnt = m_selectionRingEntities[ringIdx++];
                    float baseR = (u.type == UnitType::Plane) ? 2.8f : ((u.type == UnitType::Factory) ? 6.0f : ((u.type == UnitType::LandFactory || u.type == UnitType::AirFactory) ? 5.2f : ((u.type == UnitType::CommandMech) ? 3.6f : 2.5f)));
                    float ringZ = (u.type == UnitType::Plane) ? 11.5f : 0.05f;
                    ringEnt->origin = glm::dvec3(u.position.x, u.position.y, ringZ);
                    ringEnt->scale = glm::vec3(baseR, baseR, 1.0f);
                    ringEnt->albedoColor = glm::vec3(0.0f, 1.0f, 0.6f);
                    ringEnt->emission = 6.0f;
                }
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

            // 1. Force-Field Shield Interception check
            RTSUnit* hitShieldUnit = nullptr;
            for (auto& su : m_units) {
                if (su.team != p.team && su.isShieldActive && su.shieldHealth > 0.0f) {
                    float distToShield = glm::distance(p.position, su.position);
                    if (distToShield <= su.shieldRadius) {
                        hitShieldUnit = &su;
                        impacted = true;
                        break;
                    }
                }
            }

            // 2. Proximity check against enemy units
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

                if (hitShieldUnit) {
                    // Force-field absorbs damage and intercepts blast!
                    hitShieldUnit->shieldHealth -= p.damage;
                    glm::vec3 hitPoint = hitShieldUnit->position + ((glm::length(p.position - hitShieldUnit->position) > 0.001f) ? glm::normalize(p.position - hitShieldUnit->position) : glm::vec3(0, 0, 1)) * hitShieldUnit->shieldRadius;
                    SpawnExplosionCluster(hitPoint, 0.7f, 4);
                    if (audioServer) audioServer->PlayOneShot("assets/audio/shield_hit.wav", 0.85f);

                    if (hitShieldUnit->shieldHealth <= 0.0f) {
                        hitShieldUnit->shieldHealth = 0.0f;
                        hitShieldUnit->isShieldActive = false;
                        hitShieldUnit->shieldRechargeTimer = 180.0f; // 3 minutes recharge
                        SpawnExplosionCluster(hitShieldUnit->position, 2.2f, 12);
                        if (hitShieldUnit->team == Team::Player) {
                            gameMessage = "CRITICAL: Player Force-Field Shield collapsed! Recharging (180s)...";
                        } else {
                            gameMessage = "Enemy Force-Field Shield collapsed under heavy fire!";
                        }
                    }
                    continue; // Protected! No splash damage through active shield
                }
                // Explosion cluster scale and splash radius based on warhead
                float explScale = 1.0f;
                int sphereCount = 5;
                float splashRadius = 4.0f;
                if (p.type == ProjectileType::Rocket) {
                    explScale = 1.5f; sphereCount = 7; splashRadius = 6.5f;
                } else if (p.type == ProjectileType::AAFlak) {
                    explScale = 1.2f; sphereCount = 6; splashRadius = 5.0f;
                } else if (p.type == ProjectileType::StrategicNuke) {
                    explScale = 4.5f; sphereCount = 28; splashRadius = 38.0f;
                    gameMessage = "CRITICAL: Strategic Nuclear Detonation on battlefield!";
                    if (audioServer) audioServer->PlayOneShot("assets/audio/explosion.wav", 1.0f);
                } else if (p.type == ProjectileType::EMPPulse) {
                    explScale = 3.0f; sphereCount = 18; splashRadius = 65.0f;
                    gameMessage = "EMP Shockwave: All shields collapsed and defenses disabled!";
                    if (audioServer) audioServer->PlayOneShot("assets/audio/emp.wav", 1.0f);
                    for (auto& target : m_units) {
                        if (target.team != p.team && target.health > 0.0f) {
                            if (glm::distance(p.position, target.position) <= splashRadius) {
                                if (target.isShieldActive) {
                                    target.isShieldActive = false;
                                    target.shieldHealth = 0.0f;
                                    target.shieldRechargeTimer = 25.0f;
                                }
                                if (target.type == UnitType::Turret || target.type == UnitType::AATurret) {
                                    target.attackCooldown = 10.0f;
                                }
                            }
                        }
                    }
                }
                SpawnExplosionCluster(p.position, explScale, sphereCount);

                if (audioServer && p.type != ProjectileType::StrategicNuke && p.type != ProjectileType::EMPPulse) {
                    audioServer->PlayOneShot("assets/audio/explosion.wav", 0.85f);
                }

                // Area damage
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
        // --- 6B. UPDATE FORCE-FIELD SHIELDS & 3-MINUTE (180s) RECHARGE ---
        for (auto& u : m_units) {
            if (u.maxShieldHealth > 0.0f && u.health > 0.0f) {
                // If collapsed under fire, count down 180s (3 minutes) until full recharge
                if (!u.isShieldActive) {
                    u.shieldRechargeTimer -= deltaTime;
                    if (u.shieldRechargeTimer <= 0.0f) {
                        u.isShieldActive = true;
                        u.shieldHealth = u.maxShieldHealth;
                        u.shieldRechargeTimer = 0.0f;
                        SpawnExplosionCluster(u.position, 1.4f, 8);
                        if (u.team == Team::Player) {
                            gameMessage = "Force-Field Shield fully recharged (100%) and online!";
                        } else {
                            gameMessage = "Enemy Force-Field Shield recharged back to full power!";
                        }
                    }
                }

                // Update this unit's own dedicated shield orb entity (never swapped or shared!)
                if (u.shieldOrbEntity) {
                    if (u.isShieldActive && u.shieldHealth > 0.0f) {
                        u.shieldOrbEntity->origin = glm::dvec3(u.position.x, u.position.y, 0.0);
                        float orbDiameter = u.shieldRadius * 2.0f;
                        u.shieldOrbEntity->scale = glm::vec3(orbDiameter, orbDiameter, orbDiameter);
                    } else {
                        // Shield down or recharging: hide orb
                        u.shieldOrbEntity->origin = glm::dvec3(u.position.x, u.position.y, -999.0);
                        u.shieldOrbEntity->scale = glm::vec3(0.0f);
                    }
                }
            }
        }
        // Note: Ground units can freely enter and exit shields (repulsion removed as requested).


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
                if (unit.doorEntity) {
                    unit.doorEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
                    unit.doorEntity->scale = glm::vec3(0.0f);
                }
                m_units.erase(m_units.begin() + uIdx);
                continue;
            }

            // Movement logic
            // Movement logic: ALWAYS advance simulation from unit.position!
            if (unit.hasTarget && !IsStaticBuilding(unit.type)) {
                glm::vec3 currPos = unit.position;
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
                    
                    currPos.x = std::clamp(currPos.x, -480.0f, 480.0f);
                    currPos.y = std::clamp(currPos.y, -480.0f, 480.0f);

                    float groundZ = GetTerrainHeight(currPos.x, currPos.y);
                    if (unit.type == UnitType::Plane) {
                        currPos.z = groundZ + 14.0f;
                    } else {
                        currPos.z = groundZ;
                    }
                    unit.position = currPos;
                    if (ent) {
                        ent->origin = glm::dvec3(currPos.x, currPos.y, currPos.z);
                        // Orient unit along movement direction
                        float moveYaw = glm::degrees(atan2(dir.y, dir.x));
                        ent->angles = glm::vec3(0.0f, 0.0f, moveYaw);
                    }
                } else {
                    unit.hasTarget = false;
                }
            }

            // Combat Engagement & Target Acquisition
            unit.attackCooldown -= deltaTime;
            if (unit.type == UnitType::Tank || unit.type == UnitType::Plane || unit.type == UnitType::Turret ||
                unit.type == UnitType::AATurret || unit.type == UnitType::SupportMech || unit.type == UnitType::CommandMech) {
                float engageRadius = (unit.type == UnitType::AATurret) ? 70.0f :
                    ((unit.type == UnitType::Plane) ? 55.0f : ((unit.type == UnitType::CommandMech) ? 45.0f : ((unit.type == UnitType::Turret) ? 50.0f : 35.0f)));
                const auto canEngage = [&](const RTSUnit& target) {
                    if (unit.type == UnitType::AATurret) return target.type == UnitType::Plane;
                    if (unit.type == UnitType::Turret || unit.type == UnitType::Tank || unit.type == UnitType::SupportMech)
                        return target.type != UnitType::Plane;
                    return true;
                };

                RTSUnit* bestTarget = nullptr;
                float bestDist = engageRadius;
                // Priority 0: Threats attacking or within close range of friendly buildings/engineers
                for (const auto& friendly : m_units) {
                    if (friendly.team == unit.team && friendly.health > 0.0f && (IsStaticBuilding(friendly.type) || friendly.type == UnitType::Engineer)) {
                        float distToFriendly = glm::distance(unit.position, friendly.position);
                        if (distToFriendly <= 55.0f) {
                            for (auto& threat : m_units) {
                                if (threat.team != unit.team && threat.health > 0.0f) {
                                    if (!canEngage(threat)) continue;
                                    if (unit.team == Team::Player && threat.entity && threat.entity->scale.x <= 0.01f) continue;
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
                        if (!canEngage(enemy)) continue;
                        if (unit.team == Team::Player && enemy.entity && enemy.entity->scale.x <= 0.01f) continue;
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
                            if (!canEngage(enemy)) continue;
                            if (unit.team == Team::Player && enemy.entity && enemy.entity->scale.x <= 0.01f) continue;
                            float d = glm::distance(unit.position, enemy.position);
                            if (d < bestDist) {
                                bestDist = d;
                                bestTarget = &enemy;
                            }
                        }
                    }
                }

                // Priority 3: Active enemy force-field shields within weapon range of the shield perimeter!
                if (!bestTarget) {
                    for (auto& enemy : m_units) {
                        if (enemy.team != unit.team && enemy.isShieldActive && enemy.shieldHealth > 0.0f && enemy.health > 0.0f) {
                            if (!canEngage(enemy)) continue;
                            if (unit.team == Team::Player && enemy.entity && enemy.entity->scale.x <= 0.01f) continue;
                            float distToCenter = glm::distance(unit.position, enemy.position);
                            float distToPerimeter = std::max(0.0f, distToCenter - enemy.shieldRadius);
                            if (distToPerimeter <= engageRadius) {
                                if (distToPerimeter < bestDist) {
                                    bestDist = distToPerimeter;
                                    bestTarget = &enemy;
                                }
                            }
                        }
                    }
                }

                if (bestTarget) {
                    unit.inCombat = true;
                    if (unit.attackCooldown <= 0.0f) {
                        glm::vec3 targetAim = bestTarget->position + glm::vec3(0.0f, 0.0f, 0.6f);
                        // If targeting an active shield dome from outside, aim directly at the facing shield surface
                        if (bestTarget->isShieldActive && bestTarget->shieldHealth > 0.0f) {
                            float dToCenter = glm::distance(unit.position, bestTarget->position);
                            if (dToCenter > bestTarget->shieldRadius) {
                                glm::vec3 toUnit = (dToCenter > 0.001f) ? (unit.position - bestTarget->position) / dToCenter : glm::vec3(1, 0, 0);
                                targetAim = bestTarget->position + toUnit * (bestTarget->shieldRadius * 0.98f) + glm::vec3(0.0f, 0.0f, 2.0f);
                            }
                        }

                        const auto& atkFac = GetFactionDef(unit.faction);
                        float dmgMult = atkFac.mods.weaponDamageMult * ((unit.type == UnitType::Plane) ? atkFac.mods.airDamageMult : 1.0f);
                        if (unit.type == UnitType::Tank || unit.type == UnitType::SupportMech) {
                            glm::vec3 muzzle = unit.position + glm::vec3(0.0f, 0.0f, 0.6f);
                            FireProjectile(ProjectileType::TankShell, unit.team, muzzle, targetAim,
                                (unit.type == UnitType::SupportMech ? 38.0f : 26.0f) * dmgMult);
                            SpawnExplosionCluster(muzzle, 0.35f, 2);
                            if (audioServer) audioServer->PlayOneShot("assets/audio/cannon.wav", 0.9f);
                            unit.attackCooldown = 1.8f;
                        } else if (unit.type == UnitType::Plane) {
                            glm::vec3 pLaunch = unit.position - glm::vec3(0.0f, 0.0f, 1.0f);
                            FireProjectile(ProjectileType::Rocket, unit.team, pLaunch, targetAim, 39.0f * dmgMult);
                            SpawnExplosionCluster(pLaunch, 0.45f, 2);
                            if (audioServer) audioServer->PlayOneShot("assets/audio/rocket.wav", 0.9f);
                            unit.attackCooldown = 2.2f;
                        } else if (unit.type == UnitType::Turret) {
                            glm::vec3 muzzle = unit.position + glm::vec3(0.0f, 0.0f, 2.8f);
                            FireProjectile(ProjectileType::TankShell, unit.team, muzzle, targetAim, 18.0f * dmgMult);
                            SpawnExplosionCluster(muzzle, 0.35f, 2);
                            if (audioServer) audioServer->PlayOneShot("assets/audio/turret.wav", 0.8f);
                            unit.attackCooldown = 1.1f;
                        } else if (unit.type == UnitType::AATurret) {
                            FireProjectile(ProjectileType::AAFlak, unit.team,
                                unit.position + glm::vec3(0.0f, 0.0f, 3.0f), targetAim, 30.0f * dmgMult);
                            unit.attackCooldown = 1.4f;
                        } else if (unit.type == UnitType::CommandMech) {
                            glm::vec3 muzzle = unit.position + glm::vec3(0.0f, 0.0f, 2.5f);
                            FireProjectile(ProjectileType::TankShell, unit.team, muzzle, targetAim, 60.0f * dmgMult);
                            SpawnExplosionCluster(muzzle, 0.6f, 6);
                            if (audioServer) audioServer->PlayOneShot("assets/audio/cannon.wav", 1.0f);
                            unit.attackCooldown = 2.5f;
                        }
                    }
                } else {
                    if (unit.inCombat) {
                        unit.inCombat = false;
                    }
                }
            }

            ++uIdx;
        }
        UpdateConstruction(deltaTime, audioServer);


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

                    float dx = uA.position.x - uB.position.x;
                    float dy = uA.position.y - uB.position.y;
                    float distSq = dx * dx + dy * dy;

                    float rA = GetUnitRadius(uA.type);
                    float rB = GetUnitRadius(uB.type);
                    float minSep = rA + rB;

                    if (airA && airB) {
                        float yawRadA = glm::radians(uA.entity ? uA.entity->angles.z : 0.0f);
                        glm::vec2 forwardA(cos(yawRadA), sin(yawRadA));
                        glm::vec2 wingA(-sin(yawRadA), cos(yawRadA));

                        glm::vec2 relPos(dx, dy);
                        float wingAlign = std::abs(glm::dot(relPos, wingA));
                        float forwardAlign = std::abs(glm::dot(relPos, forwardA));

                        // Wingtip-to-wingtip separation requires at least 7.2m, nose-to-tail 6.0m
                        float wingSep = 7.2f;
                        float fwdSep = 6.0f;
                        float reqSep = std::max(minSep, (wingAlign > forwardAlign) ? wingSep : fwdSep);
                        minSep = reqSep;
                    }

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
                            uB.position.x = std::clamp(uB.position.x, -480.0f, 480.0f);
                            uB.position.y = std::clamp(uB.position.y, -480.0f, 480.0f);
                            if (uB.entity) {
                                uB.entity->origin.x = uB.position.x;
                                uB.entity->origin.y = uB.position.y;
                            }
                        } else if (!staticA && staticB) {
                            // Structure B is static: bump movable unit A away
                            uA.position.x += normal.x * overlap;
                            uA.position.y += normal.y * overlap;
                            uA.position.x = std::clamp(uA.position.x, -480.0f, 480.0f);
                            uA.position.y = std::clamp(uA.position.y, -480.0f, 480.0f);
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

                            uA.position.x = std::clamp(uA.position.x, -480.0f, 480.0f);
                            uA.position.y = std::clamp(uA.position.y, -480.0f, 480.0f);
                            uB.position.x = std::clamp(uB.position.x, -480.0f, 480.0f);
                            uB.position.y = std::clamp(uB.position.y, -480.0f, 480.0f);

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

        // 8B. Ground Unit Obstacle Collision against Forest Trees
        for (auto& u : m_units) {
            if (u.health <= 0.0f || IsAir(u.type) || IsStaticBuilding(u.type)) continue;
            for (const auto& t : m_trees) {
                float dx = u.position.x - t.position.x;
                float dy = u.position.y - t.position.y;
                float dSq = dx * dx + dy * dy;
                float minSep = GetUnitRadius(u.type) * 0.5f + 0.8f;
                if (dSq < minSep * minSep && dSq > 0.001f) {
                    float dist = sqrt(dSq);
                    glm::vec2 push(dx / dist, dy / dist);
                    float overlap = minSep - dist;
                    u.position.x += push.x * overlap * 0.5f;
                    u.position.y += push.y * overlap * 0.5f;
                    if (u.entity) {
                        u.entity->origin.x = u.position.x;
                        u.entity->origin.y = u.position.y;
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
        // --- 9B. FACTORY PRODUCTION QUEUES TICK ---
        // Overdrive is a player action; it never accelerates AI production.
        for (auto it = m_factoryQueues.begin(); it != m_factoryQueues.end();) {
            RTSUnit* factoryUnit = nullptr;
            for (auto& u : m_units) {
                if (u.entityIndex == it->factoryEntityIndex && u.health > 0.0f) {
                    factoryUnit = &u;
                    break;
                }
            }

            if (!factoryUnit) {
                it = m_factoryQueues.erase(it);
                continue;
            }

            if (!it->orders.empty()) {
                auto& currentOrder = it->orders.front();
                float facBuildMult = GetFactionDef(factoryUnit->faction).mods.buildSpeedMult;
                const float boost = factoryUnit->team == Team::Player && m_boostTimer > 0.0f ? 2.0f : 1.0f;
                currentOrder.progress += deltaTime * boost * facBuildMult;

                it->sparkTimer += deltaTime;
                if (it->sparkTimer >= 0.75f) {
                    it->sparkTimer = 0.0f;
                    SpawnExplosionCluster(factoryUnit->position + glm::vec3(0.0f, 0.0f, 1.5f), 0.35f, 2);
                }

                if (currentOrder.progress >= currentOrder.buildTime) {
                    float z = (currentOrder.type == UnitType::Plane) ? 12.0f : 0.0f;
                    bool hasBayDoor = (factoryUnit->doorEntity != nullptr && currentOrder.type != UnitType::Plane);
                    float launchDist = hasBayDoor ? (factoryUnit->baseScale.x * 1.18f) : 12.0f;
                    float launchOffset = (factoryUnit->team == Team::Player) ? launchDist : -launchDist;
                    float lateralSpread = hasBayDoor ? ((float)(rand() % 5 - 2) * 0.45f) : (float)(rand() % 14 - 7);
                    glm::vec3 spawnPos = factoryUnit->position + glm::vec3(launchOffset, lateralSpread, z);
                    factoryUnit->doorReleaseTimer = 1.6f;
                    SpawnUnit(scene, physicsServer, currentOrder.type, currentOrder.team, spawnPos);
                    SpawnExplosionCluster(spawnPos, 0.6f, 4);

                    if (currentOrder.team == Team::Player && audioServer) {
                        audioServer->PlayOneShot(currentOrder.type == UnitType::Plane ? "assets/audio/rocket.wav" : "assets/audio/cannon.wav", 0.75f);
                    }

                    it->orders.erase(it->orders.begin());
                }
            }

            ++it;
        }

        // --- 9C. LAND FACILITY DOOR ANIMATION (TIMED WITH UNIT BUILDING & RELEASING) ---
        for (auto& u : m_units) {
            if (!u.doorEntity || u.health <= 0.0f) continue;

            if (u.doorReleaseTimer > 0.0f) {
                u.doorReleaseTimer = std::max(0.0f, u.doorReleaseTimer - deltaTime);
            }

            bool isBuildingUnit = false;
            bool isReleasingUnit = (u.doorReleaseTimer > 0.0f);

            for (const auto& fq : m_factoryQueues) {
                if (fq.factoryEntityIndex == u.entityIndex && !fq.orders.empty()) {
                    isBuildingUnit = true;
                    const auto& ord = fq.orders.front();
                    float releaseLead = std::min(1.5f, ord.buildTime * 0.22f);
                    if (ord.progress >= ord.buildTime - releaseLead) {
                        isReleasingUnit = true;
                    }
                    break;
                }
            }

            // Close (raise door from under map) while actively building; open (retract under map) to release unit!
            float targetClose = (isBuildingUnit && !isReleasingUnit) ? 1.0f : 0.0f;
            float doorAnimSpeed = 1.85f * deltaTime;
            if (u.doorCloseProgress < targetClose) {
                u.doorCloseProgress = std::min(targetClose, u.doorCloseProgress + doorAnimSpeed);
            } else if (u.doorCloseProgress > targetClose) {
                u.doorCloseProgress = std::max(targetClose, u.doorCloseProgress - doorAnimSpeed);
            }

            float smoothClose = u.doorCloseProgress * u.doorCloseProgress * (3.0f - 2.0f * u.doorCloseProgress);
            // In model space, door Z is [-1.0093, -0.0093]. At smoothClose=0 it sits below ground; at smoothClose=1 it rises +1.0093*scale.z to seal the bay.
            float closedLiftZ = 1.0093f * u.baseScale.z;
            float retractedSinkZ = -0.15f;
            float doorWorldZ = u.position.z + glm::mix(retractedSinkZ, closedLiftZ, smoothClose);

            u.doorEntity->origin = glm::dvec3(u.position.x, u.position.y, doorWorldZ);
            if (u.entity) {
                u.doorEntity->angles = u.entity->angles;
                u.doorEntity->scale = u.entity->scale;
                u.doorEntity->alpha = u.entity->alpha;
                u.doorEntity->emission = u.entity->emission * 0.75f;
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
        if (m_boostTimer > 0.0f) {
            gameMessage = "Mass Overdrive is already active.";
        } else if (playerMass >= 1000.0f) {
            playerMass -= 1000.0f;
            m_boostTimer = 90.0f;
            gameMessage = "Mass Overdrive activated for 90 seconds: 2x mass income and player build speed.";
        } else {
            gameMessage = "Need 1000 Mass to activate Overdrive.";
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

        glm::vec3 hit = m_cameraTarget;
        if (std::abs(rayDir.z) > 1e-5f) {
            float tFlat = -camPos.z / rayDir.z;
            if (tFlat > 0.0f) {
                hit = camPos + rayDir * tFlat;
            }
        }

        // Raymarch against heightfield to intersect actual 3D mountains & valleys
        if (!m_heightmap.empty()) {
            float t = 5.0f;
            for (int step = 0; step < 28; ++step) {
                glm::vec3 p = camPos + rayDir * t;
                float h = GetTerrainHeight(p.x, p.y);
                if (p.z <= h) {
                    float tMin = std::max(0.0f, t - 35.0f);
                    float tMax = t;
                    for (int b = 0; b < 6; ++b) {
                        float tMid = (tMin + tMax) * 0.5f;
                        glm::vec3 pMid = camPos + rayDir * tMid;
                        if (pMid.z <= GetTerrainHeight(pMid.x, pMid.y)) tMax = tMid;
                        else tMin = tMid;
                    }
                    hit = camPos + rayDir * tMax;
                    break;
                }
                t += std::max(4.0f, (p.z - h) * 0.75f);
                if (t > 1800.0f) break;
            }
        }

        hit.x = std::clamp(hit.x, -480.0f, 480.0f);
        hit.y = std::clamp(hit.y, -480.0f, 480.0f);
        hit.z = GetTerrainHeight(hit.x, hit.y);
        return hit;
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
            float minWorldDist = 7.5f;
            for (auto& unit : m_units) {
                if (unit.team != Team::Player) continue;
                float d = glm::distance(glm::vec2(groundClick.x, groundClick.y), glm::vec2(unit.position.x, unit.position.y));
                float maxR = (unit.type == UnitType::Factory || unit.type == UnitType::LandFactory || unit.type == UnitType::AirFactory) ? 7.0f : ((unit.type == UnitType::Plane) ? 5.0f : 4.0f);
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
            if (unit.team != Team::Player || IsStaticBuilding(unit.type)) continue;
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
        if (m_noRushTimer > 0.0f && targetPos.x > -15.0f) {
            targetPos.x = -15.0f;
            gameMessage = "Cannot cross midfield boundary during No Rush period!";
        }
        std::vector<RTSUnit*> selectedUnits;
        for (auto& unit : m_units) {
            if (unit.selected && unit.team == Team::Player && !IsStaticBuilding(unit.type)) {
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
            if (selectedUnits[i]->type == UnitType::Engineer || selectedUnits[i]->type == UnitType::CommandMech)
                CancelConstruction(selectedUnits[i]->entityIndex);

            selectedUnits[i]->targetPosition = targetPos + offset;
            selectedUnits[i]->hasTarget = true;
        }
        gameMessage = "Move order issued to " + std::to_string(num) + " units.";
    }

    bool RTSController::IsScreenPosOverHUD(glm::vec2 pos) const {
        // Build Panel at bottom-left
        if (pos.x >= m_viewportPos.x && pos.x <= m_viewportPos.x + 350.0f &&
            pos.y >= m_viewportPos.y + m_viewportSize.y - 530.0f && pos.y <= m_viewportPos.y + m_viewportSize.y) {
            return true;
        }
        // Selected Units Bar at bottom-center
        float barCenter = m_viewportPos.x + m_viewportSize.x * 0.5f;
        if (pos.x >= barCenter - 240.0f && pos.x <= barCenter + 240.0f &&
            pos.y >= m_viewportPos.y + m_viewportSize.y - 120.0f && pos.y <= m_viewportPos.y + m_viewportSize.y) {
            return true;
        }
        // Tactical Minimap at top-right
        if (pos.x >= m_viewportPos.x + m_viewportSize.x - 360.0f && pos.x <= m_viewportPos.x + m_viewportSize.x &&
            pos.y >= m_viewportPos.y && pos.y <= m_viewportPos.y + 360.0f) {
            return true;
        }
        // Top HUD Status bar at top-left
        if (pos.x >= m_viewportPos.x && pos.x <= m_viewportPos.x + 455.0f &&
            pos.y >= m_viewportPos.y && pos.y <= m_viewportPos.y + 150.0f) {
            return true;
        }
        return false;
    }

    void RTSController::DrawViewportOverlays(ImVec2 viewportPos, ImVec2 viewportSize, ImDrawList* customDrawList) {
        if (!m_inMatch && !m_inForgeMode) return;
        m_viewportSize = viewportSize;
        m_overlaysDrawnThisFrame = true;

        ImDrawList* drawList = customDrawList ? customDrawList : ImGui::GetForegroundDrawList();
        if (!customDrawList) drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);
        // 0. Forge Mode Brush Cursor
        if (m_inForgeMode && m_activeCam) {
            ImVec2 mPos = ImGui::GetMousePos();
            glm::vec3 cursorWorld = ScreenToGround(m_activeCam, glm::vec2(mPos.x, mPos.y));
            glm::vec2 cursorScreen;
            if (WorldToScreen(m_activeCam, cursorWorld, cursorScreen)) {
                if (m_forgeToolMode == 0) {
                    float radPx = (m_forgeBrushRadius / 1000.0f) * m_viewportSize.y * (m_cameraZoom / 55.0f);
                    radPx = std::clamp(radPx, 12.0f, 320.0f);
                    drawList->AddCircle(ImVec2(cursorScreen.x, cursorScreen.y), radPx, IM_COL32(0, 240, 255, 230), 36, 2.0f);
                    drawList->AddCircleFilled(ImVec2(cursorScreen.x, cursorScreen.y), 4.0f, IM_COL32(0, 255, 255, 255));
                } else if (m_forgeToolMode == 1) {
                    // Mass Node stamp preview (Green)
                    drawList->AddCircleFilled(ImVec2(cursorScreen.x, cursorScreen.y), 14.0f, IM_COL32(30, 240, 70, 160));
                    drawList->AddCircle(ImVec2(cursorScreen.x, cursorScreen.y), 14.0f, IM_COL32(50, 255, 100, 255), 20, 2.0f);
                    drawList->AddText(ImVec2(cursorScreen.x - 14.0f, cursorScreen.y - 28.0f), IM_COL32(50, 255, 100, 255), "MASS (+15M)");
                } else if (m_forgeToolMode == 2) {
                    // Power Node stamp preview (Yellow)
                    drawList->AddCircleFilled(ImVec2(cursorScreen.x, cursorScreen.y), 14.0f, IM_COL32(250, 220, 30, 160));
                    drawList->AddCircle(ImVec2(cursorScreen.x, cursorScreen.y), 14.0f, IM_COL32(255, 240, 60, 255), 20, 2.0f);
                    drawList->AddText(ImVec2(cursorScreen.x - 16.0f, cursorScreen.y - 28.0f), IM_COL32(255, 240, 60, 255), "POWER (+15E)");
                } else if (m_forgeToolMode == 3) {
                    // Delete crosshair
                    drawList->AddLine(ImVec2(cursorScreen.x - 10, cursorScreen.y - 10), ImVec2(cursorScreen.x + 10, cursorScreen.y + 10), IM_COL32(255, 50, 40, 255), 2.5f);
                    drawList->AddLine(ImVec2(cursorScreen.x - 10, cursorScreen.y + 10), ImVec2(cursorScreen.x + 10, cursorScreen.y - 10), IM_COL32(255, 50, 40, 255), 2.5f);
                } else if (m_forgeToolMode == 4) {
                    // Tree stamp preview (Reddish/Orange Planet Foliage)
                    drawList->AddCircleFilled(ImVec2(cursorScreen.x, cursorScreen.y), 13.0f, IM_COL32(230, 88, 30, 175));
                    drawList->AddCircle(ImVec2(cursorScreen.x, cursorScreen.y), 13.0f, IM_COL32(255, 130, 50, 255), 18, 2.0f);
                    drawList->AddText(ImVec2(cursorScreen.x - 14.0f, cursorScreen.y - 28.0f), IM_COL32(255, 145, 70, 255), "TREE");
                }
            }
        }


        // 1. Drag Selection Box
        if (m_isDraggingBox) {
            drawList->AddRect(
                ImVec2(m_dragStartScreen.x, m_dragStartScreen.y),
                ImVec2(m_dragEndScreen.x, m_dragEndScreen.y),
                IM_COL32(0, 255, 100, 200), 0.0f, 0, 2.0f
            );
        }

        // 2. Line Formation Preview
        if (m_isDraggingFormation && m_activeCam) {
            float dragScreenDist = glm::distance(m_formationStartScreen, m_formationEndScreen);
            std::vector<const RTSUnit*> selectedUnits;
            for (const auto& u : m_units) {
                if (u.selected && u.team == Team::Player && !IsStaticBuilding(u.type) && u.health > 0.0f) {
                    selectedUnits.push_back(&u);
                }
            }

            if (dragScreenDist > 18.0f && selectedUnits.size() > 1) {
                drawList->AddLine(
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
                        drawList->AddCircle(
                            ImVec2(slotScreen.x, slotScreen.y),
                            14.0f,
                            IM_COL32(0, 255, 200, 220),
                            16,
                            2.0f
                        );
                        drawList->AddCircleFilled(
                            ImVec2(slotScreen.x, slotScreen.y),
                            4.0f,
                            IM_COL32(0, 255, 240, 255)
                        );
                    }
                }
            }
        }

        // 3. Selection Hulls and Floating Health Bars
        if (m_activeCam) {
            DrawGroupSelectionHulls(m_activeCam, drawList);
            DrawUnitHealthBars(m_activeCam, drawList);
        }

        // 4. Debug Telemetry View (F3)
        if (m_showDebugTelemetry && m_activeCam) {
            for (const auto& u : m_units) {
                if (u.health <= 0.0f) continue;
                glm::vec2 uScreen;
                if (!WorldToScreen(m_activeCam, u.position + glm::vec3(0.0f, 0.0f, 0.6f), uScreen)) continue;

                // Movement destination vector
                if (u.hasTarget) {
                    glm::vec2 targetScreen;
                    if (WorldToScreen(m_activeCam, u.targetPosition, targetScreen)) {
                        ImU32 vecCol = (u.team == Team::Player) ? IM_COL32(0, 255, 200, 200) : IM_COL32(255, 175, 25, 200);
                        drawList->AddLine(ImVec2(uScreen.x, uScreen.y), ImVec2(targetScreen.x, targetScreen.y), vecCol, 1.5f);
                        drawList->AddCircleFilled(ImVec2(targetScreen.x, targetScreen.y), 3.5f, vecCol, 8);
                    }
                }

                // Combat engagement raycast
                if (u.inCombat) {
                    float bestD = 9999.0f;
                    const RTSUnit* combatTarget = nullptr;
                    for (const auto& other : m_units) {
                        if (other.team != u.team && other.health > 0.0f) {
                            float d = glm::distance(u.position, other.position);
                            if (d < bestD) {
                                bestD = d;
                                combatTarget = &other;
                            }
                        }
                    }
                    if (combatTarget) {
                        glm::vec2 tgtScreen;
                        if (WorldToScreen(m_activeCam, combatTarget->position + glm::vec3(0.0f, 0.0f, 0.6f), tgtScreen)) {
                            drawList->AddLine(ImVec2(uScreen.x, uScreen.y), ImVec2(tgtScreen.x, tgtScreen.y), IM_COL32(255, 45, 30, 240), 2.0f);
                            drawList->AddCircle(ImVec2(tgtScreen.x, tgtScreen.y), 6.0f, IM_COL32(255, 60, 40, 255), 12, 1.5f);
                        }
                    }
                }

                // Vision radius wireframe (Screen projected)
                float visR = (u.type == UnitType::Plane) ? 95.0f : ((u.type == UnitType::Turret || u.type == UnitType::CommandMech) ? 70.0f : 55.0f);
                glm::vec2 visEdgeScreen;
                if (WorldToScreen(m_activeCam, u.position + glm::vec3(visR, 0.0f, 0.0f), visEdgeScreen)) {
                    float rPx = glm::distance(uScreen, visEdgeScreen);
                    ImU32 visCol = (u.team == Team::Player) ? IM_COL32(40, 220, 100, 70) : IM_COL32(240, 80, 50, 60);
                    drawList->AddCircle(ImVec2(uScreen.x, uScreen.y), rPx, visCol, 24, 1.0f);
                }

                // Shield dome perimeter
                if (u.isShieldActive && u.shieldHealth > 0.0f) {
                    glm::vec2 shieldEdge;
                    if (WorldToScreen(m_activeCam, u.position + glm::vec3(u.shieldRadius, 0.0f, 0.0f), shieldEdge)) {
                        float rPx = glm::distance(uScreen, shieldEdge);
                        drawList->AddCircle(ImVec2(uScreen.x, uScreen.y), rPx, IM_COL32(0, 220, 255, 180), 32, 2.0f);
                    }
                }
            }

            // Telemetry Status Window
            ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 15.0f, viewportPos.y + 140.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(430.0f, 440.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("RTS Telemetry & Raycasts (F3)", nullptr, ImGuiWindowFlags_None);
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "=== TELEMETRY ACTIVE ===");
            ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
            ImGui::Text("Match Time: %.1fs | Status: %s", m_matchTime, gameOver ? "GAME OVER" : (m_inMatch ? "RUNNING" : "MENU"));
            
            int pCount = 0, eCount = 0, combatUnits = 0;
            for (const auto& u : m_units) {
                if (u.health > 0.0f) {
                    if (u.team == Team::Player) pCount++; else eCount++;
                    if (u.inCombat) combatUnits++;
                }
            }
            ImGui::Text("Units: %d Player | %d Enemy (Combat: %d)", pCount, eCount, combatUnits);
            ImGui::Text("Active Projectiles: %d | Explosions: %d", (int)m_projectiles.size(), (int)m_explosionSpheres.size());
            ImGui::Text("Trees on Terrain: %d (%d Instanced Batches)", (int)m_trees.size(), (int)m_treeBatchIndices.size());
            ImGui::Text("Water Plane: %s (Elevation: %.1fm)", m_enableWater ? "ENABLED" : "DISABLED", m_waterHeight);
            const auto& pFac = GetTeamFactionDef(Team::Player);
            const auto& eFac = GetTeamFactionDef(Team::Enemy);
            ImGui::Text("Factions: [%s] vs [%s]", pFac.shortCode, eFac.shortCode);
            const char* pName = (m_skirmishSettings.aiPersonality == 1) ? "Aggressive" : ((m_skirmishSettings.aiPersonality == 2) ? "Defensive" : "Balanced");
            int clearCount = m_scene ? (int)m_scene->environment.fogClearSources.size() : 0;
            ImGui::Text("AI Mode: %s (Diff: %d)", pName, m_skirmishSettings.aiDifficulty);
            const auto& decision = m_aiPlanner.Decision();
            ImGui::Text("Phase: %s | Tactic: %s", RTSAIPlanner::PhaseName(decision.phase), RTSAIPlanner::TacticName(decision.tactic));
            ImGui::TextWrapped("%s", decision.reason);
            ImGui::Text("Income M/E: %.1f / %.1f per second", m_aiWorld.massIncome, m_aiWorld.energyIncome);
            ImGui::Text("Reserve: %.1f / %.1f power | Assembly: %.0f%%",
                m_aiWorld.reservePower, m_aiWorld.armyPower, m_aiWorld.stagedFraction * 100.0f);
            ImGui::Text("Assault readiness: %.0f%% | Intel age: %.0fs",
                decision.assaultReadiness * 100.0f, m_aiWorld.intelAge);
            ImGui::Text("Army: %d/%d tanks, %d/%d air | Defense: %.1f",
                m_aiWorld.tanks, decision.desiredTanks, m_aiWorld.planes, decision.desiredPlanes, m_aiWorld.homeDefenseScore);
            ImGui::Text("Defense interrupt: %s", decision.emergencyDefense ? "ACTIVE" : "clear");
            ImGui::Text("Fog of War: %s (%d Vision Sources)", m_skirmishSettings.enableFogOfWar ? "ACTIVE" : "OFF", clearCount);
            ImGui::End();
        }

        if (!customDrawList) drawList->PopClipRect();
    }

    void RTSController::DrawHUD(ImVec2 viewportPos, ImVec2 viewportSize) {
        m_viewportPos = viewportPos;
        m_viewportSize = viewportSize;
        if (m_inForgeMode) {
            DrawForgeUI(viewportPos, viewportSize);
            return;
        }

        // If not in match (in Main Menu), display only Main Menu & Settings
        if (!m_inMatch) {
            if (m_showSettingsMenu) {
                DrawSettingsMenu(viewportPos, viewportSize);
            }
            DrawSkirmishMenu(viewportPos, viewportSize);
            return;
        }

        // If overlays were not already rendered directly inside the Viewport window, fallback to rendering them here
        if (!m_overlaysDrawnThisFrame) {
            DrawViewportOverlays(viewportPos, viewportSize, ImGui::GetForegroundDrawList());
        }
        m_overlaysDrawnThisFrame = false; // Reset flag for next frame
        // Persistent match clock centered at the top of the game viewport.
        const int elapsedSeconds = std::max(0, static_cast<int>(m_matchTime));
        const int elapsedHours = elapsedSeconds / 3600;
        const int elapsedMinutes = (elapsedSeconds / 60) % 60;
        const int elapsedRemainder = elapsedSeconds % 60;
        char matchClock[16];
        snprintf(matchClock, sizeof(matchClock), "%02d:%02d:%02d", elapsedHours, elapsedMinutes, elapsedRemainder);
        constexpr float clockWidth = 150.0f;
        ImGui::SetNextWindowPos(
            ImVec2(viewportPos.x + (viewportSize.x - clockWidth) * 0.5f, viewportPos.y + 15.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(clockWidth, 58.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::Begin("##RTSMatchClock", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoSavedSettings);
        ImVec2 labelSize = ImGui::CalcTextSize("MATCH TIME");
        ImGui::SetCursorPosX((clockWidth - labelSize.x) * 0.5f);
        ImGui::TextDisabled("MATCH TIME");
        ImGui::SetWindowFontScale(1.25f);
        ImVec2 clockSize = ImGui::CalcTextSize(matchClock);
        ImGui::SetCursorPosX((clockWidth - clockSize.x) * 0.5f);
        ImGui::TextUnformatted(matchClock);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();


        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 15, viewportPos.y + 15), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(440, 136), ImGuiCond_Always);
        ImGui::Begin("Rude RTS HUD", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        const auto& hudPlayerFac = GetTeamFactionDef(Team::Player);
        const auto& hudEnemyFac = GetTeamFactionDef(Team::Enemy);
        glm::vec3 hudEnemyRgb = GetTeamDisplayColor(Team::Enemy, false);
        ImGui::TextColored(hudPlayerFac.uiColor, "[%s] %s", hudPlayerFac.shortCode, hudPlayerFac.name);
        ImGui::SameLine();
        ImGui::TextDisabled("vs");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(hudEnemyRgb.r, hudEnemyRgb.g, hudEnemyRgb.b, 1.0f), "[%s] %s", hudEnemyFac.shortCode, hudEnemyFac.name);

        float pMassCap = 1000.0f;
        float pEnergyCap = 5000.0f;
        for (const auto& u : m_units) {
            if (u.team == Team::Player && u.health > 0.0f) {
                if (u.type == UnitType::MassStorage) pMassCap += 5000.0f;
                if (u.type == UnitType::EnergyStorage) pEnergyCap += 15000.0f;
            }
        }
        ImGui::Text("Mass: %.0f/%.0f | Energy: %.0f/%.0f", playerMass, pMassCap, playerEnergy, pEnergyCap);
        ImGui::SameLine(285);
        if (ImGui::Button(m_showDebugTelemetry ? "[F3: ON]" : "[F3: OFF]", ImVec2(75, 22))) {
            m_showDebugTelemetry = !m_showDebugTelemetry;
        }
        ImGui::SameLine(365);
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
            ImGui::Text("Overdrive: Inactive ($1000M -> 90s Boost)");
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
            else if (singleSelectedUnit->type == UnitType::ShieldGen) typeName = "Shield Generator (Force-Field Dome)";
            if (singleSelectedUnit->maxShieldHealth > 0.0f) {
                ImGui::Text("Selected: %s | Hull: %.0f/%.0f | Shield: %.0f/%.0f", typeName, singleSelectedUnit->health, singleSelectedUnit->maxHealth, singleSelectedUnit->shieldHealth, singleSelectedUnit->maxShieldHealth);
            } else {
                ImGui::Text("Selected: %s | Health: %.0f / %.0f", typeName, singleSelectedUnit->health, singleSelectedUnit->maxHealth);
            }
        } else {
            ImGui::Text("Player Units: %d (Selected: %d)", totalPlayerUnits, selectedCount);
        }
        ImGui::End();

        // In-world overlays were drawn in ##ViewportOverlayLayer above

        // Draw Tactical Graphical Build & Command Menu (FAF Production Grid)
        DrawBuildPanel(viewportPos, viewportSize);
        // Draw Selected Units Command Bar
        DrawSelectedUnitsBar(viewportPos, viewportSize);
        // Draw Tactical Minimap (FAF Topdown Radar) in Top-Right
        DrawMinimap(viewportPos, viewportSize);
        if (gameOver) {
            DrawGameOver(viewportPos, viewportSize);
        }
        if (m_showSettingsMenu) {
            DrawSettingsMenu(viewportPos, viewportSize);
        }
        if (m_showSkirmishMenu) {
            DrawSkirmishMenu(viewportPos, viewportSize);
        }
    }
    void RTSController::DrawBuildPanel(ImVec2 viewportPos, ImVec2 viewportSize) {
        float panelW = 325.0f;
        float panelH = 515.0f;
        ImVec2 panelPos(viewportPos.x + 16.0f, viewportPos.y + viewportSize.y - panelH - 16.0f);

        ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
        ImGui::Begin("TacticalBuildPanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        // 1. Texture Loading
        ImTextureID buildPanelTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/build_panel_body.png") : nullptr);
        if (!buildPanelTex && m_renderer) {
            buildPanelTex = (ImTextureID)(uintptr_t)m_renderer->getImGuiTextureID("machinevision/build_panel_body.png");
        }

        ImTextureID powerTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/true_power.png") : nullptr);
        ImTextureID wrenchTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/wrench.png") : nullptr);
        ImTextureID nukeTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/nuke.png") : nullptr);
        ImTextureID clockTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/clock.png") : nullptr);
        ImTextureID circleArrowTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/circle_arrow.png") : nullptr);
        // 2. Render Panel Bezel Frame
        if (buildPanelTex) {
            // Tight UV crop to panel bounds inside PNG (2048x2048)
            ImVec2 uv0(0.1846f, 0.0298f);
            ImVec2 uv1(0.7822f, 0.9761f);
            drawList->AddImage(buildPanelTex, panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH), uv0, uv1);
        } else {
            // Fallback military panel
            drawList->AddRectFilled(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH), IM_COL32(20, 26, 36, 245), 6.0f);
            drawList->AddRect(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH), IM_COL32(50, 80, 115, 230), 6.0f, 0, 2.0f);
        }

        // 3. Normalized Column and Row boundaries of the 12 black boxes
        const float colX0[3] = { 0.1838f, 0.4142f, 0.6446f };
        const float colX1[3] = { 0.4044f, 0.6356f, 0.8660f };
        const float rowY0[4] = { 0.2054f, 0.3509f, 0.4964f, 0.6419f };
        const float rowY1[4] = { 0.3452f, 0.4907f, 0.6362f, 0.7817f };

        // 4. Normalized Top Tab boundaries
        const float tabX0[4] = { 0.1814f, 0.3595f, 0.5498f, 0.7377f };
        const float tabX1[4] = { 0.3039f, 0.4820f, 0.6724f, 0.8603f };
        const float tabY0 = 0.0315f;
        const float tabY1 = 0.1357f;

        ImVec2 mousePos = ImGui::GetMousePos();
        bool isLeftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        // Draw and interact with 4 top category tabs
        const char* tabLabels[4] = { "ALL", "UNIT", "BASE", "ECON" };
        for (int t = 0; t < 4; ++t) {
            ImVec2 tMin(panelPos.x + tabX0[t] * panelW, panelPos.y + tabY0 * panelH);
            ImVec2 tMax(panelPos.x + tabX1[t] * panelW, panelPos.y + tabY1 * panelH);

            bool isTabHovered = (mousePos.x >= tMin.x && mousePos.x <= tMax.x && mousePos.y >= tMin.y && mousePos.y <= tMax.y);
            if (isTabHovered && isLeftClicked) {
                m_buildPanelTab = t;
            }

            bool isActiveTab = (m_buildPanelTab == t);
            if (isActiveTab) {
                drawList->AddRectFilled(tMin, tMax, IM_COL32(35, 110, 160, 200), 2.0f);
                drawList->AddRect(tMin, tMax, IM_COL32(0, 240, 255, 255), 2.0f, 0, 1.5f);
            } else if (isTabHovered) {
                drawList->AddRectFilled(tMin, tMax, IM_COL32(40, 60, 80, 150), 2.0f);
                drawList->AddRect(tMin, tMax, IM_COL32(100, 180, 220, 200), 2.0f);
            }

            float tabFontSize = 12.0f;
            ImVec2 textSize = font ? font->CalcTextSizeA(tabFontSize, FLT_MAX, -1.0f, tabLabels[t]) : ImGui::CalcTextSize(tabLabels[t]);
            ImVec2 textPos(tMin.x + (tMax.x - tMin.x - textSize.x) * 0.5f, tMin.y + (tMax.y - tMin.y - textSize.y) * 0.5f);
            if (font) {
                drawList->AddText(font, tabFontSize, textPos, isActiveTab ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 190, 215, 220), tabLabels[t]);
            } else {
                drawList->AddText(textPos, isActiveTab ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 190, 215, 220), tabLabels[t]);
            }
        }

        // 5. Build Item Definitions based on current selection
        bool isEngineerOrMechSelected = false;
        bool isLandFactorySelected = false;
        bool isAirFactorySelected = false;
        for (const auto& u : m_units) {
            if (u.selected && u.team == Team::Player && u.health > 0.0f) {
                if (u.type == UnitType::Engineer || u.type == UnitType::CommandMech) isEngineerOrMechSelected = true;
                else if (u.type == UnitType::LandFactory) isLandFactorySelected = true;
                else if (u.type == UnitType::AirFactory) isAirFactorySelected = true;
            }
        }

        struct BuildSlotItem {
            std::string name;
            std::string costText;
            int massCost = 0;
            int energyCost = 0;
            UnitType unitType = UnitType::Tank;
            bool isBuilding = false;
            bool isBoost = false;
            int category = 0; // 1=UNITS, 2=BASE, 3=ECON
            std::string tooltip;
            ImTextureID icon = (ImTextureID)0;
            ImU32 glyphColor = IM_COL32(255, 255, 255, 255);
        };

        FactionId playerFacId = GetTeamFaction(Team::Player);
        std::vector<BuildSlotItem> availableItems;
        auto addSlot = [&](const char* name, UnitType ut, bool isBuilding, bool isBoost, int cat, const char* tip, ImTextureID icon, ImU32 glyphCol) {
            int mC = 1000, eC = 0;
            float bT = 10.0f;
            char costBuf[32];
            if (isBoost) {
                snprintf(costBuf, sizeof(costBuf), "$1000M");
            } else {
                GetUnitBuildCost(ut, playerFacId, mC, eC, bT);
                snprintf(costBuf, sizeof(costBuf), "$%dM/$%dE", mC, eC);
            }
            availableItems.push_back({ name, costBuf, mC, eC, ut, isBuilding, isBoost, cat, tip, icon, glyphCol });
        };

        if (isEngineerOrMechSelected) {
            addSlot("LAND FACT", UnitType::LandFactory, true, false, 2, "Construct Land Factory (Produces Tanks & Engineers)", wrenchTex, IM_COL32(65, 155, 230, 255));
            addSlot("AIR FACT", UnitType::AirFactory, true, false, 2, "Construct Air Factory (Produces Combat Planes)", wrenchTex, IM_COL32(65, 215, 230, 255));
            addSlot("POINT DEF", UnitType::Turret, true, false, 2, "Deploy Point Defense Ground Cannon for base defense", nukeTex, IM_COL32(245, 160, 40, 255));
            addSlot("AA DEFENSE", UnitType::AATurret, true, false, 2, "Deploy Anti-Air Point Defense Turret (Anti-Air Flak)", nukeTex, IM_COL32(0, 220, 255, 255));
            addSlot("SHIELD GEN", UnitType::ShieldGen, true, false, 2, "Deploy Mobile Force-Field Shield Generator (38m Dome & Deflection)", circleArrowTex, IM_COL32(30, 200, 255, 255));
            addSlot("MASS STORE", UnitType::MassStorage, true, false, 3, "Construct Mass Storage Silo (+5000 Mass Capacity)", (ImTextureID)0, IM_COL32(180, 70, 240, 255));
            addSlot("POWER STORE", UnitType::EnergyStorage, true, false, 3, "Construct Energy Storage Bank (+15000 Energy Capacity)", powerTex, IM_COL32(250, 230, 40, 255));
            addSlot("MASS GEN", UnitType::MassGen, true, false, 3, "Construct Mass Generator (Income on green deposit)", (ImTextureID)0, IM_COL32(200, 70, 240, 255));
            addSlot("POWER GEN", UnitType::EnergyGen, true, false, 3, "Construct Energy Generator (Income on yellow deposit)", powerTex, IM_COL32(250, 230, 40, 255));
            addSlot("NUKE SILO", UnitType::NukeSilo, true, false, 2, "Construct Strategic Missile Nuke Silo", nukeTex, IM_COL32(255, 50, 30, 255));
            addSlot("SUPPORT MECH", UnitType::SupportMech, false, false, 1, "Summon T3 Combat Engineer Support Mech", (ImTextureID)0, IM_COL32(70, 210, 255, 255));
            addSlot("OVERDRIVE", UnitType::CommandMech, false, true, 3, "90-second Mass Overdrive (2x player mass income and build speed)", clockTex, IM_COL32(255, 190, 30, 255));
        } else if (isLandFactorySelected) {
            addSlot("TANK", UnitType::Tank, false, false, 1, "Produce heavy battle tank with cannon", (ImTextureID)0, IM_COL32(60, 160, 255, 255));
            addSlot("ENGINEER", UnitType::Engineer, false, false, 1, "Produce field combat engineer for construction and repair", wrenchTex, IM_COL32(255, 255, 255, 255));
            addSlot("SUPPORT MECH", UnitType::SupportMech, false, false, 1, "Produce T3 Support Combat Engineer Mech", (ImTextureID)0, IM_COL32(70, 210, 255, 255));
            addSlot("POINT DEF", UnitType::Turret, true, false, 2, "Deploy Point Defense Tower for base security", nukeTex, IM_COL32(245, 160, 40, 255));
            addSlot("AA DEFENSE", UnitType::AATurret, true, false, 2, "Deploy Anti-Air Point Defense Turret", nukeTex, IM_COL32(0, 220, 255, 255));
            addSlot("SHIELD GEN", UnitType::ShieldGen, true, false, 2, "Deploy Mobile Force-Field Shield Generator (38m Dome & Deflection)", circleArrowTex, IM_COL32(30, 200, 255, 255));
        } else if (isAirFactorySelected) {
            addSlot("AIRPLANE", UnitType::Plane, false, false, 1, "Produce high-speed rocket fighter airplane", (ImTextureID)0, IM_COL32(80, 220, 255, 255));
            addSlot("AA DEFENSE", UnitType::AATurret, true, false, 2, "Deploy Anti-Air Defense Turret", nukeTex, IM_COL32(0, 220, 255, 255));
            addSlot("SHIELD GEN", UnitType::ShieldGen, true, false, 2, "Deploy Force-Field Shield Generator", circleArrowTex, IM_COL32(30, 200, 255, 255));
        } else {
            addSlot("TANK", UnitType::Tank, false, false, 1, "Produce heavy battle tank from Headquarters", (ImTextureID)0, IM_COL32(60, 160, 255, 255));
            addSlot("AIRPLANE", UnitType::Plane, false, false, 1, "Produce rocket fighter airplane from Headquarters", (ImTextureID)0, IM_COL32(80, 220, 255, 255));
            addSlot("ENGINEER", UnitType::Engineer, false, false, 1, "Produce builder engineer from Headquarters", wrenchTex, IM_COL32(255, 255, 255, 255));
            addSlot("LAND FACT", UnitType::LandFactory, true, false, 2, "Place Land Factory on terrain", wrenchTex, IM_COL32(65, 155, 230, 255));
            addSlot("AIR FACT", UnitType::AirFactory, true, false, 2, "Place Air Factory on terrain", wrenchTex, IM_COL32(65, 215, 230, 255));
            addSlot("POINT DEF", UnitType::Turret, true, false, 2, "Deploy Point Defense Tower on terrain", nukeTex, IM_COL32(245, 160, 40, 255));
            addSlot("AA DEFENSE", UnitType::AATurret, true, false, 2, "Deploy Anti-Air Point Defense Turret", nukeTex, IM_COL32(0, 220, 255, 255));
            addSlot("SHIELD GEN", UnitType::ShieldGen, true, false, 2, "Deploy Force-Field Shield Generator (38m Dome)", circleArrowTex, IM_COL32(30, 200, 255, 255));
            addSlot("MASS STORE", UnitType::MassStorage, true, false, 3, "Construct Mass Storage Silo (+5000 Mass)", (ImTextureID)0, IM_COL32(180, 70, 240, 255));
            addSlot("POWER STORE", UnitType::EnergyStorage, true, false, 3, "Construct Energy Storage Bank (+15000 Energy)", powerTex, IM_COL32(250, 230, 40, 255));
            addSlot("NUKE SILO", UnitType::NukeSilo, true, false, 2, "Construct Strategic Nuke Silo", nukeTex, IM_COL32(255, 50, 30, 255));
            addSlot("OVERDRIVE", UnitType::CommandMech, false, true, 3, "Activate 90-second Mass Overdrive ($1000M)", clockTex, IM_COL32(255, 190, 30, 255));
        }
        std::vector<BuildSlotItem> displayItems;
        for (const auto& item : availableItems) {
            if (m_buildPanelTab == 0 || item.category == m_buildPanelTab) {
                displayItems.push_back(item);
                if (displayItems.size() >= 12) break;
            }
        }

        // 6. Populate the 12 Black Boxes in the 3x4 Grid
        int itemIdx = 0;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 3; ++col) {
                ImVec2 sMin(panelPos.x + colX0[col] * panelW, panelPos.y + rowY0[row] * panelH);
                ImVec2 sMax(panelPos.x + colX1[col] * panelW, panelPos.y + rowY1[row] * panelH);

                if (itemIdx < (int)displayItems.size()) {
                    const auto& item = displayItems[itemIdx];
                    bool hasResources = (playerMass >= (float)item.massCost && playerEnergy >= (float)item.energyCost);
                    bool isHovered = (mousePos.x >= sMin.x && mousePos.x <= sMax.x && mousePos.y >= sMin.y && mousePos.y <= sMax.y);

                    if (isHovered) {
                        drawList->AddRectFilled(sMin, sMax, hasResources ? IM_COL32(0, 180, 255, 45) : IM_COL32(220, 50, 50, 45), 2.0f);
                        drawList->AddRect(sMin, sMax, hasResources ? IM_COL32(0, 240, 255, 230) : IM_COL32(240, 80, 80, 230), 2.0f, 0, 1.8f);

                        ImGui::BeginTooltip();
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "%s", item.name.c_str());
                        ImGui::Separator();
                        ImGui::Text("%s", item.tooltip.c_str());
                        ImGui::TextColored(hasResources ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                           "Cost: %s", item.costText.c_str());
                        if (!hasResources) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "[Insufficient Mass or Energy]");
                        }
                        ImGui::EndTooltip();

                        if (isLeftClicked) {
                            if (item.isBoost) {
                                ActivateBoost();
                            } else if (item.isBuilding) {
                                m_isPlacingBuilding = true;
                                m_placingBuildingType = item.unitType;
                                gameMessage = "Click ground to place structure. Right-click cancels.";
                            } else {
                                int selectedFactIdx = -1;
                                for (const auto& u : m_units) {
                                    if (u.selected && u.team == Team::Player && u.health > 0.0f) {
                                        if (item.unitType == UnitType::Plane && (u.type == UnitType::AirFactory || u.type == UnitType::Factory)) {
                                            selectedFactIdx = u.entityIndex;
                                            break;
                                        } else if (item.unitType != UnitType::Plane && (u.type == UnitType::LandFactory || u.type == UnitType::Factory)) {
                                            selectedFactIdx = u.entityIndex;
                                            break;
                                        }
                                    }
                                }
                                QueueFactoryUnit(item.unitType, Team::Player, selectedFactIdx);
                            }
                        }
                    } else {
                        drawList->AddRect(sMin, sMax, hasResources ? IM_COL32(45, 75, 105, 160) : IM_COL32(75, 45, 45, 120), 2.0f);
                    }

                    float boxCenterY = (sMin.y + sMax.y) * 0.5f;
                    float boxCenterX = (sMin.x + sMax.x) * 0.5f;

                    float iconSz = 26.0f;
                    if (item.icon) {
                        drawList->AddImage(item.icon,
                                           ImVec2(boxCenterX - iconSz * 0.5f, boxCenterY - iconSz * 0.5f - 2.0f),
                                           ImVec2(boxCenterX + iconSz * 0.5f, boxCenterY + iconSz * 0.5f - 2.0f),
                                           ImVec2(0, 0), ImVec2(1, 1),
                                           hasResources ? IM_COL32(255, 255, 255, 235) : IM_COL32(160, 160, 160, 140));
                    } else {
                        if (item.unitType == UnitType::Tank) {
                            drawList->AddRectFilled(ImVec2(boxCenterX - 11.0f, boxCenterY - 7.0f), ImVec2(boxCenterX + 11.0f, boxCenterY + 5.0f), item.glyphColor, 1.5f);
                            drawList->AddLine(ImVec2(boxCenterX, boxCenterY - 7.0f), ImVec2(boxCenterX, boxCenterY - 14.0f), item.glyphColor, 2.0f);
                        } else if (item.unitType == UnitType::Plane) {
                            drawList->AddTriangleFilled(ImVec2(boxCenterX, boxCenterY - 12.0f), ImVec2(boxCenterX - 11.0f, boxCenterY + 6.0f), ImVec2(boxCenterX + 11.0f, boxCenterY + 6.0f), item.glyphColor);
                        } else if (item.unitType == UnitType::MassGen) {
                            drawList->AddQuadFilled(ImVec2(boxCenterX, boxCenterY - 12.0f), ImVec2(boxCenterX + 9.0f, boxCenterY - 2.0f), ImVec2(boxCenterX, boxCenterY + 7.0f), ImVec2(boxCenterX - 9.0f, boxCenterY - 2.0f), item.glyphColor);
                        } else if (item.unitType == UnitType::EnergyGen) {
                            drawList->AddRectFilled(ImVec2(boxCenterX - 7.0f, boxCenterY - 11.0f), ImVec2(boxCenterX + 7.0f, boxCenterY + 6.0f), item.glyphColor, 2.0f);
                        } else {
                            drawList->AddRectFilled(ImVec2(boxCenterX - 10.0f, boxCenterY - 8.0f), ImVec2(boxCenterX + 10.0f, boxCenterY + 6.0f), item.glyphColor, 1.0f);
                        }
                    }

                    float nameFontSize = 11.5f;
                    ImVec2 nameSz = font ? font->CalcTextSizeA(nameFontSize, FLT_MAX, -1.0f, item.name.c_str()) : ImGui::CalcTextSize(item.name.c_str());
                    ImVec2 namePos(boxCenterX - nameSz.x * 0.5f, sMin.y + 5.0f);
                    ImU32 nameCol = hasResources ? IM_COL32(230, 240, 255, 240) : IM_COL32(160, 160, 160, 160);
                    if (font) {
                        drawList->AddText(font, nameFontSize, namePos, nameCol, item.name.c_str());
                    } else {
                        drawList->AddText(namePos, nameCol, item.name.c_str());
                    }

                    float costFontSize = 10.5f;
                    ImVec2 costSz = font ? font->CalcTextSizeA(costFontSize, FLT_MAX, -1.0f, item.costText.c_str()) : ImGui::CalcTextSize(item.costText.c_str());
                    ImVec2 costPos(boxCenterX - costSz.x * 0.5f, sMax.y - costSz.y - 4.0f);
                    ImU32 costCol = hasResources ? IM_COL32(255, 220, 60, 255) : IM_COL32(200, 90, 80, 220);
                    if (font) {
                        drawList->AddText(font, costFontSize, costPos, costCol, item.costText.c_str());
                    } else {
                        drawList->AddText(costPos, costCol, item.costText.c_str());
                    }
                    drawList->AddRect(sMin, sMax, IM_COL32(30, 42, 58, 80), 2.0f);
                }

                itemIdx++;
            }
        }
        // Factory build queue status overlay: SupCom-style unit badges with xN count!
        struct QueueBatch {
            UnitType type;
            int count = 0;
            float progress = 0.0f;
            float duration = 10.0f;
            int factoryIndex = -1;
        };

        std::vector<QueueBatch> playerBatches;
        for (const auto& fq : m_factoryQueues) {
            if (fq.team == Team::Player && !fq.orders.empty()) {
                for (size_t oIdx = 0; oIdx < fq.orders.size(); ++oIdx) {
                    const auto& ord = fq.orders[oIdx];
                    if (playerBatches.empty() || playerBatches.back().type != ord.type || playerBatches.back().factoryIndex != fq.factoryEntityIndex) {
                        playerBatches.push_back({ ord.type, 1, ord.progress, ord.buildTime, fq.factoryEntityIndex });
                    } else {
                        playerBatches.back().count++;
                    }
                }
            }
        }

        if (!playerBatches.empty()) {
            float badgeStartX = panelPos.x + 14.0f;
            float badgeY = panelPos.y + panelH - 34.0f;
            float badgeW = 50.0f;
            float badgeH = 26.0f;
            float badgeGap = 6.0f;

            for (size_t b = 0; b < playerBatches.size() && b < 4; ++b) {
                const auto& batch = playerBatches[b];
                ImVec2 bMin(badgeStartX + b * (badgeW + badgeGap), badgeY);
                ImVec2 bMax(bMin.x + badgeW, bMin.y + badgeH);

                bool isHovered = (mousePos.x >= bMin.x && mousePos.x <= bMax.x && mousePos.y >= bMin.y && mousePos.y <= bMax.y);
                drawList->AddRectFilled(bMin, bMax, isHovered ? IM_COL32(35, 75, 110, 240) : IM_COL32(18, 32, 48, 220), 3.0f);
                drawList->AddRect(bMin, bMax, (b == 0) ? IM_COL32(0, 240, 255, 255) : IM_COL32(40, 85, 130, 200), 3.0f, 0, (b == 0) ? 1.5f : 1.0f);

                const char* shortName = (batch.type == UnitType::Plane) ? "AIR" : ((batch.type == UnitType::Engineer) ? "ENG" : ((batch.type == UnitType::SupportMech) ? "MECH" : "TANK"));
                drawList->AddText(font, 10.0f, ImVec2(bMin.x + 4.0f, bMin.y + 3.0f), IM_COL32(200, 230, 255, 240), shortName);

                // Multiplier counter in corner: xN
                char countBuf[16];
                snprintf(countBuf, sizeof(countBuf), "x%d", batch.count);
                drawList->AddText(font, 10.0f, ImVec2(bMax.x - 18.0f, bMin.y + 3.0f), IM_COL32(255, 230, 40, 255), countBuf);

                // If first badge (currently in production), draw progress line at bottom of badge
                if (b == 0) {
                    float pct = std::clamp(batch.progress / batch.duration, 0.0f, 1.0f);
                    drawList->AddRectFilled(ImVec2(bMin.x + 2.0f, bMax.y - 3.5f), ImVec2(bMin.x + 2.0f + (badgeW - 4.0f) * pct, bMax.y - 1.5f), IM_COL32(0, 255, 220, 255), 1.0f);
                }

                // Interactive cancellation on click
                if (isHovered) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s (Queued: %d)", (batch.type == UnitType::Plane) ? "Airplane" : ((batch.type == UnitType::Engineer) ? "Engineer" : "Tank"), batch.count);
                    ImGui::TextDisabled("Left-Click to Cancel 1 | Right-Click to Cancel Batch");
                    ImGui::EndTooltip();

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        for (auto& fq : m_factoryQueues) {
                            if (fq.factoryEntityIndex == batch.factoryIndex && !fq.orders.empty()) {
                                for (auto it = fq.orders.begin(); it != fq.orders.end(); ++it) {
                                    if (it->type == batch.type) {
                                        playerMass += (float)it->massCost;
                                        playerEnergy += (float)it->energyCost;
                                        fq.orders.erase(it);
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        for (auto& fq : m_factoryQueues) {
                            if (fq.factoryEntityIndex == batch.factoryIndex && !fq.orders.empty()) {
                                for (auto it = fq.orders.begin(); it != fq.orders.end();) {
                                    if (it->type == batch.type) {
                                        playerMass += (float)it->massCost;
                                        playerEnergy += (float)it->energyCost;
                                        it = fq.orders.erase(it);
                                    } else {
                                        ++it;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        ImGui::End();
    }
    void RTSController::QueueFactoryUnit(UnitType type, Team team, int specificFactoryIndex) {
        if (gameOver || (type != UnitType::Tank && type != UnitType::Plane &&
            type != UnitType::Engineer && type != UnitType::SupportMech)) return;
        int bestFactoryIndex = -1;
        float shortestQueue = std::numeric_limits<float>::max();
        for (const auto& unit : m_units) {
            if (unit.team != team || unit.health <= 0.0f ||
                (specificFactoryIndex != -1 && unit.entityIndex != specificFactoryIndex)) continue;
            const bool compatible = unit.type == UnitType::Factory ||
                (type == UnitType::Plane ? unit.type == UnitType::AirFactory : unit.type == UnitType::LandFactory);
            if (!compatible) continue;
            float remaining = 0.0f;
            for (const auto& queue : m_factoryQueues) {
                if (queue.factoryEntityIndex != unit.entityIndex) continue;
                for (const auto& order : queue.orders) remaining += order.buildTime - order.progress;
                break;
            }
            if (remaining < shortestQueue) {
                shortestQueue = remaining;
                bestFactoryIndex = unit.entityIndex;
            }
        }
        if (bestFactoryIndex == -1) {
            if (team == Team::Player) gameMessage = "No compatible factory available.";
            return;
        }

        FactionId facId = GetTeamFaction(team);
        float bTime = 10.0f;
        int mCost = 80;
        int eCost = 60;
        GetUnitBuildCost(type, facId, mCost, eCost, bTime);

        if (team == Team::Player) {
            if (playerMass < (float)mCost || playerEnergy < (float)eCost) {
                gameMessage = "Not enough Mass or Energy to queue unit!";
                return;
            }
            playerMass -= (float)mCost;
            playerEnergy -= (float)eCost;
        } else {
            if (enemyMass < (float)mCost || enemyEnergy < (float)eCost) return;
            enemyMass -= (float)mCost;
            enemyEnergy -= (float)eCost;
        }

        FactoryQueue* targetQueue = nullptr;
        for (auto& fq : m_factoryQueues) {
            if (fq.factoryEntityIndex == bestFactoryIndex) {
                targetQueue = &fq;
                break;
            }
        }
        if (!targetQueue) {
            m_factoryQueues.push_back({ bestFactoryIndex, team, facId, {} });
            targetQueue = &m_factoryQueues.back();
        }

        targetQueue->orders.push_back({ type, team, facId, bTime, 0.0f, mCost, eCost });
        if (team == Team::Player) {
            const char* uName = (type == UnitType::Plane) ? "Airplane" : ((type == UnitType::Engineer) ? "Engineer" : ((type == UnitType::SupportMech) ? "Support Mech" : "Tank"));
            gameMessage = std::string(uName) + " queued (Order #" + std::to_string(targetQueue->orders.size()) + ")!";
        }
    }

    void RTSController::DrawUnitHealthBars(Camera* cam, ImDrawList* customDrawList) {
        if (!cam) return;
        ImDrawList* drawList = customDrawList ? customDrawList : ImGui::GetForegroundDrawList();
        if (!customDrawList) drawList->PushClipRect(m_viewportPos, ImVec2(m_viewportPos.x + m_viewportSize.x, m_viewportPos.y + m_viewportSize.y), true);

        for (const auto& u : m_units) {
            if (u.selected && u.health > 0.0f && u.team == Team::Player) {
                float headZ = (u.type == UnitType::Plane) ? 2.8f : ((u.type == UnitType::CommandMech) ? 5.8f : ((u.type == UnitType::Factory || u.type == UnitType::LandFactory || u.type == UnitType::AirFactory) ? 5.6f : 2.5f));
                glm::vec3 headPos = u.position + glm::vec3(0.0f, 0.0f, headZ);
                glm::vec2 screenPos;
                if (WorldToScreen(cam, headPos, screenPos)) {
                    if (IsScreenPosOverHUD(screenPos)) continue;
                    float barW = (u.type == UnitType::CommandMech) ? 56.0f : ((u.type == UnitType::Factory || u.type == UnitType::LandFactory || u.type == UnitType::AirFactory) ? 62.0f : 44.0f);
                    float barH = 6.0f;
                    ImVec2 barMin(screenPos.x - barW * 0.5f, screenPos.y - barH - 8.0f);
                    ImVec2 barMax(screenPos.x + barW * 0.5f, screenPos.y - 8.0f);

                    // Dark drop shadow backing
                    drawList->AddRectFilled(ImVec2(barMin.x - 1, barMin.y - 1), ImVec2(barMax.x + 1, barMax.y + 1), IM_COL32(10, 15, 20, 240), 2.0f);
                    drawList->AddRect(ImVec2(barMin.x - 1, barMin.y - 1), ImVec2(barMax.x + 1, barMax.y + 1), IM_COL32(0, 0, 0, 255), 2.0f);

                    float hpPct = std::clamp(u.health / u.maxHealth, 0.0f, 1.0f);
                    ImU32 hpCol = (hpPct > 0.6f) ? IM_COL32(45, 235, 80, 255) :
                                  ((hpPct > 0.3f) ? IM_COL32(250, 205, 30, 255) : IM_COL32(245, 45, 40, 255));
                    drawList->AddRectFilled(barMin, ImVec2(barMin.x + barW * hpPct, barMax.y), hpCol, 1.0f);

                    // Inner center divider notch
                    drawList->AddLine(ImVec2(screenPos.x, barMin.y), ImVec2(screenPos.x, barMax.y), IM_COL32(0, 0, 0, 160), 1.0f);

                    // Force-Field Shield bar & 120s recharge timer display
                    if (u.maxShieldHealth > 0.0f) {
                        ImVec2 shMin(barMin.x, barMin.y - 8.0f);
                        ImVec2 shMax(barMax.x, barMin.y - 2.0f);
                        drawList->AddRectFilled(ImVec2(shMin.x - 1, shMin.y - 1), ImVec2(shMax.x + 1, shMax.y + 1), IM_COL32(10, 20, 35, 240), 1.0f);
                        drawList->AddRect(ImVec2(shMin.x - 1, shMin.y - 1), ImVec2(shMax.x + 1, shMax.y + 1), IM_COL32(0, 0, 0, 255), 1.0f);

                        if (u.isShieldActive) {
                            float shPct = std::clamp(u.shieldHealth / u.maxShieldHealth, 0.0f, 1.0f);
                            drawList->AddRectFilled(shMin, ImVec2(shMin.x + barW * shPct, shMax.y), IM_COL32(0, 220, 255, 255), 1.0f);
                        } else {
                            int sec = (int)std::max(0.0f, u.shieldRechargeTimer);
                            char tBuf[32];
                            snprintf(tBuf, sizeof(tBuf), "RECHARGE %d:%02d", sec / 60, sec % 60);
                            drawList->AddText(ImVec2(shMin.x - 8.0f, shMin.y - 14.0f), IM_COL32(255, 145, 30, 255), tBuf);
                        }
                    } else if (u.type == UnitType::CommandMech) {
                        drawList->AddLine(ImVec2(barMin.x, barMin.y - 2), ImVec2(barMax.x, barMin.y - 2), IM_COL32(0, 220, 255, 200), 1.5f);
                    }
                }
            }
        }

        if (!customDrawList) drawList->PopClipRect();
    }

    void RTSController::DrawGroupSelectionHulls(Camera* cam, ImDrawList* customDrawList) {
        if (!cam) return;

        std::vector<const RTSUnit*> selUnits;
        for (const auto& u : m_units) {
            if (u.selected && u.team == Team::Player && u.health > 0.0f && !IsStaticBuilding(u.type)) {
                selUnits.push_back(&u);
            }
        }
        if (selUnits.size() <= 1) return; // Single unit uses individual 3D ground ring

        ImDrawList* drawList = customDrawList ? customDrawList : ImGui::GetForegroundDrawList();
        if (!customDrawList) drawList->PushClipRect(m_viewportPos, ImVec2(m_viewportPos.x + m_viewportSize.x, m_viewportPos.y + m_viewportSize.y), true);

        // 1. Spatial clustering: partition selected units into squads within 38m of each other
        std::vector<std::vector<const RTSUnit*>> clusters;
        std::vector<bool> visited(selUnits.size(), false);

        for (size_t i = 0; i < selUnits.size(); ++i) {
            if (visited[i]) continue;
            std::vector<const RTSUnit*> cluster = { selUnits[i] };
            visited[i] = true;

            std::vector<size_t> queue = { i };
            while (!queue.empty()) {
                size_t currIdx = queue.back();
                queue.pop_back();
                const auto* curr = selUnits[currIdx];

                for (size_t j = 0; j < selUnits.size(); ++j) {
                    if (!visited[j]) {
                        float d = glm::distance(glm::vec2(curr->position.x, curr->position.y),
                                                glm::vec2(selUnits[j]->position.x, selUnits[j]->position.y));
                        if (d <= 38.0f) {
                            visited[j] = true;
                            cluster.push_back(selUnits[j]);
                            queue.push_back(j);
                        }
                    }
                }
            }
            clusters.push_back(cluster);
        }

        // 2. Render encompassing hull for each cluster
        for (const auto& cluster : clusters) {
            if (cluster.size() <= 1) continue;

            std::vector<glm::vec3> perimeterPoints;
            float pad = 3.4f;

            // Compute convex hull of all units in this cluster
            std::vector<glm::vec2> pts;
            for (const auto* u : cluster) {
                pts.push_back(glm::vec2(u->position.x, u->position.y));
            }
            std::sort(pts.begin(), pts.end(), [](const glm::vec2& a, const glm::vec2& b) {
                return (a.x < b.x) || (a.x == b.x && a.y < b.y);
            });

            auto cross2d = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
                return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
            };

            std::vector<glm::vec2> lower, upper;
            for (const auto& p : pts) {
                while (lower.size() >= 2 && cross2d(lower[lower.size() - 2], lower.back(), p) <= 0.0f) {
                    lower.pop_back();
                }
                lower.push_back(p);
            }
            for (int i = (int)pts.size() - 1; i >= 0; --i) {
                while (upper.size() >= 2 && cross2d(upper[upper.size() - 2], upper.back(), pts[i]) <= 0.0f) {
                    upper.pop_back();
                }
                upper.push_back(pts[i]);
            }
            lower.pop_back();
            upper.pop_back();
            lower.insert(lower.end(), upper.begin(), upper.end());

            int hn = (int)lower.size();
            if (hn < 3) {
                // Collinear, overlapping, or 2 units: render smooth stadium capsule or circle
                glm::vec2 p0 = pts.front();
                glm::vec2 p1 = pts.back();
                float dist = glm::distance(p0, p1);
                if (dist < 0.4f) {
                    for (int s = 0; s < 16; ++s) {
                        float a = ((float)s / 16.0f) * 6.2831853f;
                        perimeterPoints.push_back(glm::vec3(p0.x + cos(a) * pad, p0.y + sin(a) * pad, 0.05f));
                    }
                } else {
                    glm::vec2 dir = (p1 - p0) / dist;
                    glm::vec2 perp(-dir.y, dir.x); // CCW outward normal

                    // Semicircle cap around p1 from -pi/2 to +pi/2
                    for (int s = 0; s <= 8; ++s) {
                        float a = -1.5707963f + ((float)s / 8.0f) * 3.14159265f;
                        glm::vec2 arc = (dir * cos(a) + perp * sin(a)) * pad;
                        perimeterPoints.push_back(glm::vec3(p1 + arc, 0.05f));
                    }
                    // Semicircle cap around p0 from +pi/2 to +3*pi/2
                    for (int s = 0; s <= 8; ++s) {
                        float a = 1.5707963f + ((float)s / 8.0f) * 3.14159265f;
                        glm::vec2 arc = (dir * cos(a) + perp * sin(a)) * pad;
                        perimeterPoints.push_back(glm::vec3(p0 + arc, 0.05f));
                    }
                }
            } else {
                // 3+ vertices: Monotone chain guarantees strictly CCW winding.
                // Compute outward normal for each directed edge (i -> i+1)
                std::vector<glm::vec2> edgeNormals;
                edgeNormals.reserve(hn);
                for (int i = 0; i < hn; ++i) {
                    glm::vec2 e = lower[(i + 1) % hn] - lower[i];
                    float elen = glm::length(e);
                    glm::vec2 n = (elen > 0.0001f) ? glm::vec2(e.y, -e.x) / elen : glm::vec2(1.0f, 0.0f);
                    edgeNormals.push_back(n);
                }

                // At each vertex, bevel the corner from incoming edge normal to outgoing edge normal
                for (int i = 0; i < hn; ++i) {
                    glm::vec2 v = lower[i];
                    glm::vec2 nIn = edgeNormals[(i - 1 + hn) % hn];
                    glm::vec2 nOut = edgeNormals[i];

                    float a1 = atan2(nIn.y, nIn.x);
                    float a2 = atan2(nOut.y, nOut.x);
                    float diff = a2 - a1;
                    while (diff < 0.0f) diff += 6.2831853f;
                    while (diff > 6.2831853f) diff -= 6.2831853f;

                    for (int s = 0; s <= 2; ++s) {
                        float ang = a1 + ((float)s / 2.0f) * diff;
                        glm::vec2 offset(cos(ang) * pad, sin(ang) * pad);
                        perimeterPoints.push_back(glm::vec3(v + offset, 0.05f));
                    }
                }
            }

            // Project perimeter to screen space
            std::vector<ImVec2> screenPoints;
            for (const auto& wPos : perimeterPoints) {
                glm::vec2 sPos;
                if (WorldToScreen(cam, wPos, sPos)) {
                    screenPoints.push_back(ImVec2(sPos.x, sPos.y));
                }
            }

            if (screenPoints.size() >= 3) {
                // Soft glowing cyan fill inside the group boundary
                drawList->AddConvexPolyFilled(screenPoints.data(), (int)screenPoints.size(), IM_COL32(0, 240, 210, 28));
                // Outer subtle glow line
                drawList->AddPolyline(screenPoints.data(), (int)screenPoints.size(), IM_COL32(0, 255, 200, 70), ImDrawFlags_Closed, 4.5f);
                // Sharp tactical boundary line
                drawList->AddPolyline(screenPoints.data(), (int)screenPoints.size(), IM_COL32(0, 255, 200, 230), ImDrawFlags_Closed, 2.2f);
            }

            // Subtle center pips under each unit within the group
            for (const auto* u : cluster) {
                glm::vec2 uScreen;
                if (WorldToScreen(cam, glm::vec3(u->position.x, u->position.y, 0.05f), uScreen)) {
                    if (!IsScreenPosOverHUD(uScreen)) {
                        drawList->AddCircleFilled(ImVec2(uScreen.x, uScreen.y), 3.0f, IM_COL32(0, 255, 200, 180));
                    }
                }
            }
        }
        if (!customDrawList) drawList->PopClipRect();
    }

    void RTSController::DrawSelectedUnitsBar(ImVec2 viewportPos, ImVec2 viewportSize) {
        std::vector<RTSUnit*> selUnits;
        for (auto& u : m_units) {
            if (u.selected && u.team == Team::Player && u.health > 0.0f) {
                selUnits.push_back(&u);
                if (selUnits.size() >= 16) break; // 16 slots matching group_select_panel_body.png
            }
        }
        if (selUnits.empty()) return;

        float chassisW = 425.0f;
        float chassisH = 100.0f;
        ImVec2 barPos(viewportPos.x + (viewportSize.x * 0.5f) - chassisW * 0.5f, viewportPos.y + viewportSize.y - chassisH - 12.0f);

        ImGui::SetNextWindowPos(barPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(chassisW + 70.0f, chassisH + 75.0f), ImGuiCond_Always);
        ImGui::Begin("SelectedUnitsBar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImTextureID selBarTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/group_select_panel_body.png") : nullptr);
        if (!selBarTex && m_renderer) {
            selBarTex = (ImTextureID)(uintptr_t)m_renderer->getImGuiTextureID("machinevision/group_select_panel_body.png");
        }

        if (selBarTex) {
            // Render metal chassis and trailing power cable plugged into right side
            ImVec2 uv0(0.0439f, 0.4170f);
            ImVec2 uv1(0.9609f, 0.6338f);
            drawList->AddImage(selBarTex, barPos, ImVec2(barPos.x + chassisW, barPos.y + chassisH), uv0, uv1);
        } else {
            drawList->AddRectFilled(barPos, ImVec2(barPos.x + chassisW, barPos.y + chassisH), IM_COL32(18, 24, 34, 240), 6.0f);
            drawList->AddRect(barPos, ImVec2(barPos.x + chassisW, barPos.y + chassisH), IM_COL32(50, 80, 115, 230), 6.0f, 0, 1.5f);
        }

        // Normalized slot column and row boundaries from group_select_panel_body.png:
        const float colX0[8] = { 0.0330f, 0.1395f, 0.2460f, 0.3514f, 0.4579f, 0.5644f, 0.6709f, 0.7774f };
        const float colX1[8] = { 0.1342f, 0.2407f, 0.3472f, 0.4537f, 0.5602f, 0.6667f, 0.7732f, 0.8797f };
        const float rowY0[2] = { 0.0586f, 0.5090f };
        const float rowY1[2] = { 0.4865f, 0.9369f };

        ImVec2 mousePos = ImGui::GetMousePos();
        bool isLeftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        for (int i = 0; i < 16; ++i) {
            int col = i % 8;
            int row = i / 8;
            ImVec2 sMin(barPos.x + colX0[col] * chassisW, barPos.y + rowY0[row] * chassisH);
            ImVec2 sMax(barPos.x + colX1[col] * chassisW, barPos.y + rowY1[row] * chassisH);

            if (i < (int)selUnits.size()) {
                RTSUnit* u = selUnits[i];
                bool isHovered = (mousePos.x >= sMin.x && mousePos.x <= sMax.x && mousePos.y >= sMin.y && mousePos.y <= sMax.y);

                if (isHovered) {
                    drawList->AddRectFilled(sMin, sMax, IM_COL32(0, 180, 255, 40), 2.0f);
                    drawList->AddRect(sMin, sMax, IM_COL32(0, 240, 255, 255), 2.0f, 0, 1.5f);
                } else {
                    drawList->AddRect(sMin, sMax, IM_COL32(40, 65, 95, 140), 2.0f);
                }

                float cx = (sMin.x + sMax.x) * 0.5f;
                float cy = (sMin.y + sMax.y) * 0.5f - 3.0f;
                ImU32 glyphCol = (u->type == UnitType::CommandMech) ? IM_COL32(70, 220, 255, 255) :
                                 ((u->type == UnitType::Plane) ? IM_COL32(90, 240, 255, 255) :
                                 ((u->type == UnitType::Engineer) ? IM_COL32(255, 255, 255, 255) :
                                 ((u->type == UnitType::Turret) ? IM_COL32(255, 180, 50, 255) : IM_COL32(60, 160, 255, 255))));

                if (u->type == UnitType::Plane) {
                    drawList->AddTriangleFilled(ImVec2(cx, cy - 8.0f), ImVec2(cx - 8.0f, cy + 6.0f), ImVec2(cx + 8.0f, cy + 6.0f), glyphCol);
                } else if (u->type == UnitType::CommandMech) {
                    drawList->AddCircleFilled(ImVec2(cx, cy), 6.0f, glyphCol);
                    drawList->AddCircle(ImVec2(cx, cy), 8.0f, IM_COL32(255, 255, 255, 255), 14, 1.2f);
                } else if (u->type == UnitType::Engineer) {
                    drawList->AddCircleFilled(ImVec2(cx, cy), 4.5f, glyphCol);
                } else {
                    drawList->AddRectFilled(ImVec2(cx - 8.0f, cy - 5.0f), ImVec2(cx + 8.0f, cy + 5.0f), glyphCol, 1.5f);
                }

                // Health bar at bottom of slot
                float slotW = sMax.x - sMin.x;
                float hpPct = std::clamp(u->health / u->maxHealth, 0.0f, 1.0f);
                ImU32 hpCol = (hpPct > 0.6f) ? IM_COL32(45, 230, 80, 255) : ((hpPct > 0.3f) ? IM_COL32(245, 200, 30, 255) : IM_COL32(245, 50, 40, 255));
                drawList->AddRectFilled(ImVec2(sMin.x + 3.0f, sMax.y - 4.5f), ImVec2(sMin.x + 3.0f + (slotW - 6.0f) * hpPct, sMax.y - 1.5f), hpCol);

                if (isHovered) {
                    const char* typeName = (u->type == UnitType::Plane) ? "Airplane" :
                                           ((u->type == UnitType::Tank) ? "Tank" :
                                           ((u->type == UnitType::Engineer) ? "Engineer" :
                                           ((u->type == UnitType::CommandMech) ? "Armored Command Mech" : "Structure")));
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", typeName);
                    ImGui::Text("Health: %.0f / %.0f (%.0f%%)", u->health, u->maxHealth, hpPct * 100.0f);
                    ImGui::TextDisabled("Click to isolate selection");
                    ImGui::EndTooltip();

                    if (isLeftClicked) {
                        ClearSelection();
                        u->selected = true;
                    }
                }
            } else {
                drawList->AddRect(sMin, sMax, IM_COL32(24, 32, 42, 80), 2.0f);
            }
        }

        ImGui::End();
    }

    // =========================================================================
    // AERO GLASS TILING UI HELPERS
    // =========================================================================
    namespace AeroUI {
        inline void DrawGlassPanel(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding = 8.0f,
                                   ImU32 baseTop = IM_COL32(14, 26, 42, 215),
                                   ImU32 baseBot = IM_COL32(8, 14, 24, 235),
                                   ImU32 borderCol = IM_COL32(100, 195, 255, 140)) {
            float w = p1.x - p0.x;
            float h = p1.y - p0.y;
            if (w <= 0.0f || h <= 0.0f) return;

            // 1. Soft Ambient Drop Shadow
            dl->AddRectFilled(ImVec2(p0.x + 3.0f, p0.y + 5.0f), ImVec2(p1.x + 3.0f, p1.y + 5.0f), IM_COL32(0, 0, 0, 70), rounding + 2.0f);
            dl->AddRectFilled(ImVec2(p0.x + 1.0f, p0.y + 2.0f), ImVec2(p1.x + 1.0f, p1.y + 2.0f), IM_COL32(0, 0, 0, 110), rounding + 1.0f);

            // 2. Translucent Tinted Base Glass
            dl->AddRectFilledMultiColor(p0, p1, baseTop, baseTop, baseBot, baseBot);

            // 3. Curved Glossy Specular Sheen (Upper half dome reflection)
            float sheenH = std::max(6.0f, h * 0.44f);
            ImVec2 sheenP1(p1.x, p0.y + sheenH);
            dl->AddRectFilledMultiColor(p0, sheenP1, IM_COL32(255, 255, 255, 65), IM_COL32(255, 255, 255, 65),
                                                   IM_COL32(255, 255, 255, 5),  IM_COL32(255, 255, 255, 5));

            // 4. Subtle Diagonal Light Streak (Aero prism artifact)
            float streakW = std::min(w * 0.35f, 90.0f);
            ImVec2 q0(p0.x + 4.0f, p0.y + 2.0f);
            ImVec2 q1(p0.x + 4.0f + streakW, p0.y + 2.0f);
            ImVec2 q2(p0.x + 14.0f + streakW * 0.4f, p0.y + sheenH);
            ImVec2 q3(p0.x + 4.0f, p0.y + sheenH);
            dl->AddQuadFilled(q0, q1, q2, q3, IM_COL32(255, 255, 255, 22));

            // 5. Outer Beveled Glass Border
            dl->AddRect(p0, p1, borderCol, rounding, 0, 1.2f);

            // 6. Top Interior Highlight Hairline (Light catching upper edge)
            dl->AddLine(ImVec2(p0.x + rounding * 0.5f, p0.y + 1.0f),
                        ImVec2(p1.x - rounding * 0.5f, p0.y + 1.0f),
                        IM_COL32(255, 255, 255, 140), 1.0f);
        }

        inline bool DrawGlassTileButton(const char* idStr, const char* label, const char* sublabel,
                                       ImVec2 size, bool selected, ImU32 accentCol = IM_COL32(0, 220, 255, 255)) {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + size.x, p0.y + size.y);
            bool clicked = ImGui::InvisibleButton(idStr, size);
            bool hovered = ImGui::IsItemHovered();

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 topCol = selected ? IM_COL32(24, 55, 85, 235)
                                    : (hovered ? IM_COL32(20, 42, 65, 215) : IM_COL32(10, 20, 32, 185));
            ImU32 botCol = selected ? IM_COL32(12, 32, 52, 245)
                                    : (hovered ? IM_COL32(10, 24, 38, 230) : IM_COL32(6, 12, 20, 215));
            ImU32 border = selected ? accentCol
                                    : (hovered ? IM_COL32(160, 220, 255, 180) : IM_COL32(70, 130, 180, 100));

            DrawGlassPanel(dl, p0, p1, 6.0f, topCol, botCol, border);

            // Extra luminous bottom bar when selected
            if (selected) {
                dl->AddRectFilled(ImVec2(p0.x + 3.0f, p1.y - 3.0f), ImVec2(p1.x - 3.0f, p1.y - 1.0f), accentCol, 1.0f);
            }

            // Render Title & optional subtitle
            ImFont* font = ImGui::GetFont();
            float textW = font ? font->CalcTextSizeA(13.0f, FLT_MAX, -1.0f, label).x : ImGui::CalcTextSize(label).x;
            float textY = (sublabel && sublabel[0]) ? (p0.y + (size.y * 0.5f) - 13.0f) : (p0.y + (size.y - 14.0f) * 0.5f);
            ImVec2 tPos(p0.x + std::max(4.0f, (size.x - textW) * 0.5f), textY);
            dl->AddText(font, 13.0f, tPos, selected ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(230, 240, 255, 240) : IM_COL32(185, 205, 225, 220)), label);

            if (sublabel && sublabel[0]) {
                float subW = font ? font->CalcTextSizeA(10.5f, FLT_MAX, -1.0f, sublabel).x : ImGui::CalcTextSize(sublabel).x;
                ImVec2 sPos(p0.x + std::max(4.0f, (size.x - subW) * 0.5f), textY + 14.0f);
                dl->AddText(font, 10.5f, sPos, selected ? IM_COL32(160, 230, 255, 230) : IM_COL32(130, 160, 185, 190), sublabel);
            }

            return clicked;
        }
    }
    void RTSController::DrawSettingsMenu(ImVec2 viewportPos, ImVec2 viewportSize) {
        float winW = 480.0f;
        float winH = 390.0f;
        ImVec2 winP0(viewportPos.x + (viewportSize.x - winW) * 0.5f, viewportPos.y + (viewportSize.y - winH) * 0.5f);
        ImVec2 winP1(winP0.x + winW, winP0.y + winH);

        ImGui::SetNextWindowPos(winP0, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.08f, 0.14f, 0.90f));

        ImGui::Begin("##AeroSettingsWindow", &m_showSettingsMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        AeroUI::DrawGlassPanel(dl, winP0, winP1, 12.0f,
                               IM_COL32(18, 36, 58, 235),
                               IM_COL32(8, 14, 24, 245),
                               IM_COL32(90, 185, 255, 170));

        // Header Bar
        ImVec2 head0(winP0.x + 10.0f, winP0.y + 10.0f);
        ImVec2 head1(winP1.x - 10.0f, winP0.y + 48.0f);
        AeroUI::DrawGlassPanel(dl, head0, head1, 8.0f,
                               IM_COL32(28, 58, 92, 240),
                               IM_COL32(12, 26, 44, 250),
                               IM_COL32(120, 210, 255, 200));

        ImFont* font = ImGui::GetFont();
        const char* title = "OPERATIONS SETTINGS";
        ImVec2 tSz = font ? font->CalcTextSizeA(15.0f, FLT_MAX, -1.0f, title) : ImGui::CalcTextSize(title);
        dl->AddText(font, 15.0f, ImVec2(head0.x + 14.0f, head0.y + (38.0f - tSz.y) * 0.5f), IM_COL32(255, 255, 255, 255), title);
        ImGui::Dummy(ImVec2(0.0f, 44.0f));
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "AUDIO MIXER");
        ImGui::SliderFloat("Master Volume", &m_masterVolume, 0.0f, 1.0f, "%.0f%%");
        ImGui::SliderFloat("SFX Volume", &m_sfxVolume, 0.0f, 1.0f, "%.0f%%");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "DISPLAY RESOLUTION");
        const char* resOpts[] = { "1920 x 1080 (FHD)", "1600 x 900", "1280 x 720 (HD)", "2560 x 1440 (QHD)" };
        ImGui::Combo("Resolution", &m_selectedResolution, resOpts, 4);
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "FAF TACTICAL RULES");
        const char* noRushOpts[] = { "Off (No Limit)", "5 Minutes", "10 Minutes", "15 Minutes", "20 Minutes" };
        int noRushVals[] = { 0, 5, 10, 15, 20 };
        int selIdx = 0;
        for (int i = 0; i < 5; i++) if (m_skirmishSettings.noRushMinutes == noRushVals[i]) selIdx = i;
        if (ImGui::Combo("No Rush Timer", &selIdx, noRushOpts, 5)) {
            m_skirmishSettings.noRushMinutes = noRushVals[selIdx];
        }
        if (ImGui::Checkbox("Enable Fog of War (Dynamic Intel & Shroud)", &m_skirmishSettings.enableFogOfWar)) {
            if (m_scene) m_scene->environment.enableFog = m_skirmishSettings.enableFogOfWar;
        }
        ImGui::Spacing();

        float availW = ImGui::GetContentRegionAvail().x;
        if (AeroUI::DrawGlassTileButton("CloseSettingsBtn", "APPLY & CLOSE", "Save settings", ImVec2(availW, 38.0f), true, IM_COL32(0, 240, 255, 255))) {
            m_showSettingsMenu = false;
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }


    void RTSController::DrawMinimap(ImVec2 viewportPos, ImVec2 viewportSize) {
        if (!m_showMinimap) return;

        float panelW = 340.0f;
        float panelH = 340.0f;
        ImVec2 mapWindowPos(viewportPos.x + viewportSize.x - panelW - 16.0f, viewportPos.y + 16.0f);

        ImGui::SetNextWindowPos(mapWindowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
        ImGui::Begin("TacticalMinimap", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Frame Bezel: Try to load custom textured monitor panel frame
        ImTextureID mapFrameTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/worldmap_panel_body.png") : nullptr);
        if (!mapFrameTex && m_renderer) {
            mapFrameTex = (ImTextureID)(uintptr_t)m_renderer->getImGuiTextureID("machinevision/worldmap_panel_body.png");
        }

        // Inner radar screen bounds
        ImVec2 screenMin, screenMax;
        if (mapFrameTex) {
            drawList->AddImage(mapFrameTex, mapWindowPos, ImVec2(mapWindowPos.x + panelW, mapWindowPos.y + panelH));
            // Inset matching the screen cutout inside worldmap_panel_body.png
            screenMin = ImVec2(mapWindowPos.x + panelW * 0.145f, mapWindowPos.y + panelH * 0.08f);
            screenMax = ImVec2(mapWindowPos.x + panelW * 0.855f, mapWindowPos.y + panelH * 0.92f);
        } else {
            // High-tech sci-fi military frame fallback
            drawList->AddRectFilled(mapWindowPos, ImVec2(mapWindowPos.x + panelW, mapWindowPos.y + panelH), IM_COL32(18, 24, 32, 245), 6.0f);
            drawList->AddRect(mapWindowPos, ImVec2(mapWindowPos.x + panelW, mapWindowPos.y + panelH), IM_COL32(50, 80, 115, 230), 6.0f, 0, 2.0f);
            // Header bar
            drawList->AddRectFilled(mapWindowPos, ImVec2(mapWindowPos.x + panelW, mapWindowPos.y + 22.0f), IM_COL32(28, 42, 60, 255), 6.0f, ImDrawFlags_RoundCornersTop);
            drawList->AddText(ImVec2(mapWindowPos.x + 10.0f, mapWindowPos.y + 4.0f), IM_COL32(140, 205, 255, 255), "TACTICAL RADAR (FAF INTEL)");
            screenMin = ImVec2(mapWindowPos.x + 8.0f, mapWindowPos.y + 26.0f);
            screenMax = ImVec2(mapWindowPos.x + panelW - 8.0f, mapWindowPos.y + panelH - 8.0f);
        }

        // Ensure 1:1 square aspect ratio for tactical grid
        float screenSide = std::min(screenMax.x - screenMin.x, screenMax.y - screenMin.y);
        float padX = (screenMax.x - screenMin.x - screenSide) * 0.5f;
        float padY = (screenMax.y - screenMin.y - screenSide) * 0.5f;
        screenMin.x += padX; screenMax.x -= padX;
        screenMin.y += padY; screenMax.y -= padY;

        // Clip radar graphics strictly to the screen area
        drawList->PushClipRect(screenMin, screenMax, true);
        // Coordinate transforms
        auto WorldToMinimap = [&](float wx, float wy) -> ImVec2 {
            float nx = std::clamp((wx + 500.0f) / 1000.0f, 0.0f, 1.0f);
            float ny = std::clamp(1.0f - (wy + 500.0f) / 1000.0f, 0.0f, 1.0f); // North (+Y) is UP
            return ImVec2(screenMin.x + nx * screenSide, screenMin.y + ny * screenSide);
        };

        auto MinimapToWorld = [&](ImVec2 mpos) -> glm::vec2 {
            float nx = (mpos.x - screenMin.x) / screenSide;
            float ny = (mpos.y - screenMin.y) / screenSide;
            float wx = nx * 1000.0f - 500.0f;
            float wy = (1.0f - ny) * 1000.0f - 500.0f;
            return glm::vec2(std::clamp(wx, -480.0f, 480.0f), std::clamp(wy, -480.0f, 480.0f));
        };

        // 2. Base Shroud Background (Dense Navy Fog)
        drawList->AddRectFilled(screenMin, screenMax, IM_COL32(14, 18, 26, 255));

        // 2B. Territory Domination Overlay (Faction-colored infill & zone boundaries)
        glm::vec3 pFacPrim = GetTeamDisplayColor(Team::Player, false);
        glm::vec3 pFacAcc = GetTeamDisplayColor(Team::Player, true);
        glm::vec3 eFacPrim = GetTeamDisplayColor(Team::Enemy, false);
        glm::vec3 eFacAcc = GetTeamDisplayColor(Team::Enemy, true);

        if (m_showTerritories) {
            // Player Territory Infill
            for (const auto& u : m_units) {
                if (u.team == Team::Player && u.health > 0.0f && IsStaticBuilding(u.type)) {
                    ImVec2 pM = WorldToMinimap(u.position.x, u.position.y);
                    float rPx = ((u.type == UnitType::ShieldGen) ? 75.0f : 55.0f) / 1000.0f * screenSide;
                    drawList->AddCircleFilled(pM, rPx, ToImU32(u.type == UnitType::ShieldGen ? pFacAcc : pFacPrim, u.type == UnitType::ShieldGen ? 75 : 50), 24);
                    drawList->AddCircle(pM, rPx, ToImU32(u.type == UnitType::ShieldGen ? pFacAcc : pFacPrim, u.type == UnitType::ShieldGen ? 220 : 160), 24, 1.2f);
                }
            }
            // Enemy Territory Infill
            for (const auto& u : m_units) {
                if (u.team == Team::Enemy && u.health > 0.0f && IsStaticBuilding(u.type)) {
                    ImVec2 eM = WorldToMinimap(u.position.x, u.position.y);
                    float rPx = ((u.type == UnitType::ShieldGen) ? 75.0f : 55.0f) / 1000.0f * screenSide;
                    drawList->AddCircleFilled(eM, rPx, ToImU32(u.type == UnitType::ShieldGen ? eFacAcc : eFacPrim, u.type == UnitType::ShieldGen ? 75 : 50), 24);
                    drawList->AddCircle(eM, rPx, ToImU32(u.type == UnitType::ShieldGen ? eFacAcc : eFacPrim, u.type == UnitType::ShieldGen ? 220 : 160), 24, 1.2f);
                }
            }
            // Claimed resource nodes territory control
            for (const auto& node : m_resourceNodes) {
                if (node.occupied) {
                    float minP = 9999.0f, minE = 9999.0f;
                    for (const auto& u : m_units) {
                        if (u.health > 0.0f && IsStaticBuilding(u.type)) {
                            float d = glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(u.position.x, u.position.y));
                            if (u.team == Team::Player && d < minP) minP = d;
                            if (u.team == Team::Enemy && d < minE) minE = d;
                        }
                    }
                    ImVec2 nM = WorldToMinimap(node.position.x, node.position.y);
                    float nodeR_px = (45.0f / 1000.0f) * screenSide;
                    if (minP < 20.0f) {
                        drawList->AddCircleFilled(nM, nodeR_px, ToImU32(pFacPrim, 55), 18);
                        drawList->AddCircle(nM, nodeR_px, ToImU32(pFacAcc, 180), 18, 1.2f);
                    } else if (minE < 20.0f) {
                        drawList->AddCircleFilled(nM, nodeR_px, ToImU32(eFacPrim, 55), 18);
                        drawList->AddCircle(nM, nodeR_px, ToImU32(eFacAcc, 180), 18, 1.2f);
                    }
                }
            }
        }

        // 3. Tactical Grid Lines (4x4)
        for (int g = 1; g < 4; ++g) {
            float t = (float)g / 4.0f;
            drawList->AddLine(ImVec2(screenMin.x + t * screenSide, screenMin.y),
                              ImVec2(screenMin.x + t * screenSide, screenMax.y),
                              IM_COL32(32, 48, 68, 120), 1.0f);
            drawList->AddLine(ImVec2(screenMin.x, screenMin.y + t * screenSide),
                              ImVec2(screenMax.x, screenMin.y + t * screenSide),
                              IM_COL32(32, 48, 68, 120), 1.0f);
        }
        // Center battlefield equator line
        drawList->AddLine(ImVec2(screenMin.x + 0.5f * screenSide, screenMin.y),
                          ImVec2(screenMin.x + 0.5f * screenSide, screenMax.y),
                          IM_COL32(45, 75, 105, 160), 1.0f);

        // 4. Dynamic Fog of War Vision Circles (Green Revealed Terrain on Minimap)
        ImVec2 baseM = WorldToMinimap(-260.0f, 0.0f);
        float baseR_px = (110.0f / 1000.0f) * screenSide;
        drawList->AddCircleFilled(baseM, baseR_px, IM_COL32(34, 68, 38, 235), 24);
        drawList->AddCircle(baseM, baseR_px, IM_COL32(55, 115, 65, 150), 24, 1.0f);

        for (const auto& pu : m_units) {
            if (pu.team == Team::Player && pu.health > 0.0f) {
                float uVis = (pu.type == UnitType::Plane) ? 95.0f :
                            ((pu.type == UnitType::Turret) ? 70.0f :
                            ((pu.type == UnitType::CommandMech) ? 65.0f :
                            ((pu.type == UnitType::Tank) ? 55.0f : 45.0f)));
                float uVis_px = (uVis / 1000.0f) * screenSide;
                ImVec2 uM = WorldToMinimap(pu.position.x, pu.position.y);
                drawList->AddCircleFilled(uM, uVis_px, IM_COL32(34, 68, 38, 235), 18);
                drawList->AddCircle(uM, uVis_px, IM_COL32(55, 115, 65, 120), 18, 0.8f);
            }
        }

        // 5. Resource Deposit Nodes
        for (const auto& node : m_resourceNodes) {
            bool inVision = (glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(-260.0f, 0.0f)) <= 110.0f);
            if (!inVision) {
                for (const auto& pu : m_units) {
                    if (pu.team == Team::Player && pu.health > 0.0f) {
                        float puVis = (pu.type == UnitType::Plane) ? 95.0f : 60.0f;
                        if (glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(pu.position.x, pu.position.y)) <= puVis) {
                            inVision = true;
                            break;
                        }
                    }
                }
            }
            if (inVision) {
                ImVec2 nM = WorldToMinimap(node.position.x, node.position.y);
                if (node.type == ResourceNodeType::Mass) {
                    drawList->AddRectFilled(ImVec2(nM.x - 2.5f, nM.y - 2.5f), ImVec2(nM.x + 2.5f, nM.y + 2.5f), IM_COL32(40, 245, 90, 255), 1.0f);
                } else {
                    drawList->AddCircleFilled(nM, 2.5f, IM_COL32(255, 220, 35, 255));
                }
            }
        }

        // 6. Enemy Units & Structures (FAF Intel Denial)
        for (const auto& u : m_units) {
            if (u.team == Team::Enemy && u.health > 0.0f) {
                ImVec2 uM = WorldToMinimap(u.position.x, u.position.y);
                if (IsStaticBuilding(u.type)) {
                    // Static buildings only shown if scouted by player!
                    if (u.scoutedByPlayer) {
                        bool inVision = (u.entity && u.entity->emission > 0.01f);
                        ImU32 col = inVision ? ToImU32(eFacPrim, 255) : ToImU32(eFacPrim * 0.65f, 175);
                        float sz = (u.type == UnitType::Factory) ? 4.5f : 3.0f;
                        drawList->AddRectFilled(ImVec2(uM.x - sz, uM.y - sz), ImVec2(uM.x + sz, uM.y + sz), col);
                        drawList->AddRect(ImVec2(uM.x - sz, uM.y - sz), ImVec2(uM.x + sz, uM.y + sz), IM_COL32(20, 20, 20, 220));
                    }
                } else {
                    // Mobile units (Tank, Plane, Mech, Engineer) ONLY drawn if currently in line of sight!
                    if (u.entity && u.entity->scale.x > 0.1f) {
                        if (u.type == UnitType::CommandMech) {
                            drawList->AddCircleFilled(uM, 5.0f, ToImU32(eFacPrim, 255));
                            drawList->AddCircle(uM, 6.5f, IM_COL32(255, 255, 255, 255), 16, 1.2f);
                        } else if (u.type == UnitType::Plane) {
                            drawList->AddTriangleFilled(ImVec2(uM.x, uM.y - 4.0f), ImVec2(uM.x - 3.5f, uM.y + 3.5f), ImVec2(uM.x + 3.5f, uM.y + 3.5f), ToImU32(eFacAcc, 255));
                        } else if (u.type == UnitType::Engineer) {
                            drawList->AddCircleFilled(uM, 2.5f, ToImU32(glm::mix(glm::vec3(1.0f), eFacPrim, 0.45f), 255));
                        } else {
                            // Tanks
                            drawList->AddCircleFilled(uM, 3.0f, ToImU32(eFacPrim, 255));
                        }
                    }
                }
            }
        }

        // 7. Player Units & Structures
        for (const auto& u : m_units) {
            if (u.team == Team::Player && u.health > 0.0f) {
                ImVec2 uM = WorldToMinimap(u.position.x, u.position.y);
                if (IsStaticBuilding(u.type)) {
                    float sz = (u.type == UnitType::Factory) ? 4.5f : 3.0f;
                    drawList->AddRectFilled(ImVec2(uM.x - sz, uM.y - sz), ImVec2(uM.x + sz, uM.y + sz), ToImU32(pFacPrim, 255));
                    drawList->AddRect(ImVec2(uM.x - sz, uM.y - sz), ImVec2(uM.x + sz, uM.y + sz), IM_COL32(15, 30, 50, 220));
                } else {
                    if (u.type == UnitType::CommandMech) {
                        drawList->AddCircleFilled(uM, 5.0f, ToImU32(pFacPrim, 255));
                        drawList->AddCircle(uM, 6.5f, IM_COL32(255, 255, 255, 255), 16, 1.2f);
                    } else if (u.type == UnitType::Plane) {
                        drawList->AddTriangleFilled(ImVec2(uM.x, uM.y - 4.0f), ImVec2(uM.x - 3.5f, uM.y + 3.5f), ImVec2(uM.x + 3.5f, uM.y + 3.5f), ToImU32(pFacAcc, 255));
                    } else if (u.type == UnitType::Engineer) {
                        drawList->AddCircleFilled(uM, 2.5f, IM_COL32(255, 255, 255, 255));
                    } else {
                        drawList->AddCircleFilled(uM, 3.0f, ToImU32(pFacPrim, 255));
                    }
                    if (u.selected) {
                        drawList->AddCircle(uM, 5.0f, IM_COL32(255, 240, 50, 240), 12, 1.2f);
                    }
                }
            }
        }

        // 8. Tactical Ping Ripple
        if (m_minimapPingTimer > 0.0f) {
            float pingProgress = 1.0f - (m_minimapPingTimer / 1.5f);
            float pingRadius = pingProgress * 20.0f;
            int pingAlpha = (int)((1.0f - pingProgress) * 255);
            ImVec2 pM = WorldToMinimap(m_minimapPingPos.x, m_minimapPingPos.y);
            drawList->AddCircle(pM, pingRadius, IM_COL32(0, 255, 180, pingAlpha), 20, 2.0f);
        }

        // 9. Camera Viewport Frustum Box
        ImVec2 camCenter = WorldToMinimap(m_cameraTarget.x, m_cameraTarget.y);
        float camW = (m_cameraZoom * 1.6f / 1000.0f) * screenSide;
        float camH = (m_cameraZoom * 1.15f / 1000.0f) * screenSide;
        drawList->AddRect(ImVec2(camCenter.x - camW * 0.5f, camCenter.y - camH * 0.5f),
                          ImVec2(camCenter.x + camW * 0.5f, camCenter.y + camH * 0.5f),
                          IM_COL32(255, 235, 100, 210), 1.0f, 0, 1.5f);
        drawList->AddLine(ImVec2(camCenter.x - 3.5f, camCenter.y), ImVec2(camCenter.x + 3.5f, camCenter.y), IM_COL32(255, 235, 100, 240));
        drawList->AddLine(ImVec2(camCenter.x, camCenter.y - 3.5f), ImVec2(camCenter.x, camCenter.y + 3.5f), IM_COL32(255, 235, 100, 240));

        // 9B. Minimap Territory Toggle Button & Map Domination Bar
        int pDomScore = 0, eDomScore = 0;
        for (const auto& u : m_units) {
            if (u.health > 0.0f && IsStaticBuilding(u.type)) {
                if (u.team == Team::Player) pDomScore += (u.type == UnitType::Factory) ? 25 : 10;
                else eDomScore += (u.type == UnitType::Factory) ? 25 : 10;
            }
        }
        for (const auto& node : m_resourceNodes) {
            if (node.occupied) {
                float minP = 9999.0f, minE = 9999.0f;
                for (const auto& u : m_units) {
                    if (u.health > 0.0f && IsStaticBuilding(u.type)) {
                        float d = glm::distance(glm::vec2(node.position.x, node.position.y), glm::vec2(u.position.x, u.position.y));
                        if (u.team == Team::Player && d < minP) minP = d;
                        if (u.team == Team::Enemy && d < minE) minE = d;
                    }
                }
                if (minP < 15.0f) pDomScore += 15;
                else if (minE < 15.0f) eDomScore += 15;
            }
        }
        int totDomScore = std::max(1, pDomScore + eDomScore);
        float pDomPct = (float)pDomScore / (float)totDomScore * 100.0f;
        float eDomPct = 100.0f - pDomPct;

        ImVec2 btnMin(screenMin.x + 6.0f, screenMin.y + 6.0f);
        ImVec2 btnMax(screenMin.x + 104.0f, screenMin.y + 22.0f);
        if (m_showTerritories) {
            drawList->AddRectFilled(btnMin, btnMax, IM_COL32(20, 80, 130, 230), 3.0f);
            drawList->AddRect(btnMin, btnMax, IM_COL32(0, 240, 255, 255), 3.0f, 0, 1.5f);
            drawList->AddText(ImVec2(btnMin.x + 6.0f, btnMin.y + 2.0f), IM_COL32(255, 255, 255, 255), "TERRITORY: ON");
        } else {
            drawList->AddRectFilled(btnMin, btnMax, IM_COL32(15, 22, 32, 200), 3.0f);
            drawList->AddRect(btnMin, btnMax, IM_COL32(60, 90, 120, 200), 3.0f, 0, 1.0f);
            drawList->AddText(ImVec2(btnMin.x + 6.0f, btnMin.y + 2.0f), IM_COL32(150, 180, 210, 220), "TERRITORY: OFF");
        }

        ImVec2 barMin(btnMax.x + 8.0f, screenMin.y + 7.0f);
        ImVec2 barMax(screenMax.x - 6.0f, screenMin.y + 21.0f);
        float barW = barMax.x - barMin.x;
        drawList->AddRectFilled(barMin, barMax, IM_COL32(15, 20, 28, 230), 2.0f);
        drawList->AddRect(barMin, barMax, IM_COL32(0, 0, 0, 255), 2.0f);
        const auto& minPlayerFac = GetTeamFactionDef(Team::Player);
        const auto& minEnemyFac = GetTeamFactionDef(Team::Enemy);
        float splitX = barMin.x + barW * (pDomPct / 100.0f);
        drawList->AddRectFilled(barMin, ImVec2(splitX, barMax.y), ToImU32(pFacPrim, 225), 2.0f, ImDrawFlags_RoundCornersLeft);
        drawList->AddRectFilled(ImVec2(splitX, barMin.y), barMax, ToImU32(eFacPrim, 225), 2.0f, ImDrawFlags_RoundCornersRight);
        char domBuf[48]; snprintf(domBuf, sizeof(domBuf), "%.0f%% %s / %.0f%% %s", pDomPct, minPlayerFac.shortCode, eDomPct, minEnemyFac.shortCode);
        ImVec2 tSz = ImGui::CalcTextSize(domBuf);
        drawList->AddText(ImVec2(barMin.x + (barW - tSz.x) * 0.5f, barMin.y + (barMax.y - barMin.y - tSz.y) * 0.5f), IM_COL32(255, 255, 255, 255), domBuf);

        drawList->PopClipRect();

        // 10. Interactive Click & Drag Handling on Minimap
        ImVec2 mousePos = ImGui::GetMousePos();
        bool isHovered = (mousePos.x >= screenMin.x && mousePos.x <= screenMax.x &&
                          mousePos.y >= screenMin.y && mousePos.y <= screenMax.y);

        bool isLeftDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        bool isRightDown = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;

        if (mousePos.x >= btnMin.x && mousePos.x <= btnMax.x && mousePos.y >= btnMin.y && mousePos.y <= btnMax.y) {
            ImGui::SetTooltip("Click to toggle Territory Domination Overlay\nVisualizes team territory zones, outpost control & map progress");
            if (isLeftDown && !m_wasLeftMouseDown) {
                m_showTerritories = !m_showTerritories;
            }
        } else if (isHovered) {
            // Left click or drag: Pan camera
            if (isLeftDown) {
                glm::vec2 targetWorld = MinimapToWorld(mousePos);
                m_cameraTarget.x = targetWorld.x;
                m_cameraTarget.y = targetWorld.y;
            }
            // Right click: Issue Move Order to selected player units!
            if (isRightDown && !m_wasRightMouseDown) {
                glm::vec2 targetWorld = MinimapToWorld(mousePos);
                IssueMoveOrder(m_scene, glm::vec3(targetWorld.x, targetWorld.y, 0.0f));
                m_minimapPingPos = glm::vec3(targetWorld.x, targetWorld.y, 0.0f);
                m_minimapPingTimer = 1.5f;
            }
            glm::vec2 curWorld = MinimapToWorld(mousePos);
            ImGui::SetTooltip("Tactical Grid [%.0f, %.0f]\nLeft-Click: Pan Camera\nRight-Click: Move Selected Units", curWorld.x, curWorld.y);
        }
        m_wasLeftMouseDown = isLeftDown;
        ImGui::End();
    }


    void RTSController::DrawGameOver(ImVec2 viewportPos, ImVec2 viewportSize) {
        float winW = 500.0f;
        float winH = 370.0f;
        ImVec2 winP0(viewportPos.x + (viewportSize.x - winW) * 0.5f, viewportPos.y + (viewportSize.y - winH) * 0.5f);
        ImVec2 winP1(winP0.x + winW, winP0.y + winH);

        ImGui::SetNextWindowPos(winP0, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.08f, 0.14f, 0.90f));

        ImGui::Begin("##AeroGameOver", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const auto& pFac = GetTeamFactionDef(Team::Player);
        const auto& eFac = GetTeamFactionDef(Team::Enemy);

        ImU32 accentCol = m_playerWon ? IM_COL32(50, 240, 130, 255) : IM_COL32(255, 75, 75, 255);
        AeroUI::DrawGlassPanel(dl, winP0, winP1, 12.0f,
                               m_playerWon ? IM_COL32(18, 48, 38, 235) : IM_COL32(48, 18, 24, 235),
                               IM_COL32(8, 14, 22, 245),
                               accentCol);

        // Header Bar
        ImVec2 head0(winP0.x + 10.0f, winP0.y + 10.0f);
        ImVec2 head1(winP1.x - 10.0f, winP0.y + 52.0f);
        AeroUI::DrawGlassPanel(dl, head0, head1, 8.0f,
                               m_playerWon ? IM_COL32(24, 70, 52, 240) : IM_COL32(70, 22, 30, 240),
                               IM_COL32(10, 20, 28, 250),
                               accentCol);

        ImFont* font = ImGui::GetFont();
        const char* resTitle = m_playerWon ? "OPERATION COMPLETE: VICTORY" : "OPERATION FAILED: DEFEAT";
        ImVec2 tSz = font ? font->CalcTextSizeA(16.0f, FLT_MAX, -1.0f, resTitle) : ImGui::CalcTextSize(resTitle);
        dl->AddText(font, 16.0f, ImVec2(head0.x + (head1.x - head0.x - tSz.x) * 0.5f, head0.y + (42.0f - tSz.y) * 0.5f),
                    m_playerWon ? IM_COL32(120, 255, 170, 255) : IM_COL32(255, 150, 150, 255), resTitle);
        ImGui::Dummy(ImVec2(0.0f, 46.0f));
        const auto& winFac = m_playerWon ? pFac : eFac;
        const auto& loseFac = m_playerWon ? eFac : pFac;
        ImGui::TextColored(winFac.uiColor, "Victor: [%s] %s", winFac.shortCode, winFac.name);
        ImGui::Text("Defeated: [%s] %s", loseFac.shortCode, loseFac.name);
        const char* gModeName = (m_skirmishSettings.gameMode == 0) ? "Annihilation" :
                                ((m_skirmishSettings.gameMode == 1) ? "Command Center (HQ)" : "Commander Assassination (SupCom FAF)");
        ImGui::Text("Victory Condition: %s", gModeName);
        ImGui::TextWrapped("%s", gameMessage.c_str());
        ImGui::Spacing();
        ImGui::Text("Combat Duration: %.1f seconds", m_matchTime);
        ImGui::Text("Final Reserves: %.0f Mass | %.0f Energy", playerMass, playerEnergy);
        ImGui::Spacing();

        float availW = ImGui::GetContentRegionAvail().x;
        float btnW = std::floor((availW - 16.0f) / 3.0f);
        float btnH = 40.0f;

        if (AeroUI::DrawGlassTileButton("GameOverRestart", "RESTART", "Re-launch", ImVec2(btnW, btnH), true, IM_COL32(0, 240, 255, 255))) {
            StartNewSkirmish();
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (AeroUI::DrawGlassTileButton("GameOverMenu", "MAIN MENU", "Leave Match", ImVec2(btnW, btnH), false, IM_COL32(255, 180, 50, 255))) {
            Cleanup();
            gameOver = false;
            m_playerWon = false;
            m_inMatch = false;
            m_showSkirmishMenu = true;
            m_cameraZoom = 240.0f;
            m_cameraPitch = 45.0f;
            m_cameraYaw = 25.0f;
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (AeroUI::DrawGlassTileButton("GameOverExit", "EXIT", "Desktop", ImVec2(btnW, btnH), false, IM_COL32(255, 80, 80, 255))) {
            SDL_Event quitEv{};
            quitEv.type = SDL_QUIT;
            SDL_PushEvent(&quitEv);
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    void RTSController::DrawSkirmishMenu(ImVec2 viewportPos, ImVec2 viewportSize) {
        float winW = std::min(980.0f, std::max(560.0f, viewportSize.x - 32.0f));
        float winH = std::min(780.0f, std::max(520.0f, viewportSize.y - 32.0f));
        ImVec2 winPos(viewportPos.x + (viewportSize.x - winW) * 0.5f, viewportPos.y + (viewportSize.y - winH) * 0.5f);

        ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.08f, 0.14f, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.75f, 1.0f, 0.55f));

        const char* menuTitle = m_inMatch ? "##AeroSkirmishPause" : "##AeroSkirmishSetup";
        ImGui::Begin(menuTitle, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winP0 = ImGui::GetWindowPos();
        ImVec2 winP1(winP0.x + winW, winP0.y + winH);

        // Main Aero Backdrop Glass with gentle vertical gradient and gloss
        AeroUI::DrawGlassPanel(dl, winP0, winP1, 12.0f,
                               IM_COL32(16, 32, 54, 230),
                               IM_COL32(6, 12, 22, 245),
                               IM_COL32(90, 185, 255, 160));

        // Header Bar (Aero Glass Title Strip)
        float headerH = 42.0f;
        ImVec2 headP0(winP0.x + 10.0f, winP0.y + 10.0f);
        ImVec2 headP1(winP1.x - 10.0f, winP0.y + 10.0f + headerH);
        AeroUI::DrawGlassPanel(dl, headP0, headP1, 8.0f,
                               IM_COL32(28, 58, 92, 235),
                               IM_COL32(12, 26, 44, 245),
                               IM_COL32(120, 210, 255, 200));

        ImFont* font = ImGui::GetFont();
        const char* titleText = m_inMatch ? "TACTICAL COMMAND: GAME PAUSED" : "RUDE RTS: SKIRMISH OPERATIONS";
        float titleSz = 16.0f;
        ImVec2 tSz = font ? font->CalcTextSizeA(titleSz, FLT_MAX, -1.0f, titleText) : ImGui::CalcTextSize(titleText);
        dl->AddText(font, titleSz, ImVec2(headP0.x + 16.0f, headP0.y + (headerH - tSz.y) * 0.5f), IM_COL32(255, 255, 255, 255), titleText);

        const char* subTitle = m_inMatch ? "SupCom FAF Simulation Active" : "Aero Glass Command Interface";
        float subSz = 11.5f;
        ImVec2 sSz = font ? font->CalcTextSizeA(subSz, FLT_MAX, -1.0f, subTitle) : ImGui::CalcTextSize(subTitle);
        dl->AddText(font, subSz, ImVec2(headP1.x - sSz.x - 16.0f, headP0.y + (headerH - sSz.y) * 0.5f), IM_COL32(140, 210, 255, 220), subTitle);
        float footerH = 58.0f;
        ImGui::Dummy(ImVec2(0.0f, headerH + 8.0f));
        ImGui::BeginChild("AeroScrollArea", ImVec2(0.0f, -footerH), false);

        float availW = ImGui::GetContentRegionAvail().x;
        const auto& factions = GetRudeFactions();
        ImTextureID factionAtlasTex = (ImTextureID)(uintptr_t)(m_renderer ? m_renderer->getImGuiTextureID("assets/ui/game_menu/atlasfactions.png") : nullptr);
        if (!factionAtlasTex && m_renderer) {
            factionAtlasTex = (ImTextureID)(uintptr_t)m_renderer->getImGuiTextureID("machinevision/atlasfactions.png");
        }

        // =====================================================================
        // TILE BLOCK 1: FACTION SELECTION (Two 3-column Aero glass grids)
        // =====================================================================
        auto drawFactionAeroGrid = [&](const char* heading, const ImVec4& headCol, FactionId& curFaction, int idBase, bool isEnemy) {
            ImGui::TextColored(headCol, "%s", heading);
            ImGui::Spacing();

            float gridSpacing = 8.0f;
            float cardW = std::floor((availW - gridSpacing * 2.0f) / 3.0f);
            float cardH = 74.0f; // Responsive wide Aero glass card

            for (int i = 0; i < RUDE_FACTION_COUNT; ++i) {
                if (i % 3 != 0) ImGui::SameLine(0.0f, gridSpacing);
                const auto& f = factions[i];
                bool selected = (curFaction == f.id);
                FactionAtlasUV uv = GetFactionAtlasUV(f.id);

                ImGui::PushID(idBase + i);
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1(p0.x + cardW, p0.y + cardH);

                if (ImGui::InvisibleButton("FactionAeroTile", ImVec2(cardW, cardH))) {
                    curFaction = f.id;
                }
                bool hovered = ImGui::IsItemHovered();

                // Draw Glass Tile Backing
                ImU32 tileTop = selected ? IM_COL32(26, 60, 95, 240) : (hovered ? IM_COL32(22, 45, 70, 220) : IM_COL32(12, 22, 36, 195));
                ImU32 tileBot = selected ? IM_COL32(14, 34, 56, 250) : (hovered ? IM_COL32(10, 26, 42, 235) : IM_COL32(8, 14, 24, 225));
                ImU32 borderC = selected ? ToImU32(f.primaryColor, 255)
                                         : (hovered ? IM_COL32(180, 230, 255, 200) : ToImU32(f.primaryColor, 120));

                AeroUI::DrawGlassPanel(dl, p0, p1, 7.0f, tileTop, tileBot, borderC);

                // Left thumbnail slot: 48x62 aspect from atlasfactions.png
                float thumbW = 46.0f;
                float thumbH = cardH - 12.0f;
                ImVec2 th0(p0.x + 6.0f, p0.y + 6.0f);
                ImVec2 th1(th0.x + thumbW, th0.y + thumbH);

                if (factionAtlasTex) {
                    dl->AddImage(factionAtlasTex, th0, th1, uv.uv0, uv.uv1);
                } else {
                    dl->AddRectFilled(th0, th1, ToImU32(f.primaryColor, 240), 4.0f);
                }
                // Glass gloss bevel around thumbnail
                dl->AddRect(th0, th1, IM_COL32(255, 255, 255, 90), 4.0f);

                // Right side: Faction title & short tagline
                float textLeft = th1.x + 10.0f;
                char nameBuf[48];
                snprintf(nameBuf, sizeof(nameBuf), "[%s] %s", f.shortCode, f.name);
                dl->AddText(font, 13.0f, ImVec2(textLeft, p0.y + 8.0f),
                            selected ? IM_COL32(255, 255, 255, 255) : (hovered ? IM_COL32(230, 245, 255, 240) : IM_COL32(190, 215, 235, 220)),
                            nameBuf);

                dl->AddText(font, 10.5f, ImVec2(textLeft, p0.y + 26.0f),
                            selected ? IM_COL32(140, 225, 255, 230) : IM_COL32(130, 165, 195, 190),
                            f.archetype);

                // Selected glowing pill indicator
                if (selected) {
                    dl->AddRectFilled(ImVec2(p0.x + 4.0f, p1.y - 3.5f), ImVec2(p1.x - 4.0f, p1.y - 1.5f), ToImU32(f.primaryColor, 255), 1.0f);
                }

                ImGui::PopID();
            }

            // Live summary strip for active selection
            const auto& selDef = GetFactionDef(curFaction);
            ImVec2 strip0 = ImGui::GetCursorScreenPos();
            ImVec2 strip1(strip0.x + availW, strip0.y + 36.0f);
            AeroUI::DrawGlassPanel(dl, strip0, strip1, 6.0f,
                                   IM_COL32(18, 38, 62, 210),
                                   IM_COL32(10, 20, 34, 230),
                                   ToImU32(selDef.primaryColor, 180));
            char summaryBuf[128];
            snprintf(summaryBuf, sizeof(summaryBuf), "%s Archetype", selDef.name);
            dl->AddText(font, 12.0f, ImVec2(strip0.x + 10.0f, strip0.y + 4.0f), ToImU32(selDef.primaryColor, 255), summaryBuf);
            dl->AddText(font, 11.0f, ImVec2(strip0.x + 10.0f, strip0.y + 19.0f), IM_COL32(210, 225, 240, 230), selDef.perkSummary);
            ImGui::Dummy(ImVec2(0.0f, 42.0f));
        };

        drawFactionAeroGrid("PLAYER FACTION", ImVec4(0.40f, 0.85f, 1.0f, 1.0f), m_skirmishSettings.playerFaction, 100, false);
        drawFactionAeroGrid("ENEMY AI FACTION", ImVec4(1.0f, 0.65f, 0.35f, 1.0f), m_skirmishSettings.enemyFaction, 200, true);

        if (m_skirmishSettings.playerFaction == m_skirmishSettings.enemyFaction) {
            ImGui::TextDisabled("Mirror Match: Rival forces automatically tint-shifted for combat contrast.");
            ImGui::Spacing();
        }

        // =====================================================================
        // TILE BLOCK 2: GAMEPLAY TILES (Game Mode, AI Aggression, Starting Mass)
        // =====================================================================
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "VICTORY CONDITION");
        ImGui::Spacing();
        float modeW = std::floor((availW - 16.0f) / 3.0f);
        if (AeroUI::DrawGlassTileButton("ModeAssass", "COMMANDER ASSASSIN", "FAF ACU Death", ImVec2(modeW, 40.0f), m_skirmishSettings.gameMode == 2)) {
            m_skirmishSettings.gameMode = 2;
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (AeroUI::DrawGlassTileButton("ModeAnnihil", "ANNIHILATION", "Total Destruction", ImVec2(modeW, 40.0f), m_skirmishSettings.gameMode == 0)) {
            m_skirmishSettings.gameMode = 0;
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (AeroUI::DrawGlassTileButton("ModeHQ", "COMMAND CENTER", "HQ Assassination", ImVec2(modeW, 40.0f), m_skirmishSettings.gameMode == 1)) {
            m_skirmishSettings.gameMode = 1;
        }
        ImGui::Spacing();

        // Judgment difficulty and starting economy are independent of doctrine.
        float halfW = std::floor((availW - 12.0f) * 0.5f);
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "AI DIFFICULTY");
        ImGui::Spacing();
        float diffW = std::floor((halfW - 12.0f) / 3.0f);
        if (AeroUI::DrawGlassTileButton("DiffEasy", "EASY", "Deliberate", ImVec2(diffW, 36.0f), m_skirmishSettings.aiDifficulty == 0, IM_COL32(80, 220, 140, 255))) {
            m_skirmishSettings.aiDifficulty = 0;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (AeroUI::DrawGlassTileButton("DiffNorm", "NORMAL", "Adaptive", ImVec2(diffW, 36.0f), m_skirmishSettings.aiDifficulty == 1, IM_COL32(0, 200, 255, 255))) {
            m_skirmishSettings.aiDifficulty = 1;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (AeroUI::DrawGlassTileButton("DiffBrut", "BRUTAL", "Disciplined", ImVec2(diffW, 36.0f), m_skirmishSettings.aiDifficulty == 2, IM_COL32(255, 90, 80, 255))) {
            m_skirmishSettings.aiDifficulty = 2;
        }
        ImGui::EndGroup();

        ImGui::SameLine(0.0f, 12.0f);

        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "STARTING RESOURCES");
        ImGui::Spacing();
        float econW = std::floor((halfW - 18.0f) / 4.0f);
        if (AeroUI::DrawGlassTileButton("EconLow", "200", "Low", ImVec2(econW, 36.0f), m_skirmishSettings.startingMass == 200)) {
            m_skirmishSettings.startingMass = 200; m_skirmishSettings.startingEnergy = 200;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (AeroUI::DrawGlassTileButton("EconStd", "500", "Standard", ImVec2(econW, 36.0f), m_skirmishSettings.startingMass == 500)) {
            m_skirmishSettings.startingMass = 500; m_skirmishSettings.startingEnergy = 500;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (AeroUI::DrawGlassTileButton("EconHigh", "1000", "High", ImVec2(econW, 36.0f), m_skirmishSettings.startingMass == 1000)) {
            m_skirmishSettings.startingMass = 1000; m_skirmishSettings.startingEnergy = 1000;
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (AeroUI::DrawGlassTileButton("EconRich", "5000", "Rich", ImVec2(econW, 36.0f), m_skirmishSettings.startingMass == 5000)) {
            m_skirmishSettings.startingMass = 5000; m_skirmishSettings.startingEnergy = 5000;
        }
        ImGui::EndGroup();

        const char* doctrines[] = { "Balanced buildup", "Aggressive maneuver", "Defensive bastion" };
        ImGui::SetNextItemWidth(230.0f);
        ImGui::Combo("AI Doctrine (next match)", &m_skirmishSettings.aiPersonality, doctrines, 3);
        ImGui::TextDisabled("Shared build costs and times. Doctrine never changes when selecting a faction.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // =====================================================================
        // TILE BLOCK 3: ENVIRONMENT, MAP & BATTLEFIELD RULES
        // =====================================================================
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "BATTLEFIELD MAP & SIMULATION");
        ImGui::Spacing();

        ImVec2 envP0 = ImGui::GetCursorScreenPos();
        ImVec2 envP1(envP0.x + availW, envP0.y + 118.0f);
        AeroUI::DrawGlassPanel(dl, envP0, envP1, 8.0f,
                               IM_COL32(16, 30, 48, 190),
                               IM_COL32(8, 16, 26, 215),
                               IM_COL32(70, 140, 200, 120));

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
        ImGui::BeginGroup();
        if (!m_availableCustomMaps.empty()) {
            std::vector<const char*> mapNames;
            for (const auto& s : m_availableCustomMaps) mapNames.push_back(s.c_str());
            m_selectedCustomMapIndex = std::clamp(m_selectedCustomMapIndex, 0, (int)mapNames.size() - 1);
            ImGui::SetNextItemWidth(availW * 0.45f);
            if (ImGui::Combo("Map", &m_selectedCustomMapIndex, mapNames.data(), (int)mapNames.size())) {
                LoadCustomMap(m_availableCustomMaps[m_selectedCustomMapIndex]);
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (ImGui::Button("Scan Maps", ImVec2(100.0f, 22.0f))) {
                RefreshAvailableCustomMaps();
            }
        } else {
            ImGui::TextDisabled("Default Terrain Active (Create maps in Forge Mode!)");
            ImGui::SameLine(0.0f, 8.0f);
            if (ImGui::Button("Scan Maps", ImVec2(100.0f, 22.0f))) {
                RefreshAvailableCustomMaps();
            }
        }

        const char* noRushOpts[] = { "Off (No Rush)", "5 Mins", "10 Mins", "15 Mins", "20 Mins" };
        int noRushVals[] = { 0, 5, 10, 15, 20 };
        int selNoRush = 0;
        for (int i = 0; i < 5; i++) if (m_skirmishSettings.noRushMinutes == noRushVals[i]) selNoRush = i;
        ImGui::SetNextItemWidth(availW * 0.35f);
        if (ImGui::Combo("No Rush Buildup", &selNoRush, noRushOpts, 5)) {
            m_skirmishSettings.noRushMinutes = noRushVals[selNoRush];
        }

        if (ImGui::Checkbox("Dynamic Fog of War & Intel", &m_skirmishSettings.enableFogOfWar)) {
            if (m_scene) m_scene->environment.enableFog = m_skirmishSettings.enableFogOfWar;
        }
        ImGui::SameLine(0.0f, 18.0f);
        if (ImGui::Checkbox("Valley Water Plane", &m_enableWater)) {
            UpdateWaterPlane();
        }
        if (m_enableWater) {
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::SliderFloat("Water Level", &m_waterHeight, -10.0f, 60.0f, "%.1fm")) {
                UpdateWaterPlane();
            }
        }
        ImGui::EndGroup();

        ImGui::Spacing();
        ImGui::EndChild(); // Closes AeroScrollArea

        // =====================================================================
        // AERO GLASS FOOTER ACTION BAR
        // =====================================================================
        ImVec2 footP0(winP0.x + 10.0f, winP1.y - footerH + 4.0f);
        ImVec2 footP1(winP1.x - 10.0f, winP1.y - 10.0f);
        AeroUI::DrawGlassPanel(dl, footP0, footP1, 8.0f,
                               IM_COL32(20, 42, 68, 240),
                               IM_COL32(10, 20, 34, 250),
                               IM_COL32(100, 190, 255, 180));

        float barAvailW = footP1.x - footP0.x - 16.0f;
        float btnH = footP1.y - footP0.y - 12.0f;
        ImGui::SetCursorPos(ImVec2(18.0f, winH - footerH + 10.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));

        if (m_inMatch && !gameOver) {
            float subBtnW = std::floor((barAvailW - 24.0f) / 4.0f);
            if (AeroUI::DrawGlassTileButton("AeroResume", "RESUME MATCH", "Return to Game", ImVec2(subBtnW, btnH), false, IM_COL32(0, 240, 255, 255))) {
                m_showSkirmishMenu = false;
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (AeroUI::DrawGlassTileButton("AeroRestart", "RESTART", "Re-deploy Match", ImVec2(subBtnW, btnH), true, IM_COL32(50, 220, 120, 255))) {
                m_inMatch = true;
                m_showSkirmishMenu = false;
                StartNewSkirmish();
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (AeroUI::DrawGlassTileButton("AeroLeave", "MAIN MENU", "Leave Battle", ImVec2(subBtnW, btnH), false, IM_COL32(255, 175, 50, 255))) {
                Cleanup();
                m_inMatch = false;
                m_showSkirmishMenu = true;
                m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
                m_cameraZoom = 240.0f;
                m_cameraPitch = 45.0f;
                m_cameraYaw = 25.0f;
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (AeroUI::DrawGlassTileButton("AeroExit", "QUIT", "Desktop", ImVec2(subBtnW, btnH), false, IM_COL32(255, 75, 75, 255))) {
                SDL_Event quitEv{};
                quitEv.type = SDL_QUIT;
                SDL_PushEvent(&quitEv);
            }
        } else {
            float subBtnW = std::floor((barAvailW - 24.0f) / 4.0f);
            float launchW = subBtnW * 1.55f;
            float otherW = std::floor((barAvailW - launchW - 24.0f) / 3.0f);

            if (AeroUI::DrawGlassTileButton("AeroLaunch", "LAUNCH SKIRMISH", "Deploy to Battlefield", ImVec2(launchW, btnH), true, IM_COL32(0, 255, 180, 255))) {
                m_inMatch = true;
                m_showSkirmishMenu = false;
                StartNewSkirmish();
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (AeroUI::DrawGlassTileButton("AeroForge", "FORGE SCULPTOR", "Create Maps", ImVec2(otherW, btnH), false, IM_COL32(60, 180, 255, 255))) {
                m_inForgeMode = true;
                m_showSkirmishMenu = false;
                m_cameraTarget = glm::vec3(0.0f, 0.0f, 20.0f);
                m_cameraZoom = 180.0f;
                m_cameraPitch = 50.0f;
                gameMessage = "Forge Mode active: Sculpt mountains and valleys with Left-Click!";
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (AeroUI::DrawGlassTileButton("AeroSettings", "SETTINGS", "Audio & Video", ImVec2(otherW, btnH), false, IM_COL32(180, 200, 225, 255))) {
                m_showSettingsMenu = true;
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (AeroUI::DrawGlassTileButton("AeroExit", "EXIT", "Quit Game", ImVec2(otherW, btnH), false, IM_COL32(255, 80, 80, 255))) {
                SDL_Event quitEv{};
                quitEv.type = SDL_QUIT;
                SDL_PushEvent(&quitEv);
            }
        }

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    void RTSController::Cleanup() {
        m_units.clear();
        ClearAllTrees();
        m_projectiles.clear();
        m_deadUnitEntities.clear();
        m_explosionSpheres.clear();
        m_resourceNodes.clear();
        m_buildGhostEntity = nullptr;
        m_buildBeamEntity = nullptr;
        m_playerRallyEntity = nullptr;
        m_enemyRallyEntity = nullptr;
        m_waterEntity = nullptr;
        m_terrainEntity = nullptr;
    }
}
