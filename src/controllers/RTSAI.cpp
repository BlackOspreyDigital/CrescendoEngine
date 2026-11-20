#include "RTSController.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Crescendo {
namespace {
    float Distance2D(const glm::vec3& a, const glm::vec3& b) {
        return glm::length(glm::vec2(a - b));
    }

    bool IsArmy(UnitType type) {
        return type == UnitType::Tank || type == UnitType::Plane || type == UnitType::SupportMech;
    }

    bool IsStructure(UnitType type) {
        return !IsArmy(type) && type != UnitType::Engineer && type != UnitType::CommandMech;
    }

    float CombatPower(const RTSUnit& unit) {
        float power = 0.0f;
        switch (unit.type) {
            case UnitType::Tank: power = 1.0f; break;
            case UnitType::Plane: power = 1.2f; break;
            case UnitType::SupportMech: power = 2.5f; break;
            case UnitType::CommandMech: power = 6.0f; break;
            case UnitType::Turret: power = 2.0f; break;
            case UnitType::AATurret: power = 1.5f; break;
            default: break;
        }
        return power * std::clamp(unit.health / unit.maxHealth, 0.0f, 1.0f);
    }
}

void RTSController::ResetAI() {
    m_aiPlanner.Reset(m_skirmishSettings.aiSeed, m_skirmishSettings.aiPersonality,
        m_skirmishSettings.aiDifficulty, static_cast<int>(GetTeamFaction(Team::Enemy)));
    m_aiWorld = AIWorldState{};
    m_aiContacts.clear();
    m_aiContacts.reserve(128);
    m_aiTimer = 0.0f;
    m_aiStableEconomy = 0.0f;
    m_aiLastAttack = -1000.0f;
    m_aiThreatSince = -1.0f;
    m_aiRaidActive = false;
    m_aiHasObjective = false;
    for (const auto& unit : m_units) {
        if (unit.team == Team::Enemy && unit.type == UnitType::Factory) {
            m_aiHome = unit.position;
            break;
        }
    }
    // The map center and opposing start region are public map information, not live enemy intel.
    glm::vec2 toCenter = -glm::vec2(m_aiHome);
    m_aiForward = glm::vec3(glm::length(toCenter) > 0.001f ? glm::normalize(toCenter) : glm::vec2(-1.0f, 0.0f), 0.0f);
    m_aiObjective = -m_aiHome;
    m_aiObjective.z = GetTerrainHeight(m_aiObjective.x, m_aiObjective.y);
    m_aiStaging = m_aiHome + m_aiForward * 65.0f;
    m_aiExpansion = m_aiHome;
    m_aiChoke = m_aiStaging;
    m_aiHasChoke = false;
    float bestNarrowness = 8.0f;
    const glm::vec3 side(-m_aiForward.y, m_aiForward.x, 0.0f);
    for (int step = 2; step <= 7; ++step) {
        const glm::vec3 p = m_aiHome + m_aiForward * (20.0f * step);
        const float center = GetTerrainHeight(p.x, p.y);
        const glm::vec3 left = p + side * 35.0f;
        const glm::vec3 right = p - side * 35.0f;
        const float narrowness = std::min(GetTerrainHeight(left.x, left.y), GetTerrainHeight(right.x, right.y)) - center;
        if (narrowness > bestNarrowness && (!m_enableWater || center > m_waterHeight + 0.5f) && GetTerrainNormal(p.x, p.y).z > 0.85f) {
            bestNarrowness = narrowness;
            m_aiChoke = p;
            m_aiHasChoke = true;
        }
    }
}

bool RTSController::AIPositionSafe(const glm::vec3& position) const {
    // Do not blindly expand into the opposing half or a recently observed enemy weapon envelope.
    if (glm::dot(glm::vec2(position), glm::vec2(m_aiForward)) > -20.0f) return false;
    if (m_enableWater && GetTerrainHeight(position.x, position.y) <= m_waterHeight + 0.5f) return false;
    for (const auto& contact : m_aiContacts) {
        if (contact.power > 0.0f && Distance2D(contact.position, position) < 85.0f) return false;
    }
    return true;
}

