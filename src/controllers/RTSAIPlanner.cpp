#include "RTSAIPlanner.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Crescendo {
namespace {
constexpr int TacticCount = static_cast<int>(AITactic::Count);

float Fraction(float value, float required) {
    return std::clamp(value / required, 0.0f, 1.0f);
}

bool CanFund(const AIWorldState& w, float mass, float energy) {
    // Planning may anticipate one short income window; payment remains entirely
    // the consumer's responsibility. Never promise advanced infrastructure off
    // a negative net-energy economy.
    return w.mass + std::max(0.0f, w.massIncome) * 30.0f >= mass &&
           w.energy + std::max(0.0f, w.energyIncome) * 30.0f >= energy;
}

bool IsSortie(AIPhase phase) {
    return phase == AIPhase::Probe || phase == AIPhase::Assault;
}
} // namespace

void RTSAIPlanner::Reset(uint32_t seed, int personality, int difficulty, int faction) {
    m_decision = AIDecision{};
    m_rng = seed != 0 ? seed : 0x9e3779b9u;
    m_personality = std::clamp(personality, 0, 2);
    m_difficulty = std::clamp(difficulty, 0, 2);
    m_faction = std::clamp(faction, 0, 5);
    m_decision.reactionDelay = m_difficulty == 0 ? 4.0f : (m_difficulty == 1 ? 2.0f : 0.8f);
    m_decision.intelLifetime = m_difficulty == 0 ? 90.0f : (m_difficulty == 1 ? 120.0f : 180.0f);
    m_decision.reserveFraction = m_personality == 1 ? 0.35f : (m_personality == 2 ? 0.5f : 0.4f);
    m_phaseSince = 0.0f;
    m_tacticSince = -1000.0f;
    m_nextTacticReview = 0.0f;
    m_lastTime = 0.0f;
    m_readySince = -1.0f;
    m_threatSince = -1.0f;
    m_lastDefense = -1000.0f;
    m_nextOffensive = 0.0f;
    m_probeConfirmedAt = -1000.0f;
    m_sortiePeakPower = 0.0f;
    m_tacticChosen = false;
}

uint32_t RTSAIPlanner::NextRandom() {
    // Fixed algorithm, unlike implementation-dependent library distributions.
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return m_rng;
}

float RTSAIPlanner::OffensiveFloor() const {
    return m_personality == 1 ? 210.0f : (m_personality == 2 ? 300.0f : 240.0f);
}

bool RTSAIPlanner::EconomyReady(const AIWorldState& w) const {
    const float stableSeconds = m_difficulty == 0 ? 40.0f : (m_difficulty == 1 ? 30.0f : 25.0f);
    return w.massIncome > 0.0f && w.energyIncome > 0.0f &&
           w.stableEconomySeconds >= stableSeconds &&
           w.mass >= m_decision.massReserve && w.energy >= m_decision.energyReserve;
}

bool RTSAIPlanner::ForceReady(const AIWorldState& w) const {
    return w.tanks >= 8 && w.planes >= 2 && w.armyPower > 0.0f &&
           w.armyHealthFraction >= 0.8f && w.landFactories > 0 && w.airFactories > 0;
}

bool RTSAIPlanner::ReserveReady(const AIWorldState& w) const {
    // Account for indivisible units; do not demand more than the assigned 40%.
    return w.armyPower > 0.0f &&
           w.reservePower >= w.armyPower * m_decision.reserveFraction * 0.85f;
}

void RTSAIPlanner::EnterPhase(AIPhase phase, float time, const char* reason) {
    m_decision.phase = phase;
    m_decision.reason = reason;
    m_phaseSince = time;
    m_readySince = -1.0f;
}

