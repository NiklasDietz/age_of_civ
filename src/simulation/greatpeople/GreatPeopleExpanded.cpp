/**
 * @file GreatPeopleExpanded.cpp
 * @brief 108 named great people with historical names across 9 categories.
 */

#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"

#include <array>
#include <cassert>

namespace aoc::sim {

namespace {

// 12 per category x 9 categories = 108 named great people
constexpr std::array<NamedGreatPersonDef, NAMED_GP_COUNT> NAMED_GP_DEFS = {{
    // === Scientists (0-11) ===
    { 0, GreatPersonCategory::Scientist, "Euclid",            "Elements",           "Free tech: Mathematics", EraId{1}},
    { 1, GreatPersonCategory::Scientist, "Hypatia",           "Library of Alexandria","Free Library in 1 city", EraId{1}},
    { 2, GreatPersonCategory::Scientist, "Aryabhata",         "Astronomy",          "Free eureka on 2 techs", EraId{2}},
    { 3, GreatPersonCategory::Scientist, "Hildegard of Bingen","Natural Philosophy","Free eureka on 2 techs", EraId{2}},
    { 4, GreatPersonCategory::Scientist, "Galileo Galilei",   "Telescopic Observation","+100% science for 5 turns", EraId{3}},
    { 5, GreatPersonCategory::Scientist, "Isaac Newton",      "Principia Mathematica","Free Campus Research Lab", EraId{3}},
    { 6, GreatPersonCategory::Scientist, "Charles Darwin",    "On the Origin of Species","+500 science", EraId{4}},
    { 7, GreatPersonCategory::Scientist, "Nikola Tesla",      "Alternating Current","Free power plant in 1 city", EraId{4}},
    { 8, GreatPersonCategory::Scientist, "Albert Einstein",   "Theory of Relativity","Free eureka on 3 techs", EraId{5}},
    { 9, GreatPersonCategory::Scientist, "Marie Curie",       "Radioactivity",      "+100% science for 10 turns", EraId{5}},
    {10, GreatPersonCategory::Scientist, "Alan Turing",       "Computing Machine",  "Free tech: Computers", EraId{5}},
    {11, GreatPersonCategory::Scientist, "Rosalind Franklin", "DNA Structure",      "+300 science + eureka", EraId{5}},

    // === Engineers (12-23) ===
    {12, GreatPersonCategory::Engineer, "Imhotep",            "Step Pyramid",       "+200 production toward wonder", EraId{1}},
    {13, GreatPersonCategory::Engineer, "Bi Sheng",           "Movable Type",       "+2 production in all cities", EraId{2}},
    {14, GreatPersonCategory::Engineer, "Leonardo da Vinci",  "Renaissance Workshop","Free Workshop in 1 city", EraId{3}},
    {15, GreatPersonCategory::Engineer, "James Watt",         "Steam Engine",       "Industrial buildings +20% for 10 turns", EraId{4}},
    {16, GreatPersonCategory::Engineer, "Isambard Brunel",    "Great Western Railway","Free Railway on 5 tiles", EraId{4}},
    {17, GreatPersonCategory::Engineer, "Nikola Tesla",       "Power Systems",      "Free power plant in 1 city", EraId{4}},
    {18, GreatPersonCategory::Engineer, "Gustave Eiffel",     "Iron Construction",  "+400 production toward wonder", EraId{4}},
    {19, GreatPersonCategory::Engineer, "Robert Goddard",     "Rocketry Pioneer",   "+300 production to space project", EraId{5}},
    {20, GreatPersonCategory::Engineer, "Wernher von Braun",  "Saturn V",           "+500 production to wonder", EraId{5}},
    {21, GreatPersonCategory::Engineer, "Ada Lovelace",       "First Program",      "Free Semiconductor Fab", EraId{5}},
    {22, GreatPersonCategory::Engineer, "Hedy Lamarr",        "Spread Spectrum",    "Free Telecom Hub", EraId{5}},
    {23, GreatPersonCategory::Engineer, "John Roebling",      "Suspension Bridge",  "+3 production in city", EraId{4}},

    // === Generals (24-35) ===
    {24, GreatPersonCategory::General, "Sun Tzu",             "Art of War",         "+5 combat to adjacent units", EraId{1}},
    {25, GreatPersonCategory::General, "Hannibal Barca",      "Crossing the Alps",  "+2 movement for 10 turns", EraId{1}},
    {26, GreatPersonCategory::General, "Julius Caesar",       "Veni Vidi Vici",     "+10 combat, 1 free unit", EraId{1}},
    {27, GreatPersonCategory::General, "Genghis Khan",        "Mongol Horde",       "+3 movement cavalry, 2 free horse units", EraId{2}},
    {28, GreatPersonCategory::General, "Joan of Arc",         "Divine Mission",     "+15 loyalty, +5 combat for 10 turns", EraId{2}},
    {29, GreatPersonCategory::General, "Napoleon Bonaparte",  "Grand Army",         "All units gain Corps formation free", EraId{4}},
    {30, GreatPersonCategory::General, "George Washington",   "Continental Army",   "+10 combat defending own territory", EraId{4}},
    {31, GreatPersonCategory::General, "Erwin Rommel",        "Desert Fox",         "+3 movement for 10 turns", EraId{5}},
    {32, GreatPersonCategory::General, "Dwight Eisenhower",   "D-Day",              "All military units heal 50 HP", EraId{5}},
    {33, GreatPersonCategory::General, "Simón Bolívar",       "Liberator",          "+20 loyalty in all cities for 10 turns", EraId{4}},
    {34, GreatPersonCategory::General, "Alexander the Great", "Hellenistic Campaign","Adjacent enemy cities -15 loyalty", EraId{1}},
    {35, GreatPersonCategory::General, "Khalid ibn al-Walid", "Sword of God",       "+8 combat to all melee for 5 turns", EraId{2}},

    // === Artists (36-47) ===
    {36, GreatPersonCategory::Artist, "Michelangelo",         "Sistine Chapel",     "+4 culture in this city", EraId{3}},
    {37, GreatPersonCategory::Artist, "Donatello",            "David",              "+2 culture, +2 faith", EraId{3}},
    {38, GreatPersonCategory::Artist, "Rembrandt",            "Night Watch",        "+3 culture in this city", EraId{3}},
    {39, GreatPersonCategory::Artist, "El Greco",             "View of Toledo",     "+2 culture, +1 faith", EraId{3}},
    {40, GreatPersonCategory::Artist, "Claude Monet",         "Water Lilies",       "+200 tourism", EraId{4}},
    {41, GreatPersonCategory::Artist, "Vincent van Gogh",     "Starry Night",       "+300 culture", EraId{4}},
    {42, GreatPersonCategory::Artist, "Frida Kahlo",          "The Two Fridas",     "+200 culture", EraId{5}},
    {43, GreatPersonCategory::Artist, "Pablo Picasso",        "Guernica",           "+400 culture, +100 tourism", EraId{5}},
    {44, GreatPersonCategory::Artist, "Hokusai",              "Great Wave",         "+250 culture", EraId{4}},
    {45, GreatPersonCategory::Artist, "Raphael",              "School of Athens",   "+3 culture, +1 science", EraId{3}},
    {46, GreatPersonCategory::Artist, "Diego Rivera",         "Man at the Crossroads","+300 culture", EraId{5}},
    {47, GreatPersonCategory::Artist, "Georgia O'Keeffe",     "Flowers",            "+200 culture, appeal +2 in city", EraId{5}},

    // === Merchants (48-59) ===
    {48, GreatPersonCategory::Merchant, "Marcus Licinius Crassus","Roman Wealth",   "+500 gold", EraId{1}},
    {49, GreatPersonCategory::Merchant, "Zhang Qian",          "Silk Road",          "+1 trade route, +2 gold per route", EraId{1}},
    {50, GreatPersonCategory::Merchant, "Marco Polo",          "Travels",            "+1 trade route, reveal map portion", EraId{2}},
    {51, GreatPersonCategory::Merchant, "Jakob Fugger",        "Banking Empire",     "+300 gold, free Bank building", EraId{3}},
    {52, GreatPersonCategory::Merchant, "Mansa Musa",          "Richest Man",        "+1000 gold", EraId{2}},
    {53, GreatPersonCategory::Merchant, "Giovanni de Medici",  "Patron of Arts",     "+300 gold, +200 culture", EraId{3}},
    {54, GreatPersonCategory::Merchant, "Adam Smith",          "Invisible Hand",     "+10% gold in all cities for 10 turns", EraId{4}},
    {55, GreatPersonCategory::Merchant, "John D. Rockefeller", "Standard Oil",       "+500 gold, +3 oil stockpile", EraId{4}},
    {56, GreatPersonCategory::Merchant, "J.P. Morgan",         "Banking Reform",     "Free Stock Exchange", EraId{4}},
    {57, GreatPersonCategory::Merchant, "Estée Lauder",        "Beauty Empire",      "+400 gold, +100 tourism", EraId{5}},
    {58, GreatPersonCategory::Merchant, "Sarah Breedlove",     "Madam C.J. Walker", "+300 gold, +1 amenity", EraId{5}},
    {59, GreatPersonCategory::Merchant, "Colaeus",             "First Trader",       "+200 gold, +1 trade route", EraId{1}},

    // === Admirals (60-71) ===
    {60, GreatPersonCategory::Admiral, "Themistocles",         "Salamis",            "+10 naval combat for 10 turns", EraId{1}},
    {61, GreatPersonCategory::Admiral, "Leif Erikson",         "Vinland",            "All naval units +2 movement", EraId{2}},
    {62, GreatPersonCategory::Admiral, "Zheng He",             "Treasure Voyages",   "+2 trade routes, all naval", EraId{2}},
    {63, GreatPersonCategory::Admiral, "Francis Drake",        "Circumnavigation",   "+200 gold, reveal coastlines", EraId{3}},
    {64, GreatPersonCategory::Admiral, "Horatio Nelson",       "Trafalgar",          "+15 naval combat for 5 turns", EraId{4}},
    {65, GreatPersonCategory::Admiral, "Yi Sun-sin",           "Turtle Ship",        "Free naval unit, +10 def", EraId{3}},
    {66, GreatPersonCategory::Admiral, "Chester Nimitz",       "Pacific Campaign",   "All naval units heal 50 HP", EraId{5}},
    {67, GreatPersonCategory::Admiral, "Grace Hopper",         "COBOL",              "Free tech: Computers (if unobtained)", EraId{5}},
    {68, GreatPersonCategory::Admiral, "Hayreddin Barbarossa", "Ottoman Fleet",      "Free Armada formation", EraId{3}},
    {69, GreatPersonCategory::Admiral, "Andrea Doria",         "Genoese Fleet",      "+2 gold per coastal city", EraId{3}},
    {70, GreatPersonCategory::Admiral, "Matthew Perry",        "Black Ships",        "Open Borders with all civs for 10 turns", EraId{4}},
    {71, GreatPersonCategory::Admiral, "Rajendra Chola",       "Naval Empire",       "+3 naval combat, +1 trade route", EraId{2}},

    // === Prophets (72-83) ===
    {72, GreatPersonCategory::Prophet, "Confucius",            "Analects",           "Found religion + free belief", EraId{1}},
    {73, GreatPersonCategory::Prophet, "Siddhartha Gautama",   "Enlightenment",      "Found religion + +3 faith all cities", EraId{1}},
    {74, GreatPersonCategory::Prophet, "Zoroaster",            "Avesta",             "Found religion + free temple", EraId{1}},
    {75, GreatPersonCategory::Prophet, "Jesus of Nazareth",    "Gospels",            "Found religion + +10 loyalty", EraId{1}},
    {76, GreatPersonCategory::Prophet, "Muhammad",             "Quran",              "Found religion + free missionary", EraId{2}},
    {77, GreatPersonCategory::Prophet, "Adi Shankara",         "Advaita Vedanta",    "Found religion + +2 science", EraId{2}},
    {78, GreatPersonCategory::Prophet, "Martin Luther",        "95 Theses",          "Religious reform: +5 faith, +3 culture", EraId{3}},
    {79, GreatPersonCategory::Prophet, "John Calvin",          "Institutes",         "+4 faith per turn in capital", EraId{3}},
    {80, GreatPersonCategory::Prophet, "Guru Nanak",           "Sikhism",            "Found religion + +2 food all cities", EraId{3}},
    {81, GreatPersonCategory::Prophet, "Laozi",                "Tao Te Ching",       "+200 culture, +100 faith", EraId{1}},
    {82, GreatPersonCategory::Prophet, "Moses",                "Exodus",             "+10 loyalty in all cities", EraId{1}},
    {83, GreatPersonCategory::Prophet, "Haile Selassie",       "Lion of Judah",      "+3 faith, +3 culture for 10 turns", EraId{5}},

    // === Writers (84-95) ===
    {84, GreatPersonCategory::Writer, "Homer",                 "Iliad & Odyssey",    "+200 culture", EraId{1}},
    {85, GreatPersonCategory::Writer, "Murasaki Shikibu",      "Tale of Genji",      "+150 culture", EraId{2}},
    {86, GreatPersonCategory::Writer, "Dante Alighieri",       "Divine Comedy",      "+200 culture, +1 faith", EraId{2}},
    {87, GreatPersonCategory::Writer, "William Shakespeare",   "Complete Works",     "+300 culture, +100 tourism", EraId{3}},
    {88, GreatPersonCategory::Writer, "Cervantes",             "Don Quixote",        "+250 culture", EraId{3}},
    {89, GreatPersonCategory::Writer, "Jane Austen",           "Pride & Prejudice",  "+200 culture, +50 tourism", EraId{4}},
    {90, GreatPersonCategory::Writer, "Leo Tolstoy",           "War and Peace",      "+300 culture", EraId{4}},
    {91, GreatPersonCategory::Writer, "Mark Twain",            "Adventures",         "+200 culture, +1 amenity", EraId{4}},
    {92, GreatPersonCategory::Writer, "Rabindranath Tagore",   "Gitanjali",          "+250 culture, +100 tourism", EraId{5}},
    {93, GreatPersonCategory::Writer, "Gabriel García Márquez","100 Years of Solitude","+300 culture", EraId{5}},
    {94, GreatPersonCategory::Writer, "Toni Morrison",         "Beloved",            "+250 culture, +150 tourism", EraId{5}},
    {95, GreatPersonCategory::Writer, "Chinua Achebe",         "Things Fall Apart",  "+200 culture", EraId{5}},

    // === Musicians (96-107) ===
    { 96, GreatPersonCategory::Musician, "Antonio Vivaldi",    "Four Seasons",       "+200 culture, +100 tourism", EraId{3}},
    { 97, GreatPersonCategory::Musician, "Johann Sebastian Bach","Well-Tempered Clavier","+250 culture, +1 faith", EraId{3}},
    { 98, GreatPersonCategory::Musician, "Wolfgang Mozart",    "Requiem",            "+300 culture, +150 tourism", EraId{3}},
    { 99, GreatPersonCategory::Musician, "Ludwig van Beethoven","Symphony No. 9",    "+400 culture, +200 tourism", EraId{4}},
    {100, GreatPersonCategory::Musician, "Frédéric Chopin",    "Nocturnes",          "+250 culture, +100 tourism", EraId{4}},
    {101, GreatPersonCategory::Musician, "Pyotr Tchaikovsky",  "Swan Lake",          "+300 culture, +150 tourism", EraId{4}},
    {102, GreatPersonCategory::Musician, "Louis Armstrong",    "What a Wonderful World","+200 culture, +2 amenity", EraId{5}},
    {103, GreatPersonCategory::Musician, "Ella Fitzgerald",    "Jazz Vocals",        "+200 culture, +1 amenity", EraId{5}},
    {104, GreatPersonCategory::Musician, "Ravi Shankar",       "Sitar Master",       "+250 culture, +100 tourism", EraId{5}},
    {105, GreatPersonCategory::Musician, "Bob Marley",         "Reggae",             "+200 culture, +2 amenity, +50 tourism", EraId{5}},
    {106, GreatPersonCategory::Musician, "Miriam Makeba",      "Mama Africa",        "+200 culture, +1 amenity", EraId{5}},
    {107, GreatPersonCategory::Musician, "Yo-Yo Ma",           "Cello Suites",       "+300 culture, +200 tourism", EraId{5}},
}};

} // anonymous namespace

const NamedGreatPersonDef& namedGreatPersonDef(uint8_t id) {
    assert(id < NAMED_GP_COUNT);
    return NAMED_GP_DEFS[id];
}

const NamedGreatPersonDef* allNamedGreatPeople() {
    return NAMED_GP_DEFS.data();
}

} // namespace aoc::sim
