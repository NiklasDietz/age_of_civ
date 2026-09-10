/**
 * @file GreatPeople.cpp
 * @brief Great Person definitions, point accumulation, recruitment, and activation.
 */

#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <vector>

namespace aoc::sim {

// ============================================================================
// Great Person definitions (18 total, ~3-4 per type)
// ============================================================================

static const std::array<GreatPersonDef, GREAT_PERSON_COUNT> s_greatPersonDefs = {{
    // Scientists (0-3)
    { .id = 0, .name = "Archimedes",      .type = GreatPersonType::Scientist, .abilityDescription = "Eureka! banks a discovery toward whatever is being researched.",            .researchFraction = 0.40f, .effect = GreatPersonEffect::Eureka},
    { .id = 1, .name = "Euclid",          .type = GreatPersonType::Scientist, .abilityDescription = "The Elements: +50% research on the current tech.",      .researchFraction = 0.50f},
    { .id = 2, .name = "Isaac Newton",    .type = GreatPersonType::Scientist, .abilityDescription = "Principia: +70% research on the current tech.",         .researchFraction = 0.70f},
    { .id = 3, .name = "Galileo Galilei", .type = GreatPersonType::Scientist, .abilityDescription = "Telescope: a long, gentle pulse of science.",           .researchFraction = 0.30f, .pulseAmount = 6.0f, .pulseTurns = 30},

    // Engineers (4-6)
    { .id = 4, .name = "Leonardo da Vinci", .type = GreatPersonType::Engineer, .abilityDescription = "Renaissance Man: +150 production to the nearest city.", .production = 150.0f},
    { .id = 5, .name = "James Watt",        .type = GreatPersonType::Engineer, .abilityDescription = "Steam Power: +100 production to the nearest city.",     .production = 100.0f},
    { .id = 6, .name = "Nikola Tesla",      .type = GreatPersonType::Engineer, .abilityDescription = "Alternating Current: +120 production to the nearest city.", .production = 120.0f},

    // Generals (7-9)
    { .id = 7, .name = "Sun Tzu",   .type = GreatPersonType::General, .abilityDescription = "Art of War: veteran experience to every unit within 2 hexes.", .effect = GreatPersonEffect::TrainTroops, .experience = 40},
    { 8, "Napoleon",  GreatPersonType::General, "Grande Armee: heal all units within 2 hexes to full."},
    { 9, "Patton",    GreatPersonType::General, "Blitzkrieg: heal all units within 2 hexes to full."},

    // Artists (10-13)
    {10, "Michelangelo",         GreatPersonType::Artist, "Masterpiece: culture bomb (claim tiles within 2 hexes)."},
    {11, "William Shakespeare",  GreatPersonType::Artist, "Globe Theatre: culture bomb (claim tiles within 2 hexes)."},
    {12, "Wolfgang A. Mozart",   GreatPersonType::Artist, "Symphony: culture bomb (claim tiles within 2 hexes)."},
    {13, "Rembrandt",            GreatPersonType::Artist, "Night Watch: culture bomb (claim tiles within 2 hexes)."},

    // Merchants (14-17)
    { .id = 14, .name = "Marco Polo",          .type = GreatPersonType::Merchant, .abilityDescription = "Silk Road: +250 gold to the treasury.",        .gold = 250},
    { .id = 15, .name = "Adam Smith",          .type = GreatPersonType::Merchant, .abilityDescription = "Wealth of Nations: +200 gold to the treasury.", .gold = 200},
    { .id = 16, .name = "John D. Rockefeller", .type = GreatPersonType::Merchant, .abilityDescription = "Standard Oil: +300 gold to the treasury.",      .gold = 300},
    { .id = 17, .name = "Mansa Musa",          .type = GreatPersonType::Merchant, .abilityDescription = "Pilgrimage: faith rather than gold.",        .gold = 400, .faith = 250.0f, .effect = GreatPersonEffect::Pilgrimage},

    // Admirals (18-20)
    {18, "Themistocles",  GreatPersonType::Admiral, "Salamis: heal all ships within 2 hexes to full."},
    {19, "Horatio Nelson",GreatPersonType::Admiral, "Trafalgar: heal all ships within 2 hexes to full."},
    {20, "Yi Sun-sin",    GreatPersonType::Admiral, "Turtle Ship: heal all ships within 2 hexes to full."},

    // Prophets (21-23)
    { .id = 21, .name = "Siddhartha Gautama", .type = GreatPersonType::Prophet, .abilityDescription = "Enlightenment: +350 faith.", .faith = 350.0f},
    { .id = 22, .name = "Confucius",          .type = GreatPersonType::Prophet, .abilityDescription = "Analects: +300 faith.",      .faith = 300.0f},
    { .id = 23, .name = "Zoroaster",          .type = GreatPersonType::Prophet, .abilityDescription = "Avesta: +250 faith.",        .faith = 250.0f},

    // Writers (24-26)
    {24, "Homer",              GreatPersonType::Writer, "Iliad: a Great Work of Writing."},
    {25, "Murasaki Shikibu",   GreatPersonType::Writer, "Genji: a Great Work of Writing."},
    {26, "Leo Tolstoy",        GreatPersonType::Writer, "War and Peace: a Great Work of Writing."},

    // Musicians (27-29)
    {27, "Ludwig van Beethoven", GreatPersonType::Musician, "Ninth Symphony: a Great Work of Music."},
    {28, "Johann S. Bach",       GreatPersonType::Musician, "Mass in B minor: a Great Work of Music."},
    {29, "Frederic Chopin",      GreatPersonType::Musician, "Nocturnes: a Great Work of Music."},
}};

const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& allGreatPersonDefs() {
    return s_greatPersonDefs;
}

// ============================================================================
// Point accumulation
// ============================================================================

void accumulateGreatPeoplePoints(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    PlayerGreatPeopleComponent& gpComp = playerObj->greatPeople();

    // Tally district/building contributions across all of the player's cities
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) {
            continue;
        }

