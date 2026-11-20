#include "RTSController.hpp"

#include <algorithm>
#include <cmath>

namespace Crescendo {
namespace {
float Distance2D(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec2(a - b));
}

bool IsBuildingType(UnitType type) {
    return type == UnitType::LandFactory || type == UnitType::AirFactory || type == UnitType::Turret ||
           type == UnitType::AATurret || type == UnitType::MassGen || type == UnitType::EnergyGen ||
           type == UnitType::ShieldGen || type == UnitType::MassStorage || type == UnitType::EnergyStorage ||
           type == UnitType::NukeSilo;
}

float Footprint(UnitType type) {
    switch (type) {
        case UnitType::LandFactory:
        case UnitType::AirFactory: return 12.0f;
        case UnitType::NukeSilo: return 10.0f;
        case UnitType::ShieldGen: return 8.0f;
        case UnitType::MassGen:
        case UnitType::EnergyGen:
        case UnitType::MassStorage:
        case UnitType::EnergyStorage: return 7.0f;
        default: return 5.0f;
    }
}

float SpawnHeight(UnitType type) {
    switch (type) {
        case UnitType::MassGen: return 1.5f;
        case UnitType::EnergyGen: return 2.0f;
        case UnitType::Turret: return 3.0f;
        case UnitType::AATurret: return 2.25f;
        case UnitType::ShieldGen: return 1.0f;
        case UnitType::EnergyStorage: return 1.9f;
        case UnitType::LandFactory:
        case UnitType::AirFactory: return 0.0f;
        default: return 1.75f;
    }
}
}

bool RTSController::HasConstructionTask(int builderEntityIndex) const {
    return std::any_of(m_constructionTasks.begin(), m_constructionTasks.end(), [&](const EngineerBuildTask& task) {
        return task.engineerEntityIndex == builderEntityIndex;
    });
}

bool RTSController::IsConstructionSiteValid(UnitType type, const glm::vec3& position, int builderEntityIndex) const {
    if (!IsBuildingType(type) || std::abs(position.x) > 455.0f || std::abs(position.y) > 455.0f) return false;
    if (GetTerrainNormal(position.x, position.y).z < 0.82f) return false;
    if (m_enableWater && GetTerrainHeight(position.x, position.y) <= m_waterHeight + 0.5f) return false;

    const float radius = Footprint(type);
    for (const auto& unit : m_units) {
        if (unit.health <= 0.0f || unit.entityIndex == builderEntityIndex) continue;
        const bool structure = unit.type == UnitType::Factory || IsBuildingType(unit.type);
        if (structure && Distance2D(unit.position, position) < radius + Footprint(unit.type)) return false;
    }
    for (const auto& task : m_constructionTasks) {
        if (task.engineerEntityIndex != builderEntityIndex &&
            Distance2D(task.buildPos, position) < radius + Footprint(task.buildType)) return false;
    }
    for (const auto& tree : m_trees) {
        if (Distance2D(tree.position, position) < radius + 1.5f) return false;
    }
    return true;
}

bool RTSController::TryStartConstruction(UnitType type, Team team, int builderEntityIndex, glm::vec3 position) {
    if (gameOver || HasConstructionTask(builderEntityIndex) || !IsBuildingType(type)) return false;
    RTSUnit* builder = nullptr;
    for (auto& unit : m_units) {
        if (unit.entityIndex == builderEntityIndex && unit.team == team && unit.health > 0.0f &&
            (unit.type == UnitType::Engineer || unit.type == UnitType::CommandMech)) {
            builder = &unit;
            break;
        }
    }
    if (!builder || !IsConstructionSiteValid(type, position, builderEntityIndex)) return false;

    int nodeIndex = -1;
    if (type == UnitType::MassGen || type == UnitType::EnergyGen) {
        const ResourceNodeType required = type == UnitType::MassGen ? ResourceNodeType::Mass : ResourceNodeType::Energy;
        float closest = 5.0f;
        for (size_t i = 0; i < m_resourceNodes.size(); ++i) {
            const auto& node = m_resourceNodes[i];
            const float distance = Distance2D(position, node.position);
            if (node.type == required && !node.occupied && node.reservedBy == -1 && distance < closest) {
                closest = distance;
                nodeIndex = static_cast<int>(i);
            }
        }
        if (nodeIndex < 0) return false;
        position = m_resourceNodes[nodeIndex].position;
    }

    int massCost = 0, energyCost = 0;
    float buildTime = 0.0f;
    GetUnitBuildCost(type, builder->faction, massCost, energyCost, buildTime);
    float& mass = team == Team::Player ? playerMass : enemyMass;
    float& energy = team == Team::Player ? playerEnergy : enemyEnergy;
    if (mass < massCost || energy < energyCost) return false;
    mass -= massCost;
    energy -= energyCost;

    EngineerBuildTask task;
    task.team = team;
    task.buildType = type;
    task.buildPos = position;
    task.engineerEntityIndex = builderEntityIndex;
    task.totalTime = buildTime;
    task.massCost = massCost;
    task.energyCost = energyCost;
    task.nodeIndex = nodeIndex;
    if (nodeIndex >= 0) m_resourceNodes[nodeIndex].reservedBy = builderEntityIndex;
    m_constructionTasks.push_back(task);
    builder->targetPosition = position;
    builder->hasTarget = true;
    builder->fleeing = false;
    return true;
}

