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
    // right "kind of thing" even without real art. Civs are named by
    // their Civilization enum labels; resources match goods table.
    auto reg = [&](const char* name, float r, float g, float b) {
        IconRegion x;
        x.fallback = {r, g, b, 1.0f};
        this->registerSprite(name, x);
    };
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

    reg("civs.rome",    0.70f, 0.20f, 0.20f);
    reg("civs.egypt",   0.95f, 0.80f, 0.30f);
    reg("civs.greece",  0.25f, 0.55f, 0.85f);
    reg("civs.china",   0.85f, 0.30f, 0.30f);
    reg("civs.unknown", 0.35f, 0.35f, 0.40f);

    reg("techs.mining",      0.55f, 0.55f, 0.60f);
    reg("techs.iron-working",0.50f, 0.50f, 0.55f);
    reg("techs.electricity", 1.0f,  0.95f, 0.35f);
    reg("techs.computers",   0.30f, 0.80f, 0.95f);

    reg("units.military",    0.65f, 0.20f, 0.20f);
    reg("units.settler",     0.50f, 0.70f, 0.40f);
    reg("units.trader",      0.90f, 0.75f, 0.30f);
    reg("units.spy",         0.30f, 0.30f, 0.40f);
}

} // namespace aoc::ui
