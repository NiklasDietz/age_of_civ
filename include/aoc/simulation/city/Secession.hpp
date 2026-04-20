#pragma once

/// @file Secession.hpp
/// @brief City secession resolution — a distinct mechanic from loyalty decay.
///
/// Extracted from CityLoyalty.cpp so the "city flips owner" control flow lives
/// on its own axis. Loyalty is the trigger; secession is the consequence.

#include "aoc/core/Types.hpp"

namespace aoc::game { class GameState; class City; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

struct CityLoyaltyComponent;

/// Decide whether @p city should secede this turn and, if so, flip it.
///
/// Trigger (either suffices):
///   - loyalty <= 0
///   - sustained unrest (>=3 turns) AND city is >=5 hexes from capital
///
/// On secession:
///   - City flips to the foreign civ exerting the most loyalty pressure,
///     or becomes a Free City if none qualifies.
///   - Former owner records a LostCityToSecession grievance.
///   - Tile and surrounding-ring ownership updates.
///   - loyalty.loyalty snaps to 50.
///
/// @param secededThisTurn  pass the per-civ cap flag (one secession per turn)
///                         -- if true on entry, the call is a no-op.
/// @return true when secession occurred this turn.
[[nodiscard]] bool checkAndPerformSecession(aoc::game::GameState& gameState,
                                            aoc::map::HexGrid& grid,
                                            aoc::game::City& city,
                                            CityLoyaltyComponent& loyalty,
                                            PlayerId player,
                                            bool secededThisTurn);

} // namespace aoc::sim