        // Wonders draw great people too. Until 2026-09-07 they did not: points
        // came from districts alone, so a city holding the Great Library and
        // Oxford University contributed nothing toward a Scientist.
        for (const WonderId built : cityPtr->wonders().wonders) {
            const GreatPersonType drawn = greatPersonForWonder(built);
            gpComp.points[static_cast<std::size_t>(drawn)] += WONDER_GREAT_PERSON_POINTS;
        }

        const CityDistrictsComponent& districts = cityPtr->districts();

        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            switch (district.type) {
                case DistrictType::Campus:
                    // Campus district: +2 Scientist points per building, +1 Artist per Library
                    for (BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Scientist)] += 2.0f;
                        // Library (BuildingId 7) also gives +1 Artist point
                        if (bid.value == 7) {
                            gpComp.points[static_cast<std::size_t>(GreatPersonType::Artist)] += 1.0f;
                        }
                    }
                    // Base campus contribution even without buildings
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Scientist)] += 1.0f;
                    }
                    break;

                case DistrictType::Industrial:
                    // Industrial district: +2 Engineer points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Engineer)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Engineer)] += 1.0f;
                    }
                    break;

                case DistrictType::Encampment:
                    // Encampment: +2 General points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::General)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::General)] += 1.0f;
                    }
                    break;

                case DistrictType::HolySite:
                    // Holy Site: +2 Prophet points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Prophet)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Prophet)] += 1.0f;
                    }
                    break;

                case DistrictType::Theatre:
                    // Theatre: writers and musicians both come out of it.
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Writer)] += 1.0f;
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Musician)] += 1.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Writer)] += 0.5f;
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Musician)] += 0.5f;
                    }
                    break;

                case DistrictType::Harbor:
                    // Harbor: +2 Admiral points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Admiral)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Admiral)] += 1.0f;
                    }
                    break;

                case DistrictType::Commercial:
                    // Commercial hub: +2 Merchant points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Merchant)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Merchant)] += 1.0f;
                    }
                    break;

                default:
                    break;
            }
        }
    }

    // H3.8: zero out any type whose roster was exhausted on a prior turn, so
    // district/building contributions don't keep sliding into a dead bucket.
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(GreatPersonType::Count);
         ++i) {
        if (gpComp.exhausted[i]) {
            gpComp.points[i] = 0.0f;
        }
    }
}

