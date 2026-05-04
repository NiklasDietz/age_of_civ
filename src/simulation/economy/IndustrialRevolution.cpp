/**
 * @file IndustrialRevolution.cpp
 * @brief Industrial revolution detection and progression.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/core/Log.hpp"

#ifdef AOC_DIAG_IR
#include <cstdint>
#include <unordered_map>
#endif

namespace aoc::sim {

#ifdef AOC_DIAG_IR
namespace {
// Phase 1 diagnostic: emit a structured BLOCKED line when a player fails an
// IR check. Throttle per (player, nextRev, reason) so a 1500-turn sim does
// not produce >2k lines per civ. Throttle window: 50 turns.
//
// Format: "IR_BLOCKED player=<id> ir=<n> reason=<tag> detail=<int> turn=<t>"
// Reasons: tech, cityCount, good. Detail = TechId for tech, count for
// cityCount, GoodId for good. Plain LOG_INFO under AOC_DIAG_IR; production
// builds compile this out entirely.
struct DiagKey {
    uint32_t player;
    uint8_t  rev;
    uint8_t  reason;     // 0=tech, 1=cityCount, 2=good
    uint16_t detail;
    bool operator==(const DiagKey& other) const noexcept {
        return player == other.player && rev == other.rev
            && reason == other.reason && detail == other.detail;
    }
};
struct DiagKeyHash {
    size_t operator()(const DiagKey& k) const noexcept {
        // Fold into 64 bits before hashing — avoids reinterpret aliasing.
        uint64_t mixed = (uint64_t{k.player} << 32)
                       | (uint64_t{k.rev} << 24)
                       | (uint64_t{k.reason} << 16)
                       |  uint64_t{k.detail};
        return std::hash<uint64_t>{}(mixed);
    }
};
constexpr int32_t kDiagThrottleTurns = 50;

bool diagShouldEmit(uint32_t player, uint8_t rev, uint8_t reason,
                    uint16_t detail, int32_t turn) {
    // thread_local: aoc_simulate runs single-threaded per OMP_NUM_THREADS=1
    // in audit_matrix.sh; thread_local is still the safe default in case a
    // future change re-introduces parallelism over the player loop.
    thread_local std::unordered_map<DiagKey, int32_t, DiagKeyHash> lastEmit;
    DiagKey key{player, rev, reason, detail};
    auto it = lastEmit.find(key);
    if (it == lastEmit.end()) {
        lastEmit.emplace(key, turn);
        return true;
    }
    if (turn - it->second >= kDiagThrottleTurns) {
        it->second = turn;
        return true;
    }
    return false;
}
} // anonymous namespace
#endif // AOC_DIAG_IR

bool checkIndustrialRevolution(aoc::game::GameState& gameState, PlayerId player,
                               TurnNumber currentTurn) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return false;
    }

    PlayerIndustrialComponent& ind = playerObj->industrial();

    uint8_t nextRevId = static_cast<uint8_t>(ind.currentRevolution) + 1;
    if (nextRevId > static_cast<uint8_t>(IndustrialRevolutionId::Fifth)) {
        return false;
    }

    const RevolutionDef& rev = REVOLUTION_DEFS[nextRevId - 1];

    // Check tech requirements
    const PlayerTechComponent& playerTech = playerObj->tech();
    for (int32_t i = 0; i < 3; ++i) {
        TechId reqTech = rev.requirements.requiredTechs[i];
        if (reqTech.isValid() && !playerTech.hasResearched(reqTech)) {
#ifdef AOC_DIAG_IR
            if (diagShouldEmit(static_cast<uint32_t>(player), nextRevId, 0,
                               static_cast<uint16_t>(reqTech.value),
                               static_cast<int32_t>(currentTurn))) {
                LOG_INFO("IR_BLOCKED player=%u ir=%u reason=tech detail=%u turn=%d",
                         static_cast<unsigned>(player),
                         static_cast<unsigned>(nextRevId),
                         static_cast<unsigned>(reqTech.value),
                         static_cast<int>(currentTurn));
            }
#endif
            return false;
        }
    }

    // Check city count requirement
    int32_t cityCount = static_cast<int32_t>(playerObj->cities().size());
    if (cityCount < rev.requirements.minCityCount) {
#ifdef AOC_DIAG_IR
        if (diagShouldEmit(static_cast<uint32_t>(player), nextRevId, 1,
                           static_cast<uint16_t>(cityCount),
                           static_cast<int32_t>(currentTurn))) {
            LOG_INFO("IR_BLOCKED player=%u ir=%u reason=cityCount detail=%d turn=%d",
                     static_cast<unsigned>(player),
                     static_cast<unsigned>(nextRevId),
                     cityCount,
                     static_cast<int>(currentTurn));
        }
#endif
        return false;
    }

    // Check resource requirements. 2026-05-03: also accept "ever supplied"
    // via economy.everSupplied (cumulative goodId set), not just current
    // stockpile. Civs that produce a good and consume it the same turn
    // (e.g. Steel feeding Tools/Tank production) used to fail the snapshot
    // check despite having the manufacturing capability. Tech reach already
    // gates capability; goods check now confirms the chain ran at least
    // once. The boolean-set semantic replaces the older totalSupply integer
    // map which accidentally accumulated forever and was renamed to
    // lastTurnProduction for ComparativeAdvantage / ResourceCurse use.
    const aoc::sim::PlayerEconomyComponent& econ = playerObj->economy();
    for (int32_t i = 0; i < 3; ++i) {
        uint16_t reqGood = rev.requirements.requiredGoods[i];
        if (reqGood == 0xFFFF) { continue; }

        bool found = false;
        // Path A: in any city's stockpile right now.
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
            if (cityPtr == nullptr) { continue; }
            if (cityPtr->stockpile().getAmount(reqGood) > 0) {
                found = true;
                break;
            }
        }
        // Path B: or ever supplied at the player level (capture goods
        // produced and immediately consumed).
        if (!found && econ.everSupplied.count(reqGood) > 0) {
            found = true;
        }
        if (!found) {
#ifdef AOC_DIAG_IR
            if (diagShouldEmit(static_cast<uint32_t>(player), nextRevId, 2,
                               reqGood, static_cast<int32_t>(currentTurn))) {
                LOG_INFO("IR_BLOCKED player=%u ir=%u reason=good detail=%u turn=%d",
                         static_cast<unsigned>(player),
                         static_cast<unsigned>(nextRevId),
                         static_cast<unsigned>(reqGood),
                         static_cast<int>(currentTurn));
            }
#endif
            return false;
        }
    }

    ind.currentRevolution    = static_cast<IndustrialRevolutionId>(nextRevId);
    ind.turnAchieved[nextRevId] = static_cast<int32_t>(currentTurn);

    LOG_INFO("Player %u achieved the %.*s (Industrial Revolution #%u) on turn %d!",
             static_cast<unsigned>(player),
             static_cast<int>(rev.name.size()), rev.name.data(),
             static_cast<unsigned>(nextRevId),
             static_cast<int>(currentTurn));

    return true;
}

float revolutionPollutionMultiplier(const PlayerIndustrialComponent& ind) {
    float mult = 1.0f;
    for (uint8_t r = 1; r <= static_cast<uint8_t>(ind.currentRevolution); ++r) {
        mult *= REVOLUTION_DEFS[r - 1].bonuses.pollutionMultiplier;
    }
    return mult;
}

} // namespace aoc::sim