void RTSController::UpdateAI(float deltaTime) {
    if (gameOver || !m_inMatch) return;
    m_aiTimer += deltaTime;
    if (m_aiTimer < 1.0f) return;
    const float elapsed = m_aiTimer;
    m_aiTimer = 0.0f;
    AIWorldState world;
    world.time = m_matchTime;
    world.mass = enemyMass;
    world.energy = enemyEnergy;
    world.noRush = m_noRushTimer > 0.0f;
    world.chokeAvailable = m_aiHasChoke;
    const auto& previous = m_aiPlanner.Decision();
    const float intelLifetime = std::max(10.0f, previous.intelLifetime);
    for (auto& contact : m_aiContacts) contact.visible = false;

    // Contacts are created only by living enemy units' vision, independently of player fog rendering.
    for (const auto& target : m_units) {
        if (target.team != Team::Player || target.health <= 0.0f) continue;
        bool visible = !m_skirmishSettings.enableFogOfWar;
        for (const auto& observer : m_units) {
            if (visible) break;
            if (observer.team != Team::Enemy || observer.health <= 0.0f) continue;
            const float radius = (observer.type == UnitType::Plane ? 95.0f : 65.0f) * GetFactionDef(observer.faction).mods.visionRangeMult;
            visible = Distance2D(observer.position, target.position) <= radius;
        }
        if (!visible) continue;
        auto found = std::find_if(m_aiContacts.begin(), m_aiContacts.end(), [&](const AIContact& contact) {
            return contact.entityIndex == target.entityIndex;
        });
        if (found == m_aiContacts.end()) {
            m_aiContacts.push_back({});
            found = m_aiContacts.end() - 1;
        }
        *found = {target.entityIndex, target.type, target.position, m_matchTime, CombatPower(target), true};
    }
    m_aiContacts.erase(std::remove_if(m_aiContacts.begin(), m_aiContacts.end(), [&](const AIContact& contact) {
        if (m_matchTime - contact.lastSeen > intelLifetime) return true;
        if (contact.visible) return false;
        // Forget a destroyed/moved contact if its last position can now be observed empty.
        for (const auto& observer : m_units) {
            if (observer.team == Team::Enemy && observer.health > 0.0f && Distance2D(observer.position, contact.position) < 55.0f) return true;
        }
        return false;
    }), m_aiContacts.end());

    float totalHealth = 0.0f, maxHealth = 0.0f;
    int staged = 0, assembly = 0;
    int activeShields = 0;
    float farthestExpansion = 0.0f;
    m_aiExpansion = m_aiHome;
    for (const auto& unit : m_units) {
        if (unit.team != Team::Enemy || unit.health <= 0.0f) continue;
        switch (unit.type) {
            case UnitType::Tank: ++world.tanks; break;
            case UnitType::Plane: ++world.planes; break;
            case UnitType::Engineer: ++world.engineers; break;
            case UnitType::MassGen: ++world.massGenerators; break;
            case UnitType::EnergyGen: ++world.energyGenerators; break;
            case UnitType::LandFactory: ++world.landFactories; break;
            case UnitType::AirFactory: ++world.airFactories; break;
            case UnitType::Turret: ++world.turrets; break;
            case UnitType::AATurret: ++world.aaTurrets; break;
            case UnitType::ShieldGen: ++world.shields; activeShields += unit.isShieldActive ? 1 : 0; break;
            default: break;
        }
        if ((unit.type == UnitType::MassGen || unit.type == UnitType::EnergyGen) && AIPositionSafe(unit.position)) {
            const float dist = Distance2D(unit.position, m_aiHome);
            if (dist > farthestExpansion) { farthestExpansion = dist; m_aiExpansion = unit.position; }
        }
        if (IsArmy(unit.type)) {
            const float power = CombatPower(unit);
            world.armyPower += power;
            totalHealth += unit.health;
            maxHealth += unit.maxHealth;
            if (unit.aiRole == AISquadRole::Reserve && Distance2D(unit.position, m_aiHome) < 110.0f) world.reservePower += power;
            if (unit.aiRole == AISquadRole::Assault) {
                ++assembly;
                if (Distance2D(unit.position, m_aiStaging) <= 40.0f) ++staged;
            }
        }
        if (Distance2D(unit.position, m_aiHome) <= 160.0f) {
            if (unit.type == UnitType::Turret) world.homeDefenseScore += 2.0f;
            if (unit.type == UnitType::AATurret) world.homeDefenseScore += 1.5f;
            if (unit.type == UnitType::ShieldGen && unit.isShieldActive) world.homeDefenseScore += 2.0f;
        }
    }
    const auto& modifiers = GetTeamFactionDef(Team::Enemy).mods;
    world.massIncome = (2.0f + world.massGenerators * 4.0f) * modifiers.massIncomeMult;
    world.energyIncome = (4.0f + world.energyGenerators * 10.0f) * modifiers.energyIncomeMult - activeShields * 20.0f;
    m_aiStableEconomy = world.massIncome >= 6.0f && world.energyIncome > 0.0f ? m_aiStableEconomy + elapsed : 0.0f;
    world.stableEconomySeconds = m_aiStableEconomy;
    world.armyHealthFraction = maxHealth > 0.0f ? totalHealth / maxHealth : 1.0f;
    world.stagedFraction = assembly > 0 ? static_cast<float>(staged) / assembly : 0.0f;
    world.intelAge = 10000.0f;
    float closestThreat = std::numeric_limits<float>::max();
    m_aiThreatPosition = m_aiHome;
    bool rawThreat = false;
    for (const auto& contact : m_aiContacts) {
        world.intelAge = std::min(world.intelAge, m_matchTime - contact.lastSeen);
        world.knownEnemyPower += contact.power;
        if (contact.type == UnitType::Plane) world.observedAirPower += contact.power;
        if (IsStructure(contact.type)) {
            world.targetKnown = true;
            if (contact.type == UnitType::Factory || !m_aiHasObjective) {
                m_aiObjective = contact.position;
                m_aiHasObjective = true;
            }
        }
        if (!contact.visible) continue;
        for (const auto& unit : m_units) {
            if (unit.team != Team::Enemy || unit.health <= 0.0f) continue;
            if (unit.aiRole == AISquadRole::Scout && Distance2D(unit.position, contact.position) < 100.0f) world.scoutContact = true;
            if ((IsStructure(unit.type) || unit.type == UnitType::Engineer) && contact.power > 0.0f && Distance2D(unit.position, contact.position) < 80.0f) {
                rawThreat = true;
                world.visibleThreatPower += contact.power;
                const float dist = Distance2D(contact.position, m_aiHome);
                if (dist < closestThreat) { closestThreat = dist; m_aiThreatPosition = contact.position; }
                break;
            }
        }
    }
    if (rawThreat) {
        if (m_aiThreatSince < 0.0f) m_aiThreatSince = m_matchTime;
        world.underAttack = m_matchTime - m_aiThreatSince >= previous.reactionDelay;
        m_aiLastAttack = m_matchTime;
    } else m_aiThreatSince = -1.0f;
    world.recentAttack = m_matchTime - m_aiLastAttack < 60.0f;
    for (const auto& node : m_resourceNodes) {
        if (!node.occupied && node.reservedBy == -1 && AIPositionSafe(node.position)) {
            world.safeExpansionAvailable = true;
            break;
        }
    }
    m_aiWorld = world;
    const auto& decision = m_aiPlanner.Update(m_aiWorld);
    AssignAISquads(decision);
    ExecuteAIConstruction(decision);
    ExecuteAIProduction(decision);
    ExecuteAIOrders(decision);
}

