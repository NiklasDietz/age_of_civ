#pragma once

/**
 * @file UnitTypes.hpp
 * @brief Unit type definitions and their base stats.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::sim {

/// Unit class determines movement and combat rules.
enum class UnitClass : uint8_t {
    Melee,         ///< Close combat infantry
    Ranged,        ///< Archers, crossbowmen, gunpowder infantry
    Cavalry,       ///< Mounted units (horses, later motorcycles)
    Armor,         ///< Mechanized vehicles: tanks, APCs, IFVs
    Artillery,     ///< Siege and bombardment (catapults through MLRS)
    AntiCavalry,   ///< Spears, pikes, anti-tank weapons
    Air,           ///< Fixed-wing aircraft (fighters, bombers)
    Helicopter,    ///< Rotary-wing (gunships, transport)
    Naval,         ///< Ships and submarines
    Settler,       ///< Can found cities
    Scout,         ///< Exploration units
    Civilian,      ///< Builders, medics, great people
    Religious,     ///< Missionaries, apostles, inquisitors
    Trader,        ///< Trade units that carry goods between cities
    Logistics,     ///< WP-S2: military supply units (wagon, tanker, plane)
                   ///< — refills Encampment buffers from city stockpiles.

    Count
};

/// Unit state machine.
enum class UnitState : uint8_t {
    Idle,
    Moving,
    Fortified,
    Sleeping,
    Embarked,
};

/// Era that a unit belongs to (determines when it becomes available).
enum class UnitEra : uint8_t {
    Ancient,      ///< Era 0
    Classical,    ///< Era 1
    Medieval,     ///< Era 2
    Renaissance,  ///< Era 3
    Industrial,   ///< Era 4
    Modern,       ///< Era 5
    Atomic,       ///< Era 6
    Information,  ///< Era 7
};

/// Static definition of a unit type (loaded from data eventually).
/// Strategic resource needed to produce a unit.
struct UnitResourceReq {
    uint16_t goodId = 0xFFFF;  ///< 0xFFFF = no requirement
    int32_t  amount = 0;

    [[nodiscard]] constexpr bool isValid() const { return this->goodId != 0xFFFF && this->amount > 0; }
};

struct UnitTypeDef {
    UnitTypeId      id;
    std::string_view name;
    UnitClass       unitClass;
    UnitEra         era;
    int32_t         maxHitPoints;
    int32_t         combatStrength;
    int32_t         rangedStrength;   ///< 0 for melee units
    int32_t         range;            ///< 0 for melee units
    int32_t         movementPoints;
    int32_t         productionCost;
    TechId          requiredTech;     ///< Tech needed to build (invalid = no requirement)
    UnitTypeId      upgradesTo;       ///< Next unit in the upgrade chain (invalid = none)
    int32_t         upgradeCost;      ///< Gold cost to upgrade an existing unit

    /// Strategic resources consumed when producing this unit.
    /// Up to 2 different resource requirements (e.g., Tank: Steel + Fuel).
    UnitResourceReq resourceReqs[2] = {};

    /// Optional civic prereq. Default-invalid = no civic gate. Examples:
    /// Mercenary units (post-Mercenaries civic), Spy (post-Cryptography),
    /// Special Forces (post-Cold War civic).
    CivicId         requiredCivic{};

    /// Per-turn gold maintenance. Scales gently with era so late-game armies
    /// don't bankrupt the economy. Ancient=1, Classical=1, Medieval=2, Renaissance=2,
    /// Modern=3, Atomic=3, Information=4. This keeps armies affordable relative to
    /// income growth from population and commercial buildings.
    [[nodiscard]] constexpr int32_t maintenanceGold() const {
        if (this->unitClass == UnitClass::Settler || this->unitClass == UnitClass::Civilian
            || this->unitClass == UnitClass::Trader || this->unitClass == UnitClass::Scout
            || this->unitClass == UnitClass::Logistics) {
            return 0;  // Civilian units have no maintenance
        }
        return static_cast<int32_t>(this->era) / 2 + 1;
    }

    /// Whether this unit requires any strategic resources to produce.
    [[nodiscard]] constexpr bool hasResourceRequirement() const {
        return this->resourceReqs[0].isValid() || this->resourceReqs[1].isValid();
    }

    /// WP-P1: per-turn food cost for military units. Civilian/trader/scout
    /// units consume 0 (foraging implicit). Foot infantry 1, mounted/heavy
    /// 2, mech/armor 3. Fed from owner's aggregate stockpile each turn.
    [[nodiscard]] constexpr int32_t foodPerTurn() const {
        switch (this->unitClass) {
            case UnitClass::Settler:
            case UnitClass::Civilian:
            case UnitClass::Trader:
            case UnitClass::Scout:
            case UnitClass::Religious:
            case UnitClass::Logistics:
                return 0;
            case UnitClass::Cavalry:
            case UnitClass::Helicopter:
                return 2;
            case UnitClass::Armor:
            case UnitClass::Air:
            case UnitClass::Naval:
                return 3;
            case UnitClass::Melee:
            case UnitClass::Ranged:
            case UnitClass::AntiCavalry:
            case UnitClass::Artillery:
            default:
                return 1;
        }
    }
};

// Unit type IDs: keep stable for serialization. Gaps are fine.
// Format: {id, name, class, era, hp, melee, ranged, range, move, cost, reqTech, upgradesTo, upgradeCost}

inline constexpr int32_t UNIT_TYPE_COUNT = 68;
inline constexpr std::array<UnitTypeDef, 79> UNIT_TYPE_DEFS = {{
    // ========================================================================
    // MELEE INFANTRY: Warrior -> Swordsman -> Man-at-Arms -> Musketman -> Infantry -> Mech Infantry
    // ========================================================================
    {UnitTypeId{0},  "Warrior",         UnitClass::Melee,    UnitEra::Ancient,      100, 20,  0, 0, 2,  40, TechId{},   UnitTypeId{10}, 50},
    // 2026-05-03: melee chain rewired against post-tree-expansion IDs.
    // Swordsman gated by Iron Working (37, was Pottery 2). Man-at-Arms by
    // Castles (54, was Currency 5). Musketman by Gunpowder (10) — existing
    // Metallurgy(8) prereq drifts off Civ6's Gunpowder model.
    {UnitTypeId{10}, "Swordsman",       UnitClass::Melee,    UnitEra::Classical,    110, 35,  0, 0, 2,  90, TechId{37}, UnitTypeId{33}, 80,  {{60, 1}}},             // Iron Working
    {UnitTypeId{33}, "Man-at-Arms",     UnitClass::Melee,    UnitEra::Medieval,     120, 45,  0, 0, 2, 120, TechId{54}, UnitTypeId{34}, 100, {{64, 1}}},             // Castles
    {UnitTypeId{34}, "Musketman",       UnitClass::Melee,    UnitEra::Renaissance,  130, 55,  0, 0, 2, 160, TechId{10}, UnitTypeId{15}, 120, {{5, 1}}},              // Gunpowder
    {UnitTypeId{15}, "Infantry",        UnitClass::Melee,    UnitEra::Modern,       140, 70,  0, 0, 2, 250, TechId{12}, UnitTypeId{35}, 180, {{64, 1}}},             // 1 Steel
    {UnitTypeId{35}, "Mech Infantry",   UnitClass::Melee,    UnitEra::Information,  160, 85,  0, 0, 3, 350, TechId{20}, UnitTypeId{},   0,   {{64, 1}, {65, 1}}},    // 1 Steel + 1 Fuel

    // ========================================================================
    // RANGED INFANTRY: Slinger -> Archer -> Crossbowman -> Field Cannon -> Machine Gun -> Rocket Infantry
    // ========================================================================
    {UnitTypeId{1},  "Slinger",         UnitClass::Ranged,   UnitEra::Ancient,       80, 10, 15, 1, 2,  35, TechId{},   UnitTypeId{36}, 40},
    // 2026-05-03: ranged chain rewired. Archer by Archery (35, was AnimalHusb 1).
    // Crossbowman by Machinery (51, was Currency 5). Field Cannon by Ballistics
    // (60, was Metallurgy 8).
    {UnitTypeId{36}, "Archer",          UnitClass::Ranged,   UnitEra::Classical,     80, 12, 25, 2, 2,  60, TechId{35}, UnitTypeId{11}, 60},                          // Archery
    {UnitTypeId{11}, "Crossbowman",     UnitClass::Ranged,   UnitEra::Medieval,      90, 15, 35, 2, 2, 100, TechId{51}, UnitTypeId{37}, 80,  {{60, 1}}},             // Machinery
    {UnitTypeId{37}, "Field Cannon",    UnitClass::Ranged,   UnitEra::Renaissance,  100, 20, 45, 2, 2, 160, TechId{60}, UnitTypeId{38}, 120, {{60, 1}}},             // Ballistics
    {UnitTypeId{38}, "Machine Gun",     UnitClass::Ranged,   UnitEra::Modern,       100, 25, 60, 2, 2, 260, TechId{12}, UnitTypeId{39}, 180, {{64, 1}}},             // 1 Steel
    {UnitTypeId{39}, "Rocket Infantry", UnitClass::Ranged,   UnitEra::Atomic,       100, 30, 75, 2, 2, 350, TechId{18}, UnitTypeId{},   0,   {{64, 1}, {71, 1}}},    // 1 Steel + 1 Ammunition

    // ========================================================================
    // CAVALRY: Horseman -> Knight -> Cuirassier -> Cavalry -> Helicopter Gunship
    // ========================================================================
    // 2026-05-03: cavalry chain rewired. Horseman by Horseback Riding (40,
    // was AnimalHusb 1 which is its prereq). Knight by Stirrups (52,
    // was Currency 5). Cuirassier by Metal Casting (58, was Metallurgy 8).
    {UnitTypeId{4},  "Horseman",        UnitClass::Cavalry,  UnitEra::Ancient,      100, 28,  0, 0, 4,  80, TechId{40}, UnitTypeId{12}, 70,  {{4, 1}}},              // Horseback Riding
    {UnitTypeId{12}, "Knight",          UnitClass::Cavalry,  UnitEra::Medieval,     120, 45,  0, 0, 4, 150, TechId{52}, UnitTypeId{40}, 100, {{4, 1}, {60, 1}}},     // Stirrups
    {UnitTypeId{40}, "Cuirassier",      UnitClass::Cavalry,  UnitEra::Renaissance,  130, 55,  0, 0, 5, 200, TechId{58}, UnitTypeId{14}, 130, {{4, 1}}},              // Metal Casting
    {UnitTypeId{14}, "Cavalry",         UnitClass::Cavalry,  UnitEra::Industrial,   130, 62,  0, 0, 5, 250, TechId{11}, UnitTypeId{},   0,   {{4, 1}}},              // 1 Horses

    // ========================================================================
    // ARMOR (mechanized vehicles): Landship -> Tank -> Modern Armor -> Giant Death Robot
    // ========================================================================
    {UnitTypeId{41}, "Landship",        UnitClass::Armor,    UnitEra::Industrial,   150, 60,  0, 0, 3, 280, TechId{11}, UnitTypeId{17}, 150, {{64, 1}}},              // 1 Steel
    // 2026-05-03: armor rewired. Tank by Combustion (66). Modern Armor by
    // Combined Arms (69, was SurfacePlate 18 which is non-military tech).
    {UnitTypeId{17}, "Tank",            UnitClass::Armor,    UnitEra::Modern,       180, 80,  0, 0, 4, 350, TechId{66}, UnitTypeId{42}, 200, {{64, 1}, {65, 1}}},    // Combustion
    {UnitTypeId{42}, "Modern Armor",    UnitClass::Armor,    UnitEra::Atomic,       200, 95,  0, 0, 5, 450, TechId{69}, UnitTypeId{43}, 250, {{64, 2}, {65, 1}}},    // Combined Arms
    {UnitTypeId{43}, "Giant Death Robot",UnitClass::Armor,   UnitEra::Information,  250,120,  0, 0, 4, 600, TechId{22}, UnitTypeId{},   0,   {{103, 1}, {65, 2}}},   // 1 Industrial Equip + 2 Fuel

    // ========================================================================
    // ARTILLERY: Catapult -> Trebuchet -> Bombard -> Field Artillery -> Rocket Artillery -> MLRS
    // ========================================================================
    // 2026-05-03: artillery rewired. Catapult by Engineering (6). Trebuchet
    // by Military Tactics (53). Bombard by Metal Casting (58). Field
    // Artillery by Ballistics (60).
    {UnitTypeId{23}, "Catapult",        UnitClass::Artillery,UnitEra::Ancient,       70, 10, 25, 2, 2, 100, TechId{6},  UnitTypeId{24}, 60},                          // Engineering
    {UnitTypeId{24}, "Trebuchet",       UnitClass::Artillery,UnitEra::Medieval,      80, 12, 35, 2, 2, 150, TechId{53}, UnitTypeId{25}, 80,  {{60, 1}}},             // Military Tactics
    {UnitTypeId{25}, "Bombard",         UnitClass::Artillery,UnitEra::Renaissance,   90, 15, 50, 2, 2, 220, TechId{58}, UnitTypeId{16}, 120, {{60, 1}, {5, 1}}},     // Metal Casting
    {UnitTypeId{16}, "Field Artillery", UnitClass::Artillery,UnitEra::Industrial,    80, 15, 65, 3, 2, 280, TechId{60}, UnitTypeId{44}, 160, {{64, 1}}},             // Ballistics
    {UnitTypeId{44}, "Rocket Artillery",UnitClass::Artillery,UnitEra::Modern,        90, 18, 80, 3, 2, 380, TechId{15}, UnitTypeId{45}, 200, {{64, 1}, {71, 1}}},    // 1 Steel + 1 Ammunition
    {UnitTypeId{45}, "MLRS",            UnitClass::Artillery,UnitEra::Atomic,       100, 20,100, 4, 2, 480, TechId{18}, UnitTypeId{},   0,   {{64, 2}, {71, 2}}},    // 2 Steel + 2 Ammunition

    // ========================================================================
    // ANTI-CAVALRY: Spearman -> Pikeman -> Pike & Shot -> AT Gun -> Modern AT
    // ========================================================================
    {UnitTypeId{9},  "Spearman",        UnitClass::AntiCavalry,UnitEra::Ancient,    100, 25,  0, 0, 2,  55, TechId{},   UnitTypeId{26}, 40},
    // 2026-05-03: anti-cavalry rewired. Pikeman by Military Tactics (53).
    // Pike and Shot by Gunpowder (10).
    {UnitTypeId{26}, "Pikeman",         UnitClass::AntiCavalry,UnitEra::Medieval,   110, 38,  0, 0, 2,  80, TechId{53}, UnitTypeId{46}, 60},                          // Military Tactics
    {UnitTypeId{46}, "Pike and Shot",   UnitClass::AntiCavalry,UnitEra::Renaissance,120, 48,  0, 0, 2, 140, TechId{10}, UnitTypeId{27}, 100},                         // Gunpowder
    {UnitTypeId{27}, "AT Gun",          UnitClass::AntiCavalry,UnitEra::Modern,     100, 35, 55, 1, 2, 260, TechId{12}, UnitTypeId{47}, 150},
    {UnitTypeId{47}, "Modern AT",       UnitClass::AntiCavalry,UnitEra::Atomic,     100, 40, 70, 1, 2, 340, TechId{18}, UnitTypeId{},   0},

    // ========================================================================
    // AIR: Biplane -> Fighter -> Jet Fighter -> Stealth Fighter
    //      Bomber -> Heavy Bomber -> Stealth Bomber
    // ========================================================================
    {UnitTypeId{48}, "Biplane",         UnitClass::Air,      UnitEra::Industrial,   80, 20, 40, 3, 5, 200, TechId{11}, UnitTypeId{18}, 120, {{60, 1}}},             // 1 Iron Ingots
    {UnitTypeId{18}, "Fighter",         UnitClass::Air,      UnitEra::Modern,      100, 25, 65, 4, 6, 350, TechId{15}, UnitTypeId{49}, 200, {{109, 1}, {65, 1}}},   // 1 Aircraft Components + 1 Fuel
    {UnitTypeId{49}, "Jet Fighter",     UnitClass::Air,      UnitEra::Atomic,      120, 30, 85, 5, 8, 450, TechId{18}, UnitTypeId{50}, 250, {{109, 1}, {65, 1}}},   // 1 Aircraft Components + 1 Fuel
    {UnitTypeId{50}, "Stealth Fighter", UnitClass::Air,      UnitEra::Information,  120, 35,100, 5, 8, 550, TechId{22}, UnitTypeId{},   0,   {{109, 2}, {65, 1}}},   // 2 Aircraft Components + 1 Fuel
    {UnitTypeId{51}, "Bomber",          UnitClass::Air,      UnitEra::Modern,       80, 10, 80, 5, 6, 400, TechId{15}, UnitTypeId{52}, 220, {{109, 1}, {65, 1}}},   // 1 Aircraft Components + 1 Fuel
    {UnitTypeId{52}, "Stealth Bomber",  UnitClass::Air,      UnitEra::Information,  80, 12,110, 6, 8, 600, TechId{22}, UnitTypeId{},   0,   {{109, 2}, {65, 2}}},   // 2 Aircraft Components + 2 Fuel

    // ========================================================================
    // HELICOPTER: Attack Helicopter -> Gunship
    // ========================================================================
    {UnitTypeId{53}, "Attack Helicopter",UnitClass::Helicopter,UnitEra::Atomic,     100, 40, 50, 2, 5, 380, TechId{18}, UnitTypeId{54}, 200, {{109, 1}, {65, 1}}},  // 1 Aircraft Components + 1 Fuel
    {UnitTypeId{54}, "Gunship",         UnitClass::Helicopter,UnitEra::Information, 120, 50, 65, 2, 6, 480, TechId{22}, UnitTypeId{},   0,   {{109, 1}, {65, 1}}},  // 1 Aircraft Components + 1 Fuel

    // ========================================================================
    // NAVAL: Galley -> Caravel -> Frigate -> Ironclad -> Destroyer -> Missile Cruiser
    //        Submarine -> Nuclear Submarine
    //        Carrier
    // ========================================================================
    {UnitTypeId{6},  "Galley",          UnitClass::Naval,    UnitEra::Ancient,      100, 25,  0, 0, 3,  65, TechId{},   UnitTypeId{7},  50},
    {UnitTypeId{7},  "Caravel",         UnitClass::Naval,    UnitEra::Renaissance,  120, 35,  0, 0, 4, 120, TechId{8},  UnitTypeId{55}, 80},
    {UnitTypeId{55}, "Frigate",         UnitClass::Naval,    UnitEra::Renaissance,  130, 40, 45, 2, 4, 180, TechId{8},  UnitTypeId{56}, 120, {{62, 2}}},             // 2 Lumber
    {UnitTypeId{56}, "Ironclad",        UnitClass::Naval,    UnitEra::Industrial,   160, 55, 40, 2, 4, 260, TechId{11}, UnitTypeId{57}, 150, {{64, 2}}},             // 2 Steel
    {UnitTypeId{57}, "Destroyer",       UnitClass::Naval,    UnitEra::Modern,       150, 60, 55, 2, 5, 340, TechId{14}, UnitTypeId{8},  200, {{64, 2}}},             // 2 Steel
    {UnitTypeId{8},  "Battleship",      UnitClass::Naval,    UnitEra::Modern,       200, 70, 65, 3, 4, 400, TechId{14}, UnitTypeId{58}, 250, {{64, 3}}},             // 3 Steel
    {UnitTypeId{58}, "Missile Cruiser", UnitClass::Naval,    UnitEra::Atomic,       180, 60, 90, 4, 5, 500, TechId{18}, UnitTypeId{},   0,   {{64, 2}, {65, 1}}},    // 2 Steel + 1 Fuel
    {UnitTypeId{59}, "Submarine",       UnitClass::Naval,    UnitEra::Modern,       120, 55, 60, 2, 4, 350, TechId{14}, UnitTypeId{60}, 200, {{64, 1}}},             // 1 Steel
    {UnitTypeId{60}, "Nuclear Sub",     UnitClass::Naval,    UnitEra::Atomic,       150, 65, 80, 3, 5, 480, TechId{18}, UnitTypeId{},   0,   {{64, 1}, {6, 1}}},     // 1 Steel + 1 Uranium
    {UnitTypeId{61}, "Carrier",         UnitClass::Naval,    UnitEra::Atomic,       250, 20,  0, 0, 4, 550, TechId{18}, UnitTypeId{},   0,   {{64, 3}, {65, 1}}},    // 3 Steel + 1 Fuel

    // ========================================================================
    // CIVILIAN / SUPPORT / SPECIAL
    // ========================================================================
    {UnitTypeId{2},  "Scout",           UnitClass::Scout,    UnitEra::Ancient,       80, 10,  0, 0, 3,  30, TechId{},   UnitTypeId{},   0},
    {UnitTypeId{3},  "Settler",         UnitClass::Settler,  UnitEra::Ancient,       80,  0,  0, 0, 3,  80, TechId{},   UnitTypeId{},   0},
    {UnitTypeId{5},  "Builder",         UnitClass::Civilian, UnitEra::Ancient,       80,  0,  0, 0, 2,  50, TechId{},   UnitTypeId{},   0},
    {UnitTypeId{22}, "Battering Ram",   UnitClass::Artillery,UnitEra::Ancient,       60, 10,  0, 0, 2,  65, TechId{},   UnitTypeId{23}, 40},
    {UnitTypeId{28}, "Medic",           UnitClass::Civilian, UnitEra::Medieval,      60,  0,  0, 0, 2,  80, TechId{5},  UnitTypeId{},   0},
    {UnitTypeId{29}, "Great General",   UnitClass::Civilian, UnitEra::Classical,     60,  0,  0, 0, 3,   0, TechId{},   UnitTypeId{},   0},

    // Religious
    {UnitTypeId{19}, "Missionary",      UnitClass::Religious,UnitEra::Classical,     60,  0,  0, 0, 3,   0, TechId{},   UnitTypeId{},   0},
    {UnitTypeId{20}, "Apostle",         UnitClass::Religious,UnitEra::Medieval,      80, 10, 15, 1, 3,   0, TechId{},   UnitTypeId{},   0},
    {UnitTypeId{21}, "Inquisitor",      UnitClass::Religious,UnitEra::Medieval,      60,  0,  0, 0, 3,   0, TechId{},   UnitTypeId{},   0},

    // Trade
    {UnitTypeId{30}, "Trader",          UnitClass::Trader,   UnitEra::Ancient,       60,  0,  0, 0, 3,  40, TechId{},   UnitTypeId{31}, 30},
    {UnitTypeId{31}, "Caravan",         UnitClass::Trader,   UnitEra::Medieval,      80,  0,  0, 0, 4,  80, TechId{5},  UnitTypeId{},   0},

    // WP-S2: Logistics — military supply chain. Refills Encampment buffers.
    // Separate cap from trade slots so war doesn't choke commerce.
    {UnitTypeId{62}, "Supply Wagon",    UnitClass::Logistics,UnitEra::Classical,     50,  0,  0, 0, 3,  60, TechId{6},  UnitTypeId{63}, 80},
    {UnitTypeId{63}, "Tanker Ship",     UnitClass::Logistics,UnitEra::Industrial,    80,  0,  0, 0, 5, 140, TechId{12}, UnitTypeId{},   0},

    // Courier: domestic goods transport, player-dispatched only. Never appears
    // in the production queue (productionCost 0 is filtered out by buildables).
    {UnitTypeId{32}, "Courier",         UnitClass::Trader,   UnitEra::Ancient,       40,  0,  0, 0, 3,   0, TechId{},   UnitTypeId{},   0},

    // ========================================================================
    // ESPIONAGE: Diplomat -> Spy
    // Diplomat: early espionage (Writing tech), basic missions only.
    // Spy: advanced espionage (Printing tech), all missions, can escape.
    // ========================================================================
    // IDs 55/56 collided with Frigate/Ironclad, causing unitTypeDef() to
    // return the naval unit for spy lookups.  Moved to 100/101.
    {UnitTypeId{100}, "Diplomat",       UnitClass::Civilian, UnitEra::Classical,     60,  0,  0, 0, 3,  60, TechId{3},  UnitTypeId{101}, 80},
    {UnitTypeId{101}, "Spy",            UnitClass::Civilian, UnitEra::Renaissance,   60,  0,  0, 0, 4, 120, TechId{8},  UnitTypeId{},    0},

    // ========================================================================
    // GREAT PERSON (recruited, not built)
    // Civilian-class marker unit. Earlier spawn used UnitTypeId{50} which
    // collided with Stealth Fighter -- same bug class as the 55/56 Diplomat/Spy
    // collision with Frigate/Ironclad.
    // ========================================================================
    {UnitTypeId{102}, "Great Person",   UnitClass::Civilian, UnitEra::Ancient,       50,  0,  0, 0, 3,   0, TechId{},   UnitTypeId{},    0},

    // ========================================================================
    // CIV6 PARITY UNITS (added 2026-05-02)
    // ========================================================================

    // ANTI-AIR: AA Gun -> Mobile SAM. Damages air units, weak vs ground.
    {UnitTypeId{70}, "AA Gun",          UnitClass::Ranged,   UnitEra::Modern,       100, 30, 60, 3, 2, 280, TechId{15}, UnitTypeId{71}, 200, {{63, 1}}},
    {UnitTypeId{71}, "Mobile SAM",      UnitClass::Ranged,   UnitEra::Atomic,       110, 40, 80, 4, 4, 400, TechId{18}, UnitTypeId{},   0,   {{109, 1}, {65, 1}}},

    // PARATROOPER: airborne deploy infantry. Modern/Atomic.
    {UnitTypeId{72}, "Paratrooper",     UnitClass::Melee,    UnitEra::Modern,       110, 65,  0, 0, 4, 320, TechId{15}, UnitTypeId{73}, 220},
    {UnitTypeId{73}, "Special Forces",  UnitClass::Melee,    UnitEra::Atomic,       120, 80,  0, 0, 4, 420, TechId{18}, UnitTypeId{},   0,   {{63, 1}}},

    // PRIVATEER: Renaissance naval raider. Can capture trade ships.
    {UnitTypeId{74}, "Privateer",       UnitClass::Naval,    UnitEra::Renaissance,   90, 40,  0, 0, 4, 180, TechId{8},  UnitTypeId{},   0},

    // HELICOPTER TRANSPORT: separate from Attack Helicopter.
    {UnitTypeId{75}, "Helicopter",      UnitClass::Helicopter,UnitEra::Atomic,       80, 25,  0, 0, 6, 280, TechId{18}, UnitTypeId{},   0,   {{109, 1}, {65, 1}}},

    // HEAVY CHARIOT: Ancient cavalry-class missing from current chain.
    {UnitTypeId{76}, "Heavy Chariot",   UnitClass::Cavalry,  UnitEra::Ancient,       100, 28,  0, 0, 4,  80, TechId{1},  UnitTypeId{14}, 50},

    // HOT AIR BALLOON: Industrial recon, line-of-sight bonus.
    {UnitTypeId{77}, "Hot Air Balloon", UnitClass::Scout,    UnitEra::Industrial,    60,  0,  0, 0, 3, 120, TechId{12}, UnitTypeId{},   0},

    // RANGER: Modern recon, scout upgrade.
    {UnitTypeId{78}, "Ranger",          UnitClass::Scout,    UnitEra::Modern,        80, 35,  0, 0, 4, 240, TechId{15}, UnitTypeId{},   0},

    // BATTLE CRUISER: Modern naval, between Battleship and Missile Cruiser.
    {UnitTypeId{79}, "Battle Cruiser",  UnitClass::Naval,    UnitEra::Modern,       150, 70, 50, 3, 5, 380, TechId{15}, UnitTypeId{},   0,   {{64, 2}, {65, 2}}},

    // SUPERTANKER (super-heavy logistics, late game).
    {UnitTypeId{80}, "Supertanker",     UnitClass::Logistics,UnitEra::Atomic,        120, 0,  0, 0, 5, 280, TechId{18}, UnitTypeId{},   0,   {{64, 1}, {65, 2}}},
}};

/// Lookup a unit type definition by ID. IDs are not contiguous so this
/// searches the array. For hot paths, cache the result.
[[nodiscard]] inline const UnitTypeDef& unitTypeDef(UnitTypeId id) {
    for (int32_t i = 0; i < UNIT_TYPE_COUNT; ++i) {
        if (UNIT_TYPE_DEFS[static_cast<std::size_t>(i)].id == id) {
            return UNIT_TYPE_DEFS[static_cast<std::size_t>(i)];
        }
    }
    // Fallback: return first entry (Warrior) if not found
    return UNIT_TYPE_DEFS[0];
}

/// Get the upgrade target for a unit type. Returns invalid if no upgrade.
[[nodiscard]] inline UnitTypeId getUpgradeTarget(UnitTypeId id) {
    return unitTypeDef(id).upgradesTo;
}

/// Get the gold cost to upgrade a unit.
[[nodiscard]] inline int32_t getUpgradeCost(UnitTypeId id) {
    return unitTypeDef(id).upgradeCost;
}

/// Returns true if the given unit class is a naval type.
[[nodiscard]] constexpr bool isNaval(UnitClass c) {
    return c == UnitClass::Naval;
}

/// Returns true if the given unit class is a religious type.
[[nodiscard]] constexpr bool isReligious(UnitClass c) {
    return c == UnitClass::Religious;
}

/// Returns true if the given unit class is airborne.
[[nodiscard]] constexpr bool isAirUnit(UnitClass c) {
    return c == UnitClass::Air || c == UnitClass::Helicopter;
}

/// Returns true if the given unit class is a military (combat-capable) type.
[[nodiscard]] constexpr bool isMilitary(UnitClass c) {
    return c == UnitClass::Melee
        || c == UnitClass::Ranged
        || c == UnitClass::Cavalry
        || c == UnitClass::Armor
        || c == UnitClass::Artillery
        || c == UnitClass::AntiCavalry
        || c == UnitClass::Air
        || c == UnitClass::Helicopter
        || c == UnitClass::Naval;
}

// upgradeUnit() and bestAvailableMilitaryUnit() are declared in UnitComponent.hpp
// (they need UnitComponent and World which can't be included in this constexpr header).

} // namespace aoc::sim
