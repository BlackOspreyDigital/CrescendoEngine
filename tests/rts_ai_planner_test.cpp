// Standalone: compile this file and src/controllers/RTSAIPlanner.cpp with
// c++ -std=c++17 -Wall -Wextra -pedantic -Isrc/controllers.
// Run without NDEBUG: these tests deliberately use standard-library assertions.
#include "RTSAIPlanner.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>

using namespace Crescendo;

namespace {
AIWorldState ReadyWorld() {
    AIWorldState w;
    w.mass = 1000.0f;
    w.energy = 1000.0f;
    w.massIncome = 5.0f;
    w.energyIncome = 8.0f;
    w.stableEconomySeconds = 120.0f;
    w.armyPower = 100.0f;
    w.reservePower = 40.0f;
    w.stagedFraction = 1.0f;
    w.knownEnemyPower = 35.0f;
    w.intelAge = 0.0f;
    w.armyHealthFraction = 1.0f;
    w.homeDefenseScore = 2.0f;
    w.tanks = 8;
    w.planes = 2;
    w.engineers = 2;
    w.massGenerators = 2;
    w.energyGenerators = 3;
    w.landFactories = 1;
    w.airFactories = 1;
    w.turrets = 2;
    w.aaTurrets = 1;
    w.targetKnown = true;
    return w;
}

AIDecision At(RTSAIPlanner& p, AIWorldState& w, float time) {
    w.time = time;
    const AIDecision d = p.Update(w);
    assert(static_cast<int>(d.launchProbe) + static_cast<int>(d.launchRaid) + static_cast<int>(d.launchAssault) <= 1);
    assert(!d.launchAssault || d.phase == AIPhase::Assault);
    assert(!(d.launchProbe || d.launchRaid) || d.phase == AIPhase::Probe);
    assert(!w.noRush || (!d.launchProbe && !d.launchRaid && !d.launchAssault));
    assert(!d.retreat || (!d.launchProbe && !d.launchRaid && !d.launchAssault));
    return d;
}

void ReachStage(RTSAIPlanner& p, AIWorldState& w) {
    assert(At(p, w, 0.0f).phase == AIPhase::Establish);
    assert(At(p, w, 15.0f).phase == AIPhase::Fortify);
    assert(At(p, w, 35.0f).phase == AIPhase::Expand);
    assert(At(p, w, 80.0f).phase == AIPhase::Stage);
}

void ReachAssault(RTSAIPlanner& p, AIWorldState& w) {
    ReachStage(p, w);
    assert(At(p, w, 239.0f).phase == AIPhase::Stage);
    assert(At(p, w, 240.0f).phase == AIPhase::Stage);
    const auto probe = At(p, w, 242.0f);
    assert(probe.phase == AIPhase::Probe && probe.launchProbe);
    assert(!At(p, w, 243.0f).launchProbe);
    assert(At(p, w, 252.0f).phase == AIPhase::Stage);
    assert(At(p, w, 264.0f).phase == AIPhase::Stage);
    const auto assault = At(p, w, 266.0f);
    assert(assault.phase == AIPhase::Assault && assault.launchAssault);
    assert(!At(p, w, 267.0f).launchAssault);
}

void PreparationAndFiniteMapLiveness() {
    RTSAIPlanner p;
    p.Reset(42, 0, 1, 0);
    auto w = ReadyWorld();
    // Neither generators nor a free expansion site are mandatory when the
    // actual income and completed production can sustain this army.
    w.massGenerators = w.energyGenerators = 0;
    w.safeExpansionAvailable = false;
    ReachAssault(p, w);
    assert(p.Decision().reserveFraction == 0.4f);
    assert(p.Decision().desiredTanks >= 8 && p.Decision().desiredPlanes >= 2);
    assert(p.Decision().assaultReadiness == 1.0f);
}

void EveryLaunchPrerequisiteMatters() {
    for (int missing = 0; missing < 8; ++missing) {
        RTSAIPlanner p;
        p.Reset(42, 0, 1, 0);
        auto w = ReadyWorld();
        ReachStage(p, w);
        switch (missing) {
        case 0: w.stagedFraction = 0.79f; break;
        case 1: w.reservePower = 20.0f; break;
        case 2: w.stableEconomySeconds = 0.0f; break;
        case 3: w.armyHealthFraction = 0.79f; break;
        case 4: w.planes = 1; break;
        case 5: w.tanks = 7; break;
        case 6: w.mass = 0.0f; break;
        case 7: w.energy = 0.0f; break;
        }
        for (float time : {240.0f, 245.0f, 500.0f, 1000.0f}) {
            const auto d = At(p, w, time);
            assert(d.phase == AIPhase::Stage);
            assert(!d.launchProbe && !d.launchRaid && !d.launchAssault);
        }
    }
}

void NoRushCancelsAndNeverLaunches() {
    RTSAIPlanner p;
    p.Reset(42, 0, 1, 0);
    auto w = ReadyWorld();
    w.noRush = true;
    ReachStage(p, w);
    for (float time : {240.0f, 300.0f, 600.0f}) assert(At(p, w, time).phase == AIPhase::Stage);
    w.noRush = false;
    At(p, w, 601.0f);
    assert(At(p, w, 603.0f).launchProbe);
    w.noRush = true;
    const auto stopped = At(p, w, 604.0f);
    assert(stopped.phase == AIPhase::Recover && stopped.retreat);
    assert(At(p, w, 635.0f).phase == AIPhase::Stage);
    assert(At(p, w, 1000.0f).phase == AIPhase::Stage);

    p.Reset(42, 0, 1, 0);
    w = ReadyWorld();
    ReachAssault(p, w);
    w.noRush = true;
    assert(At(p, w, 268.0f).retreat);
}

void ProbeTimeoutAndFreshIntel() {
    RTSAIPlanner p;
    p.Reset(42, 0, 1, 0);
    auto w = ReadyWorld();
    w.targetKnown = false;
    w.intelAge = 1000.0f;
    ReachStage(p, w);
    At(p, w, 240.0f);
    assert(At(p, w, 242.0f).launchProbe);
    assert(At(p, w, 276.0f).phase == AIPhase::Probe);
    assert(At(p, w, 277.0f).phase == AIPhase::Stage);
    assert(At(p, w, 306.0f).phase == AIPhase::Stage);
    At(p, w, 307.0f);
    assert(At(p, w, 309.0f).launchProbe);
    w.targetKnown = true;
    // Knowing a location is insufficient if the observation has expired.
    assert(At(p, w, 319.0f).phase == AIPhase::Probe);
    w.intelAge = 0.0f;
    assert(At(p, w, 320.0f).phase == AIPhase::Stage);
    At(p, w, 332.0f);
    assert(At(p, w, 334.0f).launchAssault);
    w.intelAge = 121.0f;
    assert(At(p, w, 335.0f).retreat);
}

void DefenseInterruptAndRecovery() {
    RTSAIPlanner p;
    p.Reset(42, 0, 1, 0);
    auto w = ReadyWorld();
    ReachAssault(p, w);
    w.underAttack = true;
    const auto defense = At(p, w, 268.0f);
    assert(defense.phase == AIPhase::Recover && defense.emergencyDefense && defense.retreat);
    assert(defense.reserveFraction == 0.6f && defense.massReserve == 0.0f);
    w.underAttack = false;
    assert(At(p, w, 279.0f).emergencyDefense);
    assert(!At(p, w, 280.0f).emergencyDefense);
    assert(At(p, w, 298.0f).phase == AIPhase::Stage);
    At(p, w, 310.0f);
    // The defensive interruption invalidated the old probe confirmation.
    assert(At(p, w, 312.0f).launchProbe);
}

void LossesEconomyAndSortieDeadline() {
    for (int failure = 0; failure < 6; ++failure) {
        RTSAIPlanner p;
        p.Reset(42, 0, 1, 0);
        auto w = ReadyWorld();
        ReachAssault(p, w);
        switch (failure) {
        case 0: w.armyPower = 69.0f; break;
        case 1: w.armyHealthFraction = 0.54f; break;
        case 2: w.energyIncome = -1.0f; w.energy = 20.0f; break;
        case 3: w.massIncome = 0.0f; w.mass = 0.0f; break;
        case 4: w.reservePower = 5.0f; break;
        case 5: w.knownEnemyPower = 100.0f; break;
        }
        const auto retreat = At(p, w, 268.0f);
        assert(retreat.phase == AIPhase::Recover && retreat.retreat);
        if (failure == 2 || failure == 3) assert(At(p, w, 400.0f).phase == AIPhase::Recover);
    }
    RTSAIPlanner p;
    p.Reset(42, 0, 1, 0);
    auto w = ReadyWorld();
    ReachAssault(p, w);
    assert(At(p, w, 356.0f).retreat);
    // Destroyed factories route recovery back through their real prerequisites.
    w.landFactories = 0;
    assert(At(p, w, 386.0f).phase == AIPhase::Bootstrap);
    assert(p.Decision().desiredLandFactories >= 1);
}

void ThreatDelayAndNoFreeDifficultyProduction() {
    std::array<AIDecision, 3> initial;
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        RTSAIPlanner p;
        p.Reset(42, 0, difficulty, 0);
        auto w = ReadyWorld();
        initial[difficulty] = At(p, w, 0.0f);
        w.visibleThreatPower = 30.0f;
        assert(!At(p, w, 1.0f).emergencyDefense);
        const float delay = p.Decision().reactionDelay;
        assert(!At(p, w, 1.0f + delay * 0.5f).emergencyDefense);
        assert(At(p, w, 1.0f + delay + 0.01f).emergencyDefense);
    }
    assert(initial[0].reactionDelay > initial[1].reactionDelay);
    assert(initial[1].reactionDelay > initial[2].reactionDelay);
    assert(initial[0].intelLifetime < initial[1].intelLifetime);
    assert(initial[1].intelLifetime < initial[2].intelLifetime);
    for (int difficulty = 1; difficulty < 3; ++difficulty) {
        assert(initial[difficulty].desiredTanks == initial[0].desiredTanks);
        assert(initial[difficulty].desiredPlanes == initial[0].desiredPlanes);
        assert(initial[difficulty].desiredLandFactories == initial[0].desiredLandFactories);
        assert(initial[difficulty].massReserve == initial[0].massReserve);
    }
}

