#pragma once

/**
 * @file CitySiege.hpp
 * @brief City hit points and the bombard-then-capture loop.
 *
 * Until Phase 3.2 a city had walls and nothing else: `requestAttack` demanded
 * an enemy unit on the target tile, so a city could not be shot at all, and
 * capture was "walk onto the tile once the walls are at 0".
 *
 * Civ VI keeps two bars. The walls soak ranged fire first; when they are gone
 * the same fire eats the city's own hit points; and only a melee unit standing
 * next to a city at 0 HP can walk in and take it. Ranged attackers are never
 * hurt by the city they shell, melee attackers always are.
 *
 * The city's defence strength is not stored: it is the garrison's best
 * defender, the wall tier and the owner's era, recomputed on demand.
 */

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <cstdint>

namespace aoc::game {
class City;
class GameState;
class Unit;
} // namespace aoc::game

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

class DiplomacyManager;

/// A city's own hit points, behind whatever walls it has.
struct CityCombatState {
    int32_t hp               = 120;
    int32_t maxHP            = 120;
    int32_t lastAttackedTurn = -1000; ///< Healing waits for a quiet turn.

    // -- Blockade (plan 3.4). Transient: recomputed every turn by
    //    updateBlockades, and deliberately not serialized, like
    //    lastAttackedTurn. A reloaded game recomputes both on its first turn.
    PlayerId blockadedBy    = INVALID_PLAYER; ///< The civ whose navy sits off the port
    int32_t  blockadedTurns = 0;              ///< Consecutive turns under blockade

    [[nodiscard]] bool isAlive() const { return this->hp > 0; }
    [[nodiscard]] float hpFraction() const {
        return (this->maxHP > 0) ? static_cast<float>(this->hp) / static_cast<float>(this->maxHP)
                                 : 0.0f;
    }
};

/// Hit points a city has before any of its own buildings are counted.
inline constexpr int32_t CITY_BASE_HP = 120;

/// Hit points a city regains per quiet turn.
inline constexpr int32_t CITY_HEAL_PER_TURN = 10;

/// Turns of blockade the people bear before the shortages show (plan 3.4).
inline constexpr int32_t BLOCKADE_AMENITY_TURNS = 5;

/// The amenity a blockaded city loses once it has borne that long.
inline constexpr float BLOCKADE_AMENITY_PENALTY = 1.0f;

/// Turns a blockaded sea lane keeps a Trader waiting before it gives up.
inline constexpr int32_t BLOCKADE_ABANDON_TURNS = 20;

/// Turns after an attack during which a city does not heal.
inline constexpr int32_t CITY_HEAL_DELAY_TURNS = 4;

/// What one attack did to a city.
struct CityAttackResult {
    int32_t wallDamage     = 0;
    int32_t cityDamage     = 0;
    int32_t attackerDamage = 0;
    bool captured          = false;
    bool repelled          = false; ///< Melee turned back by intact walls.
};

/// The city's strength when it is shot at: its owner's era, its wall tier and
/// the best defender standing on the tile.
[[nodiscard]] int32_t cityDefenceStrength(const aoc::game::GameState& gameState,
                                          const aoc::game::City& city);

/// The enemy city standing on `at`, or nullptr. City-state and barbarian
/// cities count; your own never does.
[[nodiscard]] aoc::game::City* enemyCityAt(aoc::game::GameState& gameState, PlayerId viewer,
                                           hex::AxialCoord at);

/// Resolve one attack by `attacker` on `city`. Ranged fire hits the walls
/// first and spills into the city's hit points once they are down; melee is
/// repelled while the walls stand, and captures the city when its hit points
/// reach zero.
CityAttackResult resolveAttackOnCity(aoc::game::GameState& gameState, aoc::Random& rng,
                                     aoc::map::HexGrid& grid, aoc::game::Unit& attacker,
                                     aoc::game::City& city, int32_t currentTurn);

/// A military unit pressing into an enemy city on the map: the walls first,
/// then the city's hit points, and a capture when both are gone. Deterministic
/// on purpose -- Movement has no RNG, and threading one through it would
/// change every draw order in the game.
CityAttackResult pressIntoCity(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                               aoc::game::Unit& attacker, aoc::game::City& city,
                               int32_t currentTurn);

/// Hand `city` to `captor`'s owner: population, queue, loyalty, walls, the
/// tile footprint, the era-score award and the elimination check. Shared by
/// the melee capture and the walk-in in Movement.cpp.
void captureCity(aoc::game::GameState& gameState, aoc::map::HexGrid& grid, aoc::game::Unit& captor,
                 aoc::game::City& city);

/// Per-turn regeneration for every city `player` owns, skipping those attacked
/// within CITY_HEAL_DELAY_TURNS.
void healCities(aoc::game::GameState& gameState, PlayerId player, int32_t currentTurn);

/// The civ blockading `city`, or INVALID_PLAYER. A blockade is a military
/// naval unit of a civ at war with the city's owner, sitting on water beside
/// the city or on one of its Harbor tiles. Lowest player id wins a tie, so
/// the answer does not depend on iteration order.
[[nodiscard]] PlayerId blockaderOf(const aoc::game::GameState& gameState,
                                   const aoc::map::HexGrid& grid,
                                   const DiplomacyManager* diplomacy,
                                   const aoc::game::City& city);

/// Recompute every city's blockade state for this turn: who is blockading it
/// and for how long unbroken. Runs once, before the turn's per-player work,
/// so income, amenities and the trade step all see the same answer.
void updateBlockades(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                     const DiplomacyManager* diplomacy);

/// What a conqueror does with a city it holds.
enum class CityDisposition : uint8_t {
    Keep     = 0, ///< Govern it. The default, and what capture itself does.
    Raze     = 1, ///< Burn it down. The tile keeps its antiquity site.
    Liberate = 2, ///< Hand it back to the civ that founded it.
};

/// Raze or liberate a city `player` took from someone else. Keep is a no-op
/// because capture already keeps. Rejects a city the player founded, a city
/// they do not hold, and a liberation with nobody to liberate it to.
[[nodiscard]] ErrorCode requestCityDisposition(aoc::game::GameState& gameState,
                                               aoc::map::HexGrid& grid, PlayerId player,
                                               hex::AxialCoord at, CityDisposition disposition);

} // namespace aoc::sim
