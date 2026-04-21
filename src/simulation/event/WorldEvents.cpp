/**
 * @file WorldEvents.cpp
 * @brief World event definitions, triggers, and resolution.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/event/WorldEvents.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/core/Log.hpp"

#include <array>

namespace aoc::sim {

namespace {

constexpr std::array<WorldEventDef, WORLD_EVENT_COUNT> EVENT_DEFS = {{
    {WorldEventId::Plague, "The Plague",
     "A deadly disease has struck your capital. Thousands are falling ill.",
     {{"Quarantine", "Close borders, halt trade. Saves lives but hurts economy.",
       -50, -20, 0, 0, -1.0f, 0.0f, -1, 10},
      {"Pray for Deliverance", "Turn to religion. Less effective but no economic cost.",
       0, 0, 0, 0, 0.0f, 5.0f, -2, 5},
      {"Ignore It", "Focus on production. Many will die but the economy continues.",
       0, 0, 0, 0, -3.0f, -10.0f, -3, 0}},
     3},

    {WorldEventId::GoldRush, "Gold Rush!",
     "A massive gold deposit has been discovered in your territory! People flock to the site.",
     {{"Regulate Mining", "Government controls the mine. Steady income, less chaos.",
       200, 0, 0, 0, 0.0f, 0.0f, 1, 0},
      {"Free-for-All", "Let anyone mine. Massive immigration but crime and disorder.",
       100, 0, 0, 0, -2.0f, -5.0f, 3, 10},
      {"Nationalize", "Seize all gold for the state. Efficient but citizens resent it.",
       400, 0, 0, 0, -1.0f, -8.0f, 0, 0}},
     3},

    {WorldEventId::ArtisticRenaissance, "Artistic Renaissance",
     "A great artistic movement is flourishing in your cities!",
     {{"Patronize the Arts", "Fund artists and build galleries. Expensive but lasting.",
       -100, 0, 0, 200, 2.0f, 5.0f, 0, 15},
      {"Commercialize It", "Turn art into tourism. Less culture but more gold.",
       100, 0, 0, 50, 0.0f, 0.0f, 0, 10},
      {"Suppress It", "Artists are a distraction. Redirect labor to production.",
       0, 15, 0, -50, -1.0f, -3.0f, 0, 10}},
     3},

    {WorldEventId::FamineWarning, "Famine Warning",
     "Crop yields are declining. Your people face hunger.",
     {{"Import Food", "Buy food from trade partners. Expensive but prevents starvation.",
       -150, 0, 0, 0, 0.0f, 0.0f, 0, 0},
      {"Ration Food", "Strict rationing. Everyone eats but nobody's happy.",
       0, 0, 0, 0, -2.0f, -5.0f, 0, 8},
      {"Do Nothing", "Let the market sort it out. Some will starve.",
       0, 0, 0, 0, -3.0f, -10.0f, -2, 0}},
     3},

    {WorldEventId::ScientificBreakthrough, "Scientific Breakthrough!",
     "Your scientists have made an unexpected discovery!",
     {{"Fund Further Research", "Pour resources into the discovery.",
       -100, 0, 50, 0, 1.0f, 0.0f, 0, 10},
      {"Publish Openly", "Share with the world. Earns respect but competitors benefit.",
       0, 0, 30, 30, 0.0f, 0.0f, 0, 0}},
     2},

    {WorldEventId::TradeDisruption, "Trade Disruption",
     "Pirates and storms have disrupted your trade routes!",
     {{"Naval Escort", "Send warships to protect traders. Costly but effective.",
       -80, 0, 0, 0, 0.0f, 0.0f, 0, 0},
      {"Pay Tribute", "Bribe the pirates. Quick fix but encourages more piracy.",
       -40, 0, 0, 0, 0.0f, 0.0f, 0, 0},
      {"Reroute Trade", "Find new routes. Takes time but permanent solution.",
       0, -10, 0, 0, 0.0f, 0.0f, 0, 5}},
     3},

    {WorldEventId::ReligiousSchism, "Religious Schism",
     "A religious division is splitting your cities apart!",
     {{"Enforce Unity", "Crack down on the heretics. Order restored but resentment festers.",
       0, 0, 0, 0, -1.0f, 10.0f, 0, 10},
      {"Allow Freedom", "Let people worship as they please. Diverse but fractured.",
       0, 0, 0, 20, 1.0f, -5.0f, 0, 0},
      {"Mediate", "Find a compromise. Costs influence but maintains stability.",
       -50, 0, 0, 10, 0.0f, 0.0f, 0, 0}},
     3},

    {WorldEventId::MigrantWave, "Migrant Wave",
     "Refugees from a neighboring war are arriving at your borders!",
     {{"Welcome Them", "Open borders. Population grows but strain on resources.",
       0, 0, 0, 0, -1.0f, -3.0f, 3, 5},
      {"Selective Entry", "Accept skilled workers only. Slow but beneficial.",
       0, 5, 5, 0, 0.0f, 0.0f, 1, 0},
      {"Close Borders", "Turn them away. No impact but diplomatic cost.",
       0, 0, 0, 0, 0.0f, 0.0f, 0, 0}},
     3},

    {WorldEventId::IndustrialAccident, "Industrial Accident!",
     "An explosion at a factory has caused devastation!",
     {{"Compensation Fund", "Pay the workers and rebuild. Expensive but loyal.",
       -200, 0, 0, 0, 0.0f, 10.0f, -1, 0},
      {"Safety Regulations", "New rules slow production but prevent future accidents.",
       0, -10, 0, 0, 1.0f, 5.0f, 0, 15},
      {"Cover It Up", "Suppress the news. Cheap but risky if discovered.",
       0, 0, 0, 0, -2.0f, -8.0f, -1, 0}},
     3},

    {WorldEventId::DiplomaticIncident, "Diplomatic Incident",
     "A border skirmish with a neighboring civilization threatens relations!",
     {{"Apologize", "De-escalate. Lose face but maintain peace.",
       0, 0, 0, 0, 0.0f, 0.0f, 0, 0},
      {"Demand Reparations", "Assert your rights. May improve or worsen relations.",
       50, 0, 0, 0, 0.0f, 0.0f, 0, 0},
      {"Mobilize", "Show strength. Other civs will think twice before testing you.",
       -50, 0, 0, 0, 0.0f, 5.0f, 0, 5}},
     3},

    {WorldEventId::EconomicBoom, "Economic Boom!",
     "Your economy is experiencing unprecedented growth!",
     {{"Invest in Infrastructure", "Build for the future. Long-term production gains.",
       -100, 20, 0, 0, 1.0f, 0.0f, 0, 15},
      {"Tax the Windfall", "Fill the treasury while times are good.",
       300, 0, 0, 0, -1.0f, 0.0f, 0, 0}},
     2},

    {WorldEventId::VolcanicFertility, "Volcanic Fertility",
     "The ash from a recent eruption has made surrounding land incredibly fertile!",
     {{"Farm the Ash", "Expand agriculture on the new soil. Major food boost.",
       0, 0, 0, 0, 1.0f, 0.0f, 2, 0},
      {"Mine the Minerals", "Extract rare minerals from the volcanic deposits.",
       150, 10, 0, 0, 0.0f, 0.0f, 0, 10}},
     2},
}};

} // anonymous namespace

const WorldEventDef& worldEventDef(WorldEventId id) {
    return EVENT_DEFS[static_cast<uint8_t>(id)];
}

void checkWorldEvents(aoc::game::GameState& gameState, PlayerId player, int32_t turnNumber) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) { return; }

    PlayerEventComponent* events = &playerObj->events();
    if (events == nullptr) { return; }

    // Don't trigger if there's already a pending event
    if (events->pendingEvent != static_cast<WorldEventId>(255)) { return; }

    // Check triggers (deterministic hash-based). Each event can re-fire after
    // WORLD_EVENT_COOLDOWN_TURNS have passed since its last firing. Collect all
    // eligible events this turn, then pick one fairly via a secondary hash --
    // otherwise low-`e` events (Plague, GoldRush) would always claim the slot
    // and later events (Volcanic, Boom, Schism) would starve.
    constexpr uint32_t TRIGGER_THRESHOLD = 200000000u;  // ~4.6% chance per turn per event

    int32_t eligible[WORLD_EVENT_COUNT];
    int32_t eligibleCount = 0;
    for (int32_t e = 0; e < WORLD_EVENT_COUNT; ++e) {
        if (turnNumber - events->lastFiredTurn[e] < WORLD_EVENT_COOLDOWN_TURNS) { continue; }
        if (turnNumber <= 10 + e * 5) { continue; }

        const uint32_t hash = static_cast<uint32_t>(turnNumber) * 2654435761u
                            + static_cast<uint32_t>(e) * 104729u
                            + static_cast<uint32_t>(player) * 7919u;
        if ((hash % 4294967295u) < TRIGGER_THRESHOLD) {
            eligible[eligibleCount++] = e;
        }
    }

    if (eligibleCount == 0) { return; }

    const uint32_t pickHash = static_cast<uint32_t>(turnNumber) * 2246822507u
                            + static_cast<uint32_t>(player) * 3266489917u;
    const int32_t chosen = eligible[pickHash % static_cast<uint32_t>(eligibleCount)];

    events->pendingEvent = static_cast<WorldEventId>(chosen);
    events->pendingChoice = -1;
    // H5.9: stamp the cooldown on trigger, not on resolution. Save/load round
    // trips and rewinds previously bypassed the cooldown because the stamp only
    // landed after the player resolved the event.
    events->lastFiredTurn[chosen] = turnNumber;
    LOG_INFO("World event triggered for player %u: %.*s",
             static_cast<unsigned>(player),
             static_cast<int>(EVENT_DEFS[static_cast<std::size_t>(chosen)].title.size()),
             EVENT_DEFS[static_cast<std::size_t>(chosen)].title.data());
}

ErrorCode resolveWorldEvent(aoc::game::GameState& gameState, PlayerId player, int32_t choice) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) { return ErrorCode::InvalidArgument; }

    PlayerEventComponent* events = &playerObj->events();
    if (events->pendingEvent == static_cast<WorldEventId>(255)) {
        return ErrorCode::InvalidArgument;
    }

    const WorldEventDef& eventDef = worldEventDef(events->pendingEvent);
    if (choice < 0 || choice >= eventDef.choiceCount) {
        return ErrorCode::InvalidArgument;
    }

    const EventChoice& chosen = eventDef.choices[choice];

    // Apply gold change. H5.8: positive rewards are scaled down (50%) and
    // bump inflationRate so "free gold" events don't print currency into the
    // game without a counterparty. Negative gold passes through unchanged
    // because wealth destruction is a valid one-sided event outcome.
    if (chosen.goldChange > 0) {
        const int64_t gain = static_cast<int64_t>(chosen.goldChange) / 2;
        playerObj->monetary().treasury += gain;
        const float gdpRef = std::max(
            1.0f, static_cast<float>(playerObj->monetary().gdp));
        playerObj->monetary().inflationRate +=
            static_cast<float>(gain) / gdpRef * 0.02f;
    } else if (chosen.goldChange < 0) {
        playerObj->monetary().treasury += chosen.goldChange;
    }

    // Apply population change to capital
    if (chosen.populationChange != 0) {
        for (const std::unique_ptr<aoc::game::City>& city : playerObj->cities()) {
            if (city->isOriginalCapital()) {
                const int32_t newPop = std::max(1, city->population() + chosen.populationChange);
                city->setPopulation(newPop);
                break;
            }
        }
    }

    // Store timed effects
    if (chosen.effectDuration > 0) {
        events->activeProductionMod = chosen.productionChange;
        events->activeScienceMod = chosen.scienceChange;
        events->activeEffectTurns = chosen.effectDuration;
    }

    // Cooldown stamp already applied in checkWorldEvents on trigger (H5.9).
    events->pendingEvent = static_cast<WorldEventId>(255);
    events->pendingChoice = choice;

    return ErrorCode::Ok;
}

void tickWorldEvents(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        PlayerEventComponent& events = playerPtr->events();
        if (events.activeEffectTurns > 0) {
            --events.activeEffectTurns;
            if (events.activeEffectTurns <= 0) {
                events.activeProductionMod = 0;
                events.activeScienceMod = 0;
            }
        }
    }
}

} // namespace aoc::sim