void RTSAIPlanner::SelectTactic(const AIWorldState& w) {
    if (w.time < m_nextTacticReview) return;
    m_nextTacticReview = w.time + 10.0f;
    if (m_tacticChosen && (w.time - m_tacticSince < 45.0f || IsSortie(m_decision.phase))) return;

    const bool freshIntel = w.intelAge <= m_decision.intelLifetime || w.visibleThreatPower > 0.0f;
    const float threat = w.visibleThreatPower / std::max(1.0f, w.armyPower);
    const float airThreat = freshIntel ? w.observedAirPower / std::max(1.0f, w.armyPower) : 0.0f;
    const bool weakEconomy = w.massIncome <= 0.0f || w.energyIncome <= 0.0f || w.stableEconomySeconds < 30.0f;
    std::array<float, TacticCount> utility = {{
        48.0f + (w.homeDefenseScore < 1.0f ? 20.0f : 0.0f),
        w.chokeAvailable ? 64.0f + std::min(threat, 2.0f) * 20.0f : -1000.0f,
        46.0f + (w.recentAttack ? 18.0f : 0.0f) + std::min(threat, 2.0f) * 12.0f,
        airThreat > 0.0f ? 42.0f + std::min(airThreat, 2.0f) * 90.0f : -1000.0f,
        w.safeExpansionAvailable && !weakEconomy && w.homeDefenseScore >= 1.0f && CanFund(w, 260.0f, 240.0f)
            ? 66.0f : -1000.0f,
        42.0f + (weakEconomy ? 38.0f : 0.0f) + (w.safeExpansionAvailable ? 10.0f : 0.0f),
        w.recentAttack && !w.underAttack && w.targetKnown && freshIntel && w.armyHealthFraction >= 0.8f
            ? 78.0f : -1000.0f
    }};
    if (m_personality == 1) {
        utility[static_cast<int>(AITactic::MobileReserve)] += 12.0f;
        utility[static_cast<int>(AITactic::Counterattack)] += 12.0f;
    } else if (m_personality == 2) {
        utility[static_cast<int>(AITactic::LayeredCore)] += 12.0f;
        utility[static_cast<int>(AITactic::ChokepointDenial)] += 10.0f;
    }
    constexpr AITactic factionPreference[] = {
        AITactic::LayeredCore, AITactic::EconomicShell, AITactic::LayeredCore,
        AITactic::MobileReserve, AITactic::ChokepointDenial, AITactic::AirDenial
    };
    utility[static_cast<int>(factionPreference[m_faction])] += 8.0f;

    std::array<int, 3> top = {{-1, -1, -1}};
    for (int i = 0; i < TacticCount; ++i) {
        for (int rank = 0; rank < 3; ++rank) {
            if (top[rank] == -1 || utility[i] > utility[top[rank]]) {
                for (int shift = 2; shift > rank; --shift) top[shift] = top[shift - 1];
                top[rank] = i;
                break;
            }
        }
    }
    const int current = static_cast<int>(m_decision.tactic);
    const bool invalidCurrent = utility[current] < 0.0f;
    if (m_tacticChosen && !invalidCurrent && utility[top[0]] < utility[current] + 12.0f) return;

    // Weighted top choices, but never switch to an inferior alternative just
    // because a better alternative qualified the review for consideration.
    std::array<uint32_t, 3> weights{};
    uint32_t total = 0;
    const float cutoff = utility[top[0]] - 24.0f;
    for (int rank = 0; rank < 3; ++rank) {
        const float score = utility[top[rank]];
        if (score >= cutoff && (!m_tacticChosen || invalidCurrent || score >= utility[current] + 6.0f)) {
            weights[rank] = static_cast<uint32_t>(score - cutoff) + 1u;
            total += weights[rank];
        }
    }
    if (total == 0) return;
    uint32_t choice = NextRandom() % total;
    for (int rank = 0; rank < 3; ++rank) {
        if (choice < weights[rank]) {
            m_decision.tactic = static_cast<AITactic>(top[rank]);
            m_tacticChosen = true;
            m_tacticSince = w.time;
            m_nextTacticReview = w.time + 30.0f;
            return;
        }
        choice -= weights[rank];
    }
}

