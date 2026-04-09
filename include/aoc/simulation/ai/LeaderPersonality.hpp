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
    // Core personality weights (0.0 = ignores, 1.0 = normal, 2.0 = obsessive)
    float militaryAggression = 1.0f;  ///< War declaration willingness
    float expansionism       = 1.0f;  ///< City settling priority
    float scienceFocus       = 1.0f;  ///< Tech research priority
    float cultureFocus       = 1.0f;  ///< Culture/wonder priority
    float economicFocus      = 1.0f;  ///< Trade/gold priority
    float diplomaticOpenness = 1.0f;  ///< Alliance/deal willingness
    float religiousZeal      = 1.0f;  ///< Religion priority
    float nukeWillingness    = 0.0f;  ///< Nuclear weapon usage
    float trustworthiness    = 1.0f;  ///< How likely to honor deals
    float grudgeHolding      = 0.5f;  ///< How long grievances affect relations

    // Tech route preference (which branch to prioritize in the tech tree)
    // These are multiplied with the base tech score to bias the AI's research selection
    float techMilitary    = 1.0f;  ///< Prefer military techs (Bronze Working, Gunpowder, etc.)
    float techEconomic    = 1.0f;  ///< Prefer economic techs (Currency, Banking, etc.)
    float techIndustrial  = 1.0f;  ///< Prefer production techs (Industrialization, etc.)
    float techNaval       = 1.0f;  ///< Prefer naval/exploration techs (Sailing, Cartography)
    float techInformation = 1.0f;  ///< Prefer information techs (Computers, Internet)

    // Production priority weights (multiplied with base priority scores)
    float prodSettlers    = 1.0f;  ///< Settler production weight
    float prodMilitary    = 1.0f;  ///< Military unit production weight
    float prodBuilders    = 1.0f;  ///< Builder production weight
    float prodBuildings   = 1.0f;  ///< Building construction weight
    float prodWonders     = 1.0f;  ///< Wonder construction weight
    float prodNaval       = 1.0f;  ///< Naval unit production weight
    float prodReligious   = 1.0f;  ///< Religious unit production weight

    // War and diplomacy thresholds
    float warDeclarationThreshold = 1.5f;  ///< Military advantage needed to declare war
    float peaceAcceptanceThreshold = 0.5f; ///< How readily to accept peace deals
    float allianceDesire = 1.0f;           ///< How eagerly to form alliances
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
    // 0: Rome - Trajan -- EXPANSIONIST BUILDER
    // Prioritizes roads, cities, production. Balanced military.
    {CivId{0}, "Optimus Princeps",
     "Respects civilizations with large, well-connected empires. Dislikes small, isolated civs.",
     AgendaCondition::HasMoreCities, AgendaCondition::HasLessMilitary,
     {1.3f, 1.8f, 1.0f, 1.0f, 1.2f, 1.0f, 0.5f, 0.3f, 0.8f, 0.6f,
      /*techMil*/1.2f, /*techEcon*/1.0f, /*techInd*/1.5f, /*techNav*/0.8f, /*techInfo*/0.8f,
      /*prodSet*/1.8f, /*prodMil*/1.2f, /*prodBld*/1.5f, /*prodBdg*/1.3f, /*prodWon*/1.0f, /*prodNav*/0.7f, /*prodRel*/0.3f,
      /*warThresh*/1.5f, /*peace*/0.6f, /*alliance*/1.0f}},

    // 1: Egypt - Cleopatra -- ECONOMIC TRADER
    // Prioritizes gold, trade, wonders. Low military.
    {CivId{1}, "Queen of the Nile",
     "Respects civilizations with strong economies and trade. Dislikes weak economies.",
     AgendaCondition::HasStrongEconomy, AgendaCondition::HasLessMilitary,
     {0.7f, 1.0f, 1.0f, 1.5f, 1.8f, 1.3f, 0.8f, 0.0f, 0.9f, 0.4f,
      0.7f, 1.8f, 1.0f, 1.3f, 1.0f,
      1.0f, 0.6f, 0.8f, 1.5f, 1.8f, 1.2f, 0.5f,
      2.0f, 0.3f, 1.3f}},

    // 2: China - Qin Shi Huang -- WONDER BUILDER / DEFENSIVE
    // Prioritizes wonders, walls, science. Defensive military.
    {CivId{2}, "Wall Builder",
     "Respects civilizations with strong defenses and many wonders. Dislikes warmongers.",
     AgendaCondition::HasHigherCulture, AgendaCondition::IsAtWarWithAnyone,
     {0.8f, 1.2f, 1.5f, 1.3f, 1.2f, 0.7f, 0.5f, 0.2f, 1.0f, 0.8f,
      0.8f, 1.0f, 1.5f, 0.6f, 1.3f,
      1.2f, 0.8f, 1.0f, 1.5f, 2.0f, 0.5f, 0.3f,
      2.5f, 0.4f, 0.7f}},

    // 3: Germany - Frederick -- INDUSTRIAL MILITARIST
    // Prioritizes factories, military, production. War-focused.
    {CivId{3}, "Iron Chancellor",
     "Respects militarily powerful civilizations. Dislikes those without armies.",
     AgendaCondition::HasMoreMilitary, AgendaCondition::HasLessMilitary,
     {1.5f, 1.3f, 1.3f, 0.8f, 1.5f, 0.8f, 0.3f, 0.5f, 0.9f, 0.7f,
      1.8f, 1.2f, 1.8f, 0.8f, 0.8f,
      1.2f, 1.8f, 1.0f, 1.5f, 0.5f, 0.8f, 0.2f,
      1.2f, 0.8f, 0.7f}},

    // 4: Greece - Pericles -- CULTURE / SCIENCE
    // Prioritizes culture, science, philosophy. Avoids war.
    {CivId{4}, "Delian League",
     "Respects civilizations with high culture and science. Dislikes cultural backwaters.",
     AgendaCondition::HasHigherCulture, AgendaCondition::HasDifferentGovernment,
     {0.6f, 0.8f, 1.6f, 1.8f, 0.9f, 1.5f, 0.7f, 0.0f, 1.0f, 0.3f,
      0.5f, 0.8f, 0.8f, 0.7f, 1.8f,
      0.8f, 0.5f, 0.7f, 1.8f, 1.5f, 0.5f, 0.5f,
      3.0f, 0.3f, 1.5f}},

    // 5: England - Victoria -- NAVAL TRADE EMPIRE
    // Prioritizes naval techs, trade routes, colonial expansion.
    {CivId{5}, "Sun Never Sets",
     "Respects trade partners and naval powers. Dislikes those who don't trade.",
     AgendaCondition::IsTradePartner, AgendaCondition::HasColonies,
     {1.2f, 1.5f, 1.2f, 1.2f, 1.7f, 1.2f, 0.5f, 0.3f, 0.7f, 0.5f,
      1.0f, 1.5f, 1.0f, 2.0f, 1.2f,
      1.5f, 1.0f, 1.0f, 1.3f, 1.0f, 1.8f, 0.3f,
      1.5f, 0.5f, 1.2f}},

    // 6: Japan - Hojo Tokimune -- MILITARY CULTURE WARRIOR
    // Prioritizes military + culture. Strong military + religious zeal.
    {CivId{6}, "Divine Wind",
     "Respects civilizations with strong military and culture. Dislikes those with different religion.",
     AgendaCondition::HasMoreMilitary, AgendaCondition::HasDifferentReligion,
     {1.6f, 0.9f, 1.3f, 1.5f, 1.0f, 0.7f, 1.3f, 0.4f, 1.0f, 0.9f,
      1.5f, 0.8f, 1.2f, 1.0f, 1.0f,
      0.9f, 1.6f, 0.8f, 1.3f, 1.3f, 1.0f, 1.5f,
      1.3f, 0.7f, 0.7f}},

    // 7: Persia - Cyrus -- DIPLOMATIC SURPRISE ATTACKER
    // Appears friendly, builds economy, then strikes. Low trustworthiness.
    {CivId{7}, "Fall of Babylon",
     "Respects diplomatic and wealthy civilizations. Secretly plans surprise wars.",
     AgendaCondition::HasStrongEconomy, AgendaCondition::HasMoreMilitary,
     {1.4f, 1.3f, 1.0f, 1.0f, 1.3f, 1.4f, 0.8f, 0.2f, 0.5f, 0.6f,
      1.3f, 1.5f, 1.0f, 0.8f, 0.8f,
      1.3f, 1.4f, 1.0f, 1.2f, 0.8f, 0.8f, 0.5f,
      1.0f, 0.4f, 1.5f}},

    // 8: Aztec - Montezuma -- AGGRESSIVE RELIGIOUS WARRIOR
    // Wants luxuries, fights for resources. High military + religion.
    {CivId{8}, "Tlatoani",
     "Dislikes civilizations with more luxury resources. Wants all luxuries for himself.",
     AgendaCondition::None, AgendaCondition::HasMoreLuxuries,
     {1.7f, 1.2f, 0.7f, 0.8f, 1.0f, 0.6f, 1.5f, 0.3f, 0.7f, 0.9f,
      1.8f, 0.7f, 0.8f, 0.5f, 0.5f,
      1.2f, 2.0f, 1.0f, 0.8f, 0.5f, 0.5f, 1.8f,
      1.0f, 0.8f, 0.5f}},

    // 9: India - Gandhi -- PEACEFUL RELIGIOUS SCIENTIST
    // Avoids war, focuses on religion + science + diplomacy.
    {CivId{9}, "Peacekeeper",
     "Respects peaceful civilizations. Strongly dislikes warmongers and nuclear weapon holders.",
     AgendaCondition::None, AgendaCondition::HasNuclearWeapons,
     {0.2f, 0.7f, 1.3f, 1.3f, 1.0f, 1.8f, 1.6f, 0.0f, 1.0f, 0.2f,
      0.3f, 1.0f, 0.8f, 0.5f, 1.5f,
      0.7f, 0.2f, 0.8f, 1.5f, 1.0f, 0.3f, 2.0f,
      5.0f, 0.2f, 1.8f}},

    // 10: Russia - Peter -- SCIENCE EXPANSIONIST
    // Prioritizes science + expansion. Moderate military for protection.
    {CivId{10}, "The Grand Embassy",
     "Respects scientifically advanced civilizations. Dislikes those behind in tech.",
     AgendaCondition::HasHigherScience, AgendaCondition::HasLessMilitary,
     {1.3f, 1.5f, 1.7f, 0.8f, 1.2f, 1.0f, 0.7f, 0.4f, 0.8f, 0.6f,
      1.2f, 1.0f, 1.5f, 0.8f, 1.8f,
      1.5f, 1.2f, 1.0f, 1.5f, 0.8f, 0.8f, 0.5f,
      1.5f, 0.5f, 1.0f}},

    // 11: Brazil - Pedro II -- CULTURAL PEACEMAKER
    // Culture + happiness focus. Avoids war, builds wonders.
    {CivId{11}, "Magnanimous",
     "Respects civilizations with high culture and happy citizens. Dislikes warmongers.",
     AgendaCondition::HasHigherCulture, AgendaCondition::IsAtWarWithAnyone,
     {0.5f, 1.0f, 1.0f, 1.8f, 1.2f, 1.5f, 0.8f, 0.0f, 1.0f, 0.1f,
      0.4f, 1.0f, 0.8f, 0.7f, 1.3f,
      1.0f, 0.4f, 0.8f, 1.5f, 1.8f, 0.5f, 0.8f,
      3.0f, 0.2f, 1.5f}},
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