void UtilityCommitmentAndAffordability() {
    RTSAIPlanner p;
    p.Reset(42, 0, 1, 0);
    auto w = ReadyWorld();
    const auto original = At(p, w, 0.0f).tactic;
    w.observedAirPower = 1000.0f;
    assert(At(p, w, 10.0f).tactic == original);
    assert(At(p, w, 44.0f).tactic == original);
    const auto air = At(p, w, 54.0f);
    assert(air.tactic == AITactic::AirDenial);
    assert(air.desiredAATurrets == 3 && air.desiredPlanes == 4);
    w.observedAirPower = 0.0f;
    assert(At(p, w, 70.0f).tactic == AITactic::AirDenial);
    assert(At(p, w, 100.0f).tactic != AITactic::AirDenial);

    p.Reset(42, 0, 1, 0);
    AIWorldState poor;
    poor.landFactories = 1;
    poor.safeExpansionAvailable = true;
    const auto bootstrap = At(p, poor, 0.0f);
    assert(bootstrap.desiredAirFactories == 0 && bootstrap.desiredShields == 0);
    assert(bootstrap.desiredLandFactories == 1);
    assert(bootstrap.massReserve == 0.0f && bootstrap.energyReserve == 0.0f);
    assert(bootstrap.tactic != AITactic::ForwardBastion);
    // Even large stockpiles do not justify more shield upkeep on a net deficit.
    poor.mass = poor.energy = 1000.0f;
    poor.energyIncome = -1.0f;
    assert(At(p, poor, 100.0f).desiredShields == 0);
}

