#pragma once

#include <cstdint>

namespace Crescendo {

enum class AIPhase { Bootstrap, Establish, Fortify, Expand, Stage, Probe, Assault, Recover };
enum class AITactic { LayeredCore, ChokepointDenial, MobileReserve, AirDenial, ForwardBastion, EconomicShell, Counterattack, Count };
enum class AISquadRole { Unassigned, Scout, Reserve, Escort, Raid, Assault };

struct AIWorldState {
    float time = 0.0f;
    float mass = 0.0f;
    float energy = 0.0f;
    float massIncome = 0.0f;
    float energyIncome = 0.0f; // Net of shield upkeep.
    float stableEconomySeconds = 0.0f;
    float armyPower = 0.0f;
    float reservePower = 0.0f;
    float stagedFraction = 0.0f; // Fraction of non-reserve units assembled.
    float visibleThreatPower = 0.0f;
    float knownEnemyPower = 0.0f;
    float intelAge = 1000000.0f;
    float armyHealthFraction = 1.0f;
    float homeDefenseScore = 0.0f;
    float observedAirPower = 0.0f;
    int tanks = 0;
    int planes = 0;
    int engineers = 0;
    int massGenerators = 0;
    int energyGenerators = 0;
    int landFactories = 0;
    int airFactories = 0;
    int turrets = 0;
    int aaTurrets = 0;
    int shields = 0;
    bool safeExpansionAvailable = false;
    bool chokeAvailable = false;
    bool underAttack = false;
    bool recentAttack = false;
    bool noRush = false;
    bool scoutContact = false;
    bool targetKnown = false;
};

struct AIDecision {
    AIPhase phase = AIPhase::Bootstrap;
    AITactic tactic = AITactic::LayeredCore;
    int desiredTanks = 8;
    int desiredPlanes = 2;
    int desiredEngineers = 2;
    int desiredMassGenerators = 2;
    int desiredEnergyGenerators = 3;
    int desiredLandFactories = 1;
    int desiredAirFactories = 0;
    int desiredTurrets = 2;
    int desiredAATurrets = 1;
    int desiredShields = 0;
    float reserveFraction = 0.4f;
    float massReserve = 0.0f;
    float energyReserve = 0.0f;
    float reactionDelay = 2.0f;
    float intelLifetime = 120.0f;
    float assaultReadiness = 0.0f;
    bool emergencyDefense = false;
    // Launch commands are one-update pulses, not persistent movement orders.
    bool launchProbe = false;
    bool launchRaid = false;
    bool launchAssault = false;
    // Recall deployed squads while recovering or defending the base.
    bool retreat = false;
    const char* reason = "Establishing basic production";
};

// Pure policy: the consumer owns resource payment, construction, visibility and
// squad movement. Update uses monotonic match time; Reset is required per match.
class RTSAIPlanner {
public:
    void Reset(uint32_t seed, int personality, int difficulty, int faction);
    const AIDecision& Update(const AIWorldState& world);
    const AIDecision& Decision() const { return m_decision; }
    static const char* PhaseName(AIPhase phase);
    static const char* TacticName(AITactic tactic);

private:
    uint32_t NextRandom();
    void SelectTactic(const AIWorldState& world);
    void SetTargets(const AIWorldState& world);
    void EnterPhase(AIPhase phase, float time, const char* reason);
    bool EconomyReady(const AIWorldState& world) const;
    bool ForceReady(const AIWorldState& world) const;
    bool ReserveReady(const AIWorldState& world) const;
    float OffensiveFloor() const;

    AIDecision m_decision;
    uint32_t m_rng = 1;
    int m_personality = 0;
    int m_difficulty = 1;
    int m_faction = 0;
    float m_phaseSince = 0.0f;
    float m_tacticSince = -1000.0f;
    float m_nextTacticReview = 0.0f;
    float m_lastTime = 0.0f;
    float m_readySince = -1.0f;
    float m_threatSince = -1.0f;
    float m_lastDefense = -1000.0f;
    float m_nextOffensive = 0.0f;
    float m_probeConfirmedAt = -1000.0f;
    float m_sortiePeakPower = 0.0f;
    bool m_tacticChosen = false;
};

} // namespace Crescendo