void RTSAIPlanner::SetTargets(const AIWorldState& w) {
    auto& d = m_decision;
    const bool funded = w.massIncome > 0.0f && w.energyIncome > 0.0f && w.stableEconomySeconds >= 20.0f;
    const bool developed = d.phase != AIPhase::Bootstrap && d.phase != AIPhase::Establish;
    d.desiredTanks = 8;
    d.desiredPlanes = 2;
    d.desiredEngineers = w.safeExpansionAvailable && funded ? 3 : 2;
    // These are fixed goals, not current count + 1: finite maps never trigger
    // endlessly escalating generator or factory demand.
    d.desiredMassGenerators = w.safeExpansionAvailable && developed ? 3 : 2;
    d.desiredEnergyGenerators = w.safeExpansionAvailable && developed ? 4 : 3;
    d.desiredLandFactories = funded && developed && CanFund(w, 320.0f, 200.0f) ? 2 : 1;
    d.desiredAirFactories = w.landFactories > 0 && CanFund(w, 200.0f, 140.0f) ? 1 : 0;
    d.desiredTurrets = 2;
    d.desiredAATurrets = 1;
    d.desiredShields = developed && funded && w.energyIncome >= 3.0f && CanFund(w, 140.0f, 160.0f) ? 1 : 0;
    d.reserveFraction = m_personality == 1 ? 0.35f : (m_personality == 2 ? 0.5f : 0.4f);
    d.massReserve = developed ? 80.0f : 0.0f;
    d.energyReserve = developed ? 100.0f : 0.0f;

    switch (d.tactic) {
    case AITactic::LayeredCore:
        d.desiredTurrets = 3;
        if (d.desiredShields > 0 && w.energyIncome >= 6.0f) d.desiredShields = 2;
        break;
    case AITactic::ChokepointDenial:
        d.desiredTurrets = w.chokeAvailable ? 4 : 2;
        d.desiredTanks = 10;
        break;
    case AITactic::MobileReserve:
        d.desiredTanks = 12;
        d.desiredPlanes = 3;
        break;
    case AITactic::AirDenial:
        d.desiredAATurrets = 3;
        d.desiredPlanes = 4;
        break;
    case AITactic::ForwardBastion:
        if (w.safeExpansionAvailable && funded) {
            d.desiredTurrets = 4;
            d.desiredEngineers = 3;
            d.desiredMassGenerators = 4;
            d.desiredEnergyGenerators = 4;
        }
        break;
    case AITactic::EconomicShell:
        if (w.safeExpansionAvailable) {
            d.desiredMassGenerators = 4;
            d.desiredEnergyGenerators = 5;
        }
        d.desiredTurrets = 3;
        break;
    case AITactic::Counterattack:
        d.desiredTanks = 12;
        d.desiredPlanes = 3;
        break;
    case AITactic::Count:
        break;
    }
    if (w.observedAirPower > 0.0f && (w.intelAge <= d.intelLifetime || w.visibleThreatPower > 0.0f)) {
        d.desiredAATurrets = std::max(d.desiredAATurrets, 2);
    }
    if (d.phase == AIPhase::Recover) {
        d.massReserve = 40.0f;
        d.energyReserve = 60.0f;
    }
    if (d.emergencyDefense) {
        d.reserveFraction = 0.6f;
        d.massReserve = 0.0f;
        d.energyReserve = 0.0f;
        d.desiredTurrets = std::max(d.desiredTurrets, 3);
    }
}

