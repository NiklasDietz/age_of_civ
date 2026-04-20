/**
 * @file Prestige.cpp
 * @brief Prestige victory accrual.
 */

#include "aoc/simulation/victory/Prestige.hpp"

#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"

#include <algorithm>

namespace aoc::sim {

namespace {

// Per-turn caps: sum == 13.0 maximum per turn. Game-length-dependent max
// prestige is therefore maxTurns * 13 (reaching it requires perfect play
// across every axis, which is unattainable -- the cap just prevents any
// single system from runaway scoring).
constexpr float CAP_SCIENCE    = 2.0f;
constexpr float CAP_CULTURE    = 2.0f;
constexpr float CAP_FAITH      = 1.0f;
constexpr float CAP_TRADE      = 3.0f;
constexpr float CAP_DIPLOMACY  = 2.0f;
constexpr float CAP_MILITARY   = 1.0f;
constexpr float CAP_GOVERNANCE = 2.0f;

float clampCap(float v, float cap) {
    if (v < 0.0f) { return 0.0f; }
    if (v > cap)  { return cap; }
    return v;
}

} // namespace

void processPrestige(aoc::game::GameState& gameState,
                      const aoc::map::HexGrid& grid,
                      const DiplomacyManager* diplomacy) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        aoc::game::Player* player = playerPtr.get();
        if (player == nullptr) { continue; }
        if (player->victoryTracker().isEliminated) { continue; }

        PlayerPrestigeComponent& pr = player->prestige();

        // --- Science: scaled output per turn -------------------------------
        const float sci = clampCap(player->sciencePerTurn(grid) / 20.0f,
                                   CAP_SCIENCE);
        pr.science += sci;

        // --- Culture -------------------------------------------------------
        const float cul = clampCap(player->culturePerTurn(grid) / 20.0f,
                                   CAP_CULTURE);
        pr.culture += cul;

        // --- Faith: use accumulated stock as proxy for faith engagement ----
        // Stock grows slowly; dividing by 500 produces ~0.5..1.0 for
        // religious civs, near 0 for atheist ones.
        const float fa = clampCap(player->faith().faith / 500.0f, CAP_FAITH);
        pr.faith += fa;

        // --- Trade: active agreements ------------------------------------
        int32_t activeAgreements = 0;
        for (const TradeAgreementDef& a : player->tradeAgreements().agreements) {
            if (a.isActive) { ++activeAgreements; }
        }
        const float trd = clampCap(0.4f * static_cast<float>(activeAgreements),
                                   CAP_TRADE);
        pr.trade += trd;

        // --- Diplomacy + Military: pairwise relations ----------------------
        int32_t allies = 0;
        int32_t openBorders = 0;
        int32_t wars = 0;
        if (diplomacy != nullptr) {
            const uint8_t pc = diplomacy->playerCount();
            const PlayerId me = player->id();
            if (me < pc) {
                for (uint8_t other = 0; other < pc; ++other) {
                    if (other == me) { continue; }
                    const PairwiseRelation& rel = diplomacy->relation(me, other);
                    if (rel.hasDefensiveAlliance
                        || rel.hasMilitaryAlliance
                        || rel.hasEconomicAlliance) {
                        ++allies;
                    }
                    if (rel.hasOpenBorders) { ++openBorders; }
                    if (rel.isAtWar)        { ++wars; }
                }
            }
        }
        const float dip = clampCap(
            0.5f * static_cast<float>(allies) + 0.2f * static_cast<float>(openBorders),
            CAP_DIPLOMACY);
        pr.diplomacy += dip;

        // Military: reward being at peace. Each active war penalises; if
        // at peace, full peace bonus. Caps at 1.0.
        const float mil = clampCap(0.5f - 0.15f * static_cast<float>(wars),
                                   CAP_MILITARY);
        pr.military += mil;

        // --- Governance: happy + loyalty-stable cities --------------------
        int32_t happyCities = 0;
        int32_t stableCities = 0;
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (city == nullptr) { continue; }
            if (city->happiness().happiness > 0.0f) { ++happyCities; }
            if (city->loyalty().loyalty >= 50.0f)   { ++stableCities; }
        }
        const float gov = clampCap(
            0.10f * static_cast<float>(happyCities)
            + 0.05f * static_cast<float>(stableCities),
            CAP_GOVERNANCE);
        pr.governance += gov;

        pr.total = pr.science + pr.culture + pr.faith + pr.trade
                 + pr.diplomacy + pr.military + pr.governance;
    }
}

} // namespace aoc::sim