void RTSController::AssignAISquads(const AIDecision& decision) {
    const bool deployed = decision.phase == AIPhase::Assault || decision.phase == AIPhase::Probe;
    float reservePower = 0.0f;
    int scouts = 0, escorts = 0;
    for (auto& unit : m_units) {
        if (unit.team != Team::Enemy || unit.health <= 0.0f || !IsArmy(unit.type)) continue;
        if (!deployed && unit.aiRole == AISquadRole::Raid) unit.aiRole = AISquadRole::Assault;
        if (unit.aiRole == AISquadRole::Reserve) reservePower += CombatPower(unit);
        if (unit.aiRole == AISquadRole::Scout) ++scouts;
        if (unit.aiRole == AISquadRole::Escort) ++escorts;
    }
    const float required = m_aiWorld.armyPower * decision.reserveFraction;
    // Keep assignments stable; only fill missing roles or rebalance reserve outside deployments.
    for (auto& unit : m_units) {
        if (unit.team != Team::Enemy || unit.health <= 0.0f || !IsArmy(unit.type)) continue;
        if (unit.aiRole == AISquadRole::Reserve && !deployed && reservePower - CombatPower(unit) >= required) {
            reservePower -= CombatPower(unit);
            unit.aiRole = AISquadRole::Unassigned;
        }
        if (reservePower < required && (unit.aiRole == AISquadRole::Unassigned || (!deployed && unit.aiRole == AISquadRole::Assault))) {
            unit.aiRole = AISquadRole::Reserve;
            reservePower += CombatPower(unit);
        }
    }
    // Prefer aircraft for reconnaissance so the strike group's ground strength stays together.
    for (auto& unit : m_units) {
        if (unit.team == Team::Enemy && unit.health > 0.0f && unit.type == UnitType::Plane && scouts == 0 &&
            (unit.aiRole == AISquadRole::Unassigned || (!deployed && unit.aiRole == AISquadRole::Assault))) {
            unit.aiRole = AISquadRole::Scout;
            ++scouts;
        }
    }
    for (auto& unit : m_units) {
        if (unit.team != Team::Enemy || unit.health <= 0.0f || !IsArmy(unit.type) || unit.aiRole != AISquadRole::Unassigned) continue;
        if (scouts == 0 && m_aiWorld.armyPower > 4.0f) { unit.aiRole = AISquadRole::Scout; ++scouts; }
        else if (escorts == 0 && m_aiWorld.armyPower > 8.0f) { unit.aiRole = AISquadRole::Escort; ++escorts; }
        else unit.aiRole = AISquadRole::Assault;
    }
    if (decision.launchRaid) {
        int raiders = 0;
        for (auto& unit : m_units) {
            if (unit.team == Team::Enemy && unit.health > 0.0f && unit.aiRole == AISquadRole::Assault && raiders < 2) {
                unit.aiRole = AISquadRole::Raid;
                ++raiders;
            }
        }
        m_aiRaidActive = raiders > 0;
    }
    if (!deployed || decision.retreat) m_aiRaidActive = false;
}

