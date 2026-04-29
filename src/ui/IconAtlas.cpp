/**
 * @file IconAtlas.cpp
 */

#include "aoc/ui/IconAtlas.hpp"
#include "aoc/core/Log.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace aoc::ui {

IconAtlas& IconAtlas::instance() {
    static IconAtlas s_atlas;
    return s_atlas;
}

uint32_t IconAtlas::registerSprite(std::string name, IconRegion region) {
    auto it = this->m_byName.find(name);
    if (it != this->m_byName.end()) {
        this->m_regions[it->second] = region;
        return it->second;
    }
    // IDs are 1-based so id==0 stays sentinel "invalid".
    const uint32_t id = static_cast<uint32_t>(this->m_regions.size()) + 1;
    this->m_regions.push_back(region);
    this->m_byName.emplace(std::move(name), id);
    return id;
}

uint32_t IconAtlas::id(std::string_view name) const {
    auto it = this->m_byName.find(std::string(name));
    if (it == this->m_byName.end()) { return 0; }
    return it->second;
}

const IconRegion* IconAtlas::region(uint32_t id) const {
    if (id == 0 || id > this->m_regions.size()) { return nullptr; }
    return &this->m_regions[id - 1];
}

int32_t IconAtlas::loadPlaceholders(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARN("IconAtlas: cannot open %s", path.c_str());
        return 0;
    }
    int32_t n = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        const std::string name = line.substr(0, eq);
        const std::string rgba = line.substr(eq + 1);
        std::stringstream ss(rgba);
        std::string tok;
        IconRegion reg;
        Color c{1.0f, 1.0f, 1.0f, 1.0f};
        int32_t idx = 0;
        while (std::getline(ss, tok, ',') && idx < 4) {
            float v = 0.0f;
            try { v = std::stof(tok); } catch (...) { break; }
            if (idx == 0) { c.r = v; }
            else if (idx == 1) { c.g = v; }
            else if (idx == 2) { c.b = v; }
            else if (idx == 3) { c.a = v; }
            ++idx;
        }
        reg.fallback = c;
        this->registerSprite(name, reg);
        ++n;
    }
    LOG_INFO("IconAtlas: loaded %d placeholder entries from %s", n, path.c_str());
    return n;
}

