#pragma once

/**
 * @file Confederation.hpp
 * @brief Persistent N-way alliance blocs ("Staatenbund" / EU-style unions).
 *
 * A Confederation is an explicit, opt-in multi-player alliance distinct from
 * the six pairwise alliance types. Members keep their own state, units, and
 * decision-making — the bond is diplomatic/virtual, not a state merger.
 *
 * Effects (in effect while active):
 *   - Co-win eligibility at turn limit. Shared-prestige gate uses a
 *     sqrt(N) diminishing factor so a 5-member bloc can't trivially
 *     outvote a lone front-runner.
 *   - Attacking any member triggers a war obligation for every other
 *     member (same mechanism as bilateral Military Alliances, but fanned
 *     out across the whole bloc).
 *   - Members cannot declare war on each other — forbidden for the life
 *     of the confederation.
 *
 * Formation prerequisites (enforced by `formConfederation`):
 *   - All members must be non-eliminated, non-city-state players.
 *   - Every pair of members must currently have stance >= Friendly
 *     AND have met.
 *   - Every pair must be connected by at least one completed trade
 *     route at formation (economic interdependence prereq).
 *   - Industrial era or later (techDef era >= 4 gate).
 *   - Minimum size = 2 (pairs allowed; co-win still needs size >= 3).
 *   - Maximum size = 5. Hard cap stops a "everyone but the leader"
 *     lock-in; outliers retain strategic options.
 *   - A player may belong to at most one active confederation at a
 *     time.
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }

namespace aoc::sim {

class DiplomacyManager;
struct AllianceObligationTracker;

/// Per-confederation record. Stored as `std::vector<ConfederationComponent>`
/// on GameState.
struct ConfederationComponent {
    uint32_t id = 0;              ///< Monotonic id; 0 means "unassigned".
    std::vector<PlayerId> members;
    int32_t formedTurn = 0;
    bool isActive = true;         ///< False when dissolved by war or attrition.
};

/// Upper bound on the member count of a single confederation. Prevents
/// everyone-but-one lock-in against a front-runner.
inline constexpr std::size_t CONFEDERATION_MAX_MEMBERS = 5;

/// Minimum combined size for co-win eligibility. Bilateral pacts still
/// form but don't qualify for the confederation victory path.
/// Audit 2026-04: was 3; pushed to 4 so Confederation needs a real
/// majority bloc rather than any tight trio of friendlies.
inline constexpr std::size_t CONFEDERATION_COWIN_MIN = 4;

/// Diminishing-share exponent applied to the combined-prestige sum at
/// co-win time. Effective bloc prestige = sum / sqrt(N). Set to 0.5 to
/// match the sqrt requirement; exposed as a constant so tuning doesn't
/// need a recompile chain across multiple files.
inline constexpr float CONFEDERATION_PRESTIGE_SQRT_EXP = 0.5f;

/// Form a new confederation. Returns InvalidArgument if any prereq
/// fails (see header doc); AllianceExists if any member is already in
/// another active confederation.
[[nodiscard]] ErrorCode formConfederation(aoc::game::GameState& gameState,
                                          const DiplomacyManager& diplomacy,
                                          const std::vector<PlayerId>& members,
                                          int32_t currentTurn);

/// Dissolve the confederation that contains `member`. No-op if the
/// player isn't in one. Used by war declaration and elimination hooks.
void dissolveConfederationFor(aoc::game::GameState& gameState, PlayerId member);

/// Look up the active confederation containing `player`. Returns
/// nullptr if none. Iterates the gameState vector; keeps the interface
/// simple rather than maintaining a separate per-player index.
[[nodiscard]] const ConfederationComponent* confederationForPlayer(
    const aoc::game::GameState& gameState, PlayerId player);

/// Per-turn tick. Responsibilities:
///   - Remove eliminated / invalid members.
///   - Auto-dissolve confederations that have dropped below size 2.
///   - (War obligations are generated at declareWar time, not here.)
void tickConfederations(aoc::game::GameState& gameState);

/// War-obligation fan-out. Called by declareWar when the target is in
/// a confederation. Pushes an obligation onto `tracker` for every
/// non-attacker member, mirroring the existing Military Alliance path.
/// No-op if `tracker` is nullptr or target has no confederation.
void onConfederationMemberAttacked(aoc::game::GameState& gameState,
                                   AllianceObligationTracker* tracker,
                                   PlayerId aggressor,
                                   PlayerId target,
                                   int32_t currentTurn);

} // namespace aoc::sim