// ============================================================================
// Recruitment
// ============================================================================

/// Index into allGreatPersonDefs() of the `nth` figure of `type`, or -1 when
/// that type's line is spent.
[[nodiscard]] int32_t offeredDefId(GreatPersonType type, int32_t nth) {
    const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& defs = allGreatPersonDefs();
    int32_t seen = 0;
    for (uint8_t d = 0; d < GREAT_PERSON_COUNT; ++d) {
        if (defs[d].type != type) { continue; }
        if (seen == nth) { return static_cast<int32_t>(d); }
        ++seen;
    }
    return -1;
}

void checkGreatPeopleRecruitment(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    PlayerGreatPeopleComponent& gpComp = playerObj->greatPeople();

    // Find the player's capital (first city) for spawn location
    hex::AxialCoord spawnPos = {0, 0};
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr != nullptr) {
            spawnPos = cityPtr->location();
            break;
        }
    }

    for (uint8_t typeIdx = 0;
         typeIdx < static_cast<uint8_t>(GreatPersonType::Count);
         ++typeIdx) {
        const GreatPersonType type = static_cast<GreatPersonType>(typeIdx);
        const float thresh = gpComp.threshold(type);

        if (gpComp.points[typeIdx] < thresh) {
            continue;
        }

        // A civ that passed on the current offer cannot take it. The pass is
        // spent when somebody else claims that figure.
        GlobalGreatPeopleRoster& roster = gameState.greatPeopleRoster();
        if (roster.hasPassed(type, player)) {
            continue;
        }

        // The offer is the world's next figure of this type, not this player's.
        // Recruitment used to index by `gpComp.recruited[typeIdx]`, privately
        // per player, so every civ walked the same list and two of them could
        // each hold their own Isaac Newton.
        // Era gate: the world stops offering a figure the age has left behind,
        // so an Ancient philosopher is not still on the table in the Atomic era.
        // Skipping forward costs the figure, which is the point: you missed them.
        EraId nowEra{0};
        for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
            if (other == nullptr) { continue; }
            const EraId theirs = effectiveEraFromTech(*other);
            if (theirs.value > nowEra.value) { nowEra = theirs; }
        }
        while (roster.claimed[typeIdx] < MAX_GP_PER_TYPE) {
            const int32_t candidate = offeredDefId(type, roster.claimed[typeIdx]);
            if (candidate < 0) { break; }
            const NamedGreatPersonDef& who = namedGreatPersonForCategory(
                categoryForGreatPersonType(type), roster.claimed[typeIdx]);
            const int32_t lag = static_cast<int32_t>(nowEra.value)
                                - static_cast<int32_t>(who.era.value);
            if (lag <= GP_ERA_LAG_LIMIT) { break; }
            roster.advance(type);
        }

        const int32_t defId = offeredDefId(type, roster.claimed[typeIdx]);

        // H3.8: roster exhausted or cap reached. Mark the type exhausted and
        // zero its point bucket so accumulation on later turns doesn't silently
        // drain into the void forever. threshold() now returns +inf for this
        // type so the recruitment branch never fires for it again.
        if (defId < 0 || roster.claimed[typeIdx] >= MAX_GP_PER_TYPE) {
            gpComp.exhausted[typeIdx] = true;
            gpComp.points[typeIdx] = 0.0f;
            continue;
        }

        const uint8_t defIdU = static_cast<uint8_t>(defId);

        // Spawn the great person as a unit owned by the player
        // Spawn as the dedicated Great Person marker type (UnitTypeId{102}).
        // Earlier code used UnitTypeId{50} which collides with Stealth Fighter
        // and caused unitTypeDef() lookups to return the Air unit for GPs.
        aoc::game::Unit& gpUnit = playerObj->addUnit(UnitTypeId{102}, spawnPos);
        // The nth person of a type takes the nth historical name of the matching
        // roster category; MAX_GP_PER_TYPE equals the per-category count, so the
        // twelve names of a category are used exactly once each.
        const NamedGreatPersonDef& named = namedGreatPersonForCategory(
            categoryForGreatPersonType(type), roster.claimed[typeIdx]);
        GreatPersonComponent& comp = gpUnit.greatPerson();
        comp.owner       = player;
        comp.defId       = defIdU;
        comp.namedId     = named.id;
        comp.position    = spawnPos;
        comp.isActivated = false;

        {
            VisibilityEvent ev{};
            ev.type = VisibilityEventType::GreatPersonSpawned;
            ev.location = spawnPos;
            ev.actor = player;
            // The notification names the person, so carry the roster id, not defId.
            ev.payload = static_cast<int32_t>(named.id);
            gameState.visibilityBus().emit(ev);
        }

        // Reset points and increment recruited count
        gpComp.points[typeIdx]    -= thresh;
        gpComp.recruited[typeIdx] += 1;
        // The world moves on, and every pass on the figure just taken is spent.
        roster.advance(type);
        addEraScore(*playerObj, gameState.currentTurn(), 2,
                    "Recruited " + std::string(named.name));

        LOG_INFO("Player %u recruited %s %.*s (%.*s)",
                 static_cast<unsigned>(player),
                 greatPersonCategoryName(categoryForGreatPersonType(type)),
                 static_cast<int>(named.name.size()), named.name.data(),
                 static_cast<int>(named.abilityName.size()), named.abilityName.data());
    }
}

