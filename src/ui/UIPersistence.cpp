/**
 * @file UIPersistence.cpp
 */

#include "aoc/ui/UIPersistence.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/core/Log.hpp"

#include <fstream>
#include <string>

namespace aoc::ui {

int32_t hotReloadLayout(UIManager& ui, const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        LOG_WARN("UI hot-reload: cannot open %s", path.c_str());
        return 0;
    }
    int32_t applied = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        // Key format: <widget-id>.<axis>. The widget-id is parsed as
        // a uint32. The hot-reload audience is layout testing; bigger
        // surface (e.g., named widgets) can land later.
        const std::size_t dot = key.find('.');
        if (dot == std::string::npos) { continue; }
        WidgetId id = INVALID_WIDGET;
        try { id = static_cast<WidgetId>(std::stoul(key.substr(0, dot))); }
        catch (...) { continue; }
        Widget* w = ui.getWidget(id);
        if (w == nullptr) { continue; }
        const std::string axis = key.substr(dot + 1);
        float val = 0.0f;
        try { val = std::stof(value); } catch (...) { continue; }
        if      (axis == "x") { w->requestedBounds.x = val; ++applied; }
        else if (axis == "y") { w->requestedBounds.y = val; ++applied; }
        else if (axis == "w") { w->requestedBounds.w = val; ++applied; }
        else if (axis == "h") { w->requestedBounds.h = val; ++applied; }
        else if (axis == "alpha") { w->alpha = val; ++applied; }
    }
    LOG_INFO("UI hot-reload: applied %d overrides from %s", applied, path.c_str());
    return applied;
}

} // namespace aoc::ui
