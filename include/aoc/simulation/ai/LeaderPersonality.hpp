#pragma once

/**
 * @file LeaderPersonality.hpp
 * @brief Per-leader AI personality: agendas, traits, behavioral weights, dialogue.
 *
 * Each of the 12 leaders has:
 *   - A visible agenda: the player can see this and predict AI behavior
 *   - A hidden trait: randomly assigned at game start, revealed through play
 *   - Behavioral weights: multipliers on AI decision categories
 *   - Diplomatic dialogue: context-sensitive lines that reveal personality
 *
 * Behavioral weights (0.0 = never, 1.0 = normal, 2.0 = strongly prioritizes):
 *   - militaryAggression:   How eager to build armies and declare war
 *   - expansionism:         How eager to settle new cities
 *   - scienceFocus:         How much to prioritize tech research
 *   - cultureFocus:         How much to prioritize culture/wonders
 *   - economicFocus:        How much to prioritize trade/gold/production
 *   - diplomaticOpenness:   How willing to make deals and alliances
 *   - religiousZeal:        How much to prioritize religion
 *   - nukeWillingness:      How willing to use nuclear weapons (0.0 = never)
 *   - trustworthiness:      How likely to honor agreements (1.0 = always)
 *   - grudgeHolding:        How long grievances affect relations (0.0 = forgiving)
 *
 * Agendas define what the AI LIKES and DISLIKES in other players:
 *   - likeCondition: what earns positive modifier
 *   - dislikeCondition: what earns negative modifier
 *
 * The agenda system creates predictable-but-varied AI behavior. A player
 * who learns "Montezuma hates civs with more luxuries" can adjust strategy.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::ecs { class World; }

namespace aoc::sim {

// ============================================================================
// Hidden traits (randomly assigned at game start)
// ============================================================================

enum class HiddenTrait : uint8_t {
    None,
    Backstabber,     ///< May break alliances without warning
    WonderHoarder,   ///< Prioritizes wonders obsessively
    NukeHappy,       ///< Uses nukes at first opportunity
    ParanoidDefender,///< Builds massive military but rarely attacks
    TradeAddict,     ///< Will never embargo anyone, always wants deals
    Isolationist,    ///< Avoids alliances and trade
    Opportunist,     ///< Attacks weakened neighbors
    Perfectionist,   ///< Only declares war with overwhelming advantage

    Count
};

// ============================================================================
// Behavioral weights
// ============================================================================

struct LeaderBehavior {
    float militaryAggression = 1.0f;
    float expansionism       = 1.0f;
    float scienceFocus       = 1.0f;
    float cultureFocus       = 1.0f;
    float economicFocus      = 1.0f;
    float diplomaticOpenness = 1.0f;
    float religiousZeal      = 1.0f;
    float nukeWillingness    = 0.0f;
    float trustworthiness    = 1.0f;
    float grudgeHolding      = 0.5f;
};

// ============================================================================
// Agenda conditions
// ============================================================================

enum class AgendaCondition : uint8_t {
    None,
    HasMoreMilitary,       ///< Target has larger military
    HasLessMilitary,       ///< Target has smaller military
    HasMoreLuxuries,       ///< Target has more luxury resources
    HasMoreCities,         ///< Target has more cities (expansionist)
    HasHigherScience,      ///< Target has more techs researched
    HasHigherCulture,      ///< Target has more culture output
    HasStrongEconomy,      ///< Target has higher GDP
    IsAtWarWithAnyone,     ///< Target is at war (warmonger check)
    HasDifferentReligion,  ///< Target's dominant religion differs
    HasDifferentGovernment,///< Target has different government type
    IsTradePartner,        ///< Target has trade routes with this leader
    HasNuclearWeapons,     ///< Target has nukes
    HasColonies,           ///< Target has economic zones in other civs
    IsReserveCurrency,     ///< Target holds reserve currency status
};

// ============================================================================
// Leader personality definition
// ============================================================================

struct LeaderPersonalityDef {
    CivId              civId;
    std::string_view   agendaName;
    std::string_view   agendaDescription;
    AgendaCondition    likeCondition;
    AgendaCondition    dislikeCondition;
    LeaderBehavior     behavior;
};

inline constexpr LeaderPersonalityDef LEADER_PERSONALITIES[] = {
    // 0: Rome - Trajan
    {CivId{0}, "Optimus Princeps",
     "Respects civilizations with large, well-connected empires. Dislikes small, isolated civs.",
     AgendaCondition::HasMoreCities, AgendaCondition::HasLessMilitary,
     {1.3f, 1.8f, 1.0f, 1.0f, 1.2f, 1.0f, 0.5f, 0.3f, 0.8f, 0.6f}},

    // 1: Egypt - Cleopatra
    {CivId{1}, "Queen of the Nile",
     "Respects civilizations with strong economies and trade. Dislikes weak economies.",
     AgendaCondition::HasStrongEconomy, AgendaCondition::HasLessMilitary,
     {0.7f, 1.0f, 1.0f, 1.5f, 1.8f, 1.3f, 0.8f, 0.0f, 0.9f, 0.4f}},

    // 2: China - Qin Shi Huang
    {CivId{2}, "Wall Builder",
     "Respects civilizations with strong defenses and many wonders. Dislikes warmongers.",
     AgendaCondition::HasHigherCulture, AgendaCondition::IsAtWarWithAnyone,
     {0.8f, 1.2f, 1.5f, 1.3f, 1.2f, 0.7f, 0.5f, 0.2f, 1.0f, 0.8f}},

    // 3: Germany - Frederick
    {CivId{3}, "Iron Chancellor",
     "Respects militarily powerful civilizations. Dislikes those without armies.",
     AgendaCondition::HasMoreMilitary, AgendaCondition::HasLessMilitary,
     {1.5f, 1.3f, 1.3f, 0.8f, 1.5f, 0.8f, 0.3f, 0.5f, 0.9f, 0.7f}},

    // 4: Greece - Pericles
    {CivId{4}, "Delian League",
     "Respects civilizations with high culture and science. Dislikes cultural backwaters.",
     AgendaCondition::HasHigherCulture, AgendaCondition::HasDifferentGovernment,
     {0.6f, 0.8f, 1.6f, 1.8f, 0.9f, 1.5f, 0.7f, 0.0f, 1.0f, 0.3f}},

    // 5: England - Victoria
    {CivId{5}, "Sun Never Sets",
     "Respects trade partners and naval powers. Dislikes those who don't trade.",
     AgendaCondition::IsTradePartner, AgendaCondition::HasColonies,
     {1.2f, 1.5f, 1.2f, 1.2f, 1.7f, 1.2f, 0.5f, 0.3f, 0.7f, 0.5f}},

    // 6: Japan - Hojo Tokimune
    {CivId{6}, "Divine Wind",
     "Respects civilizations with strong military and culture. Dislikes those with different religion.",
     AgendaCondition::HasMoreMilitary, AgendaCondition::HasDifferentReligion,
     {1.6f, 0.9f, 1.3f, 1.5f, 1.0f, 0.7f, 1.3f, 0.4f, 1.0f, 0.9f}},

    // 7: Persia - Cyrus
    {CivId{7}, "Fall of Babylon",
     "Respects diplomatic and wealthy civilizations. Secretly plans surprise wars.",
     AgendaCondition::HasStrongEconomy, AgendaCondition::HasMoreMilitary,
     {1.4f, 1.3f, 1.0f, 1.0f, 1.3f, 1.4f, 0.8f, 0.2f, 0.5f, 0.6f}},

    // 8: Aztec - Montezuma
    {CivId{8}, "Tlatoani",
     "Dislikes civilizations with more luxury resources. Wants all luxuries for himself.",
     AgendaCondition::None, AgendaCondition::HasMoreLuxuries,
     {1.7f, 1.2f, 0.7f, 0.8f, 1.0f, 0.6f, 1.5f, 0.3f, 0.7f, 0.9f}},

    // 9: India - Gandhi
    {CivId{9}, "Peacekeeper",
     "Respects peaceful civilizations. Strongly dislikes warmongers and nuclear weapon holders.",
     AgendaCondition::None, AgendaCondition::HasNuclearWeapons,
     {0.2f, 0.7f, 1.3f, 1.3f, 1.0f, 1.8f, 1.6f, 0.0f, 1.0f, 0.2f}},

    // 10: Russia - Peter
    {CivId{10}, "The Grand Embassy",
     "Respects scientifically advanced civilizations. Dislikes those behind in tech.",
     AgendaCondition::HasHigherScience, AgendaCondition::HasLessMilitary,
     {1.3f, 1.5f, 1.7f, 0.8f, 1.2f, 1.0f, 0.7f, 0.4f, 0.8f, 0.6f}},

    // 11: Brazil - Pedro II
    {CivId{11}, "Magnanimous",
     "Respects civilizations with high culture and happy citizens. Dislikes warmongers.",
     AgendaCondition::HasHigherCulture, AgendaCondition::IsAtWarWithAnyone,
     {0.5f, 1.0f, 1.0f, 1.8f, 1.2f, 1.5f, 0.8f, 0.0f, 1.0f, 0.1f}},
};

inline constexpr int32_t LEADER_PERSONALITY_COUNT = 12;

// ============================================================================
// Diplomatic dialogue
// ============================================================================

enum class DialogueContext : uint8_t {
    FirstMeet,
    DeclareWar,
    ProposePeace,
    ProposeTrade,
    DenouncePlayer,
    AcceptDeal,
    RejectDeal,
    AgendaLike,
    AgendaDislike,
    Greeting,

    Count
};

/// Get a dialogue line for a leader in a given context.
/// Returns a string_view into static storage.
[[nodiscard]] std::string_view getLeaderDialogue(CivId civId, DialogueContext context);

// ============================================================================
// Personality-driven AI modifiers
// ============================================================================

/**
 * @brief Get the leader personality for a civilization.
 */
[[nodiscard]] const LeaderPersonalityDef& leaderPersonality(CivId civId);

/**
 * @brief Evaluate agenda-based diplomatic modifier.
 *
 * Checks the leader's like/dislike conditions against a target player.
 * Returns a modifier to diplomatic relation score (-30 to +20).
 *
 * @param world   ECS world.
 * @param leader  The AI leader evaluating.
 * @param target  The player being evaluated.
 * @return Diplomatic modifier (positive = friendly, negative = hostile).
 */
[[nodiscard]] int32_t evaluateAgenda(const aoc::ecs::World& world,
                                     PlayerId leader, PlayerId target);

/**
 * @brief Check if a leader's hidden trait affects a specific decision.
 *
 * @param trait    The leader's hidden trait.
 * @param context  What decision is being made.
 * @return Multiplier on the decision weight (1.0 = no effect).
 */
[[nodiscard]] float hiddenTraitModifier(HiddenTrait trait, DialogueContext context);

} // namespace aoc::sim
