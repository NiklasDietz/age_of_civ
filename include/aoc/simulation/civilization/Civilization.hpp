#pragma once

/// @file Civilization.hpp
/// @brief Civilization definitions with unique abilities, units, and buildings.

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoc::sim {

using CivId = uint8_t;

/// Modifier bonuses unique to each civilization.
struct CivAbilityModifiers {
    float productionMultiplier = 1.0f;
    float scienceMultiplier    = 1.0f;
    float cultureMultiplier    = 1.0f;
    float goldMultiplier       = 1.0f;
    float combatStrengthBonus  = 0.0f;
    int32_t extraMovement      = 0;       ///< Bonus movement for specific unit types
    float faithMultiplier      = 1.0f;
    int32_t extraTradeRoutes   = 0;
};

/// Civ-specific unique unit. Replaces a standard UnitTypeId for the civ
/// during production and combat. Stats are deltas applied on top of the
/// base unit's combat/ranged/movement values.
struct UniqueUnitMod {
    UnitTypeId       baseUnit{0xFFFF};   ///< Standard unit replaced (invalid = no unique)
    std::string_view name = "";          ///< Display name (e.g. "Legion", "Immortal")
    int32_t          combatBonus = 0;    ///< Added to combatStrength
    int32_t          rangedBonus = 0;    ///< Added to rangedStrength
    int32_t          movementBonus = 0;  ///< Added to movementPoints
    int32_t          rangeBonus = 0;     ///< Added to attack range
};

/// Civ-specific unique building. Replaces a standard BuildingId for this
/// civ. Adds bonuses on top of the base building's yields.
struct UniqueBuildingMod {
    BuildingId       baseBuilding{0xFFFF};  ///< Standard building replaced
    std::string_view name = "";
    int32_t          foodBonus = 0;
    int32_t          productionBonus = 0;
    int32_t          goldBonus = 0;
    int32_t          scienceBonus = 0;
    int32_t          cultureBonus = 0;
    int32_t          faithBonus = 0;
};

/// Civ-specific unique tile improvement. Either replaces a standard
/// improvement OR is built standalone. Bonuses applied on top of base.
struct UniqueImprovementMod {
    std::string_view name = "";
    int32_t          foodBonus = 0;
    int32_t          productionBonus = 0;
    int32_t          goldBonus = 0;
    int32_t          scienceBonus = 0;
    int32_t          cultureBonus = 0;
    int32_t          faithBonus = 0;
};

/// Civ-specific unique district. Lightweight: bonuses only, no structural
/// replace. Applied to all districts of this type when civ matches.
struct UniqueDistrictMod {
    std::string_view name = "";
    int32_t          adjacencyBonus = 0;  ///< +N to relevant yield (science for Campus, culture for Theatre, etc.)
};

/// Maximum number of historical city names per civilization.
inline constexpr std::size_t MAX_CIV_CITY_NAMES = 12;

struct CivilizationDef {
    CivId            id;
    std::string_view name;
    std::string_view leaderName;
    std::string_view abilityName;
    std::string_view abilityDescription;
    CivAbilityModifiers modifiers;
    UnitTypeId       uniqueUnitReplaces;    ///< Which standard unit this civ's unique unit replaces (INVALID if none)
    BuildingId       uniqueBuildingReplaces; ///< Which standard building this replaces (INVALID if none)
    std::array<std::string_view, MAX_CIV_CITY_NAMES> cityNames; ///< Historical city names for this civilization
    UniqueUnitMod    uniqueUnit{};       ///< Per-civ unique unit (Civ-6 style)
    UniqueBuildingMod uniqueBuilding{};  ///< Per-civ unique building
    UniqueImprovementMod uniqueImprovement{};  ///< Per-civ unique improvement
    UniqueDistrictMod uniqueDistrict{};        ///< Per-civ unique district adjacency bump
};

/// Total number of civilizations.
inline constexpr uint8_t CIV_COUNT = 36;

inline constexpr std::array<CivilizationDef, CIV_COUNT> CIV_DEFS = {{
    {0, "Rome",    "Trajan",        "All Roads Lead to Rome",
     "+5% production. +1 trade route. Free roads in capital.",
     {1.05f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Rome", "Antium", "Cumae", "Neapolis", "Ravenna", "Mediolanum",
       "Arretium", "Brundisium", "Capua", "Tarentum", "Pisae", "Genua"}},
     {UnitTypeId{10}, "Legion", 5, 0, 0, 0},
     {BuildingId{15}, "Roman Bath", 2, 0, 0, 0, 1, 0},
     {"Cardo Maximus", 0, 1, 1, 0, 0, 0}, {"Forum", 2}},

    {1, "Egypt",   "Cleopatra",     "Mediterranean's Bride",
     "+15% production toward wonders and districts.",
     {1.02f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Thebes", "Memphis", "Alexandria", "Heliopolis", "Giza", "Luxor",
       "Aswan", "Abydos", "Edfu", "Karnak", "Faiyum", "Rosetta"}},
     {UnitTypeId{4}, "Maryannu Chariot", 3, 0, 1, 0},
     {BuildingId{15}, "Nilometer", 3, 0, 0, 0, 0, 0},
     {"Sphinx", 0, 0, 0, 0, 1, 1}, {"Necropolis", 2}},

    {2, "China",   "Qin Shi Huang", "Dynastic Cycle",
     "+5% science. Builders gain +1 charge.",
     {1.0f, 1.05f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Beijing", "Shanghai", "Nanjing", "Xian", "Chengdu", "Hangzhou",
       "Luoyang", "Kaifeng", "Guangzhou", "Wuhan", "Suzhou", "Tianjin"}},
     {UnitTypeId{37}, "Crouching Tiger", 0, 8, 0, 0},
     {BuildingId{7}, "Imperial Academy", 0, 0, 0, 3, 0, 0},
     {"Great Wall", 0, 1, 1, 0, 1, 0}, {"Hanyamen", 2}},

    {3, "Germany", "Frederick",     "Free Imperial Cities",
     "+15% production all cities. +1 military policy slot. +3 combat.",
     {1.15f, 1.0f, 1.0f, 1.0f, 3.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Berlin", "Hamburg", "Munich", "Cologne", "Frankfurt", "Stuttgart",
       "Dresden", "Leipzig", "Aachen", "Nuremberg", "Bremen", "Dortmund"}},
     {UnitTypeId{59}, "U-Boat", 5, 0, 1, 0},
     {BuildingId{1}, "Hansa", 0, 3, 0, 0, 0, 0},
     {"Stadt", 0, 2, 0, 0, 0, 0}, {"Hansa Quarter", 3}},

    {4, "Greece",  "Pericles",      "Plato's Republic",
     "+4% culture. +0% science (was strong).",
     {1.0f, 1.0f, 1.04f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Athens", "Sparta", "Corinth", "Argos", "Thebes", "Delphi",
       "Olympia", "Mycenae", "Rhodes", "Ephesus", "Syracuse", "Thessaloniki"}},
     {UnitTypeId{9}, "Hoplite", 5, 0, 0, 0},
     {BuildingId{7}, "Acropolis", 0, 0, 0, 1, 2, 0},
     {"Amphitheatre", 0, 0, 0, 0, 2, 0}, {"Theatre Square", 3}},

    {5, "England", "Victoria",      "British Museum",
     "+2 movement for naval units. +10% gold.",
     {1.0f, 1.0f, 1.0f, 1.1f, 0.0f, 2, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"London", "York", "Canterbury", "Oxford", "Cambridge", "Bristol",
       "Manchester", "Liverpool", "Edinburgh", "Bath", "Winchester", "Dover"}},
     {UnitTypeId{55}, "Sea Dog", 5, 5, 1, 0},
     {BuildingId{6}, "Stock Exchange", 0, 0, 4, 0, 0, 0},
     {"Naval Yard", 0, 1, 2, 0, 0, 0}, {"Royal Dockyard", 3}},

    {6, "Japan",   "Hojo Tokimune", "Meiji Restoration",
     "+7 combat. +5% production + science. Coastal bonus.",
     {1.05f, 1.05f, 1.0f, 1.0f, 7.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Kyoto", "Tokyo", "Osaka", "Nara", "Nagoya", "Sapporo",
       "Hiroshima", "Kobe", "Fukuoka", "Yokohama", "Sendai", "Kamakura"}},
     {UnitTypeId{33}, "Samurai", 7, 0, 0, 0},
     {BuildingId{3}, "Electronics Factory", 0, 3, 0, 0, 0, 0},
     {"Pagoda", 0, 0, 0, 0, 1, 1}, {"Tea House", 2}},

    {7, "Persia",  "Cyrus",         "Satrapies",
     "+10% gold. +1 trade route. +2 movement during golden age.",
     {1.0f, 1.0f, 1.0f, 1.1f, 0.0f, 0, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Persepolis", "Pasargadae", "Susa", "Ecbatana", "Isfahan", "Shiraz",
       "Tabriz", "Hamadan", "Kerman", "Yazd", "Balkh", "Merv"}},
     {UnitTypeId{10}, "Immortal", 5, 10, 0, 1},
     {BuildingId{6}, "Pairidaeza Garden", 0, 0, 2, 0, 1, 0},
     {"Royal Road", 0, 0, 2, 0, 0, 0}, {"Satrapy", 2}},

    {8, "Aztec",   "Montezuma",     "Legend of the Five Suns",
     "+35% faith. +1 amenity. +5 combat strength. +5% production.",
     {1.05f, 1.0f, 1.0f, 1.0f, 5.0f, 0, 1.35f, 0}, UnitTypeId{}, BuildingId{},
     {{"Tenochtitlan", "Texcoco", "Tlacopan", "Cholula", "Tlaxcala", "Calixtlahuaca",
       "Xochicalco", "Tula", "Cempoala", "Malinalco", "Tamuin", "Coatepec"}},
     {UnitTypeId{0}, "Eagle Warrior", 5, 0, 0, 0},
     {BuildingId{0}, "Tlachtli", 0, 1, 0, 0, 0, 2},
     {"Chinampa", 2, 0, 0, 0, 0, 1}, {"Sacrificial Pyramid", 2}},

    {9, "India",   "Gandhi",        "Satyagraha",
     "+5 faith per turn. +2 amenity when at peace. Double religion spread.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Mumbai", "Delhi", "Kolkata", "Chennai", "Varanasi", "Agra",
       "Jaipur", "Patna", "Hyderabad", "Lucknow", "Ahmedabad", "Pune"}},
     {UnitTypeId{12}, "Varu", 5, 0, 0, 0},
     {BuildingId{15}, "Stepwell", 2, 0, 0, 0, 0, 1},
     {"Ashram", 0, 0, 0, 0, 1, 2}, {"Holy District", 2}},

    {10, "Russia", "Peter",         "The Grand Embassy",
     "+1 science/culture per trade route. Extra territory from city founding (+2 tiles).",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Moscow", "St Petersburg", "Novgorod", "Kiev", "Kazan", "Samara",
       "Rostov", "Tula", "Smolensk", "Pskov", "Yaroslavl", "Vladimir"}},
     {UnitTypeId{14}, "Cossack", 7, 0, 0, 0},
     {BuildingId{0}, "Lavra", 0, 0, 0, 0, 0, 3},
     {"Tundra Village", 1, 1, 0, 0, 0, 0}, {"Holy Site", 2}},

    {11, "Brazil", "Pedro II",      "Magnanimous",
     "+20% culture. +2 amenity in cities with rainforest nearby.",
     {1.0f, 1.0f, 1.2f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Rio", "Sao Paulo", "Brasilia", "Salvador", "Recife", "Belem",
       "Manaus", "Curitiba", "Fortaleza", "Natal", "Porto Alegre", "Santos"}},
     {UnitTypeId{8}, "Minas Geraes", 5, 5, 0, 0},
     {BuildingId{6}, "Street Carnival", 0, 0, 0, 0, 3, 0},
     {"Rainforest Lodge", 0, 1, 0, 0, 1, 0}, {"Carnival Square", 2}},

    {12, "Mongolia", "Genghis Khan",  "Mongol Horde",
     "+3 combat strength for cavalry. +2 movement for cavalry. Captures yield extra gold.",
     {1.0f, 1.0f, 1.0f, 1.0f, 3.0f, 1, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Karakorum", "Ulaanbaatar", "Erdenet", "Khovd", "Olgii", "Ulaangom",
       "Choibalsan", "Bayanhongor", "Hujirt", "Mandalgovi", "Sukhbaatar", "Darhan"}},
     {UnitTypeId{12}, "Keshig", 5, 0, 1, 0},
     {BuildingId{0}, "Ordu", 0, 2, 0, 0, 0, 0},
     {"Yurt", 1, 1, 0, 0, 0, 0}, {"Khanate", 2}},

    {13, "Arabia",   "Saladin",       "Last Prophet",
     "Auto-founds first religion. +10% faith. +1 science from holy site adjacency.",
     {1.0f, 1.05f, 1.0f, 1.0f, 0.0f, 0, 1.10f, 0}, UnitTypeId{}, BuildingId{},
     {{"Mecca", "Medina", "Damascus", "Baghdad", "Cairo", "Cordoba",
       "Granada", "Kufa", "Basra", "Aleppo", "Mosul", "Riyadh"}},
     {UnitTypeId{12}, "Mamluk", 5, 0, 0, 0},
     {BuildingId{7}, "Madrasa", 0, 0, 0, 2, 0, 1},
     {"Caravanserai", 0, 0, 2, 0, 0, 0}, {"Holy Site", 2}},

    {14, "Zulu",     "Shaka",         "Impi Strike",
     "+5 combat. Captures grant +50 gold + free promotion. +5% production.",
     {1.05f, 1.0f, 1.0f, 1.0f, 5.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Ulundi", "Nodwengu", "Mgungundlovu", "Eshowe", "Empangeni", "Vryheid",
       "Pongola", "Bonjeni", "Mahlabathini", "Nkandla", "Babanango", "Pietermaritzburg"}},
     {UnitTypeId{9}, "Impi", 7, 0, 0, 0},
     {BuildingId{0}, "Ikanda", 0, 3, 0, 0, 0, 0},
     {"Kraal", 1, 1, 0, 0, 0, 0}, {"Encampment", 2}},

    {15, "Scythia",  "Tomyris",       "People of the Steppe",
     "+50% prod for cavalry. Heal +10 HP after kills. +3 combat.",
     {1.0f, 1.0f, 1.0f, 1.0f, 3.0f, 1, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Pokrovka", "Tomyris", "Saksanokhur", "Saka", "Issyk", "Pazyryk",
       "Arzhan", "Tilla Tepe", "Kostromskaya", "Aldy Bel", "Kelermes", "Ulskij"}},
     {UnitTypeId{4}, "Saka Horse Archer", 3, 15, 1, 1},
     {BuildingId{0}, "Kurgan", 0, 1, 1, 0, 0, 0},
     {"Burial Mound", 0, 0, 1, 0, 1, 0}, {"Steppe Camp", 2}},

    {16, "Macedon",  "Alexander",     "To World's End",
     "+15% combat strength. No war weariness. +5% science from kills.",
     {1.0f, 1.05f, 1.0f, 1.0f, 6.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Pella", "Aegae", "Edessa", "Stagira", "Pydna", "Amphipolis",
       "Thessalonica", "Mieza", "Dion", "Olynthus", "Methone", "Alexandria"}},
     {UnitTypeId{4}, "Hetairoi", 7, 0, 1, 0},
     {BuildingId{0}, "Basilikoi Paides", 0, 2, 0, 1, 0, 0},
     {"Stoa", 0, 0, 0, 1, 1, 0}, {"Acropolis", 2}},

    {17, "Mali",     "Mansa Musa",    "Songs of the Jeli",
     "+10% production. +10% culture. +30% gold. +2 trade routes.",
     {1.10f, 1.0f, 1.10f, 1.30f, 0.0f, 0, 1.0f, 2}, UnitTypeId{}, BuildingId{},
     {{"Niani", "Timbuktu", "Djenne", "Gao", "Walata", "Awdaghust",
       "Koumbi Saleh", "Bamako", "Segou", "Mopti", "Kayes", "Sikasso"}},
     {UnitTypeId{12}, "Mandekalu Cavalry", 5, 0, 0, 0},
     {BuildingId{6}, "Suguba", 0, 0, 3, 0, 0, 1},
     {"Salt Mine", 0, 0, 2, 0, 0, 0}, {"Commercial Hub", 2}},

    {18, "Sumeria",  "Gilgamesh",     "Epic Quest",
     "+10% production. +10% science. +5 combat.",
     {1.10f, 1.10f, 1.0f, 1.0f, 5.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Uruk", "Ur", "Eridu", "Lagash", "Nippur", "Kish",
       "Larsa", "Sippar", "Adab", "Akkad", "Mari", "Shuruppak"}},
     {UnitTypeId{4}, "War-Cart", 8, 0, 1, 0},
     {BuildingId{0}, "Ziggurat", 0, 1, 0, 1, 0, 1},
     {"Cuneiform Library", 0, 0, 0, 2, 0, 0}, {"Holy Site", 2}},

    {19, "Babylon",  "Hammurabi",     "Enuma Anu Enlil",
     "Tech eurekas grant full tech instantly. Normal science output.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Babylon", "Eshnunna", "Borsippa", "Sippar", "Kish", "Nippur",
       "Lagash", "Mari", "Akkad", "Susa", "Akshak", "Tell el-Muqayyar"}},
     {UnitTypeId{0}, "Sabum Kibittum", 5, 0, 1, 0},
     {BuildingId{7}, "Etemenanki", 0, 0, 0, 3, 0, 0},
     {"Palgum", 1, 0, 0, 1, 0, 0}, {"Code District", 2}},

    {20, "Khmer",    "Jayavarman",    "Grand Barays",
     "+2 food in cities adjacent to rivers. Rainforest tiles give +1 production.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.05f, 0}, UnitTypeId{}, BuildingId{},
     {{"Angkor", "Hariharalaya", "Sambor", "Yasodharapura", "Koh Ker", "Beng Mealea",
       "Banteay Chhmar", "Vyadhapura", "Sresthapura", "Lavo", "Bhavapura", "Isanapura"}},
     {UnitTypeId{23}, "Domrey", 0, 12, 0, 0},
     {BuildingId{15}, "Prasat", 1, 0, 0, 0, 0, 2},
     {"Baray", 2, 0, 0, 0, 0, 0}, {"Temple Complex", 2}},

    {21, "Cree",     "Poundmaker",    "Nihithaw",
     "+1 trade route. Receive a free Trader on starting Currency. Trade routes give vision.",
     {1.0f, 1.0f, 1.0f, 1.05f, 0.0f, 0, 1.0f, 2}, UnitTypeId{}, BuildingId{},
     {{"Nehiyaw", "Asiniskaw", "Sakistaw", "Maskwacis", "Pikwakanagan", "Ahtahkakoop",
       "Mistawasis", "Onion Lake", "Sturgeon Lake", "Witchekan", "Big River", "Beardy"}},
     {UnitTypeId{2}, "Okihtcitaw", 5, 0, 1, 0},
     {BuildingId{6}, "Trade Outpost", 0, 0, 2, 0, 0, 0},
     {"Mekewap", 1, 0, 1, 0, 0, 0}, {"Trading Plaza", 2}},

    {22, "Mapuche",  "Lautaro",       "Toqui",
     "+25% combat vs golden-age. Pillage +50% loot.",
     {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Wallmapu", "Temuco", "Concepcion", "Chillan", "Valdivia", "Imperial",
       "Quillen", "Lebu", "Lumaco", "Pucon", "Villarrica", "Curacautin"}},
     {UnitTypeId{14}, "Malon Raider", 5, 0, 1, 0},
     {BuildingId{0}, "Lemu", 0, 2, 0, 0, 0, 0},
     {"Chemamull", 0, 0, 0, 0, 1, 1}, {"War Lodge", 2}},

    {23, "Ottoman",  "Suleiman",      "Great Turkish Bombard",
     "+10% combat strength against city walls. Captured cities don't lose population.",
     {1.0f, 1.0f, 1.0f, 1.0f, 3.0f, 0, 1.05f, 0}, UnitTypeId{}, BuildingId{},
     {{"Constantinople", "Edirne", "Bursa", "Ankara", "Izmir", "Konya",
       "Erzurum", "Adana", "Trabzon", "Antalya", "Diyarbakir", "Gaziantep"}},
     {UnitTypeId{34}, "Janissary", 7, 0, 0, 0},
     {BuildingId{15}, "Hammam", 2, 0, 0, 0, 1, 0},
     {"Grand Bazaar", 0, 0, 3, 0, 0, 0}, {"Sultan's Court", 2}},

    {24, "Phoenicia","Dido",          "Mediterranean Colonies",
     "+50% production for naval units. Founding new cities don't reduce loyalty.",
     {1.0f, 1.0f, 1.0f, 1.05f, 0.0f, 1, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Tyre", "Sidon", "Byblos", "Carthage", "Utica", "Hadrumetum",
       "Gades", "Lixus", "Mogador", "Leptis", "Hippo", "Malaca"}},
     {UnitTypeId{6}, "Bireme", 5, 0, 1, 0},
     {BuildingId{6}, "Cothon", 0, 1, 2, 0, 0, 0},
     {"Purple Dye Works", 0, 0, 2, 0, 1, 0}, {"Harbor", 2}},

    {25, "Norway",   "Harald",        "Knarr",
     "Embarked units have +1 movement. Coastal raiding deals +50% pillage gold.",
     {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Nidaros", "Bjorgvin", "Hamar", "Kongelv", "Tunsberg", "Sarpsborg",
       "Oslo", "Stavanger", "Skien", "Tromso", "Bodo", "Tromsdalen"}},
     {UnitTypeId{33}, "Berserker", 7, 0, 0, 0},
     {BuildingId{0}, "Stave Church", 0, 0, 0, 0, 1, 2},
     {"Longhouse", 1, 1, 0, 0, 0, 0}, {"Sea King's Hall", 2}},

    {26, "Spain",    "Philip II",     "El Escorial",
     "+25% combat against civs founding a different religion. Galleons unique.",
     {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 1.10f, 0}, UnitTypeId{}, BuildingId{},
     {{"Madrid", "Toledo", "Seville", "Cordoba", "Valencia", "Zaragoza",
       "Salamanca", "Granada", "Bilbao", "Cadiz", "Pamplona", "Burgos"}},
     {UnitTypeId{34}, "Conquistador", 5, 0, 1, 0},
     {BuildingId{0}, "Fleet Headquarters", 0, 1, 1, 0, 0, 1},
     {"Mission", 0, 0, 0, 1, 0, 1}, {"El Escorial", 2}},

    {27, "Korea",    "Seondeok",      "Three Kingdoms",
     "+2 science from Mines, +1 from Farms. Seowon district replaces Campus.",
     {1.0f, 1.10f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Seoul", "Pyongyang", "Gyeongju", "Busan", "Daegu", "Incheon",
       "Suwon", "Daejeon", "Gwangju", "Ulsan", "Jeonju", "Andong"}},
     {UnitTypeId{37}, "Hwacha", 0, 15, 0, 1},
     {BuildingId{7}, "Seowon", 0, 0, 0, 4, 0, 0},
     {"Three Kingdoms Mine", 0, 1, 0, 2, 0, 0}, {"Campus", 3}},

    {28, "Indonesia","Gitarja",       "Great Nusantara",
     "Coast tiles give +1 production for adjacent cities. Naval units repair faster.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.05f, 1}, UnitTypeId{}, BuildingId{},
     {{"Java", "Sumatra", "Bali", "Jakarta", "Surabaya", "Yogyakarta",
       "Medan", "Semarang", "Makassar", "Palembang", "Pontianak", "Banjarmasin"}},
     {UnitTypeId{55}, "Jong", 5, 5, 1, 0},
     {BuildingId{6}, "Indrapura", 0, 1, 2, 0, 0, 0},
     {"Kampung", 1, 0, 1, 0, 0, 0}, {"Spice Wharf", 2}},

    {29, "Vietnam",  "Ba Trieu",      "Nine Dragon River",
     "+1 culture from rainforest tiles. Units in forests/jungles ambush at +5 strength.",
     {1.0f, 1.0f, 1.05f, 1.0f, 1.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Hanoi", "Hue", "Ho Chi Minh", "Da Nang", "Can Tho", "Hai Phong",
       "Bien Hoa", "Vung Tau", "Nha Trang", "Quy Nhon", "Buon Ma Thuot", "Pleiku"}},
     {UnitTypeId{12}, "Voi Chien", 5, 0, 0, 0},
     {BuildingId{0}, "Thanh", 0, 2, 0, 0, 0, 0},
     {"Rice Terrace", 2, 0, 0, 0, 0, 0}, {"Jungle Citadel", 2}},

    {30, "Maori",    "Kupe",          "Mana",
     "Start with Sailing tech. +1 housing per Fishing Boats. Tropical adaptation.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Aotearoa", "Auckland", "Wellington", "Christchurch", "Hamilton", "Tauranga",
       "Dunedin", "Napier", "Rotorua", "Whangarei", "Gisborne", "Nelson"}},
     {UnitTypeId{0}, "Toa", 5, 0, 0, 0},
     {BuildingId{15}, "Marae", 1, 0, 0, 0, 1, 1},
     {"Pa", 0, 1, 0, 0, 1, 0}, {"Whare", 2}},

    {31, "America",  "Roosevelt",     "Founding Fathers",
     "+1 production per district built in same city. Rough Riders unique unit.",
     {1.05f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Washington", "New York", "Boston", "Philadelphia", "Chicago", "Los Angeles",
       "Houston", "San Francisco", "Detroit", "Seattle", "Denver", "Miami"}},
     {UnitTypeId{14}, "Rough Rider", 5, 0, 0, 0},
     {BuildingId{12}, "Film Studio", 0, 0, 0, 1, 3, 0},
     {"National Park", 0, 0, 1, 0, 2, 0}, {"Capitol", 2}},

    {32, "France",   "Catherine",     "Black Queen",
     "+2 science/culture from spies. +50% production for medieval/renaissance wonders.",
     {1.0f, 1.0f, 1.10f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Paris", "Lyon", "Marseille", "Reims", "Bordeaux", "Toulouse",
       "Strasbourg", "Avignon", "Orleans", "Nantes", "Lille", "Rouen"}},
     {UnitTypeId{15}, "Garde Imperiale", 7, 0, 0, 0},
     {BuildingId{6}, "Grand Magazin", 0, 0, 2, 0, 1, 0},
     {"Chateau", 0, 0, 1, 0, 2, 0}, {"Court of Versailles", 3}},

    {33, "Netherlands","Wilhelmina",  "Radio Oranje",
     "+1 loyalty per turn for cities founded on coast. Polder unique improvement.",
     {1.0f, 1.0f, 1.0f, 1.10f, 0.0f, 0, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Amsterdam", "Rotterdam", "The Hague", "Utrecht", "Eindhoven", "Groningen",
       "Maastricht", "Leiden", "Haarlem", "Delft", "Tilburg", "Nijmegen"}},
     {UnitTypeId{55}, "De Zeven Provincien", 5, 5, 0, 0},
     {BuildingId{6}, "VOC Hub", 0, 0, 3, 0, 0, 0},
     {"Polder", 2, 1, 0, 0, 0, 0}, {"Trade Guild", 2}},

    {34, "Australia","Curtin",        "Land Down Under",
     "Coastal tiles give +3 housing. Outback Station unique improvement.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Sydney", "Melbourne", "Brisbane", "Perth", "Adelaide", "Canberra",
       "Hobart", "Darwin", "Gold Coast", "Newcastle", "Cairns", "Townsville"}},
     {UnitTypeId{15}, "Digger", 5, 0, 0, 0},
     {BuildingId{15}, "Outback Granary", 2, 1, 0, 0, 0, 0},
     {"Outback Station", 1, 1, 1, 0, 0, 0}, {"Stockman's Camp", 2}},

    {35, "Canada",   "Laurier",       "Four Faces of Peace",
     "Cannot declare surprise war. Tundra tiles give +1 production. Mountie unique unit.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Ottawa", "Toronto", "Vancouver", "Montreal", "Calgary", "Edmonton",
       "Winnipeg", "Quebec City", "Halifax", "Saskatoon", "Regina", "St Johns"}},
     {UnitTypeId{14}, "Mountie", 5, 0, 0, 0},
     {BuildingId{6}, "Mountain Trading Post", 0, 1, 1, 0, 1, 0},
     {"Tundra Village", 1, 1, 0, 0, 0, 0}, {"Confederation Hall", 2}},
}};

/// Look up a civilization definition by ID.
[[nodiscard]] inline constexpr const CivilizationDef& civDef(CivId id) {
    return CIV_DEFS[id];
}

/// ECS component attached to player entities.
struct PlayerCivilizationComponent {
    PlayerId owner = INVALID_PLAYER;
    CivId    civId = 0;
};

} // namespace aoc::sim

// Forward declarations for getNextCityName -- defined in headers included by .cpp files.
namespace aoc::game { class GameState; }

namespace aoc::sim {

/// Get the next historical city name for a player based on their civilization.
/// Counts existing cities owned by the player and returns the next name from
/// the civ's city name list. Falls back to "City [N]" if all 12 are used.
[[nodiscard]] std::string getNextCityName(const aoc::game::GameState& gameState, PlayerId player);

} // namespace aoc::sim
