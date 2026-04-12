/**
 * @file LeaderPersonality.cpp
 * @brief Leader personality evaluation, agenda checks, and dialogue.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"

namespace aoc::sim {

const LeaderPersonalityDef& leaderPersonality(CivId civId) {
    if (civId < LEADER_PERSONALITY_COUNT) {
        return LEADER_PERSONALITIES[civId];
    }
    return LEADER_PERSONALITIES[0];
}

// ============================================================================
// Agenda evaluation
// ============================================================================

static bool checkCondition(const aoc::game::GameState& gameState,
                           AgendaCondition condition,
                           PlayerId leader, PlayerId target) {
    if (condition == AgendaCondition::None) {
        return false;
    }

    switch (condition) {
        case AgendaCondition::HasMoreMilitary: {
            int32_t leaderMil = 0;
            int32_t targetMil = 0;
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr == nullptr) { continue; }
                for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
                    if (unitPtr == nullptr) { continue; }
                    if (!isMilitary(unitPtr->typeDef().unitClass)) { continue; }
                    if (playerPtr->id() == leader) { ++leaderMil; }
                    if (playerPtr->id() == target) { ++targetMil; }
                }
            }
            return targetMil > leaderMil;
        }

        case AgendaCondition::HasLessMilitary: {
            int32_t leaderMil = 0;
            int32_t targetMil = 0;
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr == nullptr) { continue; }
                for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
                    if (unitPtr == nullptr) { continue; }
                    if (!isMilitary(unitPtr->typeDef().unitClass)) { continue; }
                    if (playerPtr->id() == leader) { ++leaderMil; }
                    if (playerPtr->id() == target) { ++targetMil; }
                }
            }
            return targetMil < leaderMil / 2;
        }

        case AgendaCondition::HasMoreLuxuries: {
            int32_t targetLux = 0;
            int32_t leaderLux = 0;
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr == nullptr) { continue; }
                const bool isLeader = (playerPtr->id() == leader);
                const bool isTarget = (playerPtr->id() == target);
                if (!isLeader && !isTarget) { continue; }
                for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
                    if (cityPtr == nullptr) { continue; }
                    for (uint16_t g = goods::GOLD_ORE; g <= goods::INCENSE; ++g) {
                        if (cityPtr->stockpile().getAmount(g) > 0) {
                            if (isTarget) { ++targetLux; }
                            if (isLeader) { ++leaderLux; }
                        }
                    }
                }
            }
            return targetLux > leaderLux;
        }

        case AgendaCondition::HasMoreCities: {
            int32_t targetCities = 0;
            int32_t leaderCities = 0;
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr == nullptr) { continue; }
                if (playerPtr->id() == target) { targetCities = playerPtr->cityCount(); }
                if (playerPtr->id() == leader) { leaderCities = playerPtr->cityCount(); }
            }
            return targetCities > leaderCities;
        }

        case AgendaCondition::HasHigherScience: {
            int32_t targetTechs = 0;
            int32_t leaderTechs = 0;
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr == nullptr) { continue; }
                const bool isLeader = (playerPtr->id() == leader);
                const bool isTarget = (playerPtr->id() == target);
                if (!isLeader && !isTarget) { continue; }
                int32_t count = 0;
                for (std::size_t b = 0; b < playerPtr->tech().completedTechs.size(); ++b) {
                    if (playerPtr->tech().completedTechs[b]) { ++count; }
                }
                if (isTarget) { targetTechs = count; }
                if (isLeader) { leaderTechs = count; }
            }
            return targetTechs > leaderTechs;
        }

        case AgendaCondition::HasHigherCulture:
            // Simplified: check wonder count as culture proxy
            return false;

        case AgendaCondition::HasStrongEconomy: {
            CurrencyAmount targetGDP = 0;
            CurrencyAmount leaderGDP = 0;
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr == nullptr) { continue; }
                if (playerPtr->id() == target) { targetGDP = playerPtr->monetary().gdp; }
                if (playerPtr->id() == leader) { leaderGDP = playerPtr->monetary().gdp; }
            }
            return targetGDP > leaderGDP;
        }

        case AgendaCondition::IsAtWarWithAnyone:
            return false;  // Would need DiplomacyManager access

        case AgendaCondition::HasDifferentReligion:
            return false;  // Simplified

        case AgendaCondition::HasDifferentGovernment:
            return false;  // Would need government comparison

        case AgendaCondition::IsTradePartner:
            return false;  // Would need trade route check

        case AgendaCondition::HasNuclearWeapons: {
            const aoc::game::Player* targetPlayer = gameState.player(target);
            if (targetPlayer == nullptr) { return false; }
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : targetPlayer->units()) {
                if (unitPtr == nullptr) { continue; }
                if (unitPtr->nuclear().equipped) { return true; }
            }
            return false;
        }

        case AgendaCondition::HasColonies:
            return false;  // Would need economic zone check

        case AgendaCondition::IsReserveCurrency: {
            const aoc::game::Player* targetPlayer = gameState.player(target);
            if (targetPlayer == nullptr) { return false; }
            return targetPlayer->currencyTrust().isReserveCurrency;
        }

        default:
            return false;
    }
}

int32_t evaluateAgenda(const aoc::game::GameState& gameState,
                       PlayerId leader, PlayerId target) {
    const aoc::game::Player* leaderPlayer = gameState.player(leader);
    CivId leaderCiv = (leaderPlayer != nullptr) ? leaderPlayer->civId() : CivId{0};

    const LeaderPersonalityDef& personality = leaderPersonality(leaderCiv);
    int32_t modifier = 0;

    // Like condition: +15 diplomatic points
    if (checkCondition(gameState, personality.likeCondition, leader, target)) {
        modifier += 15;
    }

    // Dislike condition: -20 diplomatic points
    if (checkCondition(gameState, personality.dislikeCondition, leader, target)) {
        modifier -= 20;
    }

    return modifier;
}

// ============================================================================
// Dialogue
// ============================================================================

// Dialogue table: [civId][context] -> string
// Only a selection of key lines per leader

struct DialogueLine {
    uint8_t          civId;
    DialogueContext  context;
    std::string_view text;
};

constexpr DialogueLine DIALOGUE_TABLE[] = {
    // Rome - Trajan
    { 0, DialogueContext::FirstMeet,      "Ave! The roads of Rome stretch to meet you."},
    { 0, DialogueContext::DeclareWar,     "The legions march. Your cities will know Roman order."},
    { 0, DialogueContext::ProposePeace,   "Enough blood. Let us build roads, not graves."},
    { 0, DialogueContext::AgendaLike,     "Your empire impresses me. Perhaps we can build something greater together."},
    { 0, DialogueContext::AgendaDislike,  "You have no army to speak of. How do you expect to survive?"},
    { 0, DialogueContext::Greeting,       "What business brings you to the Senate today?"},

    // Egypt - Cleopatra
    { 1, DialogueContext::FirstMeet,      "The Nile's blessings extend to those who bring wealth."},
    { 1, DialogueContext::DeclareWar,     "You have spurned Egypt's generosity. Now face her wrath."},
    { 1, DialogueContext::ProposeTrade,   "Let our merchants enrich us both. What do you offer?"},
    { 1, DialogueContext::AgendaLike,     "Your treasury speaks well of your wisdom. Let us trade."},
    { 1, DialogueContext::AgendaDislike,  "Your coffers are empty. You have nothing I want."},
    { 1, DialogueContext::Greeting,       "Welcome to the court. What gifts have you brought?"},

    // China - Qin Shi Huang
    { 2, DialogueContext::FirstMeet,      "The Middle Kingdom acknowledges your existence."},
    { 2, DialogueContext::DeclareWar,     "You have disturbed the harmony. The wall will be built with your stones."},
    { 2, DialogueContext::AgendaLike,     "Your wonders and culture earn my respect."},
    { 2, DialogueContext::AgendaDislike,  "Your constant warmongering disgusts me. Civilize yourself."},
    { 2, DialogueContext::Greeting,       "Speak. The Emperor listens."},

    // Germany - Frederick
    { 3, DialogueContext::FirstMeet,      "Germany respects strength. Show me yours."},
    { 3, DialogueContext::DeclareWar,     "Efficiency demands your elimination. Nothing personal."},
    { 3, DialogueContext::ProposeTrade,   "A fair trade benefits both our industries."},
    { 3, DialogueContext::AgendaLike,     "Your military strength commands respect. We should be allies."},
    { 3, DialogueContext::AgendaDislike,  "Without an army, you are merely a target."},
    { 3, DialogueContext::Greeting,       "Get to the point. Time is production."},

    // Greece - Pericles
    { 4, DialogueContext::FirstMeet,      "Athens welcomes those who value wisdom and beauty."},
    { 4, DialogueContext::DeclareWar,     "You leave me no choice. Even Athena carried a spear."},
    { 4, DialogueContext::AgendaLike,     "Your culture shines! Let us celebrate together."},
    { 4, DialogueContext::AgendaDislike,  "Your government is a travesty. Democracy is the only way."},
    { 4, DialogueContext::Greeting,       "Come, let us discuss philosophy and the future."},

    // England - Victoria
    { 5, DialogueContext::FirstMeet,      "The Empire extends its hand. Will you take it?"},
    { 5, DialogueContext::DeclareWar,     "Britannia rules, and you have forgotten your place."},
    { 5, DialogueContext::ProposeTrade,   "Free trade benefits all nations. Shall we establish routes?"},
    { 5, DialogueContext::AgendaLike,     "A fellow trader! Let our ships fill the seas between us."},
    { 5, DialogueContext::AgendaDislike,  "You exploit your colonies. The Empire does not approve."},
    { 5, DialogueContext::Greeting,       "Tea? No? Then business it is."},

    // Japan - Hojo Tokimune
    { 6, DialogueContext::FirstMeet,      "The land of the rising sun greets you. Tread carefully."},
    { 6, DialogueContext::DeclareWar,     "The divine wind will sweep you from existence."},
    { 6, DialogueContext::AgendaLike,     "Your honor and strength are worthy of respect."},
    { 6, DialogueContext::AgendaDislike,  "Your heathen ways offend the spirits of my ancestors."},
    { 6, DialogueContext::Greeting,       "Speak with purpose, or do not speak at all."},

    // Persia - Cyrus
    { 7, DialogueContext::FirstMeet,      "The King of Kings offers you his friendship. A rare gift."},
    { 7, DialogueContext::DeclareWar,     "Surprise! Your wealth is now mine."},
    { 7, DialogueContext::ProposePeace,   "Perhaps we were too hasty. Let us talk terms."},
    { 7, DialogueContext::AgendaLike,     "Your prosperity catches my eye. Let us be friends... for now."},
    { 7, DialogueContext::AgendaDislike,  "Your military makes me... nervous."},
    { 7, DialogueContext::Greeting,       "Welcome, friend. Tell me everything about your kingdom."},

    // Aztec - Montezuma
    { 8, DialogueContext::FirstMeet,      "Montezuma demands tribute. What luxuries do you bring?"},
    { 8, DialogueContext::DeclareWar,     "The gods demand blood! Yours will do."},
    { 8, DialogueContext::AgendaDislike,  "You hoard luxuries that rightfully belong to the Aztec people!"},
    { 8, DialogueContext::Greeting,       "Show me your treasures, or leave."},

    // India - Gandhi
    { 9, DialogueContext::FirstMeet,      "Peace be upon you. Let us walk the path of non-violence."},
    { 9, DialogueContext::DeclareWar,     "You have forced my hand. Even peace has its limits."},
    { 9, DialogueContext::ProposePeace,   "Violence solves nothing. Let us find a better way."},
    { 9, DialogueContext::AgendaDislike,  "Nuclear weapons?! You endanger all of civilization!"},
    { 9, DialogueContext::AgendaLike,     "Your peaceful ways bring me great joy."},
    { 9, DialogueContext::Greeting,       "Be the change you wish to see in the world."},

    // Russia - Peter
    {10, DialogueContext::FirstMeet,      "Russia looks westward for knowledge. What can you teach us?"},
    {10, DialogueContext::DeclareWar,     "Winter is coming for you, and Russia IS winter."},
    {10, DialogueContext::AgendaLike,     "Your scientific achievements inspire us to greatness."},
    {10, DialogueContext::AgendaDislike,  "You lag behind in knowledge. How disappointing."},
    {10, DialogueContext::Greeting,       "What discoveries have you made recently?"},

    // Brazil - Pedro II
    {11, DialogueContext::FirstMeet,      "Brazil welcomes all with open arms and warm hearts!"},
    {11, DialogueContext::DeclareWar,     "With great sadness, I must defend my people. Forgive me."},
    {11, DialogueContext::AgendaLike,     "Your culture enriches the world! Let us celebrate together."},
    {11, DialogueContext::AgendaDislike,  "Your wars bring suffering to innocents. Please stop."},
    {11, DialogueContext::Greeting,       "Life is beautiful, is it not? How can I help you today?"},
};

constexpr int32_t DIALOGUE_TABLE_SIZE = sizeof(DIALOGUE_TABLE) / sizeof(DIALOGUE_TABLE[0]);

std::string_view getLeaderDialogue(CivId civId, DialogueContext context) {
    for (int32_t i = 0; i < DIALOGUE_TABLE_SIZE; ++i) {
        if (DIALOGUE_TABLE[i].civId == civId
            && DIALOGUE_TABLE[i].context == context) {
            return DIALOGUE_TABLE[i].text;
        }
    }
    // Fallback
    return "...";
}

// ============================================================================
// Hidden trait modifier
// ============================================================================

float hiddenTraitModifier(HiddenTrait trait, DialogueContext context) {
    switch (trait) {
        case HiddenTrait::Backstabber:
            if (context == DialogueContext::DeclareWar) { return 1.5f; }
            return 1.0f;
        case HiddenTrait::WonderHoarder:
            return 1.0f;  // Affects production priority, not dialogue
        case HiddenTrait::NukeHappy:
            if (context == DialogueContext::DeclareWar) { return 1.3f; }
            return 1.0f;
        case HiddenTrait::ParanoidDefender:
            if (context == DialogueContext::DeclareWar) { return 0.5f; }
            return 1.0f;
        case HiddenTrait::TradeAddict:
            if (context == DialogueContext::ProposeTrade) { return 2.0f; }
            return 1.0f;
        case HiddenTrait::Isolationist:
            if (context == DialogueContext::ProposeTrade) { return 0.3f; }
            return 1.0f;
        case HiddenTrait::Opportunist:
            if (context == DialogueContext::DeclareWar) { return 1.5f; }
            return 1.0f;
        case HiddenTrait::Perfectionist:
            if (context == DialogueContext::DeclareWar) { return 0.7f; }
            return 1.0f;
        default:
            return 1.0f;
    }
}

} // namespace aoc::sim
