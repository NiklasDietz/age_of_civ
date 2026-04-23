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
    p.integrationThreshold     = this->g[7];
    p.integrationTurnsRequired = static_cast<int32_t>(this->g[8]);
    p.religionDominanceFrac    = this->g[9];
    p.spaceRaceCostMult        = this->g[10];
    p.chainOutputMult          = this->g[11];
    p.consumerDemandScale      = this->g[12];
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
    this->g[7]  = p.integrationThreshold;
    this->g[8]  = static_cast<float>(p.integrationTurnsRequired);
    this->g[9]  = p.religionDominanceFrac;
    this->g[10] = p.spaceRaceCostMult;
    this->g[11] = p.chainOutputMult;
    this->g[12] = p.consumerDemandScale;
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
    // integrationThreshold: [0.9, 2.0]
    b.min[7]  = 0.9f;    b.max[7]  = 2.0f;
    // integrationTurnsRequired: [4, 15]
    b.min[8]  = 4.0f;    b.max[8]  = 15.0f;
    // religionDominanceFrac: [0.3, 0.8]
    b.min[9]  = 0.3f;    b.max[9]  = 0.8f;
    // spaceRaceCostMult: [0.5, 1.5]
    b.min[10] = 0.5f;    b.max[10] = 1.5f;
    // chainOutputMult: [0.75, 2.0]  — production-chain yield multiplier
    b.min[11] = 0.75f;   b.max[11] = 2.0f;
    // consumerDemandScale: [0.5, 2.5]  — pop-driven consumer drain scalar
    b.min[12] = 0.5f;    b.max[12] = 2.5f;
    return b;
}

} // namespace aoc::balance