// ============================================================================
// Activation
// ============================================================================

ErrorCode requestGreatPersonActivation(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                                       PlayerId owner, hex::AxialCoord unitAt) {
    aoc::game::Player* player = gameState.player(owner);
    if (player == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::Unit* unit = player->unitAt(unitAt);
    if (unit == nullptr || unit->typeId() != UnitTypeId{102}
        || unit->greatPerson().owner != owner || unit->greatPerson().isActivated) {
        return ErrorCode::InvalidUnitAction;
    }
    // The person acts where it stands, not where it appeared.
    unit->greatPerson().position = unit->position();
    activateGreatPerson(gameState, grid, *unit);
    return ErrorCode::Ok;
}

void activateGreatPerson(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                          aoc::game::Unit& gpUnit) {
    GreatPersonComponent& gp = gpUnit.greatPerson();
    if (gp.isActivated) {
        return;
    }

    const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& defs = allGreatPersonDefs();
    assert(gp.defId < GREAT_PERSON_COUNT);
    const GreatPersonDef& def = defs[gp.defId];

    aoc::game::Player* playerObj = gameState.player(gp.owner);
    if (playerObj == nullptr) {
        return;
    }

    // Who this actually is, not merely what type they are. Until 2026-09-08 the
    // named figure drove only the name, Great Work attribution and UI: every
    // Scientist ran the same numbers, so an ability line promising a specific
    // yield was decoration. `magnitudeScale` scales the type's own effect and
    // the bonus yields below are paid on top.
    const NamedGreatPersonDef& who = namedGreatPersonDef(gp.namedId);
    const float scale              = std::max(0.0f, who.magnitudeScale);

    // Bonus yields go only to figures on their type's default path. One with a
    // unique effect already does something of a different KIND -- Mansa Musa
    // brings faith where other merchants bring gold -- and paying the
    // type-shaped bonus on top would undo exactly the substitution that makes
    // the unique effect worth having.
    const bool takesTypeDefault = (def.effect == GreatPersonEffect::TypeDefault);
    if (takesTypeDefault && who.bonusCulture > 0.0f) {
        // Culture accumulates on the victory tracker; there is no per-turn
        // culture pool to add to.
        playerObj->victoryTracker().totalCultureAccumulated += who.bonusCulture;
    }
    if (takesTypeDefault && who.bonusFaith > 0.0f) {
        playerObj->faith().faith += who.bonusFaith;
    }
    if (takesTypeDefault && who.bonusScience > 0.0f) {
        playerObj->tech().researchProgress += who.bonusScience;
    }
    if (takesTypeDefault && who.bonusGold > 0) {
        playerObj->addGold(static_cast<CurrencyAmount>(who.bonusGold));
    }
    if (takesTypeDefault && (who.bonusCulture > 0.0f || who.bonusFaith > 0.0f ||
                             who.bonusScience > 0.0f || who.bonusGold > 0)) {
        LOG_INFO("%.*s: %.*s", static_cast<int>(who.name.size()), who.name.data(),
                 static_cast<int>(who.abilityDescription.size()), who.abilityDescription.data());
    }

    // A few named figures do something of a different KIND from the rest of
    // their type. These run instead of the type's behaviour, not alongside it.
    if (def.effect != GreatPersonEffect::TypeDefault) {
        switch (def.effect) {
            case GreatPersonEffect::Eureka: {
                // Bank every boost attached to what this civ is researching.
                // A banked boost is consumed when that research starts, so the
                // gift is never wasted on a tech already finished.
                PlayerEurekaComponent& boosts = playerObj->eureka();
                const TechId researching      = playerObj->tech().currentResearch;
                int32_t banked                = 0;
                for (const EurekaBoostDef& boost : getEurekaBoosts()) {
                    if (!researching.isValid() || boost.techId != researching) { continue; }
                    if (boosts.hasTriggered(boost.boostIndex)) { continue; }
                    boosts.markPending(boost.boostIndex);
                    ++banked;
                }
                LOG_INFO("Eureka: banked %d discoveries for player %u", banked,
                         static_cast<unsigned>(gp.owner));
                break;
            }
            case GreatPersonEffect::TrainTroops: {
                // Experience, not healing: a general who trains rather than mends.
                int32_t taught = 0;
                for (const std::unique_ptr<aoc::game::Unit>& unit : playerObj->units()) {
                    if (unit == nullptr || !unit->isMilitary()) { continue; }
                    if (grid.distance(unit->position(), gp.position) > GP_AURA_RADIUS) { continue; }
                    unit->experience().addExperience(def.experience);
                    ++taught;
                }
                LOG_INFO("Art of War: %d units gained %d experience", taught, def.experience);
                break;
            }
            case GreatPersonEffect::Pilgrimage: {
                playerObj->faith().faith += def.faith * scale;
                LOG_INFO("Pilgrimage: +%.0f faith", static_cast<double>(def.faith));
                break;
            }
            case GreatPersonEffect::TypeDefault:
                break;
        }
        gp.isActivated = true;
        playerObj->removeUnit(&gpUnit);
        return;
    }

    switch (def.type) {
        case GreatPersonType::Scientist: {
            // WP-A3: if nearest owned city has a Research Lab (BuildingId 12),
            // start a 20-turn sustained science pulse (+8/turn) instead of a
            // one-shot jolt. Otherwise fall back to +50% of current research.
            aoc::game::City* nearestCity = playerObj->nearestCity(grid, gp.position);
            // WP-A3: pulse triggers in any city with a science-focused
            // building (Library 7, University 19, Research Lab 12).
            // Audit 2026-04: Research-Lab-only gate never fired in 1000t
            // sims since Lab is rare late-game.
            const bool hasLab = (nearestCity != nullptr)
                && (nearestCity->districts().hasBuilding(BuildingId{12})
                 || nearestCity->districts().hasBuilding(BuildingId{19})
                 || nearestCity->districts().hasBuilding(BuildingId{7}));
            if (hasLab) {
                PlayerGreatPeopleComponent& gpComp = playerObj->greatPeople();
                gpComp.pulseScienceAmount = def.pulseAmount * scale;
                gpComp.pulseScienceTurns  = def.pulseTurns;
                LOG_INFO("Scientist: %d-turn +%.0f science pulse (science-building synergy)",
                         def.pulseTurns, static_cast<double>(def.pulseAmount));
            } else {
                PlayerTechComponent& tech = playerObj->tech();
                if (tech.currentResearch.isValid()) {
                    const float bonus =
                        effectiveResearchCost(tech, tech.currentResearch) * def.researchFraction * scale;
                    tech.researchProgress += bonus;
                    LOG_INFO("Scientist added %.0f research progress",
                             static_cast<double>(bonus));
                }
            }
            break;
        }

        case GreatPersonType::Engineer: {
            // WP-A3: find nearest owned city, grant +100 production. Additionally,
            // if the city has no Industrial district yet, create one at no cost
            // — "Renaissance Man" unlocks industry.
            aoc::game::City* nearestCity = playerObj->nearestCity(grid, gp.position);
            if (nearestCity != nullptr) {
                if (!nearestCity->production().isEmpty()) {
                    nearestCity->production().queue.front().progress += def.production * scale;
                }
                if (!nearestCity->districts().hasDistrict(DistrictType::Industrial)) {
                    CityDistrictsComponent::PlacedDistrict newDistrict;
                    newDistrict.type     = DistrictType::Industrial;
                    newDistrict.location = gp.position;
                    nearestCity->districts().districts.push_back(std::move(newDistrict));
                    LOG_INFO("Engineer placed free Industrial district in %s",
                             nearestCity->name().c_str());
                }
                LOG_INFO("Engineer added %.0f production to city queue",
                         static_cast<double>(def.production));
            }
            break;
        }

        case GreatPersonType::General: {
            // Heal all friendly units within 2 hexes to full
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerObj->units()) {
                if (unitPtr == nullptr) {
                    continue;
                }
                if (grid.distance(unitPtr->position(), gp.position) <= 2) {
                    unitPtr->setHitPoints(unitPtr->typeDef().maxHitPoints);
                }
            }
            LOG_INFO("General healed all nearby units to full");
            break;
        }

        case GreatPersonType::Admiral: {
            // Heal every friendly ship within 2 hexes to full.
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerObj->units()) {
                if (unitPtr == nullptr
                    || unitPtr->typeDef().unitClass != UnitClass::Naval) {
                    continue;
                }
                if (grid.distance(unitPtr->position(), gp.position) <= GP_AURA_RADIUS) {
                    unitPtr->setHitPoints(unitPtr->typeDef().maxHitPoints);
                }
            }
            LOG_INFO("Admiral healed every nearby ship");
            break;
        }

        case GreatPersonType::Artist: {
            // A work of Art in the nearest own city with a free Theatre slot; that is
            // what tourism counts since 2026-09-05. Without a slot, the culture bomb.
            if (aoc::game::City* home =
                    cityWithFreeGreatWorkSlot(*playerObj, grid, gp.position)) {
                const GreatWork work{GreatWorkType::Art, gp.owner, gp.namedId,
                                     gameState.currentTurn()};
                static_cast<void>(placeGreatWork(*home, work));
                LOG_INFO("Artist placed a work of Art in %s", home->name().c_str());
                break;
            }
            // Culture bomb: claim all unowned tiles within 2 hexes around the GP's position
            std::vector<hex::AxialCoord> tiles;
            hex::spiral(gp.position, 2, std::back_inserter(tiles));
            int32_t claimed = 0;
            for (const hex::AxialCoord& tile : tiles) {
                if (grid.isValid(tile)) {
                    const int32_t idx = grid.toIndex(tile);
                    if (grid.owner(idx) == INVALID_PLAYER) {
                        grid.setOwner(idx, gp.owner);
                        ++claimed;
                    }
                }
            }
            LOG_INFO("Artist culture-bombed %d tiles", claimed);
            break;
        }

        case GreatPersonType::Prophet: {
            // The faith comes first, because founding spends it. Then the
            // prophet walks the same path a player does: a pantheon if the civ
            // has none (which picks a follower belief properly, where setting
            // hasPantheon by hand left pantheonBelief unchosen at 255), then
            // the religion itself. When there is nothing left to found -- the
            // civ already has a religion, or the world has run out of them --
            // the faith is the whole gift.
            playerObj->faith().faith += def.faith * scale;
            if (!playerObj->faith().hasPantheon) {
                static_cast<void>(foundPantheonFor(gameState, gp.owner));
            }
            const ReligionId founded = foundReligionFor(gameState, gp.owner);
            if (founded != NO_RELIGION) {
                LOG_INFO("Prophet founded a religion for player %u",
                         static_cast<unsigned>(gp.owner));
            } else {
                LOG_INFO("Prophet: +%.0f faith, nothing left to found",
                         static_cast<double>(def.faith));
            }
            break;
        }

        case GreatPersonType::Writer: {
            if (aoc::game::City* home =
                    cityWithFreeGreatWorkSlot(*playerObj, grid, gp.position)) {
                const GreatWork work{GreatWorkType::Writing, gp.owner, gp.namedId,
                                     gameState.currentTurn()};
                static_cast<void>(placeGreatWork(*home, work));
                LOG_INFO("Writer placed a work of Writing in %s", home->name().c_str());
                break;
            }
            // Nowhere to shelve it: the words still move people, so they push
            // the civic the player is working through instead.
            playerObj->civics().researchProgress += 100.0f;
            LOG_INFO("Writer: no free slot, +100 civic progress instead");
            break;
        }

        case GreatPersonType::Musician: {
            if (aoc::game::City* home =
                    cityWithFreeGreatWorkSlot(*playerObj, grid, gp.position)) {
                const GreatWork work{GreatWorkType::Music, gp.owner, gp.namedId,
                                     gameState.currentTurn()};
                static_cast<void>(placeGreatWork(*home, work));
                LOG_INFO("Musician placed a work of Music in %s", home->name().c_str());
                break;
            }
            playerObj->civics().researchProgress += 100.0f;
            LOG_INFO("Musician: no free slot, +100 civic progress instead");
            break;
        }

        case GreatPersonType::Merchant: {
            // WP-A3: gold (per person, see GreatPersonDef) AND a permanent trade slot.
            playerObj->addGold(
                static_cast<CurrencyAmount>(static_cast<float>(def.gold) * scale));
            PlayerGreatPeopleComponent& gpComp = playerObj->greatPeople();
            gpComp.extraTradeSlots += 1;
            LOG_INFO("Merchant: +%lld gold + 1 permanent trade slot (total %d)",
                     static_cast<long long>(def.gold), gpComp.extraTradeSlots);
            break;
        }

        default:
            break;
    }

    gp.isActivated = true;

    // Remove the unit from the player's roster after activation
    playerObj->removeUnit(&gpUnit);
}