void RTSController::ExecuteAIProduction(const AIDecision& decision) {
    int tanks = m_aiWorld.tanks, planes = m_aiWorld.planes, engineers = m_aiWorld.engineers;
    for (const auto& queue : m_factoryQueues) {
        if (queue.team != Team::Enemy) continue;
        for (const auto& order : queue.orders) {
            if (order.type == UnitType::Tank) ++tanks;
            if (order.type == UnitType::Plane) ++planes;
            if (order.type == UnitType::Engineer) ++engineers;
        }
    }
    for (const auto& factory : m_units) {
        if (factory.team != Team::Enemy || factory.health <= 0.0f ||
            (factory.type != UnitType::Factory && factory.type != UnitType::LandFactory && factory.type != UnitType::AirFactory)) continue;
        bool busy = false;
        for (const auto& queue : m_factoryQueues) {
            if (queue.factoryEntityIndex == factory.entityIndex && !queue.orders.empty()) { busy = true; break; }
        }
        if (busy) continue;
        UnitType type;
        const bool land = factory.type != UnitType::AirFactory;
        const bool air = factory.type != UnitType::LandFactory;
        if (land && engineers < decision.desiredEngineers) type = UnitType::Engineer;
        else if (air && planes < decision.desiredPlanes && (!land || tanks >= decision.desiredTanks / 2)) type = UnitType::Plane;
        else if (land && tanks < decision.desiredTanks) type = UnitType::Tank;
        else continue;
        int mass, energy; float duration;
        GetUnitBuildCost(type, factory.faction, mass, energy, duration);
        const bool critical = type == UnitType::Engineer && engineers == 0;
        if (enemyMass < mass + (critical ? 0.0f : decision.massReserve) || enemyEnergy < energy + (critical ? 0.0f : decision.energyReserve)) continue;
        QueueFactoryUnit(type, Team::Enemy, factory.entityIndex);
        if (type == UnitType::Tank) ++tanks;
        if (type == UnitType::Plane) ++planes;
        if (type == UnitType::Engineer) ++engineers;
    }
}