void RTSController::CancelConstruction(int builderEntityIndex) {
    for (auto it = m_constructionTasks.begin(); it != m_constructionTasks.end();) {
        if (it->engineerEntityIndex != builderEntityIndex) {
            ++it;
            continue;
        }
        const float unfinished = 1.0f - std::clamp(it->progress / std::max(0.001f, it->totalTime), 0.0f, 1.0f);
        float& mass = it->team == Team::Player ? playerMass : enemyMass;
        float& energy = it->team == Team::Player ? playerEnergy : enemyEnergy;
        mass += std::floor(it->massCost * unfinished);
        energy += std::floor(it->energyCost * unfinished);
        if (it->nodeIndex >= 0 && it->nodeIndex < static_cast<int>(m_resourceNodes.size()) &&
            m_resourceNodes[it->nodeIndex].reservedBy == builderEntityIndex) {
            m_resourceNodes[it->nodeIndex].reservedBy = -1;
        }
        it = m_constructionTasks.erase(it);
    }
}

void RTSController::UpdateConstruction(float deltaTime, AudioServer* audioServer) {
    // Resource occupancy follows living structures, allowing destroyed deposits to be reclaimed.
    for (auto& node : m_resourceNodes) node.occupied = false;
    for (const auto& unit : m_units) {
        if (unit.health <= 0.0f || (unit.type != UnitType::MassGen && unit.type != UnitType::EnergyGen)) continue;
        for (auto& node : m_resourceNodes) {
            if (Distance2D(unit.position, node.position) < 5.0f) node.occupied = true;
        }
    }

    bool playerBeamShown = false;
    for (auto it = m_constructionTasks.begin(); it != m_constructionTasks.end();) {
        RTSUnit* builder = nullptr;
        for (auto& unit : m_units) {
            if (unit.entityIndex == it->engineerEntityIndex && unit.team == it->team && unit.health > 0.0f) {
                builder = &unit;
                break;
            }
        }
        if (!builder || !IsConstructionSiteValid(it->buildType, it->buildPos, it->engineerEntityIndex)) {
            const int id = it->engineerEntityIndex;
            ++it;
            CancelConstruction(id);
            it = m_constructionTasks.begin();
            continue;
        }
        if (builder->fleeing) {
            it->constructing = false;
            ++it;
            continue;
        }
        const float distance = Distance2D(builder->position, it->buildPos);
        if (distance > 7.5f) {
            it->constructing = false;
            if (!builder->hasTarget) {
                builder->targetPosition = it->buildPos;
                builder->hasTarget = true;
            }
            ++it;
            continue;
        }
        if (!it->constructing && it->team == Team::Player && audioServer) audioServer->PlayOneShot("assets/audio/beam.wav", 0.65f);
        it->constructing = true;
        builder->hasTarget = false;
        const float boost = it->team == Team::Player && m_boostTimer > 0.0f ? 2.0f : 1.0f;
        it->progress += deltaTime * boost * GetFactionDef(builder->faction).mods.buildSpeedMult;
        it->sparkTimer += deltaTime;
        if (it->sparkTimer >= 0.7f) {
            it->sparkTimer = 0.0f;
            SpawnExplosionCluster(it->buildPos + glm::vec3(0.0f, 0.0f, 0.4f), 0.35f, 2);
        }
        if (it->team == Team::Player && !playerBeamShown && m_buildBeamEntity) {
            const glm::vec3 p1 = builder->position + glm::vec3(0.0f, 0.0f, builder->type == UnitType::CommandMech ? 2.5f : 0.8f);
            const glm::vec3 p2 = it->buildPos + glm::vec3(0.0f, 0.0f, 1.2f);
            const glm::vec3 diff = p2 - p1;
            const float length = glm::length(diff);
            if (length > 0.1f) {
                const glm::vec3 direction = diff / length;
                m_buildBeamEntity->angles = glm::vec3(glm::degrees(std::acos(std::clamp(direction.z, -1.0f, 1.0f))), 0.0f,
                    glm::degrees(std::atan2(direction.x, -direction.y)));
                m_buildBeamEntity->origin = glm::dvec3((p1 + p2) * 0.5f);
                m_buildBeamEntity->scale = glm::vec3(0.22f, 0.22f, length);
                playerBeamShown = true;
            }
        }
        if (it->progress < it->totalTime) {
            if (it->team == Team::Player) {
                const int percent = static_cast<int>(it->progress / it->totalTime * 100.0f);
                gameMessage = "Construction " + std::to_string(percent) + "%";
            }
            ++it;
            continue;
        }

        const EngineerBuildTask completed = *it;
        if (completed.nodeIndex >= 0 && completed.nodeIndex < static_cast<int>(m_resourceNodes.size())) {
            m_resourceNodes[completed.nodeIndex].reservedBy = -1;
            m_resourceNodes[completed.nodeIndex].occupied = true;
        }
        m_pendingSpawns.push_back({completed.buildType, completed.team, builder->faction,
            glm::vec3(completed.buildPos.x, completed.buildPos.y, SpawnHeight(completed.buildType))});
        SpawnExplosionCluster(completed.buildPos, 1.0f, 6);
        it = m_constructionTasks.erase(it);
        if (completed.team == Team::Player) gameMessage = "Construction complete and operational.";
    }
    if (m_buildBeamEntity && !playerBeamShown) {
        m_buildBeamEntity->origin = glm::dvec3(0.0, 0.0, -999.0);
        m_buildBeamEntity->scale = glm::vec3(0.0f);
    }
}

} // namespace Crescendo