// ============================================================================
// Game-length-scaled AI parameters
// ============================================================================

/// AI target values scaled by game length and leader personality.
struct AIScaledTargets {
    int32_t maxCities;          ///< How many cities the AI wants
    int32_t desiredMilitaryPerCity; ///< Military units per city
    int32_t settlePopThreshold; ///< Min population before building settler
    float   warThreshold;       ///< Military advantage needed to declare war
    float   techMilBias;        ///< Tech selection bias toward military
    float   techEconBias;       ///< Tech selection bias toward economy
    float   techScienceBias;    ///< Tech selection bias toward science
};

/// Compute AI targets based on leader personality (game length does NOT
/// affect how many cities/units -- only the cost multiplier makes things
/// take longer in Marathon/Eternal, so the PACE changes, not the goals).
/// @param behavior  Leader's behavioral weights.
[[nodiscard]] inline AIScaledTargets computeScaledTargets(const LeaderBehavior& behavior) {
    AIScaledTargets targets{};

    // Max cities: expansionist leaders want more (up to 8), cautious want less (3)
    targets.maxCities = static_cast<int32_t>(4.0f * behavior.expansionism);
    targets.maxCities = (targets.maxCities < 3) ? 3 : (targets.maxCities > 8) ? 8 : targets.maxCities;

    // Military per city: aggressive leaders maintain larger armies
    targets.desiredMilitaryPerCity = static_cast<int32_t>(
        2.0f * behavior.militaryAggression);
    targets.desiredMilitaryPerCity = (targets.desiredMilitaryPerCity < 1) ? 1 : targets.desiredMilitaryPerCity;

    // Settle population threshold: expansionists settle earlier
    targets.settlePopThreshold = static_cast<int32_t>(
        2.0f + (1.0f - behavior.expansionism) * 2.0f);
    targets.settlePopThreshold = (targets.settlePopThreshold < 2) ? 2 : targets.settlePopThreshold;

    targets.warThreshold = behavior.warDeclarationThreshold;
    targets.techMilBias = behavior.techMilitary;
    targets.techEconBias = behavior.techEconomic;
    targets.techScienceBias = behavior.techInformation;

    return targets;
}

} // namespace aoc::sim