bool RTSController::TryAIConstruction(UnitType type, RTSUnit& builder, const glm::vec3& anchor) {
    if (type == UnitType::MassGen || type == UnitType::EnergyGen) {
        const ResourceNodeType nodeType = type == UnitType::MassGen ? ResourceNodeType::Mass : ResourceNodeType::Energy;
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < m_resourceNodes.size(); ++i) {
            const auto& node = m_resourceNodes[i];
            if (node.type != nodeType || node.occupied || node.reservedBy != -1 || !AIPositionSafe(node.position)) continue;
            if (builder.type == UnitType::CommandMech && Distance2D(node.position, m_aiHome) > 100.0f) continue;
            const float distance = Distance2D(node.position, builder.position);
            if (distance < bestDistance) { bestDistance = distance; best = static_cast<int>(i); }
        }
        return best >= 0 && TryStartConstruction(type, Team::Enemy, builder.entityIndex, m_resourceNodes[best].position);
    }
    // Deterministic rings around tactical anchors; shared admission validates footprint, terrain and occupancy.
    const float firstRadius = type == UnitType::ShieldGen ? 12.0f : 22.0f;
    for (int ring = 0; ring < 4; ++ring) {
        for (int sector = 0; sector < 12; ++sector) {
            const float angle = sector * 6.283185307f / 12.0f + ring * 0.23f;
            glm::vec3 site = anchor + glm::vec3(std::cos(angle), std::sin(angle), 0.0f) * (firstRadius + ring * 16.0f);
            if (!AIPositionSafe(site) || (builder.type == UnitType::CommandMech && Distance2D(site, m_aiHome) > 100.0f)) continue;
            bool covered = false;
            const float spacing = type == UnitType::ShieldGen ? 55.0f : ((type == UnitType::AATurret || type == UnitType::Turret) ? 30.0f : 20.0f);
            for (const auto& unit : m_units) {
                if (unit.team == Team::Enemy && unit.health > 0.0f && unit.type == type && Distance2D(unit.position, site) < spacing) { covered = true; break; }
            }
            for (const auto& task : m_constructionTasks) {
                if (task.team == Team::Enemy && task.buildType == type && Distance2D(task.buildPos, site) < spacing) { covered = true; break; }
            }
            if (!covered && TryStartConstruction(type, Team::Enemy, builder.entityIndex, site)) return true;
        }
    }
    return false;
}

void RTSController::ExecuteAIConstruction(const AIDecision& decision) {
    constexpr size_t typeCount = static_cast<size_t>(UnitType::SupportMech) + 1;
    std::array<int, typeCount> counts{};
    for (const auto& unit : m_units) if (unit.team == Team::Enemy && unit.health > 0.0f) ++counts[static_cast<size_t>(unit.type)];
    for (const auto& task : m_constructionTasks) if (task.team == Team::Enemy) ++counts[static_cast<size_t>(task.buildType)];
    const auto count = [&](UnitType type) { return counts[static_cast<size_t>(type)]; };
    glm::vec3 defenseAnchor = m_aiHome + m_aiForward * 40.0f;
    if (decision.tactic == AITactic::ChokepointDenial && m_aiHasChoke) defenseAnchor = m_aiChoke;
    if (decision.tactic == AITactic::ForwardBastion) defenseAnchor = m_aiExpansion;
    if (decision.tactic == AITactic::AirDenial) defenseAnchor = m_aiHome;
    if (m_aiWorld.underAttack) defenseAnchor = m_aiHome + glm::vec3(glm::normalize(glm::vec2(m_aiThreatPosition - m_aiHome) + glm::vec2(0.001f)), 0.0f) * 35.0f;

    for (auto& builder : m_units) {
        if (builder.team != Team::Enemy || builder.health <= 0.0f ||
            (builder.type != UnitType::Engineer && builder.type != UnitType::CommandMech) || builder.fleeing || HasConstructionTask(builder.entityIndex)) continue;
        struct Goal { UnitType type; int target; bool essential; };
        const std::array<Goal, 8> goals{{
            {UnitType::EnergyGen, std::max(decision.desiredEnergyGenerators, 1 + count(UnitType::ShieldGen) * 2), true},
            {UnitType::MassGen, decision.desiredMassGenerators, true},
            {UnitType::LandFactory, decision.desiredLandFactories, true},
            {UnitType::AATurret, decision.desiredAATurrets, false},
            {UnitType::Turret, decision.desiredTurrets, false},
            {UnitType::AirFactory, decision.desiredAirFactories, true},
            {UnitType::ShieldGen, decision.desiredShields, false},
            {UnitType::MassStorage, enemyMass > 800.0f ? 1 : 0, false}
        }};
        for (const auto& goal : goals) {
            if (count(goal.type) >= goal.target) continue;
            if (goal.type == UnitType::ShieldGen && m_aiWorld.energyIncome < 25.0f) continue;
            int mass, energy; float duration;
            GetUnitBuildCost(goal.type, builder.faction, mass, energy, duration);
            if (enemyMass < mass + (goal.essential ? 0.0f : decision.massReserve) || enemyEnergy < energy + (goal.essential ? 0.0f : decision.energyReserve)) continue;
            glm::vec3 anchor = defenseAnchor;
            if (goal.type == UnitType::ShieldGen) anchor = decision.tactic == AITactic::ForwardBastion ? m_aiExpansion : m_aiHome;
            if (goal.type == UnitType::LandFactory || goal.type == UnitType::AirFactory || goal.type == UnitType::MassStorage)
                anchor = decision.tactic == AITactic::ForwardBastion && count(goal.type) > 0 ? m_aiExpansion : m_aiHome - m_aiForward * 25.0f;
            if (TryAIConstruction(goal.type, builder, anchor)) { ++counts[static_cast<size_t>(goal.type)]; break; }
        }
    }
}

