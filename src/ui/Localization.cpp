/**
 * @file Localization.cpp
 */

#include "aoc/ui/Localization.hpp"
#include "aoc/core/Log.hpp"

#include <fstream>
#include <string>

namespace aoc::ui {

Localization& Localization::instance() {
    static Localization s_instance;
    return s_instance;
}

std::string_view Localization::lookup(std::string_view key) const {
    // Hash lookup keyed by the requested string. Falling back to the
    // key itself surfaces missing translations during development.
    auto it = this->m_catalog.find(std::string(key));
    if (it == this->m_catalog.end()) { return key; }
    return it->second;
}

bool Localization::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARN("Localization: cannot open %s — using fallback keys", path.c_str());
        return false;
    }
    std::unordered_map<std::string, std::string> cat;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        cat.emplace(line.substr(0, eq), line.substr(eq + 1));
    }
    LOG_INFO("Localization: loaded %zu strings from %s", cat.size(), path.c_str());
    this->m_catalog = std::move(cat);
    return true;
}

} // namespace aoc::ui
