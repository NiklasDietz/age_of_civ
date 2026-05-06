#include "aoc/map/gen/PlateReference.hpp"

namespace aoc::map::gen {

namespace {

// PB2002 plate catalog. Centroids are areal centres computed from the
// digital plate-boundary geometry published with Bird (2003).
// Composition class is the bulk crustal type (continental shield /
// oceanic plate / mixed where the plate carries both continental fragments
// and significant oceanic crust).
//
// Reference: P. Bird, "An updated digital model of plate boundaries",
//            Geochemistry, Geophysics, Geosystems 4(3), 2003.
//            doi:10.1029/2001GC000252
constexpr ReferencePlate kBirdPlateCatalog[] = {
    // Major plates (14)
    { "Pacific",         {   2.0f, -160.0f }, PlateCompositionType::Oceanic     },
    { "North America",   {  50.0f, -100.0f }, PlateCompositionType::Continental },
    { "South America",   { -15.0f,  -60.0f }, PlateCompositionType::Continental },
    { "Eurasia",         {  55.0f,   90.0f }, PlateCompositionType::Continental },
    { "Africa",          {   2.0f,   22.0f }, PlateCompositionType::Continental },
    { "Australia",       { -25.0f,  135.0f }, PlateCompositionType::Continental },
    { "Antarctica",      { -85.0f,    0.0f }, PlateCompositionType::Continental },
    { "Nazca",           { -22.0f, -100.0f }, PlateCompositionType::Oceanic     },
    { "Cocos",           {   5.0f,  -95.0f }, PlateCompositionType::Oceanic     },
    { "Caribbean",       {  15.0f,  -75.0f }, PlateCompositionType::Mixed       },
    { "Arabia",          {  25.0f,   45.0f }, PlateCompositionType::Continental },
    { "India",           {  15.0f,   80.0f }, PlateCompositionType::Continental },
    { "Philippine Sea",  {  18.0f,  135.0f }, PlateCompositionType::Mixed       },
    { "Juan de Fuca",    {  47.0f, -128.0f }, PlateCompositionType::Oceanic     },
    // Smaller / orogen plates (38)
    { "Amur",            {  50.0f,  125.0f }, PlateCompositionType::Continental },
    { "Anatolia",        {  39.0f,   33.0f }, PlateCompositionType::Continental },
    { "Aegean Sea",      {  39.0f,   23.0f }, PlateCompositionType::Continental },
    { "Altiplano",       { -20.0f,  -68.0f }, PlateCompositionType::Continental },
    { "Banda Sea",       {  -5.0f,  128.0f }, PlateCompositionType::Mixed       },
    { "Burma",           {  20.0f,   95.0f }, PlateCompositionType::Continental },
    { "Bird's Head",     {  -2.0f,  132.0f }, PlateCompositionType::Continental },
    { "Balmoral Reef",   { -20.0f,  175.0f }, PlateCompositionType::Oceanic     },
    { "Caroline",        {   5.0f,  145.0f }, PlateCompositionType::Oceanic     },
    { "Conway Reef",     { -22.0f,  175.0f }, PlateCompositionType::Oceanic     },
    { "Easter",          { -25.0f, -110.0f }, PlateCompositionType::Oceanic     },
    { "Futuna",          { -15.0f,  179.0f }, PlateCompositionType::Oceanic     },
    { "Galapagos",       {   2.0f, -101.0f }, PlateCompositionType::Oceanic     },
    { "Juan Fernandez",  { -34.0f,  -82.0f }, PlateCompositionType::Oceanic     },
    { "Kermadec",        { -32.0f,  178.0f }, PlateCompositionType::Oceanic     },
    { "Manus",           {  -3.0f,  148.0f }, PlateCompositionType::Oceanic     },
    { "Maoke",           {  -4.0f,  138.0f }, PlateCompositionType::Continental },
    { "Mariana",         {  18.0f,  145.0f }, PlateCompositionType::Oceanic     },
    { "Molucca Sea",     {   1.0f,  126.0f }, PlateCompositionType::Oceanic     },
    { "New Hebrides",    { -16.0f,  168.0f }, PlateCompositionType::Oceanic     },
    { "Niuafo'ou",       { -18.0f, -174.0f }, PlateCompositionType::Oceanic     },
    { "North Andes",     {   5.0f,  -76.0f }, PlateCompositionType::Continental },
    { "North Bismarck",  {  -3.0f,  145.0f }, PlateCompositionType::Oceanic     },
    { "Okhotsk",         {  55.0f,  145.0f }, PlateCompositionType::Continental },
    { "Okinawa",         {  26.0f,  128.0f }, PlateCompositionType::Oceanic     },
    { "Panama",          {   8.0f,  -80.0f }, PlateCompositionType::Mixed       },
    { "Rivera",          {  19.0f, -107.0f }, PlateCompositionType::Oceanic     },
    { "Sandwich",        { -57.0f,  -27.0f }, PlateCompositionType::Oceanic     },
    { "Scotia",          { -57.0f,  -45.0f }, PlateCompositionType::Oceanic     },
    { "Shetland",        { -62.0f,  -60.0f }, PlateCompositionType::Oceanic     },
    { "Solomon Sea",     {  -7.0f,  152.0f }, PlateCompositionType::Oceanic     },
    { "Somalia",         {  -5.0f,   45.0f }, PlateCompositionType::Continental },
    { "South Bismarck",  {  -5.0f,  148.0f }, PlateCompositionType::Oceanic     },
    { "Sunda",           {   5.0f,  105.0f }, PlateCompositionType::Continental },
    { "Timor",           {  -9.0f,  125.0f }, PlateCompositionType::Continental },
    { "Tonga",           { -22.0f, -175.0f }, PlateCompositionType::Oceanic     },
    { "Woodlark",        {  -8.0f,  154.0f }, PlateCompositionType::Oceanic     },
    { "Yangtze",         {  30.0f,  110.0f }, PlateCompositionType::Continental },
};

constexpr std::size_t kBirdPlateCatalogSize =
    sizeof(kBirdPlateCatalog) / sizeof(kBirdPlateCatalog[0]);

static_assert(kBirdPlateCatalogSize == 52,
              "Bird (2003) PB2002 catalog must have exactly 52 plates");

} // namespace

const ReferencePlate* birdPlateCatalog() noexcept {
    return kBirdPlateCatalog;
}

std::size_t birdPlateCatalogSize() noexcept {
    return kBirdPlateCatalogSize;
}

PlateCompositionType classifyByNearestReference(LatLon centroid) noexcept {
    PlateCompositionType nearestType = PlateCompositionType::Oceanic;
    float                nearestDistRad = 1e9f;
    for (std::size_t i = 0; i < kBirdPlateCatalogSize; ++i) {
        const float d = haversineRadians(centroid, kBirdPlateCatalog[i].centroid);
        if (d < nearestDistRad) {
            nearestDistRad = d;
            nearestType    = kBirdPlateCatalog[i].type;
        }
    }
    return nearestType;
}

} // namespace aoc::map::gen
