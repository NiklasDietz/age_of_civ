#include "aoc/balance/BalanceParams.hpp"

namespace aoc::balance {

BalanceParams& params() {
    static BalanceParams s_params;
    return s_params;
}

BalanceParams BalanceGenome::toParams() const {
    BalanceParams p;
    p.baseLoyalty              = this->g[0];
    p.loyaltyPressureRadius    = static_cast<int32_t>(this->g[1]);
    p.sustainedUnrestTurns     = static_cast<int32_t>(this->g[2]);
    p.distantCityThreshold     = static_cast<int32_t>(this->g[3]);
    p.cultureVictoryThreshold  = this->g[4];
    p.cultureVictoryMinWonders = static_cast<int32_t>(this->g[5]);
    p.cultureVictoryLeadRatio  = this->g[6];
    p.religionDominanceFrac    = this->g[7];
    p.spaceRaceCostMult        = this->g[8];
    p.chainOutputMult          = this->g[9];
    p.consumerDemandScale      = this->g[10];
    p.workerCapacityPerPop     = this->g[11];
    return p;
}

void BalanceGenome::fromParams(const BalanceParams& p) {
    this->g[0]  = p.baseLoyalty;
    this->g[1]  = static_cast<float>(p.loyaltyPressureRadius);
    this->g[2]  = static_cast<float>(p.sustainedUnrestTurns);
    this->g[3]  = static_cast<float>(p.distantCityThreshold);
    this->g[4]  = p.cultureVictoryThreshold;
    this->g[5]  = static_cast<float>(p.cultureVictoryMinWonders);
    this->g[6]  = p.cultureVictoryLeadRatio;
    this->g[7]  = p.religionDominanceFrac;
    this->g[8]  = p.spaceRaceCostMult;
    this->g[9]  = p.chainOutputMult;
    this->g[10] = p.consumerDemandScale;
    this->g[11] = p.workerCapacityPerPop;
}

BalanceBounds defaultBalanceBounds() {
    BalanceBounds b;
    // baseLoyalty: [2, 10]
    b.min[0]  = 2.0f;    b.max[0]  = 10.0f;
    // loyaltyPressureRadius: [5, 14]
    b.min[1]  = 5.0f;    b.max[1]  = 14.0f;
    // sustainedUnrestTurns: [2, 8]
    b.min[2]  = 2.0f;    b.max[2]  = 8.0f;
    // distantCityThreshold: [3, 10]
    b.min[3]  = 3.0f;    b.max[3]  = 10.0f;
    // cultureVictoryThreshold: [2000, 6000]
    b.min[4]  = 2000.0f; b.max[4]  = 6000.0f;
    // cultureVictoryMinWonders: [2, 6]
    b.min[5]  = 2.0f;    b.max[5]  = 6.0f;
    // cultureVictoryLeadRatio: [1.1, 2.0]
    b.min[6]  = 1.1f;    b.max[6]  = 2.0f;
    // religionDominanceFrac: [0.3, 0.8]
    b.min[7]  = 0.3f;    b.max[7]  = 0.8f;
    // spaceRaceCostMult: [0.5, 2.0]
    //
    // Upper bound raised from 1.5, which sat BELOW the shipped default of 1.70
    // and so put the tuned value outside the region the tuner may explore --
    // every tuned genome silently pulled it down to at most 1.5, undoing a
    // deliberate iteration (0.59 -> 1.55 -> 1.70, aimed at a ~22 % science-win
    // rate). Same defect religionDominanceFrac had at 0.08 against [0.3, 0.8];
    // found by a new test asserting the general property rather than either
    // instance.
    b.min[8]  = 0.5f;    b.max[8]  = 2.0f;
    // chainOutputMult: [0.75, 2.0]  — production-chain yield multiplier
    b.min[9]  = 0.75f;   b.max[9]  = 2.0f;
    // consumerDemandScale: [0.5, 2.5]  — pop-driven consumer drain scalar
    b.min[10] = 0.5f;    b.max[10] = 2.5f;
    // workerCapacityPerPop: [0.4, 2.0] — recipe slots per population point.
    // Lower bound just under the shipped 0.5 so the tuner can explore tighter
    // labour as well as looser; upper bound 2.0 because past about 1.5 the
    // measured constraint stops being labour and becomes buildings, so there is
    // nothing above that for the search to find.
    b.min[11] = 0.4f;    b.max[11] = 2.0f;
    return b;
}

} // namespace aoc::balance