float greatPersonAuraBonus(const aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                           const aoc::game::Unit& unit) {
    const aoc::game::Player* owner = gameState.player(unit.owner());
    if (owner == nullptr) {
        return 0.0f;
    }
    // A ship looks for an Admiral, everything else for a General.
    const bool naval = unit.typeDef().unitClass == UnitClass::Naval;
    const GreatPersonType wanted = naval ? GreatPersonType::Admiral : GreatPersonType::General;
    const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& defs = allGreatPersonDefs();

    for (const std::unique_ptr<aoc::game::Unit>& other : owner->units()) {
        if (other == nullptr || other.get() == &unit) {
            continue;
        }
        const GreatPersonComponent& gp = other->greatPerson();
        if (gp.owner != unit.owner() || gp.isActivated || gp.defId >= GREAT_PERSON_COUNT) {
            continue;
        }
        if (defs[gp.defId].type != wanted) {
            continue;
        }
        if (grid.distance(other->position(), unit.position()) <= GP_AURA_RADIUS) {
            return GP_AURA_STRENGTH; // presence, not a stacking count
        }
    }
    return 0.0f;
}

ErrorCode requestRetireGreatPerson(aoc::game::GameState& gameState, PlayerId player,
                                   hex::AxialCoord at) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::Unit* unit = owner->unitAt(at);
    if (unit == nullptr || unit->typeId() != UnitTypeId{102}) {
        return ErrorCode::InvalidArgument;
    }
    const GreatPersonComponent& gp = unit->greatPerson();
    if (gp.owner != player || gp.isActivated) {
        return ErrorCode::InvalidUnitAction;
    }
    owner->monetary().treasury += GP_RETIRE_GOLD;
    owner->victoryTracker().eraVictoryPoints += GP_RETIRE_ERA_SCORE;
    LOG_INFO("Player %u retired a great person for %lld gold", static_cast<unsigned>(player),
             static_cast<long long>(GP_RETIRE_GOLD));
    owner->removeUnit(unit);
    return ErrorCode::Ok;
}