void IconAtlas::seedBuiltIns() {
    // Hand-picked distinct colours so placeholder rects look like the
    // right "kind of thing" even without real art. Categories chosen so
    // every TechDef::unlocked* item has a class-level icon to fall back
    // on; per-id icons can be added later without touching callsites.
    auto reg = [&](const char* name, float r, float g, float b) {
        IconRegion x;
        x.fallback = {r, g, b, 1.0f};
        this->registerSprite(name, x);
    };

    // ----- Yields (8 hue families, parchment-tuned) -----
    reg("yields.food",       0.360f, 0.545f, 0.243f);
    reg("yields.production", 0.658f, 0.431f, 0.180f);
    reg("yields.gold",       0.788f, 0.639f, 0.352f);
    reg("yields.science",    0.247f, 0.435f, 0.658f);
    reg("yields.culture",    0.545f, 0.247f, 0.545f);
    reg("yields.faith",      0.784f, 0.784f, 0.784f);
    reg("yields.power",      0.839f, 0.701f, 0.255f);
    reg("yields.tourism",    0.839f, 0.482f, 0.262f);

    // ----- Resources (matched to goods table) -----
    reg("resources.food",    0.35f, 0.75f, 0.25f);
    reg("resources.wood",    0.45f, 0.30f, 0.15f);
    reg("resources.stone",   0.55f, 0.55f, 0.55f);
    reg("resources.iron",    0.40f, 0.40f, 0.45f);
    reg("resources.copper",  0.85f, 0.50f, 0.20f);
    reg("resources.gold",    0.95f, 0.80f, 0.20f);
    reg("resources.oil",     0.15f, 0.15f, 0.15f);
    reg("resources.coal",    0.25f, 0.20f, 0.20f);
    reg("resources.uranium", 0.35f, 0.85f, 0.35f);
    reg("resources.wheat",   0.90f, 0.80f, 0.35f);
    reg("resources.cotton",  0.95f, 0.95f, 0.95f);
    reg("resources.horses",  0.55f, 0.40f, 0.25f);
    reg("resources.niter",   0.85f, 0.75f, 0.55f);
    reg("resources.steel",   0.50f, 0.55f, 0.60f);
    reg("resources.fuel",    0.20f, 0.20f, 0.30f);
    reg("resources.glass",   0.65f, 0.85f, 0.95f);
    reg("resources.tools",   0.55f, 0.45f, 0.30f);
    reg("resources.unknown", 0.50f, 0.50f, 0.50f);

    // ----- Civs (placeholder per major) -----
    reg("civs.rome",    0.70f, 0.20f, 0.20f);
    reg("civs.egypt",   0.95f, 0.80f, 0.30f);
    reg("civs.greece",  0.25f, 0.55f, 0.85f);
    reg("civs.china",   0.85f, 0.30f, 0.30f);
    reg("civs.unknown", 0.35f, 0.35f, 0.40f);

    // ----- Techs (era-tinted, generic if id unknown) -----
    reg("techs.mining",        0.55f, 0.55f, 0.60f);
    reg("techs.iron-working",  0.50f, 0.50f, 0.55f);
    reg("techs.electricity",   1.0f,  0.95f, 0.35f);
    reg("techs.computers",     0.30f, 0.80f, 0.95f);
    reg("techs.ancient",       0.55f, 0.45f, 0.30f); // fallback per era
    reg("techs.classical",     0.65f, 0.50f, 0.32f);
    reg("techs.medieval",      0.55f, 0.42f, 0.40f);
    reg("techs.renaissance",   0.85f, 0.65f, 0.35f);
    reg("techs.industrial",    0.45f, 0.35f, 0.30f);
    reg("techs.modern",        0.45f, 0.55f, 0.65f);
    reg("techs.atomic",        0.75f, 0.55f, 0.30f);
    reg("techs.information",   0.30f, 0.80f, 0.95f);
    reg("techs.unknown",       0.45f, 0.45f, 0.55f);

    // ----- Civics -----
    reg("civics.code-of-laws", 0.55f, 0.30f, 0.55f);
    reg("civics.craftsmanship",0.50f, 0.40f, 0.30f);
    reg("civics.unknown",      0.55f, 0.35f, 0.55f);

    // ----- Unit classes (one per UnitClass) -----
    reg("units.melee",        0.65f, 0.25f, 0.20f);
    reg("units.ranged",       0.55f, 0.30f, 0.25f);
    reg("units.cavalry",      0.55f, 0.40f, 0.25f);
    reg("units.armor",        0.40f, 0.40f, 0.30f);
    reg("units.artillery",    0.45f, 0.30f, 0.25f);
    reg("units.anticavalry",  0.55f, 0.45f, 0.30f);
    reg("units.naval-melee",  0.20f, 0.40f, 0.55f);
    reg("units.naval-ranged", 0.25f, 0.45f, 0.65f);
    reg("units.naval-raider", 0.15f, 0.30f, 0.45f);
    reg("units.naval-carrier",0.20f, 0.35f, 0.50f);
    reg("units.air-fighter",  0.55f, 0.55f, 0.65f);
    reg("units.air-bomber",   0.45f, 0.45f, 0.55f);
    reg("units.air-helicopter",0.50f,0.55f, 0.60f);
    reg("units.support",      0.65f, 0.55f, 0.30f);
    reg("units.recon",        0.45f, 0.55f, 0.40f);
    reg("units.military",     0.65f, 0.20f, 0.20f);
    reg("units.settler",      0.50f, 0.70f, 0.40f);
    reg("units.builder",      0.65f, 0.50f, 0.25f);
    reg("units.trader",       0.90f, 0.75f, 0.30f);
    reg("units.spy",          0.30f, 0.30f, 0.40f);
    reg("units.diplomat",     0.45f, 0.55f, 0.75f);
    reg("units.missionary",   0.85f, 0.85f, 0.85f);
    reg("units.apostle",      0.85f, 0.75f, 0.55f);
    reg("units.greatperson",  0.85f, 0.70f, 0.35f);
    reg("units.unknown",      0.45f, 0.40f, 0.40f);

    // ----- Buildings (grouped by district) -----
    reg("buildings.industrial",  0.55f, 0.40f, 0.25f);
    reg("buildings.commercial",  0.75f, 0.60f, 0.30f);
    reg("buildings.campus",      0.30f, 0.45f, 0.65f);
    reg("buildings.encampment",  0.45f, 0.30f, 0.25f);
    reg("buildings.holysite",    0.85f, 0.85f, 0.85f);
    reg("buildings.theatre",     0.55f, 0.30f, 0.55f);
    reg("buildings.harbor",      0.25f, 0.45f, 0.65f);
    reg("buildings.citycenter",  0.65f, 0.55f, 0.40f);
    reg("buildings.power",       0.85f, 0.70f, 0.25f);
    reg("buildings.unknown",     0.55f, 0.50f, 0.45f);

    // ----- Districts (badges) -----
    reg("districts.citycenter",  0.65f, 0.55f, 0.40f);
    reg("districts.campus",      0.30f, 0.45f, 0.65f);
    reg("districts.commercial",  0.75f, 0.60f, 0.30f);
    reg("districts.encampment",  0.45f, 0.30f, 0.25f);
    reg("districts.industrial",  0.55f, 0.40f, 0.25f);
    reg("districts.holysite",    0.85f, 0.85f, 0.85f);
    reg("districts.theatre",     0.55f, 0.30f, 0.55f);
    reg("districts.harbor",      0.25f, 0.45f, 0.65f);

    // ----- Wonders (single icon, era-tinted optional) -----
    reg("wonders.generic",       0.85f, 0.70f, 0.35f);
    reg("wonders.ancient",       0.85f, 0.70f, 0.35f);
    reg("wonders.classical",     0.80f, 0.65f, 0.30f);
    reg("wonders.medieval",      0.65f, 0.50f, 0.30f);
    reg("wonders.renaissance",   0.90f, 0.75f, 0.40f);
    reg("wonders.industrial",    0.60f, 0.45f, 0.25f);
    reg("wonders.modern",        0.50f, 0.55f, 0.65f);
    reg("wonders.atomic",        0.85f, 0.55f, 0.30f);
    reg("wonders.information",   0.40f, 0.80f, 0.95f);

    // ----- Action verbs (used in tooltips, build queue overlays) -----
    reg("actions.research",  0.247f, 0.435f, 0.658f);
    reg("actions.build",     0.658f, 0.431f, 0.180f);
    reg("actions.found",     0.50f,  0.70f,  0.40f);
    reg("actions.move",      0.45f,  0.55f,  0.45f);
    reg("actions.attack",    0.639f, 0.227f, 0.164f);
    reg("actions.trade",     0.90f,  0.75f,  0.30f);
    reg("actions.levy",      0.65f,  0.45f,  0.30f);
    reg("actions.pillage",   0.55f,  0.30f,  0.25f);
    reg("actions.diplo",     0.45f,  0.55f,  0.75f);
    reg("actions.spy",       0.30f,  0.30f,  0.40f);
    reg("actions.eureka",    0.95f,  0.85f,  0.40f);
    reg("actions.boost",     0.95f,  0.85f,  0.40f);

    // ----- States / status badges -----
    reg("status.locked",     0.45f, 0.42f, 0.36f);
    reg("status.available",  0.643f, 0.486f, 0.227f);
    reg("status.researching",0.247f, 0.435f, 0.658f);
    reg("status.completed",  0.360f, 0.545f, 0.243f);
    reg("status.intel",      0.247f, 0.435f, 0.658f);
}

} // namespace aoc::ui
