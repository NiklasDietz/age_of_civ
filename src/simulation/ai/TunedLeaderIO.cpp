/**
 * @file TunedLeaderIO.cpp
 * @brief Implementation of the tuned-leader file parser. See TunedLeaderIO.hpp.
 */

#include "aoc/simulation/ai/TunedLeaderIO.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <unordered_map>

namespace aoc::sim {

bool parseTunedLeader(const std::string& path, LeaderBehavior& out) {
    std::ifstream in(path);
    if (!in.is_open()) { return false; }

    std::unordered_map<std::string, float> vals;
    std::string line;
    bool inHard = false;
    while (std::getline(in, line)) {
        if (line.rfind("Hard AI", 0) == 0)   { inHard = true; continue; }
        if (line.rfind("Medium AI", 0) == 0) { break; }
        if (!inHard) { continue; }

        const std::string::size_type eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        std::string name = line.substr(0, eq);
        std::string val  = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) { s.erase(s.begin()); }
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  { s.pop_back(); }
        };
        trim(name); trim(val);
        if (name.empty() || val.empty()) { continue; }
        float parsed = 0.0f;
        try {
            parsed = std::stof(val);
        } catch (...) {
            continue;
        }
        // Security: reject non-finite genes (NaN/Inf) so a hostile or corrupt
        // file cannot poison the simulation. The field keeps its default.
        if (!std::isfinite(parsed)) { continue; }
        vals[name] = parsed;
    }
    if (vals.empty()) { return false; }

    out = LeaderBehavior{};
    auto set = [&](const char* key, float& field) {
        const std::unordered_map<std::string, float>::const_iterator it = vals.find(key);
        if (it != vals.end()) { field = it->second; }
    };
    set("militaryAggression",       out.militaryAggression);
    set("expansionism",             out.expansionism);
    set("scienceFocus",             out.scienceFocus);
    set("cultureFocus",             out.cultureFocus);
    set("economicFocus",            out.economicFocus);
    set("diplomaticOpenness",       out.diplomaticOpenness);
    set("religiousZeal",            out.religiousZeal);
    set("nukeWillingness",          out.nukeWillingness);
    set("trustworthiness",          out.trustworthiness);
    set("grudgeHolding",            out.grudgeHolding);
    set("techMilitary",             out.techMilitary);
    set("techEconomic",             out.techEconomic);
    set("techIndustrial",           out.techIndustrial);
    set("techNaval",                out.techNaval);
    set("techInformation",          out.techInformation);
    set("prodSettlers",             out.prodSettlers);
    set("prodMilitary",             out.prodMilitary);
    set("prodBuilders",             out.prodBuilders);
    set("prodBuildings",            out.prodBuildings);
    set("prodWonders",              out.prodWonders);
    set("prodNaval",                out.prodNaval);
    set("prodReligious",            out.prodReligious);
    set("warDeclarationThreshold",  out.warDeclarationThreshold);
    set("peaceAcceptanceThreshold", out.peaceAcceptanceThreshold);
    set("allianceDesire",           out.allianceDesire);
    set("riskTolerance",            out.riskTolerance);
    set("environmentalism",         out.environmentalism);
    set("peripheryTolerance",       out.peripheryTolerance);
    set("greatPersonFocus",         out.greatPersonFocus);
    set("espionagePriority",        out.espionagePriority);
    set("ideologicalFervor",        out.ideologicalFervor);
    set("speculationAppetite",      out.speculationAppetite);
    set("milBaseWeight",            out.milBaseWeight);
    set("milThreatSensitivity",     out.milThreatSensitivity);
    set("milEmergencySlope",        out.milEmergencySlope);
    set("milOverstockPenalty",      out.milOverstockPenalty);
    return true;
}

} // namespace aoc::sim