int64_t patronageGoldCost(const aoc::game::GameState& gameState, GreatPersonType type) {
    const GlobalGreatPeopleRoster& roster = gameState.greatPeopleRoster();
    const auto t = static_cast<std::size_t>(type);
    if (t >= roster.claimed.size()) { return 0; }
    if (offeredDefId(type, roster.claimed[t]) < 0) { return 0; }
    return PATRONAGE_BASE_GOLD
           + PATRONAGE_GOLD_PER_CLAIMED * static_cast<int64_t>(roster.claimed[t]);
}

float patronageFaithCost(const aoc::game::GameState& gameState, GreatPersonType type) {
    const GlobalGreatPeopleRoster& roster = gameState.greatPeopleRoster();
    const auto t = static_cast<std::size_t>(type);
    if (t >= roster.claimed.size()) { return 0.0f; }
    if (offeredDefId(type, roster.claimed[t]) < 0) { return 0.0f; }
    return PATRONAGE_BASE_FAITH
           + PATRONAGE_FAITH_PER_CLAIMED * static_cast<float>(roster.claimed[t]);
}

ErrorCode requestPatronage(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                           PlayerId player, GreatPersonType type) {
    static_cast<void>(grid);
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr || type >= GreatPersonType::Count) {
        return ErrorCode::InvalidArgument;
    }
    GlobalGreatPeopleRoster& roster = gameState.greatPeopleRoster();
    const auto t = static_cast<std::size_t>(type);
    if (roster.hasPassed(type, player)) {
        return ErrorCode::InvalidState; // you already declined this one
    }
    const int32_t defId = offeredDefId(type, roster.claimed[t]);
    if (defId < 0 || roster.claimed[t] >= MAX_GP_PER_TYPE) {
        return ErrorCode::InvalidState; // nobody left of this kind
    }

    // The Prophet is bought with faith; faith is its currency everywhere else
    // in the game, and gold-buying a prophet would read wrong.
    const bool withFaith = (type == GreatPersonType::Prophet);
    if (withFaith) {
        const float price = patronageFaithCost(gameState, type);
        if (owner->faith().faith < price) { return ErrorCode::InsufficientResources; }
        owner->faith().faith -= price;
    } else {
        const int64_t price = patronageGoldCost(gameState, type);
        if (owner->treasury() < price) { return ErrorCode::InsufficientResources; }
        owner->monetary().treasury -= price;
    }

    // Spawn them where the civ's first city stands, as recruitment does.
    hex::AxialCoord spawnPos{0, 0};
    for (const std::unique_ptr<aoc::game::City>& city : owner->cities()) {
        if (city != nullptr) { spawnPos = city->location(); break; }
    }
    aoc::game::Unit& gpUnit = owner->addUnit(UnitTypeId{102}, spawnPos);
    const NamedGreatPersonDef& named = namedGreatPersonForCategory(
        categoryForGreatPersonType(type), roster.claimed[t]);
    GreatPersonComponent& comp = gpUnit.greatPerson();
    comp.owner       = player;
    comp.defId       = static_cast<uint8_t>(defId);
    comp.namedId     = named.id;
    comp.position    = spawnPos;
    comp.isActivated = false;

    owner->greatPeople().recruited[t] += 1;
    owner->greatPeople().points[t] = 0.0f;
    roster.advance(type);

    LOG_INFO("Player %u patronised %.*s", static_cast<unsigned>(player),
             static_cast<int>(named.name.size()), named.name.data());
    return ErrorCode::Ok;
}

ErrorCode requestPassGreatPerson(aoc::game::GameState& gameState, PlayerId player,
                                 GreatPersonType type) {
    if (gameState.player(player) == nullptr || type >= GreatPersonType::Count) {
        return ErrorCode::InvalidArgument;
    }
    GlobalGreatPeopleRoster& roster = gameState.greatPeopleRoster();
    const auto t = static_cast<std::size_t>(type);
    if (offeredDefId(type, roster.claimed[t]) < 0) {
        return ErrorCode::InvalidState;
    }
    if (roster.hasPassed(type, player)) {
        return ErrorCode::InvalidState;
    }
    roster.pass(type, player);
    LOG_INFO("Player %u passed on the offered great person of type %u",
             static_cast<unsigned>(player), static_cast<unsigned>(t));
    return ErrorCode::Ok;
}

} // namespace aoc::sim