const AIDecision& RTSAIPlanner::Update(const AIWorldState& world) {
    // Invalid/out-of-order samples must not rewind deadlines or duplicate orders.
    auto& d = m_decision;
    d.launchProbe = d.launchRaid = d.launchAssault = false;
    if (!std::isfinite(world.time) || world.time < m_lastTime) return d;
    const AIWorldState& w = world;
    m_lastTime = w.time;

    const bool credibleThreat = w.visibleThreatPower > std::max(1.0f, w.armyPower * 0.2f);
    if (credibleThreat) {
        if (m_threatSince < 0.0f) m_threatSince = w.time;
    } else {
        m_threatSince = -1.0f;
    }
    const bool threatReacted = m_threatSince >= 0.0f && w.time - m_threatSince >= d.reactionDelay;
    if (w.underAttack || threatReacted) m_lastDefense = w.time;
    d.emergencyDefense = w.underAttack || threatReacted || w.time - m_lastDefense < 12.0f;
    if (d.emergencyDefense) {
        m_probeConfirmedAt = -1000.0f;
        m_nextOffensive = std::max(m_nextOffensive, w.time + 30.0f);
        if (d.phase != AIPhase::Recover) EnterPhase(AIPhase::Recover, w.time, "Home defense interrupt; recalling deployed squads");
    } else if (w.noRush && IsSortie(d.phase)) {
        m_probeConfirmedAt = -1000.0f;
        m_nextOffensive = w.time + 30.0f;
        EnterPhase(AIPhase::Recover, w.time, "No-rush active; recalling deployed squads");
    }

    SelectTactic(w);
    SetTargets(w);
    const bool economyReady = EconomyReady(w);
    const bool forceReady = ForceReady(w);
    const bool reserveReady = ReserveReady(w);
    const bool assembled = w.stagedFraction >= 0.8f;
    const bool freshTarget = w.targetKnown && w.intelAge <= d.intelLifetime;
    const float judgment = m_difficulty == 0 ? 1.35f : (m_difficulty == 1 ? 1.15f : 1.05f);
    const float committedPower = std::max(0.0f, w.armyPower - w.reservePower);
    const bool favorable = freshTarget && committedPower >= std::max(1.0f, w.knownEnemyPower) * judgment;
    const bool launchReady = !w.noRush && !d.emergencyDefense && w.time >= OffensiveFloor() &&
                             w.time >= m_nextOffensive && economyReady && forceReady && reserveReady && assembled;
    if (launchReady) {
        if (m_readySince < 0.0f) m_readySince = w.time;
    } else {
        m_readySince = -1.0f;
    }
    const bool judgedReady = launchReady && w.time - m_readySince >= d.reactionDelay;
    const bool collapsed = w.energyIncome <= 0.0f && w.energy < 60.0f;
    const bool starved = w.massIncome <= 0.0f && w.mass < 40.0f;
    if (IsSortie(d.phase)) {
        m_sortiePeakPower = std::max(m_sortiePeakPower, w.armyPower);
        if (collapsed || starved || w.armyHealthFraction < 0.55f ||
            w.armyPower < m_sortiePeakPower * 0.7f || !reserveReady) {
            m_probeConfirmedAt = -1000.0f;
            m_nextOffensive = w.time + 60.0f;
            EnterPhase(AIPhase::Recover, w.time, "Sortie aborted: losses, reserves or economy require recovery");
        }
    }

    const float phaseAge = w.time - m_phaseSince;
    switch (d.phase) {
    case AIPhase::Bootstrap:
        if (w.landFactories > 0 && w.massIncome > 0.0f && w.energyIncome > 0.0f) {
            EnterPhase(AIPhase::Establish, w.time, "Establishing sustainable economy and air production");
        }
        break;
    case AIPhase::Establish:
        // Generator counts are not progression gates: starting trickle can
        // sustain a depleted map with no legal expansion/resource locations.
        if (economyReady && w.airFactories > 0 && phaseAge >= 15.0f) {
            EnterPhase(AIPhase::Fortify, w.time, "Building layered home defense");
        }
        break;
    case AIPhase::Fortify:
        if (economyReady && (w.homeDefenseScore >= 1.0f || (w.turrets >= 2 && w.aaTurrets >= 1)) && phaseAge >= 20.0f) {
            EnterPhase(AIPhase::Expand, w.time, w.safeExpansionAvailable ? "Securing a safe economic extension" : "No safe expansion; consolidating existing economy");
        }
        break;
    case AIPhase::Expand:
        if (economyReady && (!w.safeExpansionAvailable || (w.massGenerators >= 3 && w.energyGenerators >= 3) || phaseAge >= 45.0f)) {
            EnterPhase(AIPhase::Stage, w.time, "Assembling attack force while retaining a home reserve");
        }
        break;
    case AIPhase::Stage:
        if (collapsed || starved) {
            EnterPhase(AIPhase::Recover, w.time, "Restoring economy before deployment");
        } else if (judgedReady) {
            if (freshTarget && favorable && w.time - m_probeConfirmedAt <= d.intelLifetime) {
                EnterPhase(AIPhase::Assault, w.time, "Fresh reconnaissance and assembled force authorize assault");
                m_sortiePeakPower = w.armyPower;
                d.launchAssault = true;
            } else if (w.time >= m_nextOffensive) {
                EnterPhase(AIPhase::Probe, w.time, "Limited reconnaissance sortie; main force remains staged");
                m_sortiePeakPower = w.armyPower;
                d.launchRaid = freshTarget && favorable && (d.tactic == AITactic::Counterattack || m_personality == 1);
                d.launchProbe = !d.launchRaid;
            }
        } else {
            d.reason = w.noRush ? "No-rush: economy, defense and assembly only" :
                       (w.time < OffensiveFloor() ? "Minimum preparation window; no early offensive" :
                        "Waiting for stable economy, healthy army, reserve and assembly");
        }
        break;
    case AIPhase::Probe:
        if (phaseAge >= 10.0f && freshTarget) {
            m_probeConfirmedAt = w.time;
            m_nextOffensive = w.time + 12.0f;
            EnterPhase(AIPhase::Stage, w.time, "Reconnaissance confirmed target; regrouping before commitment");
        } else if (phaseAge >= 35.0f) {
            m_probeConfirmedAt = -1000.0f;
            m_nextOffensive = w.time + 30.0f;
            EnterPhase(AIPhase::Stage, w.time, "Reconnaissance timed out; regrouping before another probe");
        }
        break;
    case AIPhase::Assault:
        if (!freshTarget || committedPower < w.knownEnemyPower * 0.8f || phaseAge >= 90.0f) {
            m_probeConfirmedAt = -1000.0f;
            m_nextOffensive = w.time + 60.0f;
            EnterPhase(AIPhase::Recover, w.time, "Assault ended: stale intelligence, resistance or sortie limit");
        }
        break;
    case AIPhase::Recover:
        if (!d.emergencyDefense && phaseAge >= 30.0f && economyReady && w.armyHealthFraction >= 0.8f) {
            if (w.landFactories == 0) {
                EnterPhase(AIPhase::Bootstrap, w.time, "Rebuilding destroyed primary production");
            } else if (w.airFactories == 0) {
                EnterPhase(AIPhase::Establish, w.time, "Rebuilding air production");
            } else if (w.homeDefenseScore < 1.0f && (w.turrets < 2 || w.aaTurrets < 1)) {
                EnterPhase(AIPhase::Fortify, w.time, "Repairing home defense before restaging");
            } else if (forceReady && reserveReady) {
                EnterPhase(AIPhase::Stage, w.time, "Recovery complete; assembling a new force");
            }
        }
        break;
    }
    // Targets must describe the returned phase, not the preceding phase.
    SetTargets(w);
    d.retreat = d.emergencyDefense || d.phase == AIPhase::Recover;
    const float stableRequired = m_difficulty == 0 ? 40.0f : (m_difficulty == 1 ? 30.0f : 25.0f);
    d.assaultReadiness = std::min({
        Fraction(w.time, OffensiveFloor()), Fraction(w.stableEconomySeconds, stableRequired),
        Fraction(static_cast<float>(w.tanks), 8.0f), Fraction(static_cast<float>(w.planes), 2.0f),
        Fraction(w.armyHealthFraction, 0.8f), Fraction(w.stagedFraction, 0.8f),
        Fraction(w.reservePower, std::max(1.0f, w.armyPower * d.reserveFraction * 0.85f)),
        Fraction(w.mass, std::max(1.0f, d.massReserve)), Fraction(w.energy, std::max(1.0f, d.energyReserve))
    });
    if (w.noRush || d.emergencyDefense || !freshTarget || !favorable || !EconomyReady(w)) d.assaultReadiness = 0.0f;
    return d;
}

const char* RTSAIPlanner::PhaseName(AIPhase phase) {
    switch (phase) {
    case AIPhase::Bootstrap: return "Bootstrap";
    case AIPhase::Establish: return "Establish";
    case AIPhase::Fortify: return "Fortify";
    case AIPhase::Expand: return "Expand";
    case AIPhase::Stage: return "Stage";
    case AIPhase::Probe: return "Probe";
    case AIPhase::Assault: return "Assault";
    case AIPhase::Recover: return "Recover";
    }
    return "Unknown";
}

const char* RTSAIPlanner::TacticName(AITactic tactic) {
    switch (tactic) {
    case AITactic::LayeredCore: return "Layered core";
    case AITactic::ChokepointDenial: return "Chokepoint denial";
    case AITactic::MobileReserve: return "Mobile reserve";
    case AITactic::AirDenial: return "Air denial";
    case AITactic::ForwardBastion: return "Forward bastion";
    case AITactic::EconomicShell: return "Economic shell";
    case AITactic::Counterattack: return "Counterattack";
    case AITactic::Count: return "Unknown";
    }
    return "Unknown";
}

} // namespace Crescendo