void RTSController::ExecuteAIOrders(const AIDecision& decision) {
    const glm::vec3 side(-m_aiForward.y, m_aiForward.x, 0.0f);
    glm::vec3 raidTarget = m_aiObjective;
    for (const auto& contact : m_aiContacts) {
        if (contact.type == UnitType::MassGen || contact.type == UnitType::EnergyGen) { raidTarget = contact.position; break; }
    }
    int slot = 0;
    for (auto& unit : m_units) {
        if (unit.team != Team::Enemy || unit.health <= 0.0f || IsStructure(unit.type)) continue;
        const bool builder = unit.type == UnitType::Engineer || unit.type == UnitType::CommandMech;
        glm::vec3 destination = m_aiHome + m_aiForward * 20.0f;
        bool force = decision.retreat;
        if (builder) {
            bool threatened = false;
            for (const auto& contact : m_aiContacts) {
                if (contact.visible && contact.power > 0.0f && Distance2D(contact.position, unit.position) < 65.0f) { threatened = true; break; }
            }
            unit.fleeing = threatened && (unit.type == UnitType::Engineer || unit.health < unit.maxHealth * 0.4f);
            if (unit.fleeing) {
                destination = m_aiHome - m_aiForward * 25.0f;
                force = true;
            } else {
                if (HasConstructionTask(unit.entityIndex)) {
                    for (const auto& task : m_constructionTasks) if (task.engineerEntityIndex == unit.entityIndex) destination = task.buildPos;
                } else if (Distance2D(unit.position, m_aiHome) <= 45.0f) continue;
            }
        } else {
            const float spread = static_cast<float>((slot++ % 7) - 3) * 7.0f;
            destination += side * spread;
            if (decision.retreat || m_aiWorld.noRush) {
                destination = m_aiHome + m_aiForward * 30.0f + side * spread;
            } else if (unit.aiRole == AISquadRole::Reserve) {
                if (decision.emergencyDefense && Distance2D(m_aiThreatPosition, m_aiHome) < 135.0f) destination = m_aiThreatPosition;
            } else if (unit.aiRole == AISquadRole::Escort) {
                destination = m_aiExpansion + side * spread;
                for (const auto& task : m_constructionTasks) {
                    if (task.team == Team::Enemy && Distance2D(task.buildPos, m_aiHome) > 80.0f) { destination = task.buildPos + side * 14.0f; break; }
                }
            } else if (unit.aiRole == AISquadRole::Scout) {
                destination = decision.phase == AIPhase::Probe && !m_aiWorld.scoutContact ?
                    m_aiObjective + side * 45.0f : m_aiStaging + side * 30.0f;
                if (m_aiWorld.scoutContact) force = true;
            } else if (unit.aiRole == AISquadRole::Raid && m_aiRaidActive && decision.phase == AIPhase::Probe) {
                destination = raidTarget + side * spread;
            } else {
                destination = decision.phase == AIPhase::Assault ? m_aiObjective + side * spread : m_aiStaging + side * spread;
            }
            if (decision.emergencyDefense) {
                destination = Distance2D(m_aiThreatPosition, m_aiHome) < 135.0f ? m_aiThreatPosition : m_aiHome;
                force = true;
            }
            if (unit.health < unit.maxHealth * 0.3f) { destination = m_aiHome - m_aiForward * 20.0f; force = true; }
            if (unit.inCombat && !force) continue;
        }
        if (m_noRushTimer > 0.0f) {
            const float beyond = glm::dot(glm::vec2(destination), glm::vec2(m_aiForward)) + 20.0f;
            if (beyond > 0.0f) destination -= m_aiForward * beyond;
        }
        destination.x = std::clamp(destination.x, -460.0f, 460.0f);
        destination.y = std::clamp(destination.y, -460.0f, 460.0f);
        destination.z = GetTerrainHeight(destination.x, destination.y) + (unit.type == UnitType::Plane ? 14.0f : 0.0f);
        if (Distance2D(unit.position, destination) > 5.0f && (!unit.hasTarget || Distance2D(unit.targetPosition, destination) > 10.0f)) {
            unit.targetPosition = destination;
            unit.hasTarget = true;
        }
    }
}
}