void DeterminismWeightedChoiceAndReset() {
    bool varied = false;
    AITactic first = AITactic::Count;
    for (uint32_t seed = 0; seed < 64; ++seed) {
        RTSAIPlanner a, b;
        a.Reset(seed, 0, 1, 0);
        b.Reset(seed, 0, 1, 0);
        auto wa = ReadyWorld();
        auto wb = wa;
        wa.safeExpansionAvailable = wb.safeExpansionAvailable = true;
        for (float time : {0.0f, 15.0f, 35.0f, 80.0f, 240.0f, 242.0f, 252.0f, 264.0f, 266.0f}) {
            const auto da = At(a, wa, time);
            const auto db = At(b, wb, time);
            assert(da.phase == db.phase && da.tactic == db.tactic);
            assert(da.launchProbe == db.launchProbe && da.launchRaid == db.launchRaid && da.launchAssault == db.launchAssault);
            assert(da.desiredTanks == db.desiredTanks && da.desiredShields == db.desiredShields);
            assert(std::strcmp(da.reason, db.reason) == 0);
            if (time == 0.0f) {
                if (first == AITactic::Count) first = da.tactic;
                varied = varied || da.tactic != first;
            }
        }
        a.Reset(seed, 0, 1, 0);
        b.Reset(seed, 0, 1, 0);
        assert(a.Decision().phase == AIPhase::Bootstrap && !a.Decision().retreat);
        assert(At(a, wa, 0.0f).tactic == At(b, wb, 0.0f).tactic);
    }
    assert(varied);
}

void RaidAndInvalidSamples() {
    RTSAIPlanner p;
    p.Reset(42, 1, 1, 0);
    auto w = ReadyWorld();
    ReachStage(p, w);
    At(p, w, 210.0f);
    const auto raid = At(p, w, 212.0f);
    assert(raid.launchRaid && !raid.launchProbe);
    // Repeated snapshots and invalid clock samples cannot replay a command.
    assert(!At(p, w, 212.0f).launchRaid);
    assert(!At(p, w, 211.0f).launchRaid);
    assert(!At(p, w, std::numeric_limits<float>::quiet_NaN()).launchRaid);
}
} // namespace

int main() {
    PreparationAndFiniteMapLiveness();
    EveryLaunchPrerequisiteMatters();
    NoRushCancelsAndNeverLaunches();
    ProbeTimeoutAndFreshIntel();
    DefenseInterruptAndRecovery();
    LossesEconomyAndSortieDeadline();
    ThreatDelayAndNoFreeDifficultyProduction();
    UtilityCommitmentAndAffordability();
    DeterminismWeightedChoiceAndReset();
    RaidAndInvalidSamples();
    std::cout << "RTS AI planner behavior tests passed\n";
}
